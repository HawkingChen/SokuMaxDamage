#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <map>
struct PanelCard { int character=-1, card=-1, damage=0; std::string name; bool approximate=true; };
struct PanelView { int character=-1, rods=0, dolls=0, weather=0; double attack=1, defense=1; PanelCard strongest, record; std::vector<PanelCard> hand; };
inline std::wstring panelWide(const std::string &s) {
 int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,nullptr,0);if(n<=0)return L"";
 std::wstring w(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,w.data(),n);w.pop_back();return w;
}
inline HBITMAP panelIcon(const std::string &folder,int character,int card) {
 static std::map<std::pair<int,int>,HBITMAP> cache;auto key=std::make_pair(character,card);auto it=cache.find(key);if(it!=cache.end())return it->second;
 struct Startup{UINT version;void *callback;BOOL suppressThread,suppressCodecs;};
 static HMODULE dll=LoadLibraryW(L"gdiplus.dll");static ULONG_PTR token=0;
 using Start=int(WINAPI*)(ULONG_PTR*,const Startup*,void*);using Load=int(WINAPI*)(const WCHAR*,void**);using Bitmap=int(WINAPI*)(void*,HBITMAP*,DWORD);using Dispose=int(WINAPI*)(void*);
 if(!dll)return nullptr;
 if(!token){Startup cfg{1,nullptr,FALSE,FALSE};auto start=(Start)GetProcAddress(dll,"GdiplusStartup");if(!start||start(&token,&cfg,nullptr))return nullptr;}
 auto load=(Load)GetProcAddress(dll,"GdipLoadImageFromFile");auto bitmap=(Bitmap)GetProcAddress(dll,"GdipCreateHBITMAPFromBitmap");auto dispose=(Dispose)GetProcAddress(dll,"GdipDisposeImage");
 HBITMAP result=nullptr;void *img=nullptr;auto path=panelWide(folder+"icons/"+std::to_string(character)+"-"+std::to_string(card)+".png");
 if(load&&bitmap&&dispose&&!load(path.c_str(),&img)){bitmap(img,&result,0xff16131f);dispose(img);}cache[key]=result;return result;
}
inline void paintStyledPanel(HWND window,const PanelView &v,const std::string &folder) {
 PAINTSTRUCT ps;HDC target=BeginPaint(window,&ps);RECT bounds;GetClientRect(window,&bounds);int width=bounds.right,height=bounds.bottom;if(width<=0||height<=0){EndPaint(window,&ps);return;}
 HDC dc=CreateCompatibleDC(target);HBITMAP buffer=CreateCompatibleBitmap(target,width,height);auto previous=SelectObject(dc,buffer);
 SetMapMode(dc,MM_ANISOTROPIC);SetWindowExtEx(dc,1000,290,nullptr);SetViewportExtEx(dc,width,height,nullptr);SetBkMode(dc,TRANSPARENT);
 auto box=[&](int x,int y,int w,int h,COLORREF fill,COLORREF edge,int round=14){auto brush=CreateSolidBrush(fill);auto pen=CreatePen(PS_SOLID,2,edge);auto oldb=SelectObject(dc,brush);auto oldp=SelectObject(dc,pen);RoundRect(dc,x,y,x+w,y+h,round,round);SelectObject(dc,oldb);SelectObject(dc,oldp);DeleteObject(brush);DeleteObject(pen);};
 auto text=[&](const std::wstring &label,int x,int y,int w,int h,int size,COLORREF color,UINT align=DT_LEFT){auto font=CreateFontW(-size,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");auto old=SelectObject(dc,font);SetTextColor(dc,color);RECT r{x,y,x+w,y+h};DrawTextW(dc,label.c_str(),-1,&r,align|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);SelectObject(dc,old);DeleteObject(font);};
 auto icon=[&](int ch,int card,int x,int y,int w,int h,const std::wstring &fallback){box(x-2,y-2,w+4,h+4,RGB(30,24,42),RGB(191,156,85),4);HBITMAP bm=panelIcon(folder,ch,card);if(bm){BITMAP info;GetObjectW(bm,sizeof(info),&info);HDC image=CreateCompatibleDC(dc);auto old=SelectObject(image,bm);SetStretchBltMode(dc,HALFTONE);StretchBlt(dc,x,y,w,h,image,0,0,info.bmWidth,info.bmHeight,SRCCOPY);SelectObject(image,old);DeleteDC(image);}else text(fallback,x+2,y,w-4,h,18,RGB(207,192,231),DT_CENTER);};
 const COLORREF gold=RGB(226,180,91),cream=RGB(249,231,190),muted=RGB(170,163,185);
 box(0,0,1000,290,RGB(15,13,24),RGB(94,75,123));
 box(10,12,205,264,RGB(26,21,37),RGB(127,98,156));
 text(L"当前伤害状态",24,20,180,28,19,cream);
 icon(v.character,18,25,60,48,66,L"棒");text(L"制御棒 ×"+std::to_wstring(v.rods),88,62,115,24,16,cream);
 wchar_t value[80];swprintf_s(value,L"×%.2f",v.attack);text(value,87,90,116,34,27,gold);
 icon(v.character,6,25,142,35,46,L"偶");text(L"人偶 ×"+std::to_wstring(v.dolls),72,140,130,24,16,cream);
 swprintf_s(value,L"对手防御 ×%.2f",v.defense);text(value,25,198,180,26,17,muted);
 static const wchar_t *weatherNames[]={L"快晴",L"雾雨",L"阴天",L"苍天",L"冰雹",L"春霭",L"浓雾",L"雪",L"太阳雨",L"疏雨",L"风雨",L"晴岚",L"川雾",L"台风",L"凪",L"钻石尘",L"黄沙",L"烈日",L"梅雨",L"极光",L"黄昏",L"无天气"};
 text(v.weather>=0&&v.weather<22?weatherNames[v.weather]:L"天气未知",25,234,180,24,16,muted);
 auto mainCard=[&](int x,const wchar_t *title,const PanelCard &c,const wchar_t *empty){box(x,12,375,170,RGB(30,23,30),gold);box(x+15,17,345,30,RGB(46,33,29),RGB(119,88,46),8);text(title,x+20,17,335,30,20,cream,DT_CENTER);icon(c.character,c.card,x+15,60,65,99,L"招式");text(c.name.empty()?empty:panelWide(c.name),x+94,56,264,34,21,cream);text(c.damage>0?((c.approximate?L"≈":L"")+std::to_wstring(c.damage)):L"—",x+94,90,264,68,52,gold,DT_CENTER);};
 mainCard(225,L"当前最高伤害 · 非 SC",v.strongest,L"暂无可靠伤害资料");mainCard(610,L"本局最高实际伤害",v.record,L"尚未记录命中");
 box(225,194,760,82,RGB(28,23,43),RGB(139,109,172));text(L"当前手牌 SC",235,202,115,26,18,cream);text(L"参考最大伤害",235,232,115,24,14,muted);
 for(size_t i=0;i<v.hand.size()&&i<5;++i){const auto &c=v.hand[i];int x=365+int(i)*122;icon(c.character,c.card,x,202,39,58,L"SC");text(c.damage>0?L"≈"+std::to_wstring(c.damage):L"未知",x+45,215,73,34,22,gold);}
 if(v.hand.empty())text(L"手牌中没有符卡",370,212,580,40,20,muted,DT_CENTER);
 SetMapMode(dc,MM_TEXT);BitBlt(target,0,0,width,height,dc,0,0,SRCCOPY);SelectObject(dc,previous);DeleteObject(buffer);DeleteDC(dc);EndPaint(window,&ps);
}
