// WhatHurtsMost: local-only HUD prototype for Touhou Hisoutensoku 1.10a.
// No network packets, game damage, RNG or inputs are modified.
#include <SokuLib.hpp>
#include "DamageEngine.hpp"
#include "MoveCatalog.hpp"
#include "WikiMax.hpp"
#include "RecordTracker.hpp"
#include "FrameProbe.hpp"
#include "StyledPanel.hpp"
#include "PanelVisibility.hpp"
#include "LocalPlayerSide.hpp"
#include "ActionIdentity.hpp"

#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
HMODULE g_module = nullptr;
bool g_hooksInstalled = false;
bool g_fontReady = false;
SokuLib::BattleManager *g_lastManager = nullptr;
int g_lastLocalSide = whm::kNoLocalPlayer;
unsigned g_lastFrame = 0;
unsigned g_lastRefresh = 0;
std::string g_displayText;
SokuLib::SWRFont g_font{};
constexpr int kHudRowHeight = 18;
constexpr size_t kMaxHudRows = 20;
std::array<SokuLib::DrawUtils::Sprite, kMaxHudRows> g_lineSprites;
struct HudRow { std::string text; int iconCardId = -1; };
std::vector<HudRow> g_rows;

SokuLib::DrawUtils::RectangleShape g_background;
whm::MoveCatalog g_catalog;
whm::WikiMaxCatalog g_wikiMax;
whm::RecordTracker g_record;
bool g_allowUnverifiedProbes = true;
std::vector<whm::Move> g_probeCache;
int g_probeCharacter = -1;
const void *g_probeDataSource = nullptr;
std::array<int, 32> g_probeLevels{};
int g_x = 405;
int g_y = 160;
int g_w = 230;
int g_h = 230;

static int (SokuLib::BattleManager::*g_originalProcess)() = nullptr;
static void (SokuLib::BattleManager::*g_originalRender)() = nullptr;
static void (SokuLib::BattleManager::*g_originalExit)() = nullptr;

std::string dllFolder() {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(g_module, buf, MAX_PATH);
    std::string full(buf);
    auto last = full.find_last_of("\\/");
    return last == std::string::npos ? ".\\" : full.substr(0, last + 1);
}

// Check that the SokuLib runtime enum values match the portable mapping.
static_assert(SokuLib::BATTLE_MODE_VSCOM == 2 &&
              SokuLib::BATTLE_MODE_VSPLAYER == 3 &&
              SokuLib::BATTLE_MODE_VSCLIENT == 4 &&
              SokuLib::BATTLE_MODE_VSSERVER == 5 &&
              SokuLib::BATTLE_MODE_VSWATCH == 6 &&
              SokuLib::BATTLE_MODE_PRACTICE == 8 &&
              SokuLib::BATTLE_SUBMODE_REPLAY == 2);

int ownSide() {
    return whm::localPlayerSide(
        static_cast<int>(SokuLib::mainMode),
        static_cast<int>(SokuLib::subMode));
}

bool isOwnBattleMode() {
    return ownSide() != whm::kNoLocalPlayer;
}

const SokuLib::v2::Player *viewPlayer(const SokuLib::BattleManager *manager, int side) {
    // Both SokuLib structures intentionally describe the exact same v1.10a player layout.
    const void *ptr = side == 0 ? static_cast<const void*>(&manager->leftCharacterManager)
                                : static_cast<const void*>(&manager->rightCharacterManager);
    return reinterpret_cast<const SokuLib::v2::Player *>(ptr);
}

whm::Fighter state(const SokuLib::v2::Player &player) {
    whm::Fighter r;
    r.character = static_cast<int>(player.characterIndex);
    r.hp = player.HP;
    r.attack = player.attackPower;
    r.defense = player.defensePower;
    r.spellMultiplier = player.spellDmgMultiplier;
    r.skillMultiplier = player.specialDmgMultiplier;
    r.activeWeather = static_cast<int>(SokuLib::activeWeather);
    return r;
}

// Original card names come from the running game's own card lookup.
// The Soku text renderer expects the original Japanese codepage (CP932).
std::string shortenCp932(const std::string &src, int maxCells = 21) {
    std::string out;
    int cells = 0;
    for (size_t i = 0; i < src.size() && cells < maxCells;) {
        const unsigned char b = static_cast<unsigned char>(src[i]);
        const bool lead = (b >= 0x81 && b <= 0x9f) || (b >= 0xe0 && b <= 0xfc);
        const int width = lead ? 2 : 1;
        const size_t bytes = (lead && i + 1 < src.size()) ? 2 : 1;
        if (cells + width > maxCells) break;
        out.append(src, i, bytes);
        cells += width;
        i += bytes;
    }
    if (out.size() < src.size()) out += "..";
    return out;
}

