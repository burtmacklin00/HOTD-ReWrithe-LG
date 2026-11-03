#include "assets/archive.h"
#include "util/file64.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace hotd {

// ---------------------------------------------------------------------------------------------------------------
// CRC-32 (the zip / MAME polynomial 0xEDB88320)

uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc)
{
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

// ---------------------------------------------------------------------------------------------------------------
// SHA-1 (FIPS 180-4)

std::string sha1Hex(const uint8_t* data, size_t n)
{
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    auto rol = [](uint32_t v, int s) { return (v << s) | (v >> (32 - s)); };
    auto block = [&](const uint8_t* b) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = uint32_t(b[i * 4]) << 24 | uint32_t(b[i * 4 + 1]) << 16 | uint32_t(b[i * 4 + 2]) << 8 | b[i * 4 + 3];
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (bb & c) | (~bb & d); k = 0x5A827999u; }
            else if (i < 40) { f = bb ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = bb ^ c ^ d; k = 0xCA62C1D6u; }
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(bb, 30); bb = a; a = t;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
    };
    size_t i = 0;
    for (; i + 64 <= n; i += 64)
        block(data + i);
    uint8_t tail[128] = {};
    const size_t rest = n - i;
    std::memcpy(tail, data + i, rest);
    tail[rest] = 0x80;
    const size_t len = rest + 1 + 8 <= 64 ? 64 : 128;
    const uint64_t bits = uint64_t(n) * 8;
    for (int k = 0; k < 8; ++k)
        tail[len - 1 - k] = uint8_t(bits >> (8 * k));
    block(tail);
    if (len == 128)
        block(tail + 64);
    char out[41];
    for (int k = 0; k < 5; ++k)
        std::snprintf(out + k * 8, 9, "%08x", h[k]);
    return std::string(out, 40);
}

// ---------------------------------------------------------------------------------------------------------------
// Inflate (RFC 1951): canonical Huffman codes decoded by code length (counts / symbols per length)

namespace {

struct Bits {
    const uint8_t* in;
    size_t size, pos = 0;
    uint32_t buf = 0;
    int count = 0;
    bool bad = false;
    uint32_t get(int n)
    {
        while (count < n) {
            if (pos >= size) { bad = true; return 0; }
            buf |= uint32_t(in[pos++]) << count;
            count += 8;
        }
        const uint32_t v = buf & ((1u << n) - 1);
        buf >>= n;
        count -= n;
        return v;
    }
};

struct Huffman {
    uint16_t count[16] = {};
    uint16_t symbol[320] = {};
    bool build(const uint8_t* lengths, int n)
    {
        std::memset(count, 0, sizeof count);
        for (int i = 0; i < n; ++i)
            ++count[lengths[i]];
        uint16_t offs[16];
        offs[1] = 0;
        for (int l = 1; l < 15; ++l)
            offs[l + 1] = uint16_t(offs[l] + count[l]);
        for (int i = 0; i < n; ++i)
            if (lengths[i])
                symbol[offs[lengths[i]]++] = uint16_t(i);
        return true;
    }
    int decode(Bits& b) const
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= int(b.get(1));
            const int c = count[len];
            if (code - c < first)
                return symbol[index + (code - first)];
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
            if (b.bad)
                return -1;
        }
        return -1;
    }
};

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

} // namespace

