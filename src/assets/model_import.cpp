// Model packs: the glTF 2.0 reader (our own; the format: Khronos' glTF 2.0 specification). See model_import.h.
#include "assets/model_import.h"

#include "util/png_read.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace hotd {

std::string importInsideDir(const std::string& dir, const std::string& name)
{
    const fs::path n = fs::path(name);
    if (name.empty() || n.has_root_name() || n.has_root_directory())
        return std::string();
    for (const fs::path& part : n)
        if (part == "..")
            return std::string();
    return (fs::path(dir) / n).string();
}

void importRotation(const float deg[3], float m[9])
{
    const float k = 3.14159265358979f / 180.0f;
    const float cx = std::cos(deg[0] * k), sx = std::sin(deg[0] * k);
    const float cy = std::cos(deg[1] * k), sy = std::sin(deg[1] * k);
    const float cz = std::cos(deg[2] * k), sz = std::sin(deg[2] * k);
    // Rz * Ry * Rx (x first)
    const float rx[9] = {1, 0, 0, 0, cx, -sx, 0, sx, cx};
    const float ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy};
    const float rz[9] = {cz, -sz, 0, sz, cz, 0, 0, 0, 1};
    float t[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            t[i * 3 + j] = 0;
            for (int q = 0; q < 3; ++q) t[i * 3 + j] += ry[i * 3 + q] * rx[q * 3 + j];
        }
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            m[i * 3 + j] = 0;
            for (int q = 0; q < 3; ++q) m[i * 3 + j] += rz[i * 3 + q] * t[q * 3 + j];
        }
}

namespace {

// ---- a minimal JSON reader (objects, arrays, strings, numbers, true / false / null) ----
struct Json {
    enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
    double num = 0;
    bool b = false;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;
    const Json* get(const char* key) const
    {
        if (kind != Obj) return nullptr;
        for (const auto& [k, v] : obj)
            if (k == key) return &v;
        return nullptr;
    }
    const Json* at(size_t i) const { return kind == Arr && i < arr.size() ? &arr[i] : nullptr; }
    double numOr(const char* key, double def) const { const Json* j = get(key); return j && j->kind == Num ? j->num : def; }
    int intOr(const char* key, int def) const { return int(numOr(key, def)); }
    std::string strOr(const char* key) const { const Json* j = get(key); return j && j->kind == Str ? j->str : std::string(); }
};

struct JsonParser {
    const char* p;
    const char* end;
    int depth = 0;
    std::string err;
    void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
    bool fail(const char* what) { if (err.empty()) err = what; return false; }
    bool value(Json& out)
    {
        if (++depth > 64) return fail("nested too deeply");
        ws();
        if (p >= end) return fail("unexpected end");
        bool ok = true;
        if (*p == '{') {
            out.kind = Json::Obj;
            ++p;
            ws();
            if (p < end && *p == '}') ++p;
            else
                for (;;) {
                    ws();
                    std::string k;
                    if (p >= end || *p != '"' || !string(k)) { ok = fail("bad object key"); break; }
                    ws();
                    if (p >= end || *p != ':') { ok = fail("':' expected"); break; }
                    ++p;
                    out.obj.emplace_back(std::move(k), Json());
                    if (!value(out.obj.back().second)) { ok = false; break; }
                    ws();
                    if (p < end && *p == ',') { ++p; continue; }
                    if (p < end && *p == '}') { ++p; break; }
                    ok = fail("',' or '}' expected");
                    break;
                }
        } else if (*p == '[') {
            out.kind = Json::Arr;
            ++p;
            ws();
            if (p < end && *p == ']') ++p;
            else
                for (;;) {
                    out.arr.emplace_back();
                    if (!value(out.arr.back())) { ok = false; break; }
                    ws();
                    if (p < end && *p == ',') { ++p; continue; }
                    if (p < end && *p == ']') { ++p; break; }
                    ok = fail("',' or ']' expected");
                    break;
                }
        } else if (*p == '"') {
            out.kind = Json::Str;
            ok = string(out.str);
        } else if (end - p >= 4 && !std::strncmp(p, "true", 4)) {
            out.kind = Json::Bool, out.b = true, p += 4;
        } else if (end - p >= 5 && !std::strncmp(p, "false", 5)) {
            out.kind = Json::Bool, p += 5;
        } else if (end - p >= 4 && !std::strncmp(p, "null", 4)) {
            p += 4;
        } else if (*p == '-' || (*p >= '0' && *p <= '9')) {
            char buf[64];
            size_t n = 0;
            while (p < end && n + 1 < sizeof buf && (std::strchr("+-.eE", *p) || (*p >= '0' && *p <= '9'))) buf[n++] = *p++;
            buf[n] = 0;
            char* e = nullptr;
            out.kind = Json::Num;
            out.num = std::strtod(buf, &e);
            if (!e || *e) ok = fail("bad number");
        } else {
            ok = fail("unexpected character");
        }
        --depth;
        return ok;
    }
    bool string(std::string& s)
    {
        ++p;   // the opening quote
        while (p < end && *p != '"') {
            if (*p == '\\') {
                if (++p >= end) return fail("bad escape");
                const char c = *p++;
                switch (c) {
                case 'n': s += '\n'; break;
                case 't': s += '\t'; break;
                case 'r': s += '\r'; break;
                case 'b': s += '\b'; break;
                case 'f': s += '\f'; break;
                case 'u': {
                    if (end - p < 4) return fail("bad \\u escape");
                    const unsigned cp = unsigned(std::strtoul(std::string(p, 4).c_str(), nullptr, 16));
                    p += 4;
                    if (cp < 0x80) s += char(cp);
                    else if (cp < 0x800) { s += char(0xC0 | cp >> 6); s += char(0x80 | (cp & 0x3F)); }
                    else { s += char(0xE0 | cp >> 12); s += char(0x80 | ((cp >> 6) & 0x3F)); s += char(0x80 | (cp & 0x3F)); }
                    break;
                }
                default: s += c; break;   // \" \\ \/
                }
            } else {
                s += *p++;
            }
        }
        if (p >= end) return fail("unterminated string");
        ++p;
        return true;
    }
};

bool readModelBytes(const std::string& path, std::vector<uint8_t>& out, std::string& err)
{
    std::ifstream f(fs::path(path), std::ios::binary);
    if (!f) { err = path + ": cannot open"; return false; }
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0 || size_t(n) > kImportMaxFileBytes) { err = path + ": too large (64 MB at most)"; return false; }
    f.seekg(0);
    out.resize(size_t(n));
    if (n > 0 && !f.read(reinterpret_cast<char*>(out.data()), n)) { err = path + ": cannot read"; return false; }
    return true;
}

