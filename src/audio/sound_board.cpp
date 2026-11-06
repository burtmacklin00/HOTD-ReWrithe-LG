// HOTD's Model 2 sound board (sound_board.h). The memory map and wiring follow MAME's model2 driver (model2_snd,
// scsp_map, scsp_irq, reset_model2_scsp; docs/teaser_status/sound_board.md):
//   0x000000-0x07FFFF  sound RAM 512 KB (also the SCSP's own memory); at reset its first 16 bytes = the program's
//   0x100000-0x100FFF  SCSP registers (16-bit)
//   0x400000           sample bank control (HOTD's 8 MB of samples: no effect)
//   0x600000-0x67FFFF  program ROM epr-19720.31
//   0x800000-0xFFFFFF  samples (8 MB: banks 4 / 5 sit at samples + 0x200000 / + 0x600000, i.e. linear)
// Timing: the 68000 at 11.2896 MHz = exactly 256 cycles per 44.1 kHz SCSP sample; one MIDI byte (31.25 kbaud, 10
// bits) every 14 samples.
#include "audio/sound_board.h"

#include "emu.h"   // our MAME stand-in (src/audio/mame_shim)
#include "scsp.h"

extern "C" {
#include "m68k.h"
}

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <deque>
#include <mutex>

namespace hotd::audio {

namespace {
constexpr uint32_t kRamSize = 0x80000;
constexpr int kCyclesPerSample = 256;   // 11.2896 MHz / 44100
constexpr int kSamplesPerMidiByte = 14; // 320 us
SoundBoard::Impl* g_board = nullptr;    // Musashi's memory callbacks are plain C functions
} // namespace

struct SoundBoard::Impl {
    std::vector<uint8_t> ram = std::vector<uint8_t>(kRamSize, 0);
    std::vector<uint8_t> prog, samples;   // big-endian bytes
    machine_config mc;
    std::unique_ptr<scsp_device> scsp;
    uint32_t irqLines = 0;
    int cycleDebt = 0;
    int midiWait = 0;
    uint64_t samplesRun = 0, midiReads = 0, irqAsserts = 0, midiIn = 0;
    std::mutex fifoLock;
    std::deque<uint8_t> fifo;
    std::atomic<uint32_t> fifoCount{0};   // perf: fifo.size(), read without the lock (the board looks every idle sample)
    bool trace = std::getenv("HOTD_TRACE_BOARD") != nullptr;   // once a second: the 68000's PC / SR, the IRQ lines

    uint32_t heldLines = 0;   // lines raised with HOLD_LINE: cleared when the 68000 acknowledges that level (MAME's rule)
    // totd8 (testing, HOTD_SCSP_KEYS=1): each slot keyed on (KYONEX with KEYONB newly set) printed with its sample
    // format: PCM8B (8-bit) or 16-bit, the pitch (OCT / FNS: the rate = 44100 x 2^OCT x (1 + FNS / 1024)), the start
    // address and loop end: the arcade's own samples' format (docs/typing.md, the typing sounds by board)
    bool keysTap = std::getenv("HOTD_SCSP_KEYS") != nullptr;
    bool keyWas[32] = {};
    void tapKeys(uint32_t a, uint32_t w)
    {
        if (!keysTap || (a & 0xFFF) >= 0x400 || ((a & 0x1F) >> 1) != 0)
            return;
        if (!(w & 0x1000))
            return;
        for (int s = 0; s < 32; ++s) {
            const u16 r0 = scsp->read(s * 0x10), on = r0 & 0x0800;
            if (on && !keyWas[s]) {
                const u16 r8 = scsp->read(s * 0x10 + 8);
                int oct = (r8 >> 11) & 0xF;
                if (oct & 8) oct -= 16;
                const double rate = 44100.0 * std::pow(2.0, oct) * (1.0 + (r8 & 0x3FF) / 1024.0);
                std::printf("SCSP KEY slot %d %s rate %.0f SA %05X LEA %04X TL %d\n", s, (r0 & 0x10) ? "8-bit" : "16-bit", rate,
                            ((r0 & 0xF) << 16) | scsp->read(s * 0x10 + 1), scsp->read(s * 0x10 + 3), scsp->read(s * 0x10 + 6) & 0xFF);
            }
            keyWas[s] = on != 0;
        }
    }

    void setIrq(offs_t line, u8 state)
    {
        if (line == 0 || line > 7)
            return;
        if (state == CLEAR_LINE) {
            irqLines &= ~(1u << line);
            heldLines &= ~(1u << line);
        } else {
            irqLines |= 1u << line;
            if (state == HOLD_LINE)   // the SCSP's DMA end (scsp.cpp: m_irq_cb(DecodeSCI(SCIDMA), HOLD_LINE))
                heldLines |= 1u << line;
            ++irqAsserts;
        }
        updateIpl();
    }
    // Musashi's interrupt acknowledge (M68K_EMULATE_INT_ACK on): a held line ends here; autovectored as before
    int intAck(int level)
    {
        if (level > 0 && level < 8 && (heldLines & (1u << level))) {
            heldLines &= ~(1u << level);
            irqLines &= ~(1u << level);
            updateIpl();
        }
        return M68K_INT_ACK_AUTOVECTOR;
    }
    void updateIpl()
    {
        int level = 0;
        for (int l = 7; l > 0; --l)
            if (irqLines & (1u << l)) { level = l; break; }
        m68k_set_irq(unsigned(level));
    }