struct StaticMove { int character,card,damage; std::string kind,name,notation; };
std::vector<StaticMove> g_staticMoves;

std::string cardName(const SokuLib::v2::Player &player, unsigned short cardId) {
    for(const auto &m:g_staticMoves)
        if(m.character==static_cast<int>(player.characterIndex)&&m.card==cardId)return m.name;
    if (cardId < 100) return "卡牌 " + std::to_string(cardId);
    auto *info = const_cast<SokuLib::v2::Player &>(player).deckInfo.cardLookup(cardId);
    if (!info) return "CARD#" + std::to_string(cardId);
    const auto raw = static_cast<const char *>(info->name);
    if (!raw || !raw[0]) return "CARD#" + std::to_string(cardId);
    std::string name = shortenCp932(raw);
    int n = MultiByteToWideChar(932, 0, name.c_str(), -1, nullptr, 0);
    std::wstring wide(n, L'\0'); MultiByteToWideChar(932, 0, name.c_str(), -1, wide.data(), n);
    int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(bytes, '\0'); WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), bytes, nullptr, nullptr);
    if (!utf8.empty()) utf8.pop_back(); return utf8;
}

std::string actionName(const SokuLib::v2::Player &player, int action) {
    static const char *const normals[] = {
        "5A", "f5A", "6A", "2A", "3A", "DA", "j5A", "j6A", "j2A", "j8A"
    };
    if (action >= 300 && action < 310) return normals[action - 300];
    if (action >= 500 && action < 600) {
        int card=whm::skillCardForAction(static_cast<int>(player.characterIndex),action);
        bool air=(action-500)%5>=2;char button=(action-500)%5%2?'C':'B';
        for(const auto &m:g_staticMoves) {
            if(m.character!=static_cast<int>(player.characterIndex)||m.card!=card||m.notation.empty())continue;
            bool entryAir=m.notation.rfind("j.",0)==0;
            if(entryAir==air&&m.notation.back()==button)return m.name+" · "+m.notation;
        }
        if(card>=0)return cardName(player,static_cast<unsigned short>(card));
    }
    switch(action) {
        case 311:return "f2A";case 320:return "5AA";case 321:return "5AAA";
        case 322:return "5AAAA";case 323:return "5AAAAA";case 324:return "5AAA3A";
        case 325:return "j5AA";case 330:return "4A";case 331:return "3A";case 332:return "6A";
        case 400:return "5B";case 401:return "6B";case 402:return "2B";case 403:return "3B";
        case 404:return "j5B";case 405:return "j6B";case 406:return "j2B";case 407:return "4B";
        case 408:return "66B";case 409:return "j4B";case 410:return "5C";case 411:return "6C";
        case 412:return "2C";case 413:return "4C";case 414:return "j5C";case 415:return "j6C";
        case 416:return "j2C";case 417:return "j1C";case 418:return "66C";
    }
    if (action >= 600 && action < 620)
        return cardName(player, static_cast<unsigned short>(action - 400));
    return "ACTION#" + std::to_string(action);
}

// Built-in skill mapping is not guaranteed identical across all characters.
// Only show skill estimates if explicitly present and marked supported in the catalog.
bool skillOwned(const SokuLib::v2::Player &player, int action) {
    if (action < 500 || action >= 600) return false;
    const int relative = action - 500;
    const int slot = relative / 20;
    const int skillVariant = (relative % 20) / 5;
    if (slot < 0 || slot >= 5 || skillVariant >= 3) return false;
    const int skillIdx = slot * 3 + skillVariant;
    // For 1.10a this can indicate a learned skill; unverified for custom characters.
    return player.skilledSkillLevel[skillIdx] >= 0 &&
           player.effectiveSkillLevel[skillIdx] >= 0;
}

