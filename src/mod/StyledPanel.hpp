#pragma once
// SokuMaxDamage HUD artwork, v2 (Fig. 1 ornament + Fig. 2 information hierarchy).
// Read-only companion window. Rendering deliberately stays on the existing
// dedicated UI thread; never access live game objects from WM_PAINT.
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct PanelCard {
    int character = -1, card = -1, damage = 0;
    std::string name;
    bool approximate = true;
};
struct PanelView {
    int character = -1, opponentCharacter = -1, rods = 0, dolls = 0, weather = 0;
    double attack = 1., defense = 1.;
    double skillMultiplier = 1., spellMultiplier = 1.;
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
    return panelBitmap(panelWide(folder + "icons/" + std::to_string(character) +
                                 "-" + std::to_string(card) + ".png"));
}
inline HBITMAP panelBackdrop(const std::string &folder) {
    return panelBitmap(panelWide(folder + "art/panel-bg.png"));
}

inline void paintStyledPanel(HWND window, const PanelView &v, const std::string &folder) {
    PAINTSTRUCT ps;
    HDC target = BeginPaint(window, &ps);
    RECT bounds{};
    GetClientRect(window, &bounds);
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) { EndPaint(window, &ps); return; }
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
    SetWindowExtEx(dc, 1000, 290, nullptr);
    SetViewportExtEx(dc, width, height, nullptr);
    SetBkMode(dc, TRANSPARENT);

    const COLORREF gold = RGB(230, 182, 94);
    const COLORREF bright = RGB(255, 226, 161);
    const COLORREF cream = RGB(252, 236, 208);
    const COLORREF faded = RGB(185, 180, 203);
    const COLORREF lavender = RGB(156, 125, 196);
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
                     const wchar_t *family = L"Microsoft YaHei", bool italic = false,
                     int weight = FW_BOLD) {
        HFONT font = CreateFontW(-size, 0, 0, 0, weight, italic ? TRUE : FALSE,
                                FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH, family);
        HGDIOBJ old = SelectObject(dc, font);
        SetTextColor(dc, color);
        RECT r{x, y, x + w, y + h};
        DrawTextW(dc, value.c_str(), -1, &r, align | DT_VCENTER |
                  DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, old);
        DeleteObject(font);
    };
    auto number = [&](const std::wstring &value, int x, int y, int w, int h,
                      int fontSize, UINT alignment = DT_CENTER) {
        // Deliberately use text-outline layers, not AlphaBlend: keeps the same
        // GDI import set as the verified game-compatible window.
        const int offsets[][2] = {{-3,0},{3,0},{0,-3},{0,3},
                                  {-2,-2},{2,2},{-2,2},{2,-2}};
        for (auto &p : offsets)
            write(value, x+p[0], y+p[1], w, h, fontSize,
                  RGB(113, 58, 39), alignment, L"Georgia", true);
        for (int dx=-1; dx<=1; ++dx)
            for (int dy=-1; dy<=1; ++dy)
                write(value, x+dx, y+dy, w, h, fontSize,
                      RGB(225, 124, 57), alignment, L"Georgia", true);
        write(value, x, y-1, w, h, fontSize, RGB(255, 238, 191),
              alignment, L"Georgia", true);
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
            // Decorative fallback is a card-shaped jewel, not an invented move icon.
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
        // Draw back cards first; each offset exposes a recognizable stepped
        // border, rather than compressing a pile into one stretched texture.
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
        // Subtle ornamental corners from the Fig. 1 concept.
        const COLORREF corner = focused ? bright : RGB(167,126,194);
        rule(x+6,y+20,x+6,y+9,corner,2);
        rule(x+6,y+9,x+23,y+9,corner,2);
        rule(x+w-6,y+h-20,x+w-6,y+h-9,corner,2);
        rule(x+w-6,y+h-9,x+w-23,y+h-9,corner,2);
    };
    auto ribbon = [&](int x, int y, int w, const std::wstring &caption,
                      bool focused) {
        POINT back[]{{x,y},{x+w-16,y},{x+w,y+16},
                     {x+w-16,y+32},{x,y+32},{x+10,y+16}};
        shape(back,6,focused?RGB(122,77,44):RGB(87,61,113));
        POINT inner[]{{x+2,y+3},{x+w-17,y+3},{x+w-4,y+16},
                      {x+w-17,y+29},{x+2,y+29},{x+12,y+16}};
        shape(inner,6,focused?RGB(64,42,41):RGB(49,36,70));
        rule(x+18,y+3,x+w-20,y+3,focused?bright:RGB(185,150,223));
        write(caption,x+14,y+2,w-28,28,19,cream,DT_CENTER);
    };
    // Base gradient plus a low-contrast hand-drawn night shrine background:
    // this is procedural GDI art, with an optional user-supplied panel-bg.png.
    for(int y=0; y<290; ++y) {
        int b=34+y/9, r=15+y/26, g=15+y/22;
        fill(0,y,1000,1,RGB(r,g,b));
    }
    HBITMAP background = panelBackdrop(folder);
    if (background) {
        BITMAP info{};
        if (GetObjectW(background,sizeof(info),&info)&&info.bmWidth>0&&info.bmHeight>0) {
            HDC source=CreateCompatibleDC(dc);HGDIOBJ old=SelectObject(source,background);
            SetStretchBltMode(dc,HALFTONE);
            StretchBlt(dc,0,0,1000,290,source,0,0,info.bmWidth,info.bmHeight,SRCCOPY);
            SelectObject(source,old);DeleteDC(source);
        }
    } else {
        POINT mountainA[]{{0,145},{80,82},{168,134},{252,85},{363,153},{483,97},
                          {604,137},{719,76},{871,144},{1000,86},{1000,290},{0,290}};
        shape(mountainA,12,RGB(31,28,52));
        POINT mountainB[]{{0,186},{107,122},{238,190},{381,137},{504,185},
                          {639,136},{764,181},{901,123},{1000,189},{1000,290},{0,290}};
        shape(mountainB,11,RGB(28,23,45));
        // Faraway shrine gate and bridge silhouettes.
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
    // Marginal warm glow streaks around the two focal numbers.
    for (int j=0; j<4; ++j) {
        rule(323,166+j*2,554,166+j*2,RGB(65+j*11,42+j*8,45+j*3));
        rule(707,166+j*2,941,166+j*2,RGB(65+j*11,42+j*8,45+j*3));
    }
    // Outer frame with a hollow brush: keep the illustrated backdrop visible.
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

    // Left: card stacks with the exact "pile" presentation from Fig. 2.
    outlinedPanel(14,15,200,260,false);
    write(L"伤害影响",30,18,166,30,19,cream);
    rule(28,50,198,50,RGB(111,91,143));
    stacked(v.character,18,30,65,v.rods,33,48,9,L"棒");
    const double rodEffect=1.0+0.1*std::clamp(v.rods,0,4);
    wchar_t multiplier[80]{};
    swprintf_s(multiplier,L"×%.2f",rodEffect);
    number(multiplier,91,61,112,49,34,DT_LEFT);
    write(L"制御棒 ×"+std::to_wstring(v.rods),90,109,107,20,15,faded);
    rule(28,135,198,135,RGB(99,79,126));
    // Only observable and relevant effects are rendered; zero stacks are
    // intentionally not expanded into pretend Buff/Debuff card icons.
    int effectY=145;
    auto effect = [&](int character,int card,int copies,const std::wstring &abbr,
                      const std::wstring &label,const std::wstring &value) {
        if(effectY>237)return;
        stacked(character,card,30,effectY+1,copies,22,30,6,abbr);
        write(label,72,effectY-2,124,17,13,cream);
        write(value,72,effectY+13,126,22,18,bright);
        effectY+=43;
    };
    if (v.dolls>0 || std::abs(v.defense-1.0)>0.008) {
        swprintf_s(multiplier,L"敌受伤 ×%.2f",v.defense);
        effect(v.opponentCharacter,6,v.dolls,L"偶",L"对方防御 / 人偶",multiplier);
    }
    // The raw attack multiplier already includes rods, so this row is merely
    // a reading of game memory. We do not add rodEffect again to damage.
    if (std::abs(v.attack-rodEffect)>0.008) {
        swprintf_s(multiplier,L"实际 ×%.2f",v.attack);
        effect(v.character,-1,1,L"攻",L"其他攻击效果",multiplier);
    }
    if (std::abs(v.skillMultiplier-1.0)>0.008) {
        swprintf_s(multiplier,L"必杀 ×%.2f",v.skillMultiplier);
        effect(v.character,-1,1,L"技",L"必杀技倍率",multiplier);
    }
    if (std::abs(v.spellMultiplier-1.0)>0.008) {
        swprintf_s(multiplier,L"SC ×%.2f",v.spellMultiplier);
        effect(v.character,-1,1,L"符",L"符卡倍率",multiplier);
    }
    static const wchar_t *weatherNames[]={
        L"快晴",L"雾雨",L"阴天",L"苍天",L"冰雹",L"春霭",L"浓雾",
        L"雪",L"太阳雨",L"疏雨",L"风雨",L"晴岚",L"川雾",
        L"台风",L"凪",L"钻石尘",L"黄沙",L"烈日",L"梅雨",
        L"极光",L"黄昏",L"无天气"
    };
    const std::wstring weather=v.weather>=0&&v.weather<22?weatherNames[v.weather]:L"未知";
    // Always display current weather, even if too many effects occupy the left list.
    write(L"天气 "+weather,30,251,168,17,13,RGB(198,181,219));

    auto highlight = [&](int x,const std::wstring &title,const PanelCard &card,
                         const std::wstring &missing,bool focused) {
        outlinedPanel(x,17,373,163,focused);
        ribbon(x+18,8,337,title,focused);
        const std::wstring display=card.name.empty()?missing:panelWide(card.name);
        std::wstring fallback = card.card<0 ? L"技" : L"SC";
        if(card.card<0 && !card.name.empty()) {
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
        write(focused?L"THEORETICAL MAX · ESTIMATE":L"MATCH BEST · HIT",
              x+109,151,240,17,11,RGB(187,162,147),DT_CENTER,L"Segoe UI");
    };
    highlight(224,L"当前最高伤害 · 非 SC",v.strongest,L"暂无可靠伤害资料",true);
    highlight(608,L"本局最高实际伤害",v.record,L"尚未记录命中",false);

    // A single fixed strip. The array's order is the live game hand order;
    // there is no selection, clicking, or P1/P2-dependent mirroring.
    outlinedPanel(225,194,758,80,false);
    write(L"当前手牌",239,203,103,22,18,cream);
    write(L"SC 最大伤害",239,226,111,20,15,RGB(206,181,224));
    write(L"参考估算 · 随手牌顺序",239,249,114,16,10,faded);
    rule(356,203,356,267,RGB(110,90,139));
    const size_t shown=std::min<size_t>(v.hand.size(),5);
    if (shown==0) {
        write(L"目前手牌中没有 SC",392,206,555,60,22,faded,DT_CENTER);
    } else {
        // Cards remain in original order and left-aligned consistently,
        // regardless of the local player's side.
        for(size_t i=0;i<shown;++i) {
            const PanelCard &card=v.hand[i];
            const int x=377+static_cast<int>(i)*120;
            art(card.character,card.card,x,208,32,45,L"SC");
            const std::wstring amount=card.damage>0?
                ((card.approximate?L"≈":L"")+std::to_wstring(card.damage)):L"—";
            write(amount,x-21,255,80,20,21,bright,DT_CENTER,L"Georgia",true);
        }
    }
    SetMapMode(dc,MM_TEXT);
    BitBlt(target,0,0,width,height,dc,0,0,SRCCOPY);
    SelectObject(dc,previous);
    DeleteObject(buffer);
    DeleteDC(dc);
    EndPaint(window,&ps);
}
