// Geometry coprocessor of the arcade board (Model 2C: Fujitsu MB86235 "TGP"), A behaviour: every function the game
// calls, bit-exact in float32 (docs/coprocessor.md). A port of tools/tgp_model.py + tools/tgp_trace.py (class Tgp);
// proven against the same MAME call traces by tools/proof (hotd_proof tgp <trace>).
//
// State as the coprocessor's program keeps it: the current 3x4 matrix in the P registers (rows PR0..2, PR3..5, PR6..8,
// translation PR9..11), B-RAM (matrix stack: depth B[0], pointer B[1], entries from 0x20, 12 words each, 15 deep;
// projection constants B[2..5]; stored matrices at B[0xF8 + 12 n]) and the multiplier register MA0, which an
// arctangent of 0 / 0 returns unchanged (a quirk the game hits for zero vectors).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hotd::port {

class Tgp {
public:
    // One call's FIFO output words; bit i of unknown set = word i depends on MA0 after a function whose final MA0
    // isn't modelled (the checker skips those words).
    struct Out {
        uint32_t w[12] = {};
        int n = 0;
        uint32_t unknown = 0;
        void put(uint32_t v, bool known = true) { if (!known) unknown |= 1u << n; w[n++] = v; }
        void putf(float f, bool known = true);
    };

    // data: the coprocessor's data ROM (copro_data region: 32-bit words; functions 0x62 and 0x6E read it)
    Tgp(const uint32_t* data, size_t words);

    // Runs function n (the low byte of the command word) on its FIFO input words. False: not a function the game uses.
    bool call(int n, const uint32_t* in, int count, Out& out);
    // the current matrix (images of the x, y, z axes, then the translation: xform), for the runtime's draw list
    const float* matrix() const { return pr_; }
    // stored matrix n (0x73-0x77's slots), word k: as 0x74 loads it into the current matrix (PC Mode's shot ray)
    float stored(int n, int k) const { return bget(0xF8 + 12 * n + k); }
    // a word of the data ROM (PC Mode's camera paths: the PC's own curve evaluation on the same keys)
    uint32_t data_word(uint32_t a) const { return rom(a); }
    // words of the data ROM read as other values while set (n pairs {word, value}; nullptr / 0 = the ROM as it is):
    // the Attract Gore's PC camera keys (camera.cpp camera_path_eval)
    void set_patch(const uint32_t (*pairs)[2], int n) { patch_ = pairs, patchN_ = n; }

    // the routines, by the program's own entry points (docs/coprocessor.md)
    static float frsq(float x);                       // FRSQ as MAME runs it
    static float frcp(float x);                       // FRCP as MAME runs it
    static float sqrt_1d9(float x);                   // 0x1D9
    static float div_1cd(float a, float b);           // 0x1CD (a / b)
    static float div_1cd_ma0(float a, float b);       // what 0x1CD leaves in MA0
    static float rcp3_342(float x);                   // 0x342
    static float sin_series(float x, bool cos);       // 0x1F6 / 0x1F0
    static void sincos_21d(uint32_t angle, float& s, float& c);   // 0x21D
    // 0x31A: atan2(num, den); known = false when it returned an unknown MA0 (0 / 0)
    static float atan2_31a(float num, float den, float ma0, bool ma0Known, bool& known);
    // 0x713 (fix) / 0x6F2 (no fix): direction -> (yaw, pitch) in 16-bit angle units
    static void angles_713(float x, float y, float z, float ma0, bool ma0Known, bool fix,
                           uint32_t& yaw, bool& yawKnown, uint32_t& pitch);
    static float distance_12d(const float* p);        // function 0x1F
    static void normalize_75b(const float* v, float* out);   // function 0x0C
    static float rsq_6a0(float x);                    // 0x6A0

private:
    void push();                                      // 0x020
    void pop();                                       // 0x02A
    void rot(uint32_t angle, int r1, int r2, int sign);   // 0x0C9 / 0x0DE / 0x0F4
    void xform(float x, float y, float z, float* v) const;   // 0x10A
    static void mul(const float* n, const float* m, float* out);   // 0x6C0
    void inverse();                                   // 0x4D1
    float sphereVisible(float x, float y, float z, float rad);   // 0x51E
    void matrixAngles(bool xForward, Out& out);       // 0x368 / 0x40A
    void rayTest(int k, const float* p, const float* d, Out& out) const;   // 0x7A9
    float curve(uint32_t c, float t) const;           // 0x8E3
    void pathTable(uint32_t i, float t, Out& out) const;   // 0x8C1
    float bget(int a) const;
    void bset(int a, float v);
    uint32_t rom(uint32_t a) const
    {
        for (int i = 0; i < patchN_; ++i)
            if (patch_[i][0] == a)
                return patch_[i][1];
        return a < words_ ? data_[a] : 0;
    }

    const uint32_t* data_;
    size_t words_;
    float pr_[12] = {};
    int depth_ = 0, sp_ = 0x20;          // B[0], B[1]
    std::vector<float> b_ = std::vector<float>(0x400, 0.0f);
    float ma0_ = 0.0f;
    bool ma0Known_ = true;
    const uint32_t (*patch_)[2] = nullptr;
    int patchN_ = 0;
};

} // namespace hotd::port