bool base64(const std::string& s, std::vector<uint8_t>& out)
{
    uint32_t acc = 0;
    int bits = 0;
    for (char c : s) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        else return false;
        acc = acc << 6 | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t(acc >> bits));
        }
        if (out.size() > kImportMaxFileBytes) return false;
    }
    return true;
}

// a URI's bytes: data:...;base64,<data> or a file inside the model's folder
bool uriBytes(const std::string& uri, const std::string& dir, std::vector<uint8_t>& out, std::string& err)
{
    if (uri.rfind("data:", 0) == 0) {
        const size_t comma = uri.find(',');
        if (comma == std::string::npos || uri.find(";base64") > comma) { err = "data: URI without base64"; return false; }
        if (!base64(uri.substr(comma + 1), out)) { err = "bad base64 data"; return false; }
        return true;
    }
    std::string name;   // %20 and friends
    for (size_t i = 0; i < uri.size(); ++i)
        if (uri[i] == '%' && i + 2 < uri.size()) { name += char(std::strtoul(uri.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; }
        else name += uri[i];
    const std::string file = importInsideDir(dir, name);
    if (file.empty()) { err = uri + ": outside the mod's folder (refused)"; return false; }
    return readModelBytes(file, out, err);
}

struct Gltf {
    Json j;
    std::vector<std::vector<uint8_t>> buffers;
    std::string dir, path, err;

    bool fail(const std::string& e) { if (err.empty()) err = path + ": " + e; return false; }

    // a bufferView's bytes (pointer + length, stride)
    bool view(int index, const uint8_t*& data, size_t& len, size_t& stride)
    {
        const Json* bv = j.get("bufferViews") ? j.get("bufferViews")->at(size_t(index)) : nullptr;
        if (!bv || index < 0) return fail("bad bufferView " + std::to_string(index));
        const int b = bv->intOr("buffer", -1);
        if (b < 0 || size_t(b) >= buffers.size()) return fail("bufferView " + std::to_string(index) + ": bad buffer");
        const double off = bv->numOr("byteOffset", 0), n = bv->numOr("byteLength", -1);
        if (off < 0 || n < 0 || off + n > double(buffers[size_t(b)].size())) return fail("bufferView " + std::to_string(index) + " outside its buffer");
        data = buffers[size_t(b)].data() + size_t(off);
        len = size_t(n);
        stride = size_t(std::max(0.0, bv->numOr("byteStride", 0)));
        return true;
    }

    // an accessor as floats (count x comps; normalized integers scaled to 0..1 / -1..1)
    bool accessor(int index, int wantComps, std::vector<float>& out, size_t& count)
    {
        const Json* a = j.get("accessors") ? j.get("accessors")->at(size_t(index)) : nullptr;
        if (!a || index < 0) return fail("bad accessor " + std::to_string(index));
        if (a->get("sparse")) return fail("sparse accessors are not supported");
        const std::string type = a->strOr("type");
        const int comps = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : 0;
        if (!comps || comps < wantComps) return fail("accessor " + std::to_string(index) + ": type " + type);
        const int ct = a->intOr("componentType", 0);
        const int size = ct == 5120 || ct == 5121 ? 1 : ct == 5122 || ct == 5123 ? 2 : ct == 5125 || ct == 5126 ? 4 : 0;
        if (!size) return fail("accessor " + std::to_string(index) + ": component type " + std::to_string(ct));
        const double c = a->numOr("count", -1);
        if (c < 0 || c > double(kImportMaxTriangles) * 3) return fail("accessor " + std::to_string(index) + ": bad count");
        count = size_t(c);
        out.assign(count * size_t(wantComps), 0.0f);
        if (!a->get("bufferView")) return true;   // (all zeros, as the specification says)
        const uint8_t* d;
        size_t len, stride;
        if (!view(a->intOr("bufferView", -1), d, len, stride)) return false;
        const size_t elem = size_t(comps) * size_t(size);
        if (!stride) stride = elem;
        const double off = a->numOr("byteOffset", 0);
        if (off < 0 || (count && size_t(off) + (count - 1) * stride + elem > len)) return fail("accessor " + std::to_string(index) + " outside its bufferView");
        const bool norm = a->get("normalized") && a->get("normalized")->b;
        for (size_t i = 0; i < count; ++i)
            for (int k = 0; k < wantComps; ++k) {
                const uint8_t* q = d + size_t(off) + i * stride + size_t(k) * size_t(size);
                float v = 0;
                switch (ct) {
                case 5120: { int8_t x; std::memcpy(&x, q, 1); v = norm ? std::max(x / 127.0f, -1.0f) : x; break; }
                case 5121: v = norm ? q[0] / 255.0f : q[0]; break;
                case 5122: { int16_t x; std::memcpy(&x, q, 2); v = norm ? std::max(x / 32767.0f, -1.0f) : x; break; }
                case 5123: { uint16_t x; std::memcpy(&x, q, 2); v = norm ? x / 65535.0f : x; break; }
                case 5125: { uint32_t x; std::memcpy(&x, q, 4); v = float(x); break; }
                default: std::memcpy(&v, q, 4); break;
                }
                if (!std::isfinite(v)) return fail("accessor " + std::to_string(index) + ": not a number");
                out[i * size_t(wantComps) + size_t(k)] = v;
            }
        return true;
    }
    // indices as integers (exact for u32, unlike the float path)
    bool indices(int index, std::vector<uint32_t>& out)
    {
        const Json* a = j.get("accessors") ? j.get("accessors")->at(size_t(index)) : nullptr;
        if (!a || index < 0 || a->strOr("type") != "SCALAR") return fail("bad index accessor " + std::to_string(index));
        const int ct = a->intOr("componentType", 0);
        const int size = ct == 5121 ? 1 : ct == 5123 ? 2 : ct == 5125 ? 4 : 0;
        if (!size) return fail("index accessor " + std::to_string(index) + ": component type " + std::to_string(ct));
        const double c = a->numOr("count", -1);
        if (c < 0 || c > double(kImportMaxTriangles) * 3) return fail("index accessor " + std::to_string(index) + ": bad count");
        const uint8_t* d;
        size_t len, stride;
        if (!view(a->intOr("bufferView", -1), d, len, stride)) return false;
        if (!stride) stride = size_t(size);
        const double off = a->numOr("byteOffset", 0);
        const size_t count = size_t(c);
        if (off < 0 || (count && size_t(off) + (count - 1) * stride + size_t(size) > len)) return fail("index accessor outside its bufferView");
        out.resize(count);
        for (size_t i = 0; i < count; ++i) {
            const uint8_t* q = d + size_t(off) + i * stride;
            uint32_t v = q[0];
            if (size == 2) { uint16_t x; std::memcpy(&x, q, 2); v = x; }
            if (size == 4) std::memcpy(&v, q, 4);
            out[i] = v;
        }
        return true;
    }
};

// column-major 4 x 4 (glTF's order)
using M4 = std::array<float, 16>;
M4 mul(const M4& a, const M4& b)
{
    M4 r{};
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[size_t(k * 4 + rr)] * b[size_t(c * 4 + k)];
            r[size_t(c * 4 + rr)] = s;
        }
    return r;
}
M4 nodeMatrix(const Json& n)
{
    M4 m = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (const Json* mj = n.get("matrix"); mj && mj->kind == Json::Arr && mj->arr.size() == 16) {
        for (size_t i = 0; i < 16; ++i) m[i] = float(mj->arr[i].num);
        return m;
    }
    float t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
    if (const Json* a = n.get("translation"); a && a->arr.size() == 3) for (int i = 0; i < 3; ++i) t[i] = float(a->arr[size_t(i)].num);
    if (const Json* a = n.get("rotation"); a && a->arr.size() == 4) for (int i = 0; i < 4; ++i) q[i] = float(a->arr[size_t(i)].num);
    if (const Json* a = n.get("scale"); a && a->arr.size() == 3) for (int i = 0; i < 3; ++i) s[i] = float(a->arr[size_t(i)].num);
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
                        2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                        2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)};   // row-major
    for (int c = 0; c < 3; ++c)
        for (int rr = 0; rr < 3; ++rr) m[size_t(c * 4 + rr)] = r[rr * 3 + c] * s[c];
    for (int i = 0; i < 3; ++i) m[size_t(12 + i)] = t[i];
    return m;
}

} // namespace