    uint8_t read8(uint32_t a)
    {
        a &= 0xFFFFFF;
        if (a < kRamSize)
            return ram[a];
        if (a >= 0x100000 && a < 0x101000) {
            if ((a & 0xFFE) == 0x404) ++midiReads;
            const u16 w = scsp->read((a & 0xFFF) >> 1);
            return (a & 1) ? u8(w) : u8(w >> 8);
        }
        if (a >= 0x600000 && a < 0x680000)
            return a - 0x600000 < prog.size() ? prog[a - 0x600000] : 0;
        if (a >= 0x800000)
            return a - 0x800000 < samples.size() ? samples[a - 0x800000] : 0;
        return 0;
    }
    uint16_t read16(uint32_t a)
    {
        a &= 0xFFFFFE;
        // perf (2026-10-21): the program ROM (every opcode word) and the RAM straight (the same bytes as read8's)
        if (a - 0x600000u < 0x80000u) {
            const uint32_t o = a - 0x600000u;
            if (o + 1 < prog.size())
                return uint16_t((prog[o] << 8) | prog[o + 1]);
        } else if (a < kRamSize)
            return uint16_t((ram[a] << 8) | ram[a + 1]);
        if (a >= 0x100000 && a < 0x101000) {
            if ((a & 0xFFE) == 0x404) ++midiReads;
            return scsp->read((a & 0xFFF) >> 1);
        }
        return uint16_t((read8(a) << 8) | read8(a + 1));
    }
    void write8(uint32_t a, uint8_t v)
    {
        a &= 0xFFFFFF;
        if (a < kRamSize)
            ram[a] = v;
        else if (a >= 0x100000 && a < 0x101000) {
            scsp->write((a & 0xFFF) >> 1, (a & 1) ? u16(v) : u16(v << 8), (a & 1) ? 0x00FF : 0xFF00);
            tapKeys(a & ~1u, (a & 1) ? 0u : uint32_t(v) << 8);
        }
    }
    void write16(uint32_t a, uint16_t v)
    {
        a &= 0xFFFFFE;
        if (a < kRamSize) {
            ram[a] = uint8_t(v >> 8);
            ram[a + 1] = uint8_t(v);
        } else if (a >= 0x100000 && a < 0x101000) {
            scsp->write((a & 0xFFF) >> 1, v, 0xFFFF);
            tapKeys(a, v);
        }
    }
};

} // namespace hotd::audio

using hotd::audio::g_board;

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) { return g_board ? g_board->read8(a) : 0; }
unsigned int m68k_read_memory_16(unsigned int a) { return g_board ? g_board->read16(a) : 0; }
unsigned int m68k_read_memory_32(unsigned int a)
{
    if (!g_board) return 0;
    const unsigned e = a & 0xFFFFFEu;   // (read16's even address; a + 2's is e + 2 below the top)
    if (e - 0x600000u < 0x7FFFCu && e - 0x600000u + 3 < g_board->prog.size()) {   // perf: the program ROM straight
        const uint8_t* p = g_board->prog.data() + (e - 0x600000u);
        return unsigned(p[0]) << 24 | unsigned(p[1]) << 16 | unsigned(p[2]) << 8 | p[3];
    }
    return (unsigned(g_board->read16(a)) << 16) | g_board->read16(a + 2);
}
void m68k_write_memory_8(unsigned int a, unsigned int v) { if (g_board) g_board->write8(a, uint8_t(v)); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { if (g_board) g_board->write16(a, uint16_t(v)); }
void m68k_write_memory_32(unsigned int a, unsigned int v)
{
    if (g_board) {
        g_board->write16(a, uint16_t(v >> 16));
        g_board->write16(a + 2, uint16_t(v));
    }
}
}