void buildProbeCache(const SokuLib::v2::Player &own) {
    std::array<int, 32> levels{};
    for (unsigned i = 0; i < levels.size(); ++i)
        levels[i] = static_cast<int>(own.effectiveSkillLevel[i]);
    if (g_probeCharacter == static_cast<int>(own.characterIndex) &&
        g_probeDataSource == own.gameData.patternMap &&
        g_probeLevels == levels && !g_probeCache.empty()) return;
    g_probeCharacter = static_cast<int>(own.characterIndex);
    g_probeDataSource = own.gameData.patternMap;
    g_probeLevels = levels;
    g_probeCache.clear();
    if (!g_allowUnverifiedProbes) return;
    for (int action = 300; action < 600; ++action) {
        const auto kind = action < 500 ? whm::MoveKind::Normal : whm::MoveKind::Skill;
        whm::Move move;
        if (!whm::probe::directFrames(own, kind, action, -1, move)) continue;
        g_probeCache.push_back(std::move(move));
    }
    for (int id = 200; id < 220; ++id) {
        whm::Move move;
        if (!whm::probe::directFrames(own, whm::MoveKind::Spell, id + 400, id, move)) continue;
        g_probeCache.push_back(std::move(move));
    }
}

const whm::Move *findProbe(whm::MoveKind kind, int id) {
    for (const auto &move : g_probeCache)
        if (move.kind == kind && move.id == id) return &move;
    return nullptr;
}

std::mutex g_panelMutex;
PanelView g_panelView;
// The pinned v2::Player has incorrect late-layout card-counter offsets.
// The legacy manager exposes the verified original 1.10a layout.
static_assert(offsetof(SokuLib::CharacterManager, sacrificialDolls)==0x840);
static_assert(offsetof(SokuLib::CharacterManager, controlRod)==0x844);
std::vector<HudRow> composeHUD(const SokuLib::BattleManager *mgr) {
    const int side = ownSide();
    if (side == whm::kNoLocalPlayer) return {};
    const auto *own=viewPlayer(mgr,side);const auto *enemy=viewPlayer(mgr,1-side);
    PanelView view;view.character=static_cast<int>(own->characterIndex);
    view.attack=own->attackPower;view.defense=enemy->defensePower;
    view.opponentCharacter=static_cast<int>(enemy->characterIndex);
    const auto &ownCounters=side==0?mgr->leftCharacterManager:mgr->rightCharacterManager;
    const auto &enemyCounters=side==0?mgr->rightCharacterManager:mgr->leftCharacterManager;
    view.rods=static_cast<int>(ownCounters.controlRod);
    view.dolls=static_cast<int>(enemyCounters.sacrificialDolls); // Target's defense cards.
    view.skillMultiplier=own->specialDmgMultiplier;
    view.spellMultiplier=own->spellDmgMultiplier;
    view.weather=static_cast<int>(SokuLib::activeWeather);
    auto amount=[&](const StaticMove &m){
        double type=m.kind=="spell"?own->spellDmgMultiplier:m.kind=="skill"?own->specialDmgMultiplier:1.;
        double multiplier=view.attack*view.defense*type;
        if (!std::isfinite(multiplier)||multiplier<=0||multiplier>50)return 0;
        return static_cast<int>(m.damage*multiplier*whm::gutsFactor(enemy->HP));
    };
    for(const auto &m:g_staticMoves){
        if(m.character!=view.character||m.kind!="skill")continue; // A non-SC featured move must be a special skill.
        if(m.kind=="skill" && (m.card<100||m.card>=132||own->skilledSkillLevel[m.card-100]<0))continue;
        int damage=amount(m);
        if(damage>view.strongest.damage)view.strongest={view.character,m.card,damage,m.name+" · "+m.notation,true};
    }
    for(const auto &card:own->handInfo.hand){
        if(card.id<200||card.id>=220)continue;
        PanelCard shown{view.character,card.id,0,cardName(*own,card.id),true};
        for(const auto &m:g_staticMoves)if(m.character==view.character&&m.kind=="spell"&&m.card==card.id)shown.damage=std::max(shown.damage,amount(m));
        view.hand.push_back(shown);if(view.hand.size()==5)break;
    }
    const auto &best=g_record.best();
    if(best.attributed&&best.damage>0){
        int card=whm::cardForAction(view.character,best.actionId);
        view.record={view.character,card,best.damage,actionName(*own,best.actionId),false};
    }
    {std::lock_guard<std::mutex> guard(g_panelMutex);g_panelView=std::move(view);}
    return {{"什么最痛",-1}};
}

