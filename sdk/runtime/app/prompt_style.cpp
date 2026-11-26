// The button pictures' choice and the keyboard's key caps (app/prompt_style.h), shared by the four ports
#include "app/prompt_style.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // std::min and std::max, not Windows' macros
#endif
#include <windows.h>
#pragma comment(lib, "gdi32")  // the key caps' text
#else
#include <cstdio>
// the key caps' text: Dear ImGui's copy of stb_truetype, compiled here too, its functions this file's own
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <imstb_truetype.h>
#endif

#include "app/app.h"
#include "app/prompt_pictures.h"  // the one copy of the pictures (sdk/tools/mkprompts.py)
#include "core/input.h"
#include "core/keys.h"
#include "core/log.h"

namespace app {

uint32_t PromptStyle(uint32_t setting) {
    if (setting != 0) return setting;
    const Settings& st = Get();
    if (st.keyboard && st.keyboard_player == 1 && !input::Raw(0).connected) return 4;
    uint32_t style = uint32_t(input::Style(0)) + 1;  // 1 xbox, 2 playstation, 3 nintendo
    // a Steam Deck (Steam sets SteamDeck=1 for what it starts there): its own controls reach the game as an Xbox
    // controller; a PlayStation or Nintendo controller plugged into it still shows as itself
    static const bool deck = [] {
        const char* e = std::getenv("SteamDeck");
        return e && e[0] == '1';
    }();
    if (style == 1 && deck) return 5;
    return style;
}

const PromptPicture* FindPromptPicture(const char* style, const char* button) {
    auto same = [](const char* a, const char* b) {
        for (; *a && *b; a++, b++)
            if ((*a | 0x20) != (*b | 0x20)) return false;
        return *a == *b;
    };
    for (const PromptPicture& p : kPromptPictures)
        if (same(p.style, style) && same(p.button, button)) return &p;
    return nullptr;
}

const char* PromptStyleName(uint32_t style) {
    switch (style) {
        case 2: return "playstation";
        case 3: return "nintendo";
        case 5: return "steamdeck";
        case 6: return "steamcontroller";
        default: return nullptr;
    }
}

// a picture made for a full square (the disc filling it), scaled with a box filter into the box at (x, y), d across
// (fractions of the picture), in a kOut x kOut picture, transparent around it
void PlacePicture(const gpu::pack::Image& src, float bx_f, float by_f, float bd_f, uint32_t kOut, gpu::pack::Image& out) {
    out = gpu::pack::Image{};
    out.width = out.height = kOut;
    out.format = gpu::pack::Format::RGBA8;
    std::vector<uint8_t> px(kOut * kOut * 4, 0);
    const std::vector<uint8_t>& s = src.levels[0];
    float bx = bx_f * kOut, by = by_f * kOut, bd = bd_f * kOut, step = float(src.width) / bd;
    for (uint32_t y = 0; y < kOut; y++)
        for (uint32_t x = 0; x < kOut; x++) {
            float u0 = (x - bx) * step, v0 = (y - by) * step;
            if (u0 + step <= 0 || v0 + step <= 0 || u0 >= float(src.width) || v0 >= float(src.height)) continue;
            uint8_t* o = &px[(size_t(y) * kOut + x) * 4];
            if (step < 1.0f) {
                // a picture smaller than its place (the shared ones are about 96 across): enlarged smoothly, between
                // its four nearest pixels, colour weighted by coverage; beyond its edge is transparent
                float fu = u0 + 0.5f * step - 0.5f, fv = v0 + 0.5f * step - 0.5f;
                int iu = int(std::floor(fu)), iv = int(std::floor(fv));
                float tu = fu - float(iu), tv = fv - float(iv), acc[4] = {};
                for (int j = 0; j < 2; j++)
                    for (int i = 0; i < 2; i++) {
                        int u = iu + i, v = iv + j;
                        if (u < 0 || v < 0 || u >= int(src.width) || v >= int(src.height)) continue;
                        float wgt = (i ? tu : 1 - tu) * (j ? tv : 1 - tv);
                        const uint8_t* p = &s[(size_t(v) * src.width + u) * 4];
                        float a = p[3] / 255.0f * wgt;
                        for (int c = 0; c < 3; c++) acc[c] += p[c] * a;
                        acc[3] += a;
                    }
                for (int c = 0; c < 3; c++) o[c] = acc[3] > 0 ? uint8_t(std::clamp(acc[c] / acc[3], 0.0f, 255.0f)) : 0;
                o[3] = uint8_t(std::clamp(acc[3] * 255.0f, 0.0f, 255.0f));
                continue;
            }
            float acc[4] = {}, n = 0;
            int n_side = std::max(1, int(std::ceil(step)));
            for (int j = 0; j < n_side; j++)
                for (int i = 0; i < n_side; i++) {
                    int u = int(u0 + (i + 0.5f) * step / n_side), v = int(v0 + (j + 0.5f) * step / n_side);
                    if (u < 0 || v < 0 || u >= int(src.width) || v >= int(src.height)) {
                        n++;
                        continue;
                    }
                    const uint8_t* p = &s[(size_t(v) * src.width + u) * 4];
                    float a = p[3] / 255.0f;
                    for (int c = 0; c < 3; c++) acc[c] += p[c] * a;  // colour weighted by coverage, as it blends
                    acc[3] += a;
                    n++;
                }
            for (int c = 0; c < 3; c++) o[c] = acc[3] > 0 ? uint8_t(std::clamp(acc[c] / acc[3], 0.0f, 255.0f)) : 0;
            o[3] = uint8_t(std::clamp(acc[3] / n * 255.0f, 0.0f, 255.0f));
        }
    out.levels.push_back(std::move(px));
}

#ifndef _WIN32
void OverlayFontData(const unsigned int*& data, unsigned int& size);  // overlay.cpp: Liberation Sans Regular
#endif

namespace {

#ifdef _WIN32
// the key's short name in white on black, Windows' Arial Bold through GDI: the largest size that fits in the cap's
// face, centred on it; its coverage, N x N, in cov
bool CapText(const char* text, int N, int D, std::vector<uint8_t>& cov) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = N;
    bi.bmiHeader.biHeight = -N;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        return false;
    }
    HGDIOBJ old_bmp = SelectObject(dc, bmp);
    std::memset(bits, 0, N * N * 4);
    // the largest bold font the name fits in, inside the cap's face
    int size = int(text[1] ? (text[2] ? D * 0.36 : D * 0.5) : D * 0.62);
    HFONT font = nullptr;
    HGDIOBJ old_font = nullptr;
    SIZE ext{};
    for (; size > 10; size -= 2) {
        if (font) {
            SelectObject(dc, old_font);
            DeleteObject(font);
        }
        font = CreateFontA(-size, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH, "Arial");
        HGDIOBJ prev = SelectObject(dc, font);
        if (!old_font) old_font = prev;  // the DC's own font, put back before ours is deleted
        GetTextExtentPoint32A(dc, text, int(std::strlen(text)), &ext);
        if (ext.cx <= D * 0.78) break;
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    const float e = D * 0.94f;
    TextOutA(dc, int(e - ext.cx) / 2, int(e - ext.cy) / 2 - 1, text, int(std::strlen(text)));
    GdiFlush();
    const uint8_t* g = static_cast<const uint8_t*>(bits);  // the text's coverage, white on black
    cov.assign(size_t(N) * N, 0);
    for (size_t i = 0; i < cov.size(); i++) cov[i] = g[i * 4 + 1];
    if (old_font) SelectObject(dc, old_font);  // a font still selected into a DC isn't freed
    SelectObject(dc, old_bmp);
    if (font) DeleteObject(font);
    DeleteObject(bmp);
    DeleteDC(dc);
    return true;
}
#endif

#ifndef _WIN32
// stb_compress's format, as Dear ImGui's binary_to_compressed_c writes it (imgui_draw.cpp's stb_decompress); empty
// when it doesn't read
std::vector<uint8_t> Decompress(const uint8_t* in, size_t in_size) {
    auto be = [&](size_t at, int n) {
        uint32_t v = 0;
        for (int k = 0; k < n; k++) v = v << 8 | (at + k < in_size ? in[at + k] : 0);
        return v;
    };
    if (in_size < 16 || be(0, 4) != 0x57bC0000 || be(4, 4) != 0) return {};
    std::vector<uint8_t> out;
    out.reserve(be(8, 4));
    const size_t olen = be(8, 4);
    auto match = [&](size_t back, size_t n) {
        if (back == 0 || back > out.size() || out.size() + n > olen) return false;
        for (size_t s = out.size() - back; n--; s++) out.push_back(out[s]);  // overlapping: byte by byte
        return true;
    };
    auto lit = [&](size_t at, size_t n) {
        if (at + n > in_size || out.size() + n > olen) return false;
        out.insert(out.end(), in + at, in + at + n);
        return true;
    };
    size_t i = 16;
    for (bool ok = true; ok && i < in_size;) {
        uint8_t c = in[i];
        if (c >= 0x80) ok = match(in[i + 1] + 1, c - 0x80 + 1), i += 2;
        else if (c >= 0x40) ok = match(be(i, 2) - 0x4000 + 1, in[i + 2] + 1), i += 3;
        else if (c >= 0x20) ok = lit(i + 1, c - 0x20 + 1), i += 1 + (c - 0x20 + 1);
        else if (c >= 0x18) ok = match(be(i, 3) - 0x180000 + 1, in[i + 3] + 1), i += 4;
        else if (c >= 0x10) ok = match(be(i, 3) - 0x100000 + 1, be(i + 3, 2) + 1), i += 5;
        else if (c >= 0x08) ok = lit(i + 2, be(i, 2) - 0x0800 + 1), i += 2 + (be(i, 2) - 0x0800 + 1);
        else if (c == 0x07) ok = lit(i + 3, be(i + 1, 2) + 1), i += 3 + (be(i + 1, 2) + 1);
        else if (c == 0x06) ok = match(be(i + 1, 3) + 1, in[i + 4] + 1), i += 5;
        else if (c == 0x04) ok = match(be(i + 1, 3) + 1, be(i + 4, 2) + 1), i += 6;
        else if (c == 0x05 && i + 1 < in_size && in[i + 1] == 0xfa) return out.size() == olen ? out : std::vector<uint8_t>{};
        else break;
    }
    return {};
}

// the caps' face: Windows' Arial Bold when it is installed, else Liberation Sans Bold (Arial's metrics), else the
// menu's own Liberation Sans Regular, its strokes thickened by bold (of the em, each side) to Arial Bold's.
// XBE_KEYCAP_FONT names another font file, or "builtin" for the last.
struct CapFont {
    std::vector<uint8_t> data;
    stbtt_fontinfo info{};
    float bold = 0;
    bool ok = false;
};

const CapFont& Font() {
    // made in place, never copied: info points into data
    static const CapFont* const font = [] {
        CapFont* made = new CapFont;
        CapFont& f = *made;
        auto load = [&](const std::vector<uint8_t>& bytes, float bold) {
            f.data = bytes;
            f.ok = !f.data.empty() && stbtt_InitFont(&f.info, f.data.data(), stbtt_GetFontOffsetForIndex(f.data.data(), 0)) != 0;
            f.bold = bold;
            return f.ok;
        };
        auto file = [&](const char* path) {
            std::vector<uint8_t> bytes;
            if (FILE* fp = std::fopen(path, "rb")) {
                uint8_t buf[65536];
                for (size_t n; (n = std::fread(buf, 1, sizeof buf, fp)) > 0;) bytes.insert(bytes.end(), buf, buf + n);
                std::fclose(fp);
            }
            return !bytes.empty() && load(bytes, 0);
        };
        const char* want = std::getenv("XBE_KEYCAP_FONT");
        static const char* const kPaths[] = {
            "/usr/share/fonts/truetype/msttcorefonts/Arial_Bold.ttf",  // Debian, Ubuntu (ttf-mscorefonts-installer)
            "/usr/share/fonts/msttcore/arialbd.ttf",                    // Fedora (msttcore-fonts)
            "/usr/share/fonts/TTF/arialbd.ttf",                         // Arch (ttf-ms-fonts)
            "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Bold.ttf",  // Fedora
            "/usr/share/fonts/liberation-sans/LiberationSans-Bold.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",  // Debian, Ubuntu
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
            "/usr/share/fonts/liberation/LiberationSans-Bold.ttf",           // Arch, SteamOS
            "/usr/share/fonts/TTF/LiberationSans-Bold.ttf",
        };
        if (want && std::strcmp(want, "builtin") != 0) {
            if (file(want)) {
                LOGR("prompts: key caps' text in %s", want);
                return made;
            }
            LOGW("prompts: XBE_KEYCAP_FONT %s doesn't read as a font", want);
        }
        if (!want)
            for (const char* p : kPaths)
                if (file(p)) {
                    LOGR("prompts: key caps' text in %s", p);
                    return made;
                }
        const unsigned int* data = nullptr;
        unsigned int size = 0;
        OverlayFontData(data, size);
        if (load(Decompress(reinterpret_cast<const uint8_t*>(data), size), 0.027f))
            LOGR("prompts: key caps' text in the built-in Liberation Sans, made bold");
        else
            LOGW("prompts: no font for the key caps' text");
        return made;
    }();
    return *font;
}

// CapText as GDI draws it (above), with stb_truetype: the size is the em's, the line's height its ascent and descent;
// each glyph from its signed distance field, so a regular face can be drawn bold
bool CapText(const char* text, int N, int D, std::vector<uint8_t>& cov) {
    const CapFont& f = Font();
    if (!f.ok) return false;
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&f.info, &asc, &desc, &gap);
    const size_t len = std::strlen(text);
    auto width = [&](float scale, float bold_px) {
        float w = 0;
        for (size_t i = 0; i < len; i++) {
            int adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&f.info, uint8_t(text[i]), &adv, &lsb);
            w += std::round(adv * scale + 2 * bold_px);
        }
        return w;
    };
    int size = int(text[1] ? (text[2] ? D * 0.36 : D * 0.5) : D * 0.62);
    float scale = 0, bold_px = 0, cx = 0;
    for (; size > 10; size -= 2) {
        scale = stbtt_ScaleForMappingEmToPixels(&f.info, float(size));
        bold_px = f.bold * float(size);
        cx = width(scale, bold_px);
        if (cx <= D * 0.78) break;
    }
    const float e = D * 0.94f, cy = float(asc - desc) * scale;
    float pen = float(int(e - cx) / 2);
    const float base = float(int(e - cy) / 2 - 1) + std::round(float(asc) * scale);
    cov.assign(size_t(N) * N, 0);
    // the distance field: 128 on the outline, 32 a pixel, inwards up
    const float kPerPx = 32.0f;
    const int pad = int(std::ceil(bold_px)) + 2;
    for (size_t i = 0; i < len; i++) {
        int cp = uint8_t(text[i]), adv = 0, lsb = 0, w = 0, h = 0, xo = 0, yo = 0;
        stbtt_GetCodepointHMetrics(&f.info, cp, &adv, &lsb);
        unsigned char* sdf = stbtt_GetCodepointSDF(&f.info, scale, cp, pad, 128, kPerPx, &w, &h, &xo, &yo);
        if (sdf) {
            int ox = int(pen + bold_px) + xo, oy = int(base) + yo;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    int px = ox + x, py = oy + y;
                    if (px < 0 || py < 0 || px >= N || py >= N) continue;
                    float c = std::clamp((sdf[y * w + x] - 128.0f) / kPerPx + bold_px + 0.5f, 0.0f, 1.0f);
                    uint8_t& o = cov[size_t(py) * N + px];
                    o = std::max(o, uint8_t(c * 255.0f + 0.5f));
                }
            stbtt_FreeSDF(sdf, nullptr);
        }
        pen += std::round(adv * scale + 2 * bold_px);
    }
    return true;
}
#endif

}  // namespace