bool inflateRaw(const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize)
{
    Bits b{in, inSize};
    size_t o = 0;
    Huffman fixedLit, fixedDist;
    {
        uint8_t l[288];
        for (int i = 0; i < 144; ++i) l[i] = 8;
        for (int i = 144; i < 256; ++i) l[i] = 9;
        for (int i = 256; i < 280; ++i) l[i] = 7;
        for (int i = 280; i < 288; ++i) l[i] = 8;
        fixedLit.build(l, 288);
        uint8_t d[30];
        for (int i = 0; i < 30; ++i) d[i] = 5;
        fixedDist.build(d, 30);
    }
    for (;;) {
        const uint32_t last = b.get(1), type = b.get(2);
        if (b.bad)
            return false;
        if (type == 0) {
            b.buf = 0;
            b.count = 0;
            if (b.pos + 4 > inSize)
                return false;
            const uint32_t len = uint32_t(in[b.pos]) | uint32_t(in[b.pos + 1]) << 8;
            b.pos += 4;
            if (b.pos + len > inSize || o + len > outSize)
                return false;
            std::memcpy(out + o, in + b.pos, len);
            b.pos += len;
            o += len;
        } else if (type == 1 || type == 2) {
            Huffman dynLit, dynDist;
            const Huffman* lit = &fixedLit;
            const Huffman* dist = &fixedDist;
            if (type == 2) {
                const int nlen = int(b.get(5)) + 257, ndist = int(b.get(5)) + 1, ncode = int(b.get(4)) + 4;
                static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
                uint8_t cl[19] = {};
                for (int i = 0; i < ncode; ++i)
                    cl[order[i]] = uint8_t(b.get(3));
                Huffman lencode;
                lencode.build(cl, 19);
                uint8_t lengths[320] = {};
                int i = 0;
                while (i < nlen + ndist) {
                    const int sym = lencode.decode(b);
                    if (sym < 0)
                        return false;
                    if (sym < 16) {
                        lengths[i++] = uint8_t(sym);
                        continue;
                    }
                    int rep = 0;
                    uint8_t val = 0;
                    if (sym == 16) {
                        if (i == 0)
                            return false;
                        val = lengths[i - 1];
                        rep = 3 + int(b.get(2));
                    } else if (sym == 17) {
                        rep = 3 + int(b.get(3));
                    } else {
                        rep = 11 + int(b.get(7));
                    }
                    if (i + rep > nlen + ndist)
                        return false;
                    while (rep--)
                        lengths[i++] = val;
                }
                dynLit.build(lengths, nlen);
                dynDist.build(lengths + nlen, ndist);
                lit = &dynLit;
                dist = &dynDist;
            }
            for (;;) {
                int sym = lit->decode(b);
                if (sym < 0)
                    return false;
                if (sym < 256) {
                    if (o >= outSize)
                        return false;
                    out[o++] = uint8_t(sym);
                } else if (sym == 256) {
                    break;
                } else {
                    sym -= 257;
                    if (sym >= 29)
                        return false;
                    const size_t len = kLenBase[sym] + b.get(kLenExtra[sym]);
                    const int ds = dist->decode(b);
                    if (ds < 0 || ds >= 30)
                        return false;
                    const size_t d = kDistBase[ds] + b.get(kDistExtra[ds]);
                    if (d > o || o + len > outSize)
                        return false;
                    for (size_t k = 0; k < len; ++k, ++o)
                        out[o] = out[o - d];
                }
            }
        } else {
            return false;
        }
        if (last)
            return o == outSize;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Zip: the end-of-central-directory record, then the central directory's entries

namespace {
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

bool readAt(FILE* f, uint64_t off, uint8_t* out, size_t n)
{
    if (hotd::fseek64(f, int64_t(off), SEEK_SET) != 0)
        return false;
    return std::fread(out, 1, n, f) == n;
}
} // namespace

bool ZipFile::open(const std::string& path, std::string& err)
{
    path_ = path;
    entries_.clear();
    FILE* f = hotd::fopenLong(path);
    if (!f) {
        err = path + ": cannot open";
        return false;
    }
    hotd::fseek64(f, 0, SEEK_END);
    const uint64_t size = uint64_t(hotd::ftell64(f));
    const size_t tailSize = size_t(size < 0x10000 + 22 ? size : 0x10000 + 22);
    std::vector<uint8_t> tail(tailSize);
    if (tailSize < 22 || !readAt(f, size - tailSize, tail.data(), tailSize)) {
        std::fclose(f);
        err = path + ": not a zip file";
        return false;
    }
    size_t eocd = std::string::npos;
    for (size_t i = tailSize - 22 + 1; i-- > 0;)
        if (le32(&tail[i]) == 0x06054B50u) {
            eocd = i;
            break;
        }
    if (eocd == std::string::npos) {
        std::fclose(f);
        err = path + ": not a zip file (no central directory)";
        return false;
    }
    const uint32_t cdSize = le32(&tail[eocd + 12]), cdOff = le32(&tail[eocd + 16]);
    std::vector<uint8_t> cd(cdSize);
    if (!readAt(f, cdOff, cd.data(), cdSize)) {
        std::fclose(f);
        err = path + ": broken central directory";
        return false;
    }
    std::fclose(f);
    for (size_t p = 0; p + 46 <= cd.size() && le32(&cd[p]) == 0x02014B50u;) {
        Entry e;
        e.method = le16(&cd[p + 10]);
        e.crc = le32(&cd[p + 16]);
        e.compSize = le32(&cd[p + 20]);
        e.size = le32(&cd[p + 24]);
        const uint16_t n = le16(&cd[p + 28]), x = le16(&cd[p + 30]), c = le16(&cd[p + 32]);
        e.localOffset = le32(&cd[p + 42]);
        if (p + 46 + n > cd.size())
            break;
        e.name.assign(reinterpret_cast<const char*>(&cd[p + 46]), n);
        if (!e.name.empty() && e.name.back() != '/')
            entries_.push_back(e);
        p += 46 + n + x + c;
    }
    return true;
}

bool ZipFile::read(const Entry& e, std::vector<uint8_t>& out, std::string& err) const
{
    FILE* f = hotd::fopenLong(path_);
    if (!f) {
        err = path_ + ": cannot open";
        return false;
    }
    uint8_t lh[30];
    bool ok = readAt(f, e.localOffset, lh, 30) && le32(lh) == 0x04034B50u;
    std::vector<uint8_t> comp;
    if (ok) {
        comp.resize(size_t(e.compSize));
        ok = readAt(f, e.localOffset + 30 + le16(lh + 26) + le16(lh + 28), comp.data(), comp.size());
    }
    std::fclose(f);
    if (!ok) {
        err = path_ + ": " + e.name + ": cannot read";
        return false;
    }
    out.assign(size_t(e.size), 0);
    if (e.method == 0 && e.compSize == e.size) {
        out = std::move(comp);
    } else if (e.method != 8 || !inflateRaw(comp.data(), comp.size(), out.data(), out.size())) {
        err = path_ + ": " + e.name + ": " + (e.method == 8 ? "broken deflate data" : "unsupported compression " + std::to_string(e.method));
        return false;
    }
    return true;
}

bool ZipFile::dataOffset(const Entry& e, uint64_t& off, std::string& err) const
{
    FILE* f = hotd::fopenLong(path_);
    if (!f) {
        err = path_ + ": cannot open";
        return false;
    }
    uint8_t lh[30];
    const bool ok = readAt(f, e.localOffset, lh, 30) && le32(lh) == 0x04034B50u;
    std::fclose(f);
    if (!ok) {
        err = path_ + ": " + e.name + ": cannot read";
        return false;
    }
    off = e.localOffset + 30 + le16(lh + 26) + le16(lh + 28);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------

namespace {
std::map<std::string, std::string>& pcMap()
{
    static std::map<std::string, std::string> m;
    return m;
}
std::string mapKey(std::string rel)
{
    for (char& c : rel)
        c = c == '\\' ? '/' : char(std::tolower(uint8_t(c)));
    return rel;
}
} // namespace

void setPcFileMap(std::map<std::string, std::string> m)
{
    pcMap().clear();
    for (auto& [k, v] : m)
        pcMap()[mapKey(k)] = v;
}

std::string pcFileMapped(const std::string& rel)
{
    auto it = pcMap().find(mapKey(rel));
    return it == pcMap().end() ? std::string() : it->second;
}

} // namespace hotd
