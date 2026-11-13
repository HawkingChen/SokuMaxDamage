#pragma once
// SokuMaxDamage HUD artwork, v3 ("damage influence / total correction" sketch).
// Read-only companion window. Rendering deliberately stays on the existing
// dedicated UI thread; never access live game objects from WM_PAINT.
#include <windows.h>
#include "HudPalette.hpp"
#include "HudText.hpp"
#include "FirstHit.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

static_assert(L"中文"[0] == 0x4E2D, "Compile HUD source as UTF-8 (/utf-8).");

// Logical drawing space. The panel window keeps a 1000:290 aspect ratio, so
// x and y scales are identical and the collapsed "+" stays square.
inline constexpr int kPanelVirtualWidth = 1000;
inline constexpr int kPanelVirtualHeight = 290;
inline constexpr int kPlusLogicalSize = 34;
struct HudButton { int x, y, w, h; };
// 2x2 control pad to the right of the "current hand" caption:
// CN / JP / EN / collapse.
// Bottom-right corner of the hand strip, clear of all five SC cards.
inline constexpr HudButton kHudButtons[4] = {
    {910, 204, 28, 28}, {944, 204, 28, 28},
    {910, 238, 28, 28}, {944, 238, 28, 28}};

struct PanelCard {
    int character = -1, card = -1, damage = 0;
    std::string name;                    // official fallback name
    std::array<std::string, 3> names{};  // by HudLanguage, from MoveNames.tsv
    std::string notation;                // normalized: J2A, 623B/C, ...
    bool approximate = true;
};
struct PanelEffect { int character=-1,card=-1,copies=1; std::string label,value; };
struct PanelView {
    int character = -1, opponentCharacter = -1, rods = 0, dolls = 0, weather = 0;
    double attack = 1., defense = 1.;
    double skillMultiplier = 1., spellMultiplier = 1.;
    double hpFactor = 1.;
    std::string gutsText;  // one formatted label shared by both guts displays
    double commonPercent = 100., basePercent = 100.;
    double firstHitMultiplier = 1.;
    bool firstHitActive = false;
    bool forcedFirstHitArmed = false;
    bool weatherVisible = false;
    bool weatherSealed = false;
    whm::HudLanguage language = whm::HudLanguage::Chinese;
    bool collapsed = false;
    int cursorX = -1, cursorY = -1;
    bool systemCursorVisible = true;
    std::vector<PanelEffect> effects;
    PanelCard strongest, record;
    std::vector<PanelCard> hand;
};

inline std::wstring panelWide(const std::string &s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(static_cast<size_t>(n), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n)) return L"";
    w.pop_back();
    return w;
}
inline std::string panelFactor(double value, int decimals) {
    return whm::hudFactor(value, decimals); // identical to effect rows
}
inline std::wstring panelPercent(double value) {
    int rounded = static_cast<int>(value + 0.5);
    if (rounded < 0) rounded = 0;
    if (rounded > 9999) rounded = 9999;
    return std::to_wstring(rounded) + L"%";
}
inline std::wstring panelCardLabel(const PanelCard &card, whm::HudLanguage lang,
                                   const std::wstring &missing) {
    const std::string &localized = card.names[static_cast<int>(lang)];
    const std::string chosen = !localized.empty() ? localized : card.name;
    if (chosen.empty()) return missing;
    // Normals store their command as the "name" in the source spelling (j.2A).
    // cardDisplayName prints one ALL-CAPS command instead of "j.2A · J2A".
    return panelWide(whm::cardDisplayName(chosen, card.notation));
}