bool readGltf(const std::string& path, ImportModel& out, std::string& err)
{
    std::vector<uint8_t> file;
    if (!readModelBytes(path, file, err)) return false;
    Gltf g;
    g.path = path;
    g.dir = fs::path(path).parent_path().string();
    std::string text;
    std::vector<uint8_t> glbBin;
    bool haveBin = false;
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, &file[o], 4); return v; };
    if (file.size() >= 12 && u32(0) == 0x46546C67u) {   // .glb: 'glTF', version, length, then JSON and BIN chunks
        if (u32(4) != 2) { err = path + ": glTF version " + std::to_string(u32(4)) + " (2 expected)"; return false; }
        for (size_t o = 12; o + 8 <= file.size();) {
            const uint32_t len = u32(o), type = u32(o + 4);
            if (size_t(len) > file.size() - o - 8) { err = path + ": broken GLB chunk"; return false; }
            if (type == 0x4E4F534Au && text.empty()) text.assign(reinterpret_cast<const char*>(&file[o + 8]), len);
            else if (type == 0x004E4942u && !haveBin) { glbBin.assign(file.begin() + std::ptrdiff_t(o + 8), file.begin() + std::ptrdiff_t(o + 8 + len)); haveBin = true; }
            o += 8 + size_t(len);
        }
        if (text.empty()) { err = path + ": GLB without its JSON chunk"; return false; }
    } else {
        text.assign(file.begin(), file.end());
    }
    JsonParser jp{text.data(), text.data() + text.size()};
    if (!jp.value(g.j) || g.j.kind != Json::Obj) { err = path + ": not glTF JSON (" + (jp.err.empty() ? "not an object" : jp.err) + ")"; return false; }
    if (const Json* as = g.j.get("asset"); !as || as->strOr("version").rfind("2", 0) != 0) {
        err = path + ": not a glTF 2.0 file (asset.version)";
        return false;
    }
    if (const Json* bs = g.j.get("buffers"))
        for (size_t i = 0; i < bs->arr.size(); ++i) {
            const std::string uri = bs->arr[i].strOr("uri");
            g.buffers.emplace_back();
            if (uri.empty()) {
                if (i != 0 || !haveBin) { err = path + ": buffer " + std::to_string(i) + " has no data"; return false; }
                g.buffers.back() = std::move(glbBin);
            } else if (!uriBytes(uri, g.dir, g.buffers.back(), err)) {
                err = path + ": buffer " + std::to_string(i) + ": " + err;
                return false;
            }
            if (double(g.buffers.back().size()) < bs->arr[i].numOr("byteLength", 0)) { err = path + ": buffer " + std::to_string(i) + " shorter than its byteLength"; return false; }
        }
    // materials: name, base colour, its picture (PNG)
    std::map<int, int> matIndex;   // glTF material -> out.mats
    auto material = [&](int m) -> int {
        if (m < 0) return -1;
        if (auto it = matIndex.find(m); it != matIndex.end()) return it->second;
        const Json* mj = g.j.get("materials") ? g.j.get("materials")->at(size_t(m)) : nullptr;
        ImportMaterial im;
        im.name = mj ? mj->strOr("name") : std::string();
        if (im.name.empty()) im.name = "material" + std::to_string(m);
        const Json* pbr = mj ? mj->get("pbrMetallicRoughness") : nullptr;
        if (const Json* f = pbr ? pbr->get("baseColorFactor") : nullptr; f && f->arr.size() >= 3)
            for (int c = 0; c < 3; ++c) im.kd[c] = float(f->arr[size_t(c)].num);
        const Json* bt = pbr ? pbr->get("baseColorTexture") : nullptr;
        const Json* tex = bt && g.j.get("textures") ? g.j.get("textures")->at(size_t(bt->intOr("index", -1))) : nullptr;
        const Json* img = tex && g.j.get("images") ? g.j.get("images")->at(size_t(tex->intOr("source", -1))) : nullptr;
        if (img) {
            const std::string uri = img->strOr("uri"), mime = img->strOr("mimeType");
            if (!uri.empty() && uri.rfind("data:", 0) != 0) {
                std::string name;
                for (size_t i = 0; i < uri.size(); ++i)
                    if (uri[i] == '%' && i + 2 < uri.size()) { name += char(std::strtoul(uri.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; }
                    else name += uri[i];
                im.picture = importInsideDir(g.dir, name);
                if (im.picture.empty()) std::fprintf(stderr, "mods: %s: image %s outside the mod's folder (ignored)\n", path.c_str(), uri.c_str());
            } else {
                std::vector<uint8_t> bytes;
                std::string e;
                bool ok = false;
                if (!uri.empty()) ok = uriBytes(uri, g.dir, bytes, e);
                else if (img->get("bufferView")) {
                    const uint8_t* d;
                    size_t len, stride;
                    if (g.view(img->intOr("bufferView", -1), d, len, stride)) bytes.assign(d, d + len), ok = true;
                    else e = g.err, g.err.clear();
                }
                auto pic = std::make_shared<ReplImage>();
                if (ok && readPngRgbaMem(bytes.data(), bytes.size(), path + " (image " + std::to_string(tex->intOr("source", -1)) + ")",
                                         pic->width, pic->height, pic->rgba, e))
                    im.image = pic;
                else
                    std::fprintf(stderr, "mods: %s: material %s: its picture is not a readable PNG (%s%s): its colour is used\n", path.c_str(),
                                 im.name.c_str(), mime.c_str(), e.empty() ? "" : (", " + e).c_str());
            }
        }
        const int idx = int(out.mats.size());
        out.mats.push_back(std::move(im));
        matIndex[m] = idx;
        return idx;
    };
    const Json* nodes = g.j.get("nodes");
    const Json* meshes = g.j.get("meshes");
    if (!meshes || meshes->arr.empty()) { err = path + ": no meshes"; return false; }
    // the nodes to walk: the default scene's roots, else every node no other node holds
    std::vector<int> roots;
    if (const Json* sc = g.j.get("scenes"); sc && sc->kind == Json::Arr && !sc->arr.empty()) {
        const Json* s = sc->at(size_t(std::max(0, g.j.intOr("scene", 0))));
        if (s && s->get("nodes"))
            for (const Json& n : s->get("nodes")->arr) roots.push_back(int(n.num));
    } else if (nodes) {
        std::set<int> held;
        for (const Json& n : nodes->arr)
            if (const Json* ch = n.get("children")) for (const Json& c : ch->arr) held.insert(int(c.num));
        for (int i = 0; i < int(nodes->arr.size()); ++i)
            if (!held.count(i)) roots.push_back(i);
    }
    bool skinned = false;
    std::vector<std::pair<int, M4>> stack;
    for (int r : roots) stack.push_back({r, M4{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}});
    size_t visited = 0;
    while (!stack.empty()) {
        auto [ni, parent] = stack.back();
        stack.pop_back();
        const Json* n = nodes ? nodes->at(size_t(ni)) : nullptr;
        if (!n || ni < 0) { err = path + ": bad node " + std::to_string(ni); return false; }
        if (++visited > 100000) { err = path + ": node cycle"; return false; }
        const M4 m = mul(parent, nodeMatrix(*n));
        if (const Json* ch = n->get("children"))
            for (const Json& c : ch->arr) stack.push_back({int(c.num), m});
        if (!n->get("mesh")) continue;
        const Json* mesh = meshes->at(size_t(n->intOr("mesh", -1)));
        if (!mesh) { err = path + ": node " + std::to_string(ni) + ": bad mesh"; return false; }
        skinned |= n->get("skin") != nullptr;
        // a skinned mesh: its bind pose (the node's own transform does not apply, glTF 2.0 3.7.3)
        const M4 mm = n->get("skin") ? M4{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1} : m;
        const Json* prims = mesh->get("primitives");
        if (!prims) continue;
        for (const Json& pr : prims->arr) {
            const int mode = pr.intOr("mode", 4);
            if (mode != 4 && mode != 5 && mode != 6) continue;   // (points / lines: nothing to draw)
            const Json* at = pr.get("attributes");
            if (!at || !at->get("POSITION")) { err = path + ": a primitive without POSITION"; return false; }
            std::vector<float> pos, uv;
            size_t np = 0, nt = 0;
            if (!g.accessor(at->intOr("POSITION", -1), 3, pos, np)) { err = g.err; return false; }
            if (at->get("TEXCOORD_0") && !g.accessor(at->intOr("TEXCOORD_0", -1), 2, uv, nt)) { err = g.err; return false; }
            if (nt && nt != np) { err = path + ": TEXCOORD_0 and POSITION differ in count"; return false; }
            std::vector<uint32_t> idx;
            if (pr.get("indices")) {
                if (!g.indices(pr.intOr("indices", -1), idx)) { err = g.err; return false; }
            } else {
                idx.resize(np);
                for (size_t i = 0; i < np; ++i) idx[i] = uint32_t(i);
            }
            for (uint32_t i : idx)
                if (i >= np) { err = path + ": index " + std::to_string(i) + " past the vertices (" + std::to_string(np) + ")"; return false; }
            const int mat = material(pr.intOr("material", -1));
            auto emit = [&](uint32_t a, uint32_t b, uint32_t c) {
                ImportModel::Tri t{};
                const uint32_t q[3] = {a, b, c};
                for (int k = 0; k < 3; ++k) {
                    const float* p = &pos[size_t(q[k]) * 3];
                    for (int r = 0; r < 3; ++r) t.p[k][r] = mm[size_t(r)] * p[0] + mm[size_t(4 + r)] * p[1] + mm[size_t(8 + r)] * p[2] + mm[size_t(12 + r)];
                    if (nt) { t.t[k][0] = uv[size_t(q[k]) * 2]; t.t[k][1] = 1.0f - uv[size_t(q[k]) * 2 + 1]; }   // glTF v runs down
                }
                t.mat = mat;
                out.tris.push_back(t);
            };
            if (mode == 4)
                for (size_t i = 0; i + 2 < idx.size(); i += 3) emit(idx[i], idx[i + 1], idx[i + 2]);
            else if (mode == 5)
                for (size_t i = 0; i + 2 < idx.size(); ++i) i & 1 ? emit(idx[i + 1], idx[i], idx[i + 2]) : emit(idx[i], idx[i + 1], idx[i + 2]);
            else
                for (size_t i = 1; i + 1 < idx.size(); ++i) emit(idx[0], idx[i], idx[i + 1]);
            if (out.tris.size() > kImportMaxTriangles) { err = path + ": more than 1048576 triangles"; return false; }
        }
    }
    if (skinned) std::fprintf(stderr, "mods: %s: skinned mesh drawn in its bind pose (skins are not used: docs/modding.md)\n", path.c_str());
    if (out.tris.empty()) { err = path + ": no triangles in its scene"; return false; }
    return true;
}

} // namespace hotd
