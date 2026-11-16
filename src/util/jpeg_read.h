// Minimal JPEG reader (ours, from the JPEG standard ITU-T T.81; no third-party library): baseline / extended
// sequential Huffman JPEGs (8-bit, grey or YCbCr / Adobe RGB, any sampling factors, restart markers) to RGBA8.
// Progressive and arithmetic-coded JPEGs are refused with a message (save the picture as a baseline JPEG or a PNG).
// For the players' own pictures (menu141, GitHub #83: a splash screen mod, docs/modding.md "Splash screens").
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hotd {

// false (err set) for a file that is not a readable JPEG (progressive, broken, larger than 8192 x 8192)
bool readJpegRgba(const std::string& path, int& width, int& height, std::vector<uint8_t>& rgba, std::string& err);
bool readJpegRgbaMem(const uint8_t* data, size_t size, const std::string& path, int& width, int& height,
                     std::vector<uint8_t>& rgba, std::string& err);
// a PNG (util/png_read.h) or a JPEG by its first bytes (tests: tests/unit/test_mods.cpp "jpeg_*")
bool readImageRgba(const std::string& path, int& width, int& height, std::vector<uint8_t>& rgba, std::string& err);

} // namespace hotd