// The panel is owned by the game window, but never takes keyboard focus.
HWND g_panel = nullptr;
std::atomic<bool> g_panelWanted{false};
std::vector<HudRow> g_panelRows;
LRESULT CALLBACK panelProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (msg == WM_PAINT) {
        PanelView view;
        { std::lock_guard<std::mutex> guard(g_panelMutex); view=g_panelView; }
        paintStyledPanel(hwnd,view,dllFolder()); return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
void refreshPanel() {
    HWND game = SokuLib::window;
    if (!game || !IsWindow(game)) return;
    if (!g_panel) {
        WNDCLASSW cls{}; cls.lpfnWndProc = panelProc; cls.hInstance = g_module;
        cls.lpszClassName = L"SokuMaxDamagePanel";
        RegisterClassW(&cls);
        g_panel = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            cls.lpszClassName, L"SokuMaxDamage", WS_POPUP, 0, 0, 1, 1,
            game, nullptr, g_module, nullptr);
    }
    if (!g_panel) return;
    // Owned popups can remain above unrelated windows under Wine. Keep the
    // companion visible only while this game's process owns the foreground.
    if(!panelOwnsForeground(game)) {
        ShowWindow(g_panel,SW_HIDE);
        return;
    }
    const bool battleScene = SokuLib::sceneId == SokuLib::SCENE_BATTLE ||
        SokuLib::sceneId == SokuLib::SCENE_BATTLESV || SokuLib::sceneId == SokuLib::SCENE_BATTLECL;
    if (IsIconic(game) || !battleScene || !g_panelWanted.load()) { ShowWindow(g_panel, SW_HIDE); return; }
    RECT frame; GetWindowRect(game, &frame);
    RECT work; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    const int gameWidth=frame.right-frame.left, gameHeight=frame.bottom-frame.top;
    const int panelHeight=std::max(180,gameWidth*290/1000);
    if(gameHeight+panelHeight>work.bottom-work.top) {
        const double scale=double(work.bottom-work.top-16)/(gameHeight+panelHeight);
        int newWidth=static_cast<int>(gameWidth*scale),newHeight=static_cast<int>(gameHeight*scale);
        SetWindowPos(game,nullptr,std::max<LONG>(work.left,frame.left),work.top,newWidth,newHeight,
            SWP_NOACTIVATE|SWP_NOZORDER|SWP_ASYNCWINDOWPOS);
        return;
    }
    if(frame.bottom+panelHeight>work.bottom) {
        SetWindowPos(game,nullptr,frame.left,work.bottom-panelHeight-gameHeight,0,0,
            SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER|SWP_ASYNCWINDOWPOS);
        return;
    }
    size_t rowCount;
    { std::lock_guard<std::mutex> guard(g_panelMutex); rowCount = g_panelRows.size(); }
    SetWindowPos(g_panel, HWND_NOTOPMOST, frame.left, frame.bottom, frame.right-frame.left,
        std::max<LONG>(180, (frame.right-frame.left)*290/1000),
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_panel, nullptr, FALSE);
    // The game render thread does not pump a standard Win32 message loop.
    // Deliver WM_PAINT synchronously instead of leaving it queued forever.
    UpdateWindow(g_panel);
}

DWORD WINAPI panelThread(void *) {
    SetTimer(nullptr, 0, 100, nullptr);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_TIMER) refreshPanel();
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    return 0;
}

void loadFont() {
    if (g_fontReady) return;
    SokuLib::FontDescription desc{};
    std::strcpy(desc.faceName, "MS Gothic");
    desc.r1 = 246; desc.g1 = 245; desc.b1 = 238;
    desc.r2 = 246; desc.g2 = 245; desc.b2 = 238;
    desc.height = 13;
    desc.weight = FW_NORMAL;
    desc.shadow = 1;
    desc.bufferSize = 65536;
    g_font.create();
    g_font.setIndirect(desc);
    g_fontReady = true;
    g_background.setPosition({g_x, g_y});
    g_background.setSize({static_cast<unsigned>(g_w), static_cast<unsigned>(g_h)});
    g_background.setFillColor(SokuLib::DrawUtils::DxSokuColor(10, 10, 18, 190));
    g_background.setBorderColor(SokuLib::DrawUtils::DxSokuColor(170, 170, 185, 180));
}