// Convert existing local card PNGs (icons/<character>-<card>.png) with the
// same GDI+ dynamic loader already used in the previous working panel.
// Missing images deliberately use a drawn card-style fallback instead.
inline HBITMAP panelBitmap(const std::wstring &path) {
    static std::map<std::wstring, HBITMAP> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    struct Startup { UINT version; void *callback; BOOL suppressThread, suppressCodecs; };
    static HMODULE dll = LoadLibraryW(L"gdiplus.dll");
    static ULONG_PTR token = 0;
    using Start = int (WINAPI *)(ULONG_PTR *, const Startup *, void *);
    using Load = int (WINAPI *)(const WCHAR *, void **);
    using Bitmap = int (WINAPI *)(void *, HBITMAP *, DWORD);
    using Dispose = int (WINAPI *)(void *);
    HBITMAP result = nullptr;
    if (dll) {
        if (!token) {
            Startup cfg{1, nullptr, FALSE, FALSE};
            auto start = reinterpret_cast<Start>(GetProcAddress(dll, "GdiplusStartup"));
            if (start) start(&token, &cfg, nullptr);
        }
        if (token) {
            auto load = reinterpret_cast<Load>(GetProcAddress(dll, "GdipLoadImageFromFile"));
            auto bitmap = reinterpret_cast<Bitmap>(GetProcAddress(dll, "GdipCreateHBITMAPFromBitmap"));
            auto dispose = reinterpret_cast<Dispose>(GetProcAddress(dll, "GdipDisposeImage"));
            void *image = nullptr;
            if (load && bitmap && dispose && !load(path.c_str(), &image)) {
                bitmap(image, &result, 0xff151222);
                dispose(image);
            }
        }
    }
    cache.emplace(path, result);
    return result;
}
inline HBITMAP panelIcon(const std::string &folder, int character, int card) {
    if (card < 0) return nullptr;
    return panelBitmap(panelWide(folder + "icons/" + (card>=1000?std::string("weather"):card<100?std::string("common"):std::to_string(character)) +
                                 "-" + std::to_string(card>=1000?card-1000:card) + ".png"));
}
inline HBITMAP panelBackdrop(const std::string &folder) {
    return panelBitmap(panelWide(folder + "art/panel-bg.png"));
}

