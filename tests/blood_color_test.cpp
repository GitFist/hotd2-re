// Blood variants share global IDs with effect and enemy-model textures.
#include "render/asset_store.h"
#include "render/blood_color.h"
#include "assets/asset_override.h"
#include "game/palette_bank.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>

static void Check(bool ok, const char* why) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

using port::BloodColor;
static constexpr std::array<BloodColor, 4> kColors{
    BloodColor::Red, BloodColor::Green, BloodColor::Blue, BloodColor::Purple
};

static bool SameImage(const port::RgbaImage& a, const port::RgbaImage& b) {
    return a.width == b.width && a.height == b.height &&
        a.logicalWidth == b.logicalWidth && a.logicalHeight == b.logicalHeight && a.pixels == b.pixels;
}

struct BloodFrame {
    int redFile = -1, greenFile = -1, index = -1;
    std::array<const port::RgbaImage*, 4> cached{};
    std::array<port::RgbaImage, 4> expected;
};

static size_t CheckRecolor(const port::RgbaImage& red, const port::RgbaImage& colored, bool blue) {
    Check(colored.width == red.width && colored.height == red.height &&
        colored.logicalWidth == red.logicalWidth && colored.logicalHeight == red.logicalHeight,
        "recolor preserves physical and logical dimensions");
    Check(colored.pixels.size() == red.pixels.size(), "recolor preserves pixel count");
    size_t changed = 0;
    for (size_t p = 0; p < red.pixels.size(); p += 4) {
        const auto* src = red.pixels.data() + p;
        const auto* dst = colored.pixels.data() + p;
        Check(dst[3] == src[3], "recolor preserves every alpha byte");
        Check(*std::min_element(src, src + 3) == *std::min_element(dst, dst + 3) &&
            *std::max_element(src, src + 3) == *std::max_element(dst, dst + 3),
            "recolor preserves per-pixel HSV saturation and value");
        if (src[0] > src[1] && src[0] > src[2]) {
            Check(!std::equal(src, src + 3, dst), "each red-dominant texel changes hue");
            if (blue) Check(dst[2] > dst[0] && dst[2] > dst[1], "blue hue has dominant blue");
            ++changed;
        } else {
            Check(std::equal(src, src + 3, dst), "neutral and non-red texels remain byte-identical");
        }
    }
    Check(changed > 0, "every blood animation frame is recolored");
    return changed;
}

// Optional visual QA: real decoded textures, composited over a checkerboard.
// Columns are Red / Green / Blue / Purple; rows are spray / pool / wound / large splatter.
static void WritePreview(const std::filesystem::path& directory, const std::map<uint32_t, BloodFrame>& frames) {
    constexpr int cell = 192, width = cell * 4, height = cell * 4;
    const std::array<int, 4> indices{0, 26, 30, 37};
    std::vector<uint8_t> rgb(width * height * 3, 28);
    for (int row = 0; row < 4; ++row) {
        auto found = std::find_if(frames.begin(), frames.end(), [&](const auto& entry) {
            return entry.second.index == indices[row];
        });
        Check(found != frames.end(), "preview texture exists");
        for (int column = 0; column < 4; ++column) {
            const auto& img = found->second.expected[column];
            const double scale = double(cell - 16) / std::max(img.width, img.height);
            const int drawWidth = std::max(1, int(img.width * scale));
            const int drawHeight = std::max(1, int(img.height * scale));
            const int left = column * cell + (cell - drawWidth) / 2;
            const int top = row * cell + (cell - drawHeight) / 2;
            for (int y = 0; y < drawHeight; ++y) for (int x = 0; x < drawWidth; ++x) {
                // Stored texture rows are inverted relative to upright PNGs.
                const int sx = x * img.width / drawWidth;
                const int sy = img.height - 1 - y * img.height / drawHeight;
                const auto* src = img.pixels.data() + (sy * img.width + sx) * 4;
                const int background = ((x / 12 + y / 12) & 1) ? 78 : 110;
                const size_t out = ((top + y) * width + left + x) * 3;
                for (int c = 0; c < 3; ++c)
                    rgb[out + c] = uint8_t((src[c] * src[3] + background * (255 - src[3]) + 127) / 255);
            }
        }
    }
    std::filesystem::create_directories(directory);
    std::ofstream file(directory / "blood-colors.ppm", std::ios::binary);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    file.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    Check(file.good(), "preview contact sheet written");
}

