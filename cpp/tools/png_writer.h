// The PNG the tools write: hitbox_fit's previews and app_icon's Mac icon set.
// Stored (uncompressed) deflate blocks, so there is no zlib to link. What is
// written is small, or (the icon set) packed by iconutil, which compresses
// each picture on its way into the .icns.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace flix::tools {

inline std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0) {
    static std::uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        built = true;
    }
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

/// `width` by `height` pixels, row by row, of `channels` bytes each: 3 is RGB,
/// 4 is RGBA with straight alpha, as Canvas::getImageData returns it.
inline bool writePng(const std::string& path, int width, int height, int channels,
                     const std::vector<std::uint8_t>& pixels) {
    const std::size_t stride = static_cast<std::size_t>(width) * channels;
    std::vector<std::uint8_t> raw;
    raw.reserve((stride + 1) * height);
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), pixels.begin() + static_cast<std::ptrdiff_t>(y * stride),
                   pixels.begin() + static_cast<std::ptrdiff_t>((y + 1) * stride));
    }
    std::vector<std::uint8_t> zlib{0x78, 0x01};
    for (std::size_t pos = 0; pos < raw.size();) {
        const std::size_t len = std::min<std::size_t>(65535, raw.size() - pos);
        zlib.push_back(pos + len == raw.size() ? 1 : 0);
        zlib.push_back(static_cast<std::uint8_t>(len & 0xFF));
        zlib.push_back(static_cast<std::uint8_t>(len >> 8));
        zlib.push_back(static_cast<std::uint8_t>(~len & 0xFF));
        zlib.push_back(static_cast<std::uint8_t>((~len >> 8) & 0xFF));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                    raw.begin() + static_cast<std::ptrdiff_t>(pos + len));
        pos += len;
    }
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    const std::uint32_t adler = (b << 16) | a;
    for (int shift = 24; shift >= 0; shift -= 8) zlib.push_back(static_cast<std::uint8_t>(adler >> shift));

    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    const auto be32 = [](std::uint32_t v, std::uint8_t* out) {
        for (int i = 0; i < 4; ++i) out[i] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
    };
    const auto chunk = [&](const char* type, const std::vector<std::uint8_t>& body) {
        std::uint8_t head[8];
        be32(static_cast<std::uint32_t>(body.size()), head);
        std::copy(type, type + 4, head + 4);
        std::uint32_t crc = crc32(head + 4, 4);
        crc = crc32(body.data(), body.size(), crc);
        std::uint8_t tail[4];
        be32(crc, tail);
        std::fwrite(head, 1, 8, file);
        if (!body.empty()) std::fwrite(body.data(), 1, body.size(), file);
        std::fwrite(tail, 1, 4, file);
    };
    static const std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::fwrite(kSignature, 1, 8, file);
    std::vector<std::uint8_t> header(13, 0);
    be32(static_cast<std::uint32_t>(width), header.data());
    be32(static_cast<std::uint32_t>(height), header.data() + 4);
    header[8] = 8;                        // bit depth
    header[9] = channels == 4 ? 6 : 2;    // truecolour, with alpha or without
    chunk("IHDR", header);
    chunk("IDAT", zlib);
    chunk("IEND", {});
    return std::fclose(file) == 0;
}

} // namespace flix::tools