bool KeyCapPicture(const char* button, int size_px, int cap_px, float shadow_dy, gpu::pack::Image& out) {
    keys::Action a = keys::kCount;
    static const struct {
        const char* b;
        keys::Action a;
    } kMap[] = {{"A", keys::kA},         {"B", keys::kB},         {"X", keys::kX},
                {"Y", keys::kY},         {"WHITE", keys::kWhite}, {"BLACK", keys::kBlack},
                {"LT", keys::kLeftTrigger}, {"RT", keys::kRightTrigger}};
    for (auto& m : kMap)
        if (!std::strcmp(m.b, button)) a = m.a;
    if (a == keys::kCount) return false;
    std::string key = keys::FirstKey(a);
    if (key.empty()) return false;
    // Kenney's picture of the key or mouse button, where the key cap goes
    if (const PromptPicture* p = FindPromptPicture("keyboard", key.c_str())) {
        gpu::pack::Image pic;
        if (gpu::pack::DecodePng(p->png, p->size, pic) && !pic.levels.empty()) {
            PlacePicture(pic, 0, 0, float(cap_px) / float(size_px), uint32_t(size_px), out);
            return true;
        }
    }
    // a key the pack has no picture of: a plain cap with its short name
    const char* text = keys::ShortName(key.c_str());
    const int N = size_px, D = cap_px;
    std::vector<uint8_t> g;  // the text's coverage
    if (!CapText(text, N, D, g)) return false;
    const float e = D * 0.94f, r = D * 0.2f;
    out = gpu::pack::Image{};
    out.width = out.height = N;
    out.format = gpu::pack::Format::RGBA8;
    std::vector<uint8_t> px(N * N * 4, 0);
    // the cap: rounded square, anti-aliased from its edge distance; a shadow to the bottom right as the discs have
    auto inside = [&](float x, float y, float x0, float y0, float x1, float y1, float rr) {
        float dx = std::max({x0 + rr - x, 0.0f, x - (x1 - rr)}), dy = std::max({y0 + rr - y, 0.0f, y - (y1 - rr)});
        float d = std::sqrt(dx * dx + dy * dy) - rr;
        return std::clamp(0.5f - d, 0.0f, 1.0f);
    };
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float fx = x + 0.5f, fy = y + 0.5f;
            float shadow = inside(fx, fy, D * 0.05f, D * shadow_dy, e + D * 0.05f, e + D * shadow_dy, r);
            float body = inside(fx, fy, 0, 0, e, e, r);
            float face = inside(fx, fy, D * 0.04f, D * 0.04f, e - D * 0.04f, e - D * 0.04f, r * 0.8f);
            float t = g[y * N + x] / 255.0f;     // the text, on the face
            float shade = 1.0f - 0.35f * fy / e;  // lighter towards the top
            float c[3], al = std::max(body, shadow * 0.8f);
            for (int k = 0; k < 3; k++) {
                float edge = 150.0f, inner = (k == 2 ? 46.0f : 38.0f) * shade + 10;
                float v = body * (face * inner + (1 - face) * edge);
                v = v * (1 - t * face) + 255.0f * t * face;
                c[k] = al > 0 ? v / std::max(al, 1e-3f) * (body > 0 ? 1.0f : 0.0f) : 0;
            }
            uint8_t* o = &px[(y * N + x) * 4];
            for (int k = 0; k < 3; k++) o[k] = uint8_t(std::clamp(c[k], 0.0f, 255.0f));
            o[3] = uint8_t(std::clamp(al * 255.0f, 0.0f, 255.0f));
        }
    out.levels.push_back(std::move(px));
    return true;
}


}  // namespace app