void updateRows(std::vector<HudRow> rows) {
    std::string key;
    for (const auto &row : rows)
        key += row.text + "|" + std::to_string(row.iconCardId) + "\n";
    if (key == g_displayText) return;
    g_displayText = std::move(key);
    g_rows = std::move(rows);
    { std::lock_guard<std::mutex> guard(g_panelMutex); g_panelRows = g_rows; }
    if (!g_fontReady) return;
    const size_t count = std::min(g_rows.size(), kMaxHudRows);
    for (size_t i = 0; i < count; ++i) {
        if (g_y + 6 + static_cast<int>((i + 1) * kHudRowHeight) > g_y + g_h) break;
        auto &sprite = g_lineSprites[i];
        SokuLib::Vector2i actual{};
        const int iconWidth = g_rows[i].iconCardId >= 0 ? 29 : 0;
        const auto textAreaWidth = static_cast<unsigned>(std::max(20, g_w - 12 - iconWidth));
        if (!sprite.texture.createFromText(g_rows[i].text.c_str(), g_font,
                                           {textAreaWidth, 22u}, &actual) ||
            actual.x <= 0 || actual.y <= 0) continue;
        sprite.rect = {0, 0, actual.x, actual.y};
        sprite.setPosition({g_x + 6 + iconWidth,
                            g_y + 6 + static_cast<int>(i) * kHudRowHeight});
        sprite.setSize({static_cast<unsigned>(actual.x), static_cast<unsigned>(actual.y)});
    }
}

// Reuse the game's currently loaded card art: unlike external icons this
// always matches the player's character/card, and requires no asset copying.
// Fallback to CardInfo texture for installed skills / spent spells. The
// metadata texture region is still subject to in-game verification.
void drawCardIcon(const SokuLib::v2::Player &player, int cardId, int y) {
    if (cardId < 100 || cardId >= 220) return;
    const float left = static_cast<float>(g_x + 6);
    const float top = static_cast<float>(y);
    const float right = left + 23;
    const float bottom = top + 16;
    for (const auto &card : player.handInfo.hand) {
        if (card.id == cardId) {
            const_cast<SokuLib::Card &>(card).sprite.renderScreen(left, top, right, bottom);
            return;
        }
    }
    auto *info = const_cast<SokuLib::v2::Player &>(player).deckInfo.cardLookup(
        static_cast<unsigned short>(cardId));
    if (!info || info->texIdB <= 0) return;
    SokuLib::Sprite icon;
    icon.setTexture2(info->texIdB, 0, 0, 32, 32);
    icon.renderScreen(left, top, right, bottom);
}

int __fastcall onProcess(SokuLib::BattleManager *mgr) {
    // Take the victim's HP before and after the engine's own update. Never write into game state.
    const int side = ownSide();
    // An online side change may reuse the same BattleManager address.
    // Clear all cached cards, player stats and hit records on ANY local side change.
    if (g_lastManager != mgr || g_lastLocalSide != side) {
        g_lastManager = mgr;
        g_lastLocalSide = side;
        g_panelWanted.store(false);
        {
            std::lock_guard<std::mutex> lock(g_panelMutex);
            g_panelView = PanelView{};
        }
        g_record.reset();
        g_displayText.clear();
        g_rows.clear();
        g_lastRefresh = 0;
        g_probeCache.clear();
        g_probeCharacter = -1;
        g_probeDataSource = nullptr;
    }
    // Keep the best hit across round transitions; only re-create the HUD.
    if (mgr->frameCount < g_lastFrame) g_lastRefresh = 0;
    g_lastFrame = mgr->frameCount;
    // Spectator/replay and unsupported sessions MUST NOT read or show either
    // player as "self". Run the original engine update without any HUD work.
    if (side == whm::kNoLocalPlayer)
        return (mgr->*g_originalProcess)();
    const int before = side == 0 ? mgr->rightCharacterManager.objectBase.hp
                                 : mgr->leftCharacterManager.objectBase.hp;
    const int actionBefore = side == 0 ? mgr->leftCharacterManager.objectBase.action
                                       : mgr->rightCharacterManager.objectBase.action;
    const int result = (mgr->*g_originalProcess)();
    if (ownSide() != side) {
        g_panelWanted.store(false);
        return result;
    }
    const int after = side == 0 ? mgr->rightCharacterManager.objectBase.hp
                                : mgr->leftCharacterManager.objectBase.hp;
    const int actionAfter = side == 0 ? mgr->leftCharacterManager.objectBase.action
                                      : mgr->rightCharacterManager.objectBase.action;
    const int candidateAction = (actionBefore >= 300 && actionBefore < 690) ? actionBefore : actionAfter;
    // Do not attribute a projectile hit to a character currently just moving.
    // The direct-hitbox check deliberately trades coverage for fewer false records.
    const auto *attacker = viewPlayer(mgr, side);
    const int conservativeAction = attacker->boxData.hitBoxCount > 0 ? candidateAction : -1;
    g_record.observe(before, after, conservativeAction, mgr->frameCount);
    if (mgr->frameCount - g_lastRefresh >= 12 || g_lastRefresh == 0) {
        g_lastRefresh = mgr->frameCount;
        updateRows(composeHUD(mgr));
    }
    return result;
}

