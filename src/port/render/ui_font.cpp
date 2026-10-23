#include "render/ui_font.h"

#include <algorithm>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace port {
bool UiFont::Init() {
#ifdef _WIN32
    constexpr int width = 1280, height = 576, cellW = 80, cellH = 96;
    const wchar_t* names[] = {L"Impact", L"Bahnschrift"};
    for (size_t faceIndex = 0; faceIndex < faces_.size(); ++faceIndex) {
        HDC dc = CreateCompatibleDC(nullptr);
        if (!dc) return false;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        HFONT font = CreateFontW(-64, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, names[faceIndex]);
        if (!bitmap || !font || !bits) {
            if (font) DeleteObject(font); if (bitmap) DeleteObject(bitmap); DeleteDC(dc); return false;
        }
        const auto previousBitmap = SelectObject(dc, bitmap);
        const auto previousFont = SelectObject(dc, font);
        std::fill_n(static_cast<uint32_t*>(bits), width * height, 0u);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255,255,255)); SetTextAlign(dc, TA_TOP | TA_LEFT);
        TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
        auto& face = faces_[faceIndex]; face.height = std::max(1, static_cast<int>(metrics.tmHeight));
        bool ok = true;
        for (int code = 32; code <= 126; ++code) {
            const wchar_t character = static_cast<wchar_t>(code);
            const int slot = code - 32;
            SIZE extent{};
            ok = GetTextExtentPoint32W(dc, &character, 1, &extent) && ok;
            face.advance[slot] = static_cast<float>(extent.cx);
            ok = TextOutW(dc, slot % 16 * cellW + 2, slot / 16 * cellH + 2, &character, 1) && ok;
        }
        GdiFlush();
        face.image.width = width; face.image.height = height;
        face.image.pixels.resize(static_cast<size_t>(width) * height * 4);
        const auto* pixels = static_cast<const uint8_t*>(bits);
        for (size_t pixel = 0; pixel < static_cast<size_t>(width) * height; ++pixel) {
            const uint8_t coverage = std::max({pixels[pixel*4], pixels[pixel*4+1], pixels[pixel*4+2]});
            auto* target = face.image.pixels.data() + pixel * 4;
            target[0] = target[1] = target[2] = 255; target[3] = coverage;
        }
        SelectObject(dc, previousFont); SelectObject(dc, previousBitmap);
        DeleteObject(font); DeleteObject(bitmap); DeleteDC(dc);
        if (!ok) return false;
    }
    available_ = true;
#endif
    return available_;
}

void UiFont::Append(const std::vector<game::WorldHud::MenuText>& text, std::vector<ModelRenderer::SpriteDraw>& out, float horizontalScale) const {
    if (!available_) return;
    size_t index = 0;
    std::vector<ModelRenderer::SpriteDraw> foreground;
    for (const auto& command : text) {
        const auto& face = faces_[command.scale >= .9f ? 0 : 1];
        const float scale = command.scale * 30.0f / face.height;
        float x = command.x;
        for (unsigned char c : command.text) {
            if (c < 32 || c > 126) c = '?';
            const int glyph = c - 32;
            const float w = face.advance[glyph] + 4, h = static_cast<float>(face.height + 4);
            if (c != ' ') {
                ModelRenderer::SpriteDraw sprite{};
                sprite.texture = &face.image; sprite.linear = true;
                sprite.depth = .005f - static_cast<float>(index++) * .00000001f;
                sprite.key = 0x8000000F; sprite.isp = 0xE4000000; sprite.tsp = 0x94918000;
                const float x0 = 320 + (x - 320 - 2*scale) * horizontalScale;
                const float x1 = x0 + w * scale * horizontalScale;
                const float y0 = command.y - 2*scale, y1 = y0 + h*scale;
                const float u0 = (glyph%16 * 80) / static_cast<float>(face.image.width);
                const float v0 = (glyph/16 * 96) / static_cast<float>(face.image.height);
                const float u1 = u0 + w/face.image.width, v1 = v0 + h/face.image.height;
                sprite.v[0] = {x0,y0,0,1,command.color,0,u0,v0}; sprite.v[1] = {x0,y1,0,1,command.color,0,u0,v1};
                sprite.v[2] = {x1,y0,0,1,command.color,0,u1,v0}; sprite.v[3] = {x1,y1,0,1,command.color,0,u1,v1};
                // Keep every shadow behind every glyph, including on paths that draw
                // sprites in submission order. The offset scales with the menu canvas.
                auto shadow = sprite;
                shadow.depth += .001f;
                const float offset = std::clamp(command.scale * 1.75f, 1.25f, 2.75f);
                const uint32_t shadowColor = (((command.color >> 24) * 230 / 255) << 24);
                for (auto& vertex : shadow.v) {
                    vertex.x += offset * horizontalScale;
                    vertex.y += offset;
                    vertex.diffuse = shadowColor;
                }
                out.push_back(shadow);
                foreground.push_back(sprite);
            }
            x += face.advance[glyph] * scale;
        }
    }
    out.insert(out.end(), foreground.begin(), foreground.end());
}
} // namespace port
