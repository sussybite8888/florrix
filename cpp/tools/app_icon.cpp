// Writes the program's icon as a Windows .ico, for the Windows build to
// compile into its .exe files (cpp/CMakeLists.txt), or as a Mac .iconset, the
// folder of PNGs iconutil packs into the .icns the Mac app shows
// (scripts/build-native.js). It is drawn by the world renderer from the staged
// content, as the client draws its window icon, so it needs the content
// directory.
//
//   app_icon <data dir> <out.ico | out.iconset> [--preview <out.ppm>]
//
// Each size an icon is asked for is drawn at that size rather than shrunk
// from the biggest, so the small ones stay as sharp as vector art can be at
// that many pixels. In the .ico (WorldRenderer::drawAppIcon) every entry is a
// 32-bit bitmap with its own alpha, which every Windows from XP on reads. The
// .iconset (WorldRenderer::drawMacAppIcon) holds each of the Mac's point sizes
// at 1x and 2x, under the names iconutil reads. --preview also writes the 256
// pixel one over grey, as a PPM a person can look at.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "canvas.h"
#include "client/render/sprites.h"
#include "client/render/world_renderer.h"
#include "shared/game/config.h"
#include "tools/png_writer.h"

namespace {

void put16(std::vector<std::uint8_t>& out, unsigned v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    put16(out, v & 0xFFFFu);
    put16(out, v >> 16);
}

/// One icon image: a BITMAPINFOHEADER over twice the height (the colour rows
/// and then the AND mask, as an icon's bitmap is laid out), the BGRA rows
/// bottom up, and an all-clear mask, since the alpha says it all.
std::vector<std::uint8_t> iconImage(const flix::WorldRenderer& renderer, int size) {
    Canvas canvas = Canvas::createVirtual(size, size);
    renderer.drawAppIcon(canvas, size);
    const std::vector<std::uint8_t> rgba = canvas.getImageData(0, 0, size, size);

    std::vector<std::uint8_t> out;
    put32(out, 40);
    put32(out, static_cast<std::uint32_t>(size));
    put32(out, static_cast<std::uint32_t>(size * 2));
    put16(out, 1);
    put16(out, 32);
    for (int i = 0; i < 6; ++i) put32(out, 0);
    for (int y = size - 1; y >= 0; --y) {
        for (int x = 0; x < size; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 4;
            out.push_back(rgba[i + 2]);
            out.push_back(rgba[i + 1]);
            out.push_back(rgba[i + 0]);
            out.push_back(rgba[i + 3]);
        }
    }
    const std::size_t maskRow = static_cast<std::size_t>((size + 31) / 32) * 4;
    out.insert(out.end(), maskRow * static_cast<std::size_t>(size), 0);
    return out;
}

bool writeFile(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    return std::fclose(file) == 0 && ok;
}

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool writeIco(const flix::WorldRenderer& renderer, const std::string& path) {
    static const int kSizes[] = {256, 128, 64, 48, 32, 24, 16};
    constexpr int kCount = static_cast<int>(sizeof kSizes / sizeof kSizes[0]);
    std::vector<std::vector<std::uint8_t>> images;
    for (const int size : kSizes) images.push_back(iconImage(renderer, size));

    std::vector<std::uint8_t> ico;
    put16(ico, 0);
    put16(ico, 1);   // an icon, not a cursor
    put16(ico, kCount);
    std::uint32_t offset = 6 + 16 * kCount;
    for (int i = 0; i < kCount; ++i) {
        // A dimension of 256 is written as 0: the field is one byte.
        ico.push_back(static_cast<std::uint8_t>(kSizes[i] & 0xFF));
        ico.push_back(static_cast<std::uint8_t>(kSizes[i] & 0xFF));
        ico.push_back(0);
        ico.push_back(0);
        put16(ico, 1);
        put16(ico, 32);
        put32(ico, static_cast<std::uint32_t>(images[i].size()));
        put32(ico, offset);
        offset += static_cast<std::uint32_t>(images[i].size());
    }
    for (const auto& image : images) ico.insert(ico.end(), image.begin(), image.end());
    return writeFile(path, ico);
}

/// icon_<points>x<points>[@2x].png for every point size a Mac shows an app's
/// icon at, from a 16 point Finder list row to a 512 point Quick Look.
bool writeIconset(const flix::WorldRenderer& renderer, const std::string& dir) {
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error) return false;
    static const int kPoints[] = {16, 32, 128, 256, 512};
    for (const int points : kPoints) {
        for (const int scale : {1, 2}) {
            const int size = points * scale;
            Canvas canvas = Canvas::createVirtual(size, size);
            renderer.drawMacAppIcon(canvas, size);
            const std::string name = "icon_" + std::to_string(points) + "x" +
                                     std::to_string(points) + (scale == 2 ? "@2x" : "") + ".png";
            if (!flix::tools::writePng(dir + "/" + name, size, size, 4,
                                       canvas.getImageData(0, 0, size, size))) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3 && !(argc == 5 && std::string(argv[3]) == "--preview")) {
        std::fprintf(stderr, "usage: %s <data dir> <out.ico | out.iconset> [--preview <out.ppm>]\n",
                     argv[0]);
        return 2;
    }
    const std::string dataDir = argv[1];
    const std::string outPath = argv[2];
    const bool mac = endsWith(outPath, ".iconset");
    if (!mac && !endsWith(outPath, ".ico")) {
        std::fprintf(stderr, "%s: the icon is written as a .ico or a .iconset\n", outPath.c_str());
        return 2;
    }

    flix::ContentRegistry content;
    std::string error;
    if (!content.loadFiles(dataDir + "/mobs.json", dataDir + "/petals.json", error)) {
        std::fprintf(stderr, "content: %s\n", error.c_str());
        return 1;
    }
    flix::SpriteCache sprites;
    sprites.build(content, dataDir);
    flix::WorldRenderer renderer;
    renderer.setContent(&content);
    renderer.setSprites(&sprites);

    if (!(mac ? writeIconset(renderer, outPath) : writeIco(renderer, outPath))) {
        std::fprintf(stderr, "could not write %s\n", outPath.c_str());
        return 1;
    }

    if (argc == 5) {
        Canvas preview = Canvas::createVirtual(256, 256);
        preview.setFillStyle(Color{128, 128, 128});
        preview.fillRect(0, 0, 256, 256);
        if (mac) renderer.drawMacAppIcon(preview, 256);
        else renderer.drawAppIcon(preview, 256);
        if (!preview.savePPM(argv[4])) {
            std::fprintf(stderr, "could not write %s\n", argv[4]);
            return 1;
        }
    }
    return 0;
}