int main(int argc, char** argv) {
    Check(argc > 1, "usage: blood_color_test <game directory> [asset pack or -] [preview directory]");
    const bool packed = argc > 2 && std::string(argv[2]) != "-";
    if (packed) port::SetAssetDir(argv[2]);
    port::AssetStore assets;
    std::string error;
    Check(assets.Open(argv[1], &error), error.c_str());
    Check(assets.GetBloodColor() == BloodColor::Green, "green is the default");
    const auto files = game::ListTextureFiles(assets.Exe());
    std::map<uint32_t, BloodFrame> frames;
    for (const auto& f : files) if (f.name == "scr_blood_red.bin") for (int i = 0; i < f.count; ++i) {
        const auto d = game::ReadTextureDesc(assets.Exe(), f, i);
        const auto image = assets.Texture(f.name, i);
        Check(image != nullptr, "red source decodes");
        BloodFrame frame;
        frame.redFile = f.index; frame.index = i;
        frame.cached[0] = image; frame.expected[0] = *image;
        frames.emplace(d.id, std::move(frame));
    }
    Check(frames.size() == 39, "all 39 global blood texture IDs are covered");
    for (const auto& f : files) if (f.name == "scr_blood_green.bin") for (int i = 0; i < f.count; ++i) {
        const auto d = game::ReadTextureDesc(assets.Exe(), f, i);
        auto found = frames.find(d.id);
        Check(found != frames.end(), "green frame has corresponding red global ID");
        const auto image = assets.Texture(f.name, i);
        Check(image != nullptr, "green source decodes");
        found->second.greenFile = f.index;
        found->second.cached[1] = image; found->second.expected[1] = *image;
    }

    int scaled = 0;
    size_t redPixels = 0, greenPixels = 0, purplePixels = 0, recoloredPixels = 0;
    for (auto& [id, frame] : frames) {
        Check(frame.greenFile >= 0, "every blood frame has an original green variant");
        const auto& red = frame.expected[0];
        scaled += red.width > red.LogicalWidth();
        for (int c : {2, 3}) {
            assets.SetBloodColor(kColors[c]);
            const auto image = assets.TextureById(frame.redFile, id);
            Check(image != nullptr, "selected recolor decodes");
            frame.cached[c] = image; frame.expected[c] = *image;
            recoloredPixels += CheckRecolor(red, *image, c == 2);
        }
        Check(frame.cached[2] != frame.cached[3], "blue and purple have separate cache entries");
        Check(frame.expected[2].pixels != frame.expected[3].pixels, "blue and purple differ on every frame");
        for (size_t p = 0; p < red.pixels.size(); p += 4) {
            if (red.pixels[p + 3] > 64 && red.pixels[p] > red.pixels[p + 1] + 30) ++redPixels;
            const auto& purple = frame.expected[3].pixels;
            if (purple[p + 3] > 64 && purple[p] > purple[p + 1] + 20 && purple[p + 2] > purple[p + 1] + 20)
                ++purplePixels;
        }
        const auto& green = frame.expected[1].pixels;
        for (size_t p = 0; p < green.size(); p += 4)
            if (green[p + 3] > 64 && green[p + 1] > green[p] + 30) ++greenPixels;
        assets.SetBloodColor(BloodColor::Red);
        Check(assets.TextureById(frame.redFile, id) == frame.cached[0] && SameImage(*frame.cached[0], red),
            "switching back restores the unmodified cached red source");
    }

    int mapped = 0, different = 0, unaffected = 0;
    for (const auto& f : files) for (int i = 0; i < f.count; ++i) {
        const auto d = game::ReadTextureDesc(assets.Exe(), f, i);
        const auto found = frames.find(d.id);
        assets.SetBloodColor(BloodColor::Green);
        if (found == frames.end()) {
            if (unaffected < 5 && f.name == "common.bin") {
                const auto before = assets.Texture(f.name, i);
                Check(before != nullptr, "unrelated common texture decodes");
                const auto original = *before;
                for (auto color : kColors) {
                    assets.SetBloodColor(color);
                    Check(assets.Texture(f.name, i) == before && SameImage(*before, original),
                        "non-blood texture remains unchanged for every color");
                }
                ++unaffected;
            }
            continue;
        }
        const auto green = assets.Texture(f.name, i);
        Check(green != nullptr, "original shared texture decodes");
        const auto original = *green;
        const auto& frame = found->second;
        for (int c : {0, 2, 3}) {
            assets.SetBloodColor(kColors[c]);
            Check(assets.GetBloodColor() == kColors[c], "selection reports the requested color");
            Check(assets.Texture(f.name, i) == frame.cached[c], "model material resolves selected global blood ID");
            Check(assets.TextureById(f.index, d.id) == frame.cached[c], "sprite resolves selected global blood ID");
        }
        assets.SetBloodColor(BloodColor::Green);
        Check(assets.Texture(f.name, i) == green && SameImage(*green, original),
            "switching back restores the unmodified cached original");
        if (original.pixels != frame.expected[0].pixels) ++different;
        ++mapped;
    }
    Check(mapped > int(frames.size()) && different > 0 && unaffected == 5,
        "shared effects, model textures and unrelated textures exercised");
    Check(redPixels > 100 && greenPixels > 100 && purplePixels > 100,
        "original red and green and generated purple retain their respective colors");
    if (packed) Check(scaled > 0, "optional high-resolution red textures are used");
    if (argc > 3) WritePreview(argv[3], frames);

    for (int c = 0; c < 4; ++c) {
        assets.SetBloodColor(kColors[c]);
        assets.ClearCache();
        Check(assets.GetBloodColor() == kColors[c], "scene cache reset retains every color selection");
        for (const auto& [id, frame] : frames) {
            const auto reloaded = assets.TextureById(frame.greenFile, id);
            Check(reloaded != nullptr && SameImage(*reloaded, frame.expected[c]),
                "every selected frame survives scene reload byte-for-byte");
        }
    }
    printf("PASS %zu blood IDs, %d shared references, %zu recolored texels, %d scaled red textures\n",
        frames.size(), mapped, recoloredPixels, scaled);
}