namespace hotd::audio {

SoundBoard::SoundBoard() = default;
SoundBoard::~SoundBoard()
{
    if (impl_ && g_board == impl_.get())
        g_board = nullptr;
}

bool SoundBoard::loaded() const { return impl_ != nullptr; }
uint64_t SoundBoard::samplesRun() const { return impl_ ? impl_->samplesRun : 0; }

static std::vector<uint8_t> swapWords(const std::vector<uint8_t>& in)
{
    std::vector<uint8_t> out(in.size());
    for (size_t i = 0; i + 1 < in.size(); i += 2) {
        out[i] = in[i + 1];
        out[i + 1] = in[i];
    }
    return out;
}

bool SoundBoard::load(const std::vector<uint8_t>& program, const std::vector<uint8_t>& samples, std::string& err)
{
    if (g_board && (!impl_ || g_board != impl_.get())) {
        err = "sound board: one board per process";
        return false;
    }
    if (program.size() < 16) {
        err = "sound board: no program ROM";
        return false;
    }
    impl_ = std::make_unique<Impl>();
    // the byte order: the ROM files hold little-endian words (MAME's ROM_LOAD16_WORD_SWAP), a big-endian copy (MAME's
    // region export) is used as it is; told apart by the reset PC (in the program ROM, 0x600000-0x67FFFF)
    const uint32_t pcBE = uint32_t(program[4] << 24 | program[5] << 16 | program[6] << 8 | program[7]);
    const bool bigEndian = pcBE >= 0x600000 && pcBE < 0x680000;
    impl_->prog = bigEndian ? program : swapWords(program);
    impl_->samples = bigEndian ? samples : swapWords(samples);
    g_board = impl_.get();
    reset();
    return true;
}

void SoundBoard::reset()
{
    Impl& b = *impl_;
    std::fill(b.ram.begin(), b.ram.end(), 0);
    std::copy(b.prog.begin(), b.prog.begin() + 16, b.ram.begin());   // the 68000's reset vectors
    b.scsp = std::make_unique<scsp_device>(b.mc, "scsp", nullptr, 22'579'200);
    b.scsp->set_ram(b.ram.data(), kRamSize);
    b.scsp->irq_cb().set([&b](offs_t line, u8 state) { b.setIrq(line, state); });
    b.scsp->start();
    b.scsp->reset();
    b.irqLines = 0;
    b.heldLines = 0;
    b.cycleDebt = 0;
    b.midiWait = 0;
    {
        std::lock_guard<std::mutex> g(b.fifoLock);
        b.fifo.clear();
        b.fifoCount = 0;
    }
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_int_ack_callback([](int level) { return g_board ? g_board->intAck(level) : int(M68K_INT_ACK_AUTOVECTOR); });
    m68k_pulse_reset();
    m68k_set_irq(0);
    if (const char* n = std::getenv("HOTD_TRACE_BOARD_STEPS")) {   // debugging: the first N instructions
        std::vector<bool> seen(0x1000000);
        for (int i = std::atoi(n); i > 0; --i) {
            const unsigned pc = m68k_get_reg(nullptr, M68K_REG_PC);
            if (!seen[pc & 0xFFFFFF] && (seen[pc & 0xFFFFFF] = true))
                std::printf("[board step] pc %06X op %04X a7 %08X d0 %08X\n", pc, b.read16(pc), m68k_get_reg(nullptr, M68K_REG_A7),
                        m68k_get_reg(nullptr, M68K_REG_D0));
            m68k_execute(1);
        }
        m68k_pulse_reset();
    }
}

void SoundBoard::command(uint32_t code)
{
    if (!impl_)
        return;
    std::lock_guard<std::mutex> g(impl_->fifoLock);
    impl_->fifo.push_back(uint8_t(code >> 16));
    impl_->fifo.push_back(uint8_t(code >> 8));
    impl_->fifo.push_back(uint8_t(code));
    impl_->fifoCount += 3;
}

void SoundBoard::render(float* out, int frames, float gain)
{
    if (!impl_)
        return;
    Impl& b = *impl_;
    float s[2];
    for (int i = 0; i < frames; ++i) {
        // the MIDI input: one byte per 14 samples
        if (b.midiWait > 0)
            --b.midiWait;
        else {
            int byte = -1;
            if (b.fifoCount.load() != 0) {
                std::lock_guard<std::mutex> g(b.fifoLock);
                if (!b.fifo.empty()) {
                    byte = b.fifo.front();
                    b.fifo.pop_front();
                    --b.fifoCount;
                }
            }
            if (byte >= 0) {
                b.scsp->receive_byte(uint8_t(byte));
                ++b.midiIn;
                b.midiWait = kSamplesPerMidiByte - 1;
            }
        }
        // the 68000 for one sample's time, then the SCSP's timers and one output sample
        const int want = kCyclesPerSample - b.cycleDebt;
        const int ran = want > 0 ? m68k_execute(want) : 0;
        b.cycleDebt = ran - want;
        if (b.cycleDebt < 0)
            b.cycleDebt = 0;
        b.scsp->run_timers(1.0 / 44100.0);
        b.scsp->service();
        b.scsp->render(s, 1);
        out[i * 2] += s[0] * b.scsp->gain(0) * gain;
        out[i * 2 + 1] += s[1] * b.scsp->gain(1) * gain;
        ++b.samplesRun;
        if (b.trace && b.samplesRun % 44100 == 0)
            std::printf("[board %llus] pc %06X sr %04X irq %X out %.4f scieb %04X scipd %04X scilv %04X %04X %04X fifo %zu midi in %llu read %llu irqs %llu\n",
                        (unsigned long long)(b.samplesRun / 44100), m68k_get_reg(nullptr, M68K_REG_PC),
                        m68k_get_reg(nullptr, M68K_REG_SR), b.irqLines, s[0], b.scsp->read(0x41e / 2), b.scsp->read(0x420 / 2),
                        b.scsp->read(0x424 / 2), b.scsp->read(0x426 / 2), b.scsp->read(0x428 / 2), b.fifo.size(), (unsigned long long)b.midiIn, (unsigned long long)b.midiReads, (unsigned long long)b.irqAsserts);
    }
}

} // namespace hotd::audio