// Folded state: the entire panel disappears; only a small "+" remains. It
// still obeys the battle/foreground checks in refreshPanel.
inline void paintFoldedPanel(HWND window, HDC target, int width, int height) {
    RECT r{0, 0, width, height};
    HBRUSH brush = CreateSolidBrush(RGB(30, 21, 43));
    FillRect(target, &r, brush);
    DeleteObject(brush);
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(230, 182, 94));
    HGDIOBJ oldPen = SelectObject(target, pen);
    HGDIOBJ oldBrush = SelectObject(target, GetStockObject(NULL_BRUSH));
    RoundRect(target, 1, 1, width - 1, height - 1, 6, 6);
    SelectObject(target, oldBrush);
    SelectObject(target, oldPen);
    DeleteObject(pen);
    HFONT font = CreateFontW(-std::max(10, std::min(width, height) * 3 / 5), 0, 0, 0,
                             FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(target, font);
    SetBkMode(target, TRANSPARENT);
    SetTextColor(target, RGB(255, 226, 161));
    DrawTextW(target, L"+", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(target, oldFont);
    DeleteObject(font);
    (void)window;
}

inline void paintStyledPanel(HWND window, const PanelView &v, const std::string &folder) {
    PAINTSTRUCT ps;
    HDC target = BeginPaint(window, &ps);
    RECT bounds{};
    GetClientRect(window, &bounds);
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) { EndPaint(window, &ps); return; }
    if (v.collapsed) {
        paintFoldedPanel(window, target, width, height);
        EndPaint(window, &ps);
        return;
    }
    HDC dc = CreateCompatibleDC(target);
    HBITMAP buffer = CreateCompatibleBitmap(target, width, height);
    if (!dc || !buffer) {
        if (buffer) DeleteObject(buffer);
        if (dc) DeleteDC(dc);
        EndPaint(window, &ps);
        return;
    }
    HGDIOBJ previous = SelectObject(dc, buffer);
    SetMapMode(dc, MM_ANISOTROPIC);
    SetWindowExtEx(dc, kPanelVirtualWidth, kPanelVirtualHeight, nullptr);
    SetViewportExtEx(dc, width, height, nullptr);
    SetBkMode(dc, TRANSPARENT);

    const auto lang = v.language;
    const COLORREF gold = RGB(230, 182, 94);
    const COLORREF bright = RGB(255, 226, 161);
    const COLORREF cream = RGB(252, 236, 208);
    const COLORREF faded = RGB(185, 180, 203);
    const COLORREF lavender = RGB(156, 125, 196);
    const COLORREF quietGrey = RGB(146, 140, 162);
    auto fill = [&](int x, int y, int w, int h, COLORREF color) {
        if (w <= 0 || h <= 0) return;
        RECT r{x, y, x + w, y + h};
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(dc, &r, brush);
        DeleteObject(brush);
    };
    auto rule = [&](int x1, int y1, int x2, int y2, COLORREF color, int thickness = 1) {
        HPEN pen = CreatePen(PS_SOLID, thickness, color);
        HGDIOBJ old = SelectObject(dc, pen);
        MoveToEx(dc, x1, y1, nullptr);
        LineTo(dc, x2, y2);
        SelectObject(dc, old);
        DeleteObject(pen);
    };
    auto rounded = [&](int x, int y, int w, int h, COLORREF bg,
                       COLORREF edge, int radius = 12, int lineWidth = 1) {
        HBRUSH brush = CreateSolidBrush(bg);
        HPEN pen = CreatePen(PS_SOLID, lineWidth, edge);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        RoundRect(dc, x, y, x + w, y + h, radius, radius);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    };
    auto disk = [&](int x, int y, int diameter, COLORREF color) {
        HBRUSH brush = CreateSolidBrush(color);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, x, y, x + diameter, y + diameter);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(brush);
    };
    auto shape = [&](const POINT *points, int count, COLORREF color) {
        HBRUSH brush = CreateSolidBrush(color);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        Polygon(dc, points, count);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(brush);
    };
    auto write = [&](const std::wstring &value, int x, int y, int w, int h,
                     int size, COLORREF color, UINT align = DT_LEFT,
                     const wchar_t *family = nullptr, bool italic = false,
                     int weight = FW_BOLD) {
        HFONT font = CreateFontW(-size, 0, 0, 0, weight, italic ? TRUE : FALSE,
                                FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH, family ? family : whm::uiFontFamily(lang));
        HGDIOBJ old = SelectObject(dc, font);
        SetTextColor(dc, color);
        RECT r{x, y, x + w, y + h};
        DrawTextW(dc, value.c_str(), -1, &r, align | DT_VCENTER |
                  DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, old);
        DeleteObject(font);
    };
    auto number = [&](const std::wstring &value, int x, int y, int w, int h,
                      int fontSize, UINT alignment = DT_CENTER,
                      const whm::NumberPalette *colors = nullptr) {
        // Use ONE font for all glow passes (rather than creating ~20 per
        // number every 100ms) to keep the UI thread lightweight on Proton.
        const whm::NumberPalette palette =
            colors ? *colors : whm::kBaselineNumberPalette;
        auto cr = [](whm::Rgb c) { return RGB(c.r, c.g, c.b); };
        HFONT font=CreateFontW(-fontSize,0,0,0,FW_BOLD,TRUE,FALSE,FALSE,
                              DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
                              DEFAULT_PITCH,L"Georgia");
        HGDIOBJ old=SelectObject(dc,font);
        auto drawNumber=[&](int dx,int dy,COLORREF color) {
            RECT r{x+dx,y+dy,x+w+dx,y+h+dy};
            SetTextColor(dc,color);
            DrawTextW(dc,value.c_str(),-1,&r,alignment|DT_VCENTER|
                      DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        };
        const int offsets[][2]={{-3,0},{3,0},{0,-3},{0,3},
                                {-2,-2},{2,2},{-2,2},{2,-2}};
        for(const auto &p:offsets) drawNumber(p[0],p[1],cr(palette.glow));
        for(int dx=-1;dx<=1;++dx)
            for(int dy=-1;dy<=1;++dy)
                drawNumber(dx,dy,cr(palette.outline));
        drawNumber(0,-1,cr(palette.core));
        SelectObject(dc,old);
        DeleteObject(font);
    };
    auto art = [&](int ch, int card, int x, int y, int w, int h,
                   const std::wstring &fallback) {
        rounded(x-3, y-3, w+6, h+6, RGB(16, 13, 29), RGB(114, 89, 133), 5);
        rounded(x-1, y-1, w+2, h+2, RGB(26, 17, 37), gold, 3);
        HBITMAP bm = panelIcon(folder, ch, card);
        if (bm) {
            BITMAP info{};
            if (GetObjectW(bm, sizeof(info), &info) && info.bmWidth>0 && info.bmHeight>0) {
                HDC image = CreateCompatibleDC(dc);
                HGDIOBJ old = SelectObject(image, bm);
                SetStretchBltMode(dc, HALFTONE);
                StretchBlt(dc, x, y, w, h, image, 0, 0, info.bmWidth, info.bmHeight, SRCCOPY);
                SelectObject(image, old);
                DeleteDC(image);
            }
        } else {
            for (int stripe=0; stripe<6; ++stripe)
                fill(x, y + stripe*h/6, w, h/6+1,
                     RGB(27+stripe*3, 20+stripe*2, 54+stripe*4));
            disk(x+w/3, y+h/4, std::max(5,w/3), RGB(91, 57, 144));
            disk(x+w/2, y+h/3, std::max(4,w/6), RGB(187, 128, 227));
            write(fallback, x+2, y+h/2, w-4, h/3, std::min(w/2,19),
                  bright, DT_CENTER);
        }
        rule(x+1, y+2, x+w-2, y+2, RGB(245, 211, 144));
    };
    auto stacked = [&](int ch, int card, int x, int y, int copies,
                       int w, int h, int step, const std::wstring &fallback) {
        const int count = std::clamp(copies, 0, 4);
        if (count == 0) {
            art(ch, card, x, y, w, h, fallback);
            return;
        }
        for (int i=count-1; i>=0; --i)
            art(ch, card, x+i*step, y+i*2, w, h, fallback);
    };
    auto outlinedPanel = [&](int x, int y, int w, int h, bool focused) {
        rounded(x-3,y-3,w+6,h+6,RGB(30,21,43),
                focused ? RGB(117,77,56) : RGB(65,50,92),15);
        rounded(x-1,y-1,w+2,h+2,RGB(45,31,51),
                focused ? RGB(184,126,60) : RGB(103,78,130),13);
        rounded(x+1,y+1,w-2,h-2,RGB(21,18,36),
                focused ? gold : lavender,11);
        rule(x+17,y+6,x+w-17,y+6,focused?RGB(255,223,144):RGB(151,119,174));
        rule(x+17,y+h-7,x+w-17,y+h-7,focused?RGB(159,100,54):RGB(87,66,116));
        const COLORREF corner = focused ? bright : RGB(167,126,194);
        rule(x+6,y+20,x+6,y+9,corner,2);
        rule(x+6,y+9,x+23,y+9,corner,2);
        rule(x+w-6,y+h-20,x+w-6,y+h-9,corner,2);
        rule(x+w-6,y+h-9,x+w-23,y+h-9,corner,2);
    };
    auto ribbon = [&](int x, int y, int w, const std::wstring &caption,
                      bool focused, int fontSize) {
        POINT back[]{{x,y},{x+w-16,y},{x+w,y+16},
                     {x+w-16,y+32},{x,y+32},{x+10,y+16}};
        shape(back,6,focused?RGB(122,77,44):RGB(87,61,113));
        POINT inner[]{{x+2,y+3},{x+w-17,y+3},{x+w-4,y+16},
                      {x+w-17,y+29},{x+2,y+29},{x+12,y+16}};
        shape(inner,6,focused?RGB(64,42,41):RGB(49,36,70));
        rule(x+18,y+3,x+w-20,y+3,focused?bright:RGB(185,150,223));
        write(caption,x+14,y+2,w-28,28,fontSize,cream,DT_CENTER);
    };
    auto localized = [&](whm::HudTextKey key) {
        return panelWide(whm::hudText(key, lang));
    };
    // Base gradient plus a low-contrast hand-drawn night shrine background:
    // this is procedural GDI art, with an optional user-supplied panel-bg.png.
    for(int y=0; y<kPanelVirtualHeight; ++y) {
        int b=34+y/9, r=15+y/26, g=15+y/22;
        fill(0,y,kPanelVirtualWidth,1,RGB(r,g,b));
    }
    HBITMAP background = panelBackdrop(folder);
    if (background) {
        BITMAP info{};
        if (GetObjectW(background,sizeof(info),&info)&&info.bmWidth>0&&info.bmHeight>0) {
            HDC source=CreateCompatibleDC(dc);HGDIOBJ old=SelectObject(source,background);
            SetStretchBltMode(dc,HALFTONE);
            StretchBlt(dc,0,0,kPanelVirtualWidth,kPanelVirtualHeight,source,0,0,
                       info.bmWidth,info.bmHeight,SRCCOPY);
            SelectObject(source,old);DeleteDC(source);
        }
    } else {
        POINT mountainA[]{{0,145},{80,82},{168,134},{252,85},{363,153},{483,97},
                          {604,137},{719,76},{871,144},{1000,86},{1000,290},{0,290}};
        shape(mountainA,12,RGB(31,28,52));
        POINT mountainB[]{{0,186},{107,122},{238,190},{381,137},{504,185},
                          {639,136},{764,181},{901,123},{1000,189},{1000,290},{0,290}};
        shape(mountainB,11,RGB(28,23,45));
        fill(480,93,10,105,RGB(68,39,55));
        fill(556,93,10,105,RGB(68,39,55));
        fill(464,85,118,10,RGB(95,56,64));
        fill(476,115,96,6,RGB(94,53,64));
        POINT roof[]{{449,88},{516,49},{592,88},{581,96},{515,66},{456,97}};
        shape(roof,6,RGB(80,48,67));
        for (int i=0;i<35;++i) {
            const int x=(i*137+61)%997, y=(i*53+14)%280;
            disk(x,y,i%5==0?3:2,i%3==0?RGB(120,72,117):RGB(98,77,119));
        }
    }
    for (int j=0; j<4; ++j) {
        rule(323,166+j*2,554,166+j*2,RGB(65+j*11,42+j*8,45+j*3));
        rule(707,166+j*2,941,166+j*2,RGB(65+j*11,42+j*8,45+j*3));
    }
    {
        HPEN edge=CreatePen(PS_SOLID,2,RGB(120,79,145));
        HGDIOBJ oldPen=SelectObject(dc,edge);
        HGDIOBJ oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
        RoundRect(dc,2,2,998,288,16,16);
        SelectObject(dc,oldBrush);
        SelectObject(dc,oldPen);
        DeleteObject(edge);
    }
    rule(14,11,986,11,RGB(180,127,191));
    rule(14,281,986,281,RGB(159,109,176));
    disk(9,9,4,gold); disk(987,9,4,gold);
    disk(9,278,4,gold); disk(987,278,4,gold);

    // ---- "伤害影响 · 总修正": one big common percentage, a quiet guts
    // number and two independent type multipliers. Rows below only explain.
    outlinedPanel(14,15,200,260,false);
    write(localized(whm::HudTextKey::PanelTitle),24,18,180,25,17,cream,DT_CENTER);
    const whm::NumberPalette heat = whm::paletteForPercent(v.commonPercent);
    number(panelPercent(v.commonPercent),24,44,116,50,42,DT_LEFT,&heat);
    // Guts stays visually subordinate: small, low-contrast grey.
    const std::string guts=v.gutsText.empty()?whm::gutsLabel(v.hpFactor):v.gutsText;
    write(panelWide(guts),142,68,66,20,13,
          quietGrey,DT_LEFT,L"Georgia");
    if(v.firstHitActive) {
        write(localized(whm::HudTextKey::FirstHit)+L" ×"+
                  panelWide(panelFactor(v.firstHitMultiplier,2)),
              28,94,174,16,11,RGB(238,177,104));
    }
    const int typeY = v.firstHitActive ? 114 : 100;
    write(localized(whm::HudTextKey::SpecialType)+L"×"+
              panelWide(panelFactor(v.skillMultiplier,1)),
          28,typeY,96,20,13,cream);
    write(localized(whm::HudTextKey::SpellType)+L" ×"+
              panelWide(panelFactor(v.spellMultiplier,1)),
          124,typeY,84,20,13,cream);
    const int dividerY = typeY + 24;
    rule(28,dividerY,198,dividerY,RGB(111,91,143));
    const int count=static_cast<int>(v.effects.size());
    const int effectTop=dividerY+7;
    const int effectBottom=250;
    const int pitch=count?std::min(46,(effectBottom-effectTop)/count):46;
    int effectY=effectTop;
    for(const auto &effect:v.effects) {
        // Never silently drop an effect row: pitch shrinks instead.
        const int cardHeight=std::max(8,std::min(pitch-2,41));
        const int cardWidth=std::max(8,cardHeight*41/65);
        const std::string label=whm::localizeEffectLabel(effect.label.c_str(),lang);
        const std::string value=whm::localizeEffectValue(effect.value,lang);
        const std::wstring labelWide=panelWide(label);
        const std::wstring glyph=labelWide.empty()?L"?":labelWide.substr(0,1);
        stacked(effect.character,effect.card,29,effectY,effect.copies,cardWidth,cardHeight,
                std::max(3,cardWidth/4),glyph);
        std::wstring shownLabel=labelWide;
        if(effect.copies>1)shownLabel+=L" ×"+std::to_wstring(effect.copies);
        if(pitch>=24) {
            write(shownLabel,83,effectY-2,116,pitch/2,12,cream);
            write(panelWide(value),83,effectY+pitch/2-2,116,pitch/2,16,bright);
        } else {
            if(effect.copies>1)write(L"×"+std::to_wstring(effect.copies),51,effectY,29,pitch,11,bright);
            write(shownLabel,83,effectY,63,pitch,10,cream);
            write(panelWide(value),147,effectY,54,pitch,11,bright);
        }
        effectY+=pitch;
    }
    if(!count)write(localized(whm::HudTextKey::NoEffects),29,effectTop+4,171,30,14,faded);
    // Only an actually effective weather gets a text line.
    if(v.weatherVisible)
        write(localized(whm::HudTextKey::Weather)+L" "+
                  panelWide(whm::weatherName(v.weather,lang)),
              30,255,168,16,12,RGB(198,181,219));

    auto highlight = [&](int x, whm::HudTextKey titleKey, const PanelCard &card,
                         whm::HudTextKey missingKey, bool focused) {
        outlinedPanel(x,17,373,163,focused);
        POINT hill1[]{{x+88,149},{x+139,126},{x+182,146},{x+246,119},
                       {x+297,143},{x+367,128},{x+367,171},{x+88,171}};
        shape(hill1,8,RGB(33,27,48));
        POINT hill2[]{{x+87,157},{x+151,145},{x+208,164},{x+266,141},
                       {x+356,159},{x+366,166},{x+366,173},{x+87,173}};
        shape(hill2,8,RGB(38,27,48));
        for(int p=0;p<7;++p) {
            int px=x+115+(p*41)%230, py=42+(p*19)%33;
            disk(px,py,p%3==0?4:2,focused?RGB(113,83,82):RGB(94,73,117));
        }
        fill(x+99,130,254,17,RGB(49,30,43));
        rule(x+111,148,x+351,148,focused?RGB(159,91,49):RGB(118,77,112));
        const int titleSize=lang==whm::HudLanguage::English?14:
                            lang==whm::HudLanguage::Japanese?17:19;
        ribbon(x+18,8,337,localized(titleKey),focused,titleSize);
        const std::wstring display=panelCardLabel(card,lang,localized(missingKey));
        std::wstring fallback = card.card<0 ? L"技" : L"SC";
        if(card.card<0 && !card.notation.empty()) fallback=panelWide(card.notation);
        else if(card.card<0 && !card.name.empty()) {
            const size_t cut=display.rfind(L" · ");
            const std::wstring notation=cut!=std::wstring::npos?
                display.substr(cut+3):display;
            if(notation.size()<=8 && !notation.empty()) fallback=notation;
        }
        art(card.character,card.card,x+17,59,67,94,fallback);
        write(display,x+103,56,250,37,20,cream);
        if(card.damage>0) {
            const std::wstring value=(card.approximate?L"≈":L"")+
                                     std::to_wstring(card.damage);
            number(value,x+91,91,258,70,55,DT_CENTER);
        } else {
            write(L"—",x+98,102,248,49,46,faded,DT_CENTER);
        }
        write(localized(focused?whm::HudTextKey::Theoretical:whm::HudTextKey::MatchBest),
              x+109,151,240,17,11,RGB(187,162,147),DT_CENTER);
    };
    highlight(224,whm::HudTextKey::Strongest,v.strongest,
              whm::HudTextKey::NoSkill,true);
    highlight(608,whm::HudTextKey::Record,v.record,
              whm::HudTextKey::NoRecord,false);

    // Bottom strip: live hand order, then the square 2x2 pad at its RIGHT edge.
    outlinedPanel(225,194,758,80,false);
    write(localized(whm::HudTextKey::Hand),239,203,110,22,17,cream);
    write(localized(whm::HudTextKey::SpellMax),239,230,110,22,14,RGB(206,181,224));
    rule(348,203,348,267,RGB(110,90,139));
    const size_t shown=std::min<size_t>(v.hand.size(),5);
    if (shown==0) {
        write(localized(whm::HudTextKey::NoHand),380,206,510,60,20,faded,DT_CENTER);
    } else {
        for(size_t i=0;i<shown;++i) {
            const PanelCard &card=v.hand[i];
            const int x=380+static_cast<int>(i)*100;
            art(card.character,card.card,x,208,32,45,L"SC");
            const std::wstring amount=card.damage>0?
                ((card.approximate?L"≈":L"")+std::to_wstring(card.damage)):L"—";
            write(amount,x-21,255,80,20,20,bright,DT_CENTER,L"Georgia",true);
        }
    }
    rule(900,203,900,267,RGB(110,90,139));
    rounded(904,198,72,72,RGB(24,19,38),RGB(103,78,130),8);
    auto button = [&](const HudButton &b,const wchar_t *caption,bool active) {
        rounded(b.x,b.y,b.w,b.h,active?RGB(96,64,44):RGB(28,23,44),
                active?gold:RGB(103,78,130),5);
        write(caption,b.x,b.y,b.w,b.h,11,active?bright:cream,DT_CENTER,L"Segoe UI");
    };
    const int selected=static_cast<int>(lang);
    button(kHudButtons[0],L"CN",selected==0);
    button(kHudButtons[1],L"JP",selected==1);
    button(kHudButtons[2],L"EN",selected==2);
    button(kHudButtons[3],L"↑",false);
    // Fallback arrow for sessions where the game keeps the system cursor
    // hidden. It is only drawn when the real cursor is not showing.
    if(!v.systemCursorVisible&&v.cursorX>=0&&v.cursorY>=0) {
        auto arrow=[&](int dx,int dy,COLORREF color) {
            POINT a[]{{v.cursorX+dx,v.cursorY+dy},
                      {v.cursorX+dx,v.cursorY+dy+17},
                      {v.cursorX+dx+4,v.cursorY+dy+13},
                      {v.cursorX+dx+7,v.cursorY+dy+20},
                      {v.cursorX+dx+10,v.cursorY+dy+19},
                      {v.cursorX+dx+7,v.cursorY+dy+12},
                      {v.cursorX+dx+13,v.cursorY+dy+11}};
            shape(a,7,color);
        };
        arrow(1,1,RGB(18,14,28));
        arrow(0,0,RGB(255,246,225));
    }
    SetMapMode(dc,MM_TEXT);
    BitBlt(target,0,0,width,height,dc,0,0,SRCCOPY);
    SelectObject(dc,previous);
    DeleteObject(buffer);
    DeleteDC(dc);
    EndPaint(window,&ps);
}