void __fastcall onExit(SokuLib::BattleManager *mgr) {
    g_panelWanted.store(false);
    g_record.reset();
    g_probeCache.clear();
    g_probeCharacter = -1;
    g_probeDataSource = nullptr;
    g_lastManager = nullptr;
    g_lastLocalSide = whm::kNoLocalPlayer;
    {
        std::lock_guard<std::mutex> lock(g_panelMutex);
        g_panelView = PanelView{};
    }
    g_lastFrame = 0;
    g_lastRefresh = 0;
    g_displayText.clear();
    g_rows.clear();
    (mgr->*g_originalExit)();
}

void __fastcall onRender(SokuLib::BattleManager *mgr) {
    (mgr->*g_originalRender)();
    const bool battleScene = SokuLib::sceneId == SokuLib::SCENE_BATTLE ||
        SokuLib::sceneId == SokuLib::SCENE_BATTLESV || SokuLib::sceneId == SokuLib::SCENE_BATTLECL;
    g_panelWanted.store(battleScene && isOwnBattleMode() && !g_displayText.empty());
}

void loadSettings() {
    const std::string ini = dllFolder() + "WhatHurtsMost.ini";
    g_allowUnverifiedProbes = GetPrivateProfileIntA("Damage", "EnableExperimentalFrameProbe", 1, ini.c_str()) != 0;
    g_x = GetPrivateProfileIntA("HUD", "X", g_x, ini.c_str());
    g_y = GetPrivateProfileIntA("HUD", "Y", g_y, ini.c_str());
    g_w = std::max(170, std::min(400, static_cast<int>(GetPrivateProfileIntA("HUD", "Width", g_w, ini.c_str()))));
    g_h = std::max(170, std::min(430, static_cast<int>(GetPrivateProfileIntA("HUD", "Height", g_h, ini.c_str()))));
    std::ifstream data(dllFolder()+"FrameData.txt");
    std::string line;
    while(std::getline(data,line)) {
        if(line.empty()||line[0]=='#')continue;
        std::vector<std::string> cells;std::istringstream stream(line);std::string cell;
        while(std::getline(stream,cell,'|'))cells.push_back(cell);
        if(cells.size()!=7)continue;
        try { g_staticMoves.push_back({std::stoi(cells[0]),std::stoi(cells[2]),std::stoi(cells[3]),cells[1],cells[4],cells[5]}); } catch(...) {}
    }
    std::ifstream in(dllFolder() + "MoveProfiles.txt");
    if (in) g_catalog.read(in);
    std::ifstream wiki(dllFolder() + "WikiMaxProfiles.txt");
    if (wiki) g_wikiMax.read(wiki);
}
} // anonymous namespace

extern "C" __declspec(dllexport) bool CheckVersion(const BYTE hash[16]) {
    return std::memcmp(hash, SokuLib::targetHash, sizeof(SokuLib::targetHash)) == 0;
}
extern "C" __declspec(dllexport) int getPriority() { return 0; }
extern "C" __declspec(dllexport) bool Initialize(HMODULE self, HMODULE) {
    if (g_hooksInstalled) return true;
    g_module = self;
    loadSettings();
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void *>(RDATA_SECTION_OFFSET), RDATA_SECTION_SIZE,
                        PAGE_EXECUTE_WRITECOPY, &old)) return false;
    g_originalExit = SokuLib::TamperDword(&SokuLib::VTable_BattleManager.onExit, onExit);
    g_originalRender = SokuLib::TamperDword(&SokuLib::VTable_BattleManager.onRender, onRender);
    g_originalProcess = SokuLib::TamperDword(&SokuLib::VTable_BattleManager.onProcess, onProcess);
    DWORD ignore = 0;
    VirtualProtect(reinterpret_cast<void *>(RDATA_SECTION_OFFSET), RDATA_SECTION_SIZE, old, &ignore);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    HANDLE thread = CreateThread(nullptr, 0, panelThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
    g_hooksInstalled = true;
    return true;
}
extern "C" int APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
