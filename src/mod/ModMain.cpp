// WhatHurtsMost: local-only HUD prototype for Touhou Hisoutensoku 1.10a.
// No network packets, game damage, RNG or inputs are modified.
#include <SokuLib.hpp>
#include "DamageEngine.hpp"
#include "MoveCatalog.hpp"
#include "WikiMax.hpp"
#include "RecordTracker.hpp"
#include "FrameProbe.hpp"
#include "StyledPanel.hpp"
#include "DamageEffects.hpp"
#include "PanelVisibility.hpp"
#include "LocalPlayerSide.hpp"
#include "ActionIdentity.hpp"
#include "HudText.hpp"
#include "HudPalette.hpp"
#include "FirstHit.hpp"

#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
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

// UI-owned preferences. They are user state, not battle state: a new match
// must not silently unfold the panel or reset the chosen language.
std::atomic<int> g_language{0};            // index of whm::HudLanguage
std::atomic<bool> g_collapsed{false};
std::atomic<bool> g_firstHitConsumed{false};
unsigned g_firstHitIdleFrames = 0;         // game thread only, debounce
struct MoveNameEntry { std::array<std::string, 3> names; };
std::map<std::string, MoveNameEntry> g_moveNames;

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

void saveHudPreferences() {
    const std::string ini = dllFolder() + "WhatHurtsMost.ini";
    static const char *const codes[] = {"CN", "JP", "EN"};
    WritePrivateProfileStringA("HUD", "Language",
        codes[std::clamp(g_language.load(), 0, 2)], ini.c_str());
    WritePrivateProfileStringA("HUD", "Collapsed",
        g_collapsed.load() ? "1" : "0", ini.c_str());
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

// Localized move names are keyed by character/card/notation, never by a
// translated display string. Missing rows fall back to the official name or
// the notation; internal ids such as CARD#101 must never reach the panel.
std::string moveNameKey(int character, int card, const std::string &notation) {
    return std::to_string(character) + "|" + std::to_string(card) + "|" +
           whm::normalizeNotation(notation);
}
std::array<std::string, 3> lookupNames(int character, int card,
                                       const std::string &lookup,
                                       const std::string &fallback) {
    auto exact = g_moveNames.find(moveNameKey(character, card, lookup));
    if (exact != g_moveNames.end()) return exact->second.names;
    auto byCard = g_moveNames.find(moveNameKey(character, card, ""));
    if (byCard != g_moveNames.end()) return byCard->second.names;
    std::string safe = fallback;
    if (safe.empty() || safe.rfind("CARD#", 0) == 0 ||
        safe.rfind("ACTION#", 0) == 0 || safe.rfind("卡牌 ", 0) == 0)
        safe = whm::normalizeNotation(lookup);
    return {safe, safe, safe};
}
PanelCard makeCard(int character, int card, int damage,
                   const std::string &lookup, const std::string &display,
                   const std::string &fallback, bool approximate) {
    PanelCard c;
    c.character = character;
    c.card = card;
    c.damage = damage;
    c.approximate = approximate;
    c.notation = whm::normalizeNotation(display);
    c.names = lookupNames(character, card, lookup, fallback);
    c.name = fallback;
    return c;
}
struct MoveLabel {
    std::array<std::string, 3> names{};
    std::string notation, fallback;
};
MoveLabel labelForAction(const SokuLib::v2::Player &player, int action) {
    const int character = static_cast<int>(player.characterIndex);
    const int card = action >= 500 && action < 600
        ? whm::skillCardForAction(character, action)
        : action >= 600 && action < 620 ? action - 400 : -1;
    const std::string raw = actionName(player, action);
    const std::string separator = " · "; // "·" is two UTF-8 bytes, not one
    const std::size_t cut = raw.rfind(separator);
    std::string fallback = cut == std::string::npos ? raw : raw.substr(0, cut);
    std::string notation = cut == std::string::npos ? "" :
        whm::normalizeNotation(raw.substr(cut + separator.size()));
    std::string lookup = notation;
    if (action >= 600 && action < 620) {
        lookup = "sc" + std::to_string(card); // spell row in MoveNames.tsv
        notation.clear();                     // a card name is not a notation
    } else if (cut == std::string::npos) {
        const unsigned char first = static_cast<unsigned char>(raw.empty() ? 0 : raw[0]);
        const bool looksLikeNotation =
            (first >= '0' && first <= '9') || first == 'j' || first == 'J' ||
            first == 'f' || first == 'F' || first == 'D';
        if (looksLikeNotation) {
            notation = whm::normalizeNotation(raw);
            lookup = notation;
            fallback = notation;
        } else {
            lookup.clear(); // an official card name, not a command
        }
    }
    if (fallback.rfind("ACTION#", 0) == 0 || fallback.rfind("CARD#", 0) == 0)
        fallback.clear();
    MoveLabel out;
    out.names = lookupNames(character, card, lookup, fallback);
    out.notation = notation;
    out.fallback = fallback;
    if (out.names[0].empty()) {
        const std::string safe = !notation.empty() ? notation : "技";
        out.names = {safe, safe, safe};
    }
    return out;
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
    if(!own||!enemy)return {};
    const int ownCharacter=static_cast<int>(own->characterIndex),enemyCharacter=static_cast<int>(enemy->characterIndex);
    if(ownCharacter<0||ownCharacter>19||enemyCharacter<0||enemyCharacter>19)return {};
    const auto &ownCounters=side==0?mgr->leftCharacterManager:mgr->rightCharacterManager;
    const auto &enemyCounters=side==0?mgr->rightCharacterManager:mgr->leftCharacterManager;
    PanelView view;view.character=ownCharacter;
    view.attack=own->attackPower;view.defense=enemy->defensePower;
    view.opponentCharacter=enemyCharacter;
    view.rods=static_cast<int>(ownCounters.controlRod);
    view.dolls=static_cast<int>(enemyCounters.sacrificialDolls); // Target's defense cards.
    view.skillMultiplier=own->specialDmgMultiplier;
    view.spellMultiplier=own->spellDmgMultiplier;
    view.weather=static_cast<int>(ownCounters.effectiveWeather);
    view.hpFactor=whm::gutsFactor(enemy->HP);
    // Format ONCE, before the effect rows are built, so the grey factor and
    // the "根性补正" row are byte-identical strings in the same snapshot.
    view.gutsText=whm::gutsLabel(view.hpFactor);
    collectDamageEffects(view,&ownCounters,&enemyCounters);
    // Weather text follows effective weather / seal, not the global name.
    view.weatherVisible=whm::weatherLabelVisible(view.weather,view.weatherSealed,
                                                 view.skillMultiplier,view.spellMultiplier);

    // First-hit-only bonus. SokuLib documents comboCount at +0x4B4 and untech
    // at +0x4BA but not whose counter owns a combo, so BOTH players must be
    // idle. Meiling's active forced-first-hit buff is visible even while idle;
    // other conditional CH sources still require a live own attack box.
    // Frame counting is mere debounce, never the qualification.
    const bool comboIdle=effectField<short>(&ownCounters,0x4b4)==0&&
                         effectField<short>(&enemyCounters,0x4b4)==0;
    const bool outOfHitstun=effectField<unsigned short>(&enemyCounters,0x4ba)==0;
    // An ongoing combo (victim hitstun or either combo counter) keeps the
    // bonus consumed, so later hits of a multi-hit move cannot re-enable it.
    // It returns only after the combo has been over for several snapshots.
    if(!comboIdle||!outOfHitstun) {
        g_firstHitConsumed.store(true);
        g_firstHitIdleFrames=0;
    } else if(++g_firstHitIdleFrames>=3) g_firstHitConsumed.store(false);
    whm::FirstHitSignals signals;
    signals.localMultiplier=view.firstHitMultiplier;
    signals.sourceConfirmed=(view.forcedFirstHitArmed&&view.firstHitMultiplier>1.)||
                            (view.weather==16&&view.firstHitMultiplier>1.)||
                            effectField<short>(&ownCounters,0x1a2)>0;
    signals.comboIdle=comboIdle;
    signals.targetOutOfHitstun=outOfHitstun;
    signals.attackConfirmed=own->boxData.hitBoxCount>0;
    signals.forcedFirstHitArmed=view.forcedFirstHitArmed;
    signals.firstHitConsumed=g_firstHitConsumed.load();
    signals.attackPowerAlreadyIncludes=false; // still needs 1.10a verification
    const whm::FirstHitDecision hit=whm::evaluateFirstHit(signals);
    view.basePercent=whm::commonPercent(view.attack,view.defense,{});
    // The big percentage may describe the NEXT first hit; whole-move damage
    // estimates below deliberately do not use this first-hit-only factor.
    view.commonPercent=whm::commonPercent(view.attack,view.defense,hit);
    view.firstHitActive=hit.applies;
    view.firstHitMultiplier=hit.applies?hit.multiplier:1.;

    auto amount=[&](const StaticMove &m){
        double type=m.kind=="spell"?own->spellDmgMultiplier:m.kind=="skill"?own->specialDmgMultiplier:1.;
        double multiplier=view.attack*view.defense*type;
        if (!std::isfinite(multiplier)||multiplier<=0||multiplier>50)return 0;
        return static_cast<int>(m.damage*multiplier*whm::gutsFactor(enemy->HP));
    };
    std::string bestNotation,bestName;
    for(const auto &m:g_staticMoves){
        if(m.character!=view.character||m.kind!="skill")continue; // A non-SC featured move must be a special skill.
        if(m.card<100||m.card>=132||own->skilledSkillLevel[m.card-100]<0)continue;
        int damage=amount(m);
        if(damage>view.strongest.damage){
            bestNotation=whm::normalizeNotation(m.notation);bestName=m.name;
            view.strongest=makeCard(view.character,m.card,damage,m.notation,
                                    bestNotation,m.name,true);
        } else if(damage==view.strongest.damage&&m.card==view.strongest.card&&m.name==bestName){
            bestNotation=whm::mergeSkillButtons(bestNotation,
                                                whm::normalizeNotation(m.notation));
            view.strongest.notation=bestNotation;
        }
    }
    for(const auto &card:own->handInfo.hand){
        if(card.id<200||card.id>=220)continue;
        const int cardId=static_cast<int>(card.id);
        int damage=0;
        for(const auto &m:g_staticMoves)
            if(m.character==view.character&&m.kind=="spell"&&m.card==cardId)
                damage=std::max(damage,amount(m));
        view.hand.push_back(makeCard(view.character,cardId,damage,
                                     "sc"+std::to_string(cardId),"",
                                     cardName(*own,card.id),true));
        if(view.hand.size()==5)break;
    }
    const auto &best=g_record.best();
    if(best.attributed&&best.damage>0){
        const int action=best.actionId;
        const int card=whm::cardForAction(view.character,action);
        const MoveLabel label=labelForAction(*own,action);
        PanelCard rec=makeCard(view.character,card,best.damage,
                               label.notation,label.notation,label.fallback,false);
        rec.names=label.names;
        view.record=rec;
    }
    {std::lock_guard<std::mutex> guard(g_panelMutex);g_panelView=std::move(view);}
    return {{"什么最痛",-1}};
}

// The panel is owned by the game window, but never takes keyboard focus.
HWND g_panel = nullptr;
std::atomic<bool> g_panelWanted{false};
std::vector<HudRow> g_panelRows;
std::atomic<int> g_cursorLogicalX{-1}, g_cursorLogicalY{-1};

bool systemCursorVisible() {
    CURSORINFO info{}; info.cbSize = sizeof(info);
    return GetCursorInfo(&info) && (info.flags & CURSOR_SHOWING) != 0;
}
// IDC_ARROW expands to MAKEINTRESOURCEA/MAKEINTRESOURCEW depending on the
// UNICODE macro, while this module always calls the *W window APIs. Use the
// fixed system resource id (32512 = IDC_ARROW) so either flavour type-checks.
inline HCURSOR arrowCursor() {
    return LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
}
// The game (or Proton) may hide the system cursor and clip it to the game
// rectangle. A companion window BELOW the game then gets no mouse messages
// at all. Restore both, throttled to once per second and only when needed.
// This touches no game memory, packets, RNG or damage state.
void releaseCursorRestrictions() {
    // GetTickCount (not GetTickCount64): this module targets WINVER 0x0501.
    static DWORD last = 0;
    static int boosts = 0;
    const DWORD now = GetTickCount();
    if (last && now - last < 250) return;
    last = now;
    CURSORINFO info{}; info.cbSize = sizeof(info);
    // Bound the number of ShowCursor(TRUE) calls so the per-thread display
    // counter cannot grow forever; the drawn arrow is the fallback.
    if (boosts < 2 && GetCursorInfo(&info) && !(info.flags & CURSOR_SHOWING)) {
        ++boosts;
        int count = ShowCursor(TRUE);
        for (int i = 0; i < 4 && count < 0; ++i) count = ShowCursor(TRUE);
    }
    RECT clip{};
    if (GetClipCursor(&clip)) {
        RECT screen{};
        if (GetWindowRect(GetDesktopWindow(), &screen) &&
            (clip.left > screen.left || clip.top > screen.top ||
             clip.right < screen.right || clip.bottom < screen.bottom))
            ClipCursor(nullptr);
    }
}
LRESULT CALLBACK panelProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (msg == WM_SETCURSOR) {
        // The old window class had NO cursor; when the game hides the system
        // cursor, nothing was drawn over the panel. Always show an arrow.
        SetCursor(arrowCursor());
        return TRUE;
    }
    if (msg == WM_PAINT) {
        PanelView view;
        { std::lock_guard<std::mutex> guard(g_panelMutex); view=g_panelView; }
        // UI-owned state is applied at paint time so a button press is visible
        // immediately, without the game thread touching any live object.
        view.language=static_cast<whm::HudLanguage>(std::clamp(g_language.load(),0,2));
        view.collapsed=g_collapsed.load();
        view.cursorX=g_cursorLogicalX.load();
        view.cursorY=g_cursorLogicalY.load();
        view.systemCursorVisible=systemCursorVisible();
        paintStyledPanel(hwnd,view,dllFolder()); return 0;
    }
    if (msg == WM_MOUSEMOVE) {
        RECT client{}; GetClientRect(hwnd,&client);
        const int cw=client.right-client.left, chh=client.bottom-client.top;
        if(cw>0&&chh>0) {
            const int px=static_cast<short>(LOWORD(l));
            const int py=static_cast<short>(HIWORD(l));
            g_cursorLogicalX.store(px*kPanelVirtualWidth/cw);
            g_cursorLogicalY.store(py*kPanelVirtualHeight/chh);
        }
        TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};
        TrackMouseEvent(&track);
        InvalidateRect(hwnd,nullptr,FALSE);
        return 0;
    }
    if (msg == WM_MOUSELEAVE) {
        g_cursorLogicalX.store(-1); g_cursorLogicalY.store(-1);
        InvalidateRect(hwnd,nullptr,FALSE);
        return 0;
    }
    if (msg == WM_LBUTTONUP) {
        // Actions run on DOWN; nothing is left to do on UP. Not forwarding it
        // to the owner window also avoids an accidental game activation.
        return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        RECT client{}; GetClientRect(hwnd,&client);
        const int cw=client.right-client.left, chh=client.bottom-client.top;
        if(cw<=0||chh<=0)return 0;
        // Act on press: Proton/DirectInput sessions can swallow the matching
        // WM_LBUTTONUP, so waiting for the release made the pad feel dead.
        const int px=static_cast<short>(LOWORD(l));
        const int py=static_cast<short>(HIWORD(l));
        const int vx=px*kPanelVirtualWidth/cw;
        const int vy=py*kPanelVirtualHeight/chh;
        auto inside=[](int x,int y,const HudButton &b){
            return x>=b.x&&x<b.x+b.w&&y>=b.y&&y<b.y+b.h;
        };
        bool changed=false;
        if(g_collapsed.load()) {
            g_collapsed.store(false); // the whole folded window is the "+"
            changed=true;
        } else {
            for(int i=0;i<3;++i)
                if(inside(vx,vy,kHudButtons[i])) {
                    if(g_language.load()!=i)g_language.store(i);
                    changed=true; break;
                }
            if(!changed&&inside(vx,vy,kHudButtons[3])) {
                g_collapsed.store(true); changed=true;
            }
        }
        if(changed) {
            saveHudPreferences();
            InvalidateRect(hwnd,nullptr,FALSE);
            UpdateWindow(hwnd);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
void refreshPanel() {
    HWND game = SokuLib::window;
    if (!game || !IsWindow(game)) return;
    if (!g_panel) {
        WNDCLASSW cls{}; cls.lpfnWndProc = panelProc; cls.hInstance = g_module;
        cls.hCursor = arrowCursor();
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
    // Only while the companion is actually shown: restore a hidden/clipped
    // cursor so its controls can be seen and clicked.
    releaseCursorRestrictions();
    RECT frame; GetWindowRect(game, &frame);
    RECT work; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    const int gameWidth=frame.right-frame.left, gameHeight=frame.bottom-frame.top;
    const int panelHeight=std::max(180,gameWidth*kPanelVirtualHeight/kPanelVirtualWidth);
    const bool folded=g_collapsed.load();
    // A folded panel reserves NO full-height space and never triggers the
    // game-window resizing loop. Preference is separate from scene hiding.
    if(!folded) {
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
    }
    size_t rowCount;
    { std::lock_guard<std::mutex> guard(g_panelMutex); rowCount = g_panelRows.size(); }
    if(folded) {
        // "small" is a legacy Windows macro (small -> char); never use it as
        // an identifier here. Keep the "+" inside the work area.
        const int plusSize=std::max(24,gameWidth*kPlusLogicalSize/kPanelVirtualWidth);
        const int plusY=std::min(frame.bottom+4,work.bottom-plusSize-2);
        SetWindowPos(g_panel, HWND_NOTOPMOST,
            frame.right-plusSize-6, plusY, plusSize, plusSize,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        SetWindowPos(g_panel, HWND_NOTOPMOST, frame.left, frame.bottom, gameWidth,
            std::max<LONG>(180, gameWidth*kPanelVirtualHeight/kPanelVirtualWidth),
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
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
        // Battle state is reset, but language/fold preferences are user state.
        g_firstHitConsumed.store(false);
        g_firstHitIdleFrames = 0;
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
    const bool inBattle=SokuLib::sceneId==SokuLib::SCENE_BATTLE || SokuLib::sceneId==SokuLib::SCENE_BATTLESV || SokuLib::sceneId==SokuLib::SCENE_BATTLECL;
    if(!inBattle || &SokuLib::getBattleMgr()!=mgr){g_panelWanted.store(false);return (mgr->*g_originalProcess)();}
    const int before = side == 0 ? mgr->rightCharacterManager.objectBase.hp
                                 : mgr->leftCharacterManager.objectBase.hp;
    const int actionBefore = side == 0 ? mgr->leftCharacterManager.objectBase.action
                                       : mgr->rightCharacterManager.objectBase.action;
    const int result = (mgr->*g_originalProcess)();
    if (ownSide() != side || &SokuLib::getBattleMgr()!=mgr ||
        (SokuLib::sceneId!=SokuLib::SCENE_BATTLE && SokuLib::sceneId!=SokuLib::SCENE_BATTLESV && SokuLib::sceneId!=SokuLib::SCENE_BATTLECL)) {
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
    // Only a confirmed direct hit consumes this combo's first-hit qualifier.
    // Unattributed projectile damage or blocked pokes do not count here.
    if(after<before&&conservativeAction>=0)g_firstHitConsumed.store(true);
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
    g_firstHitConsumed.store(false);
    g_firstHitIdleFrames = 0;
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
    char language[8] = {};
    GetPrivateProfileStringA("HUD", "Language", "CN", language, sizeof(language), ini.c_str());
    const std::string chosen(language);
    g_language.store(chosen == "JP" ? 1 : chosen == "EN" ? 2 : 0);
    g_collapsed.store(GetPrivateProfileIntA("HUD", "Collapsed", 0, ini.c_str()) != 0);
    // 1602 sourced rows: character, card, notation, zh-CN, en, ja, source.
    // Index 0/1/2 of names follows HudLanguage (Chinese/Japanese/English).
    std::ifstream names(dllFolder()+"MoveNames.tsv");
    std::string nameLine;
    while(std::getline(names,nameLine)) {
        if(nameLine.empty()||nameLine[0]=='#')continue;
        std::vector<std::string> cells;std::istringstream ns(nameLine);std::string cell;
        while(std::getline(ns,cell,'\t'))cells.push_back(cell);
        if(cells.size()<6)continue;
        try {
            g_moveNames[moveNameKey(std::stoi(cells[0]),std::stoi(cells[1]),cells[2])] =
                MoveNameEntry{{cells[3],cells[5],cells[4]}};
        } catch(...) {}
    }
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
