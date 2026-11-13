// WhatHurtsMost: local-only HUD prototype for Touhou Hisoutensoku 1.10a.
// No network packets, game damage, RNG or inputs are modified.
#include <SokuLib.hpp>
#include "DamageEngine.hpp"
#include "MoveCatalog.hpp"
#include "WikiMax.hpp"
#include "RecordTracker.hpp"
#include "FrameProbe.hpp"

#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
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

bool isOwnBattleMode() {
    switch (SokuLib::mainMode) {
        case SokuLib::BATTLE_MODE_PRACTICE:
        case SokuLib::BATTLE_MODE_VSCOM:
        case SokuLib::BATTLE_MODE_VSPLAYER:
        case SokuLib::BATTLE_MODE_VSCLIENT:
        case SokuLib::BATTLE_MODE_VSSERVER:
            return true;
        default: return false;
    }
}

int ownSide() {
    // In standard online 1v1 the host is left and the client is right.
    // Not sufficient for spectator / custom team mods (excluded above).
    return SokuLib::mainMode == SokuLib::BATTLE_MODE_VSCLIENT ? 1 : 0;
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

std::string cardName(const SokuLib::v2::Player &player, unsigned short cardId) {
    if (cardId < 100) return "CARD#" + std::to_string(cardId);
    auto *info = const_cast<SokuLib::v2::Player &>(player).deckInfo.cardLookup(cardId);
    if (!info) return "CARD#" + std::to_string(cardId);
    const auto raw = static_cast<const char *>(info->name);
    if (!raw || !raw[0]) return "CARD#" + std::to_string(cardId);
    return shortenCp932(raw);
}

std::string actionName(const SokuLib::v2::Player &player, int action) {
    static const char *const normals[] = {
        "5A", "f5A", "6A", "2A", "3A", "DA", "j5A", "j6A", "j2A", "j8A"
    };
    if (action >= 300 && action < 310) return normals[action - 300];
    if (action >= 500 && action < 600) {
        const int slot = (action - 500) / 20;
        const int variant = ((action - 500) % 20) / 5;
        if (slot < 4 && variant < 3) {
            const int card = 100 + slot * 4 + variant;
            return cardName(player, static_cast<unsigned short>(card));
        }
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

std::vector<HudRow> composeHUD(const SokuLib::BattleManager *mgr) {
    std::vector<HudRow> rows;
    if (!isOwnBattleMode()) return rows;
    const int side = ownSide();
    const auto *own = viewPlayer(mgr, side);
    const auto *enemy = viewPlayer(mgr, 1 - side);
    if (!own || !enemy) return rows;
    const auto ours = state(*own);
    const auto theirs = state(*enemy);
    buildProbeCache(*own);

    rows.push_back({"WHAT HURTS MOST? [TEST]", -1});
    // These are direct game-maintained effective multipliers. Card stacks are
    // intentionally NOT reapplied: control rods, dolls, and elixir can already
    // be folded into the values at runtime.
    char buffText[100];
    std::snprintf(buffText, sizeof(buffText), "ATK %.2f  OPP DEF %.2f", ours.attack, theirs.defense);
    rows.push_back({buffText, -1});

    const whm::WikiMax *wikiBest = nullptr;
    int wikiBestDamage = 0;
    for (auto kind : {whm::MoveKind::Normal, whm::MoveKind::Skill}) {
        for (const auto *entry : g_wikiMax.forCharacter(ours.character, kind)) {
            if (entry->level != -1) continue; // upgrade-specific mapping still needs verification
            if (kind == whm::MoveKind::Skill && !skillOwned(*own, entry->id)) continue;
            const int dmg = whm::rescaleWikiMax(*entry, ours, theirs);
            if (dmg > wikiBestDamage) { wikiBestDamage = dmg; wikiBest = entry; }
        }
    }
    const whm::Move *bestMove = nullptr;
    whm::Estimate bestEstimate;
    const whm::Move *roughBest = nullptr;
    whm::Estimate roughEstimate;
    for (auto kind : {whm::MoveKind::Normal, whm::MoveKind::Skill}) {
        for (const auto *move : g_catalog.forCharacter(ours.character, kind)) {
            if (move->level != -1) continue;
            if (kind == whm::MoveKind::Skill && !skillOwned(*own, move->id)) continue;
            const auto result = whm::estimate(*move, ours, theirs);
            if (result.valid && result.complete &&
                (!bestMove || result.damage > bestEstimate.damage)) {
                bestMove = move;
                bestEstimate = result;
            }
        }
    }
    for (const auto &probe : g_probeCache) {
        if (probe.kind == whm::MoveKind::Spell) continue;
        if (probe.kind == whm::MoveKind::Skill && !skillOwned(*own, probe.id)) continue;
        const auto r = whm::estimate(probe, ours, theirs);
        if (r.valid && (!roughBest || roughEstimate.damage < r.damage)) {
            roughBest = &probe;
            roughEstimate = r;
        }
    }
    const auto *shownNonSc = bestMove ? bestMove : roughBest;
    const auto shownNonScEstimate = bestMove ? bestEstimate : roughEstimate;
    if (wikiBest && (!shownNonSc || wikiBestDamage >= shownNonScEstimate.damage)) {
        const int skillId = wikiBest->kind == whm::MoveKind::Skill ?
            100 + ((wikiBest->id - 500) / 20) * 4 +
            ((wikiBest->id - 500) % 20) / 5 : -1;
        rows.push_back({"NON-SC (wiki): " + actionName(*own, wikiBest->id) +
            " ~" + std::to_string(wikiBestDamage), skillId});
    } else if (shownNonSc) {
        const int skillId = shownNonSc->kind == whm::MoveKind::Skill ?
            100 + ((shownNonSc->id - 500) / 20) * 4 +
            ((shownNonSc->id - 500) % 20) / 5 : -1;
        rows.push_back({"NON-SC (known): " + actionName(*own, shownNonSc->id) + " " +
            (shownNonScEstimate.complete ? "" : "~") +
            std::to_string(shownNonScEstimate.damage), skillId});
    } else rows.push_back({"NON-SC: --", -1});

    rows.push_back({"HAND SPELLS (wiki max target):", -1});
    int shown = 0;
    for (const auto &card : own->handInfo.hand) {
        if (card.id < 200 || card.id >= 220) continue;
        if (++shown > 5) break;
        const auto *published = g_wikiMax.find(ours.character, whm::MoveKind::Spell, card.id, -1);
        if (published) {
            const int number = whm::rescaleWikiMax(*published, ours, theirs);
            rows.push_back({shortenCp932(cardName(*own, card.id), 18) +
                " ~" + std::to_string(number) + " [wiki]", card.id});
            continue;
        }
        const auto *move = g_catalog.find(ours.character, whm::MoveKind::Spell, card.id, -1);
        if (!move) move = findProbe(whm::MoveKind::Spell, card.id);
        const auto r = move ? whm::estimate(*move, ours, theirs) : whm::Estimate{};
        const std::string suffix = r.valid ?
            (r.complete ? std::to_string(r.damage) : "~" + std::to_string(r.damage)) : "--";
        rows.push_back({shortenCp932(cardName(*own, card.id), 18) + " " + suffix, card.id});
    }
    if (!shown) rows.push_back({"(no spell in hand)", -1});

    const auto &record = g_record.best();
    if (record.damage > 0 && record.attributed) {
        const int iconId = record.actionId >= 600 && record.actionId < 620 ?
            record.actionId - 400 : -1;
        rows.push_back({"MATCH BEST: " + actionName(*own, record.actionId) +
            " " + std::to_string(record.damage), iconId});
    } else rows.push_back({"MATCH BEST: --", -1});
    if (g_record.unattributedDamage() > 0)
        rows.push_back({"Projectile hits: unassigned", -1});
    rows.push_back({"~ incomplete; no gameplay changes", -1});
    return rows;
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
    if (g_lastManager != mgr) {
        g_lastManager = mgr;
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
    const int side = ownSide();
    const int before = side == 0 ? mgr->rightCharacterManager.objectBase.hp
                                 : mgr->leftCharacterManager.objectBase.hp;
    const int actionBefore = side == 0 ? mgr->leftCharacterManager.objectBase.action
                                       : mgr->rightCharacterManager.objectBase.action;
    const int result = (mgr->*g_originalProcess)();
    if (!isOwnBattleMode()) return result;
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
        loadFont();
        updateRows(composeHUD(mgr));
    }
    return result;
}

void __fastcall onExit(SokuLib::BattleManager *mgr) {
    g_record.reset();
    g_probeCache.clear();
    g_probeCharacter = -1;
    g_probeDataSource = nullptr;
    g_lastManager = nullptr;
    g_lastFrame = 0;
    g_lastRefresh = 0;
    g_displayText.clear();
    g_rows.clear();
    (mgr->*g_originalExit)();
}

void __fastcall onRender(SokuLib::BattleManager *mgr) {
    (mgr->*g_originalRender)();
    if (!g_fontReady || !isOwnBattleMode() || g_displayText.empty()) return;
    g_background.draw();
    const size_t count = std::min(g_rows.size(), kMaxHudRows);
    const auto *player = viewPlayer(mgr, ownSide());
    for (size_t i = 0; i < count; ++i) {
        const int y = g_y + 6 + static_cast<int>(i) * kHudRowHeight;
        if (y + kHudRowHeight >= g_y + g_h) break;
        g_lineSprites[i].draw();
        if (player && g_rows[i].iconCardId >= 0)
            drawCardIcon(*player, g_rows[i].iconCardId, y);
    }
}

void loadSettings() {
    const std::string ini = dllFolder() + "WhatHurtsMost.ini";
    g_allowUnverifiedProbes = GetPrivateProfileIntA("Damage", "EnableExperimentalFrameProbe", 1, ini.c_str()) != 0;
    g_x = GetPrivateProfileIntA("HUD", "X", g_x, ini.c_str());
    g_y = GetPrivateProfileIntA("HUD", "Y", g_y, ini.c_str());
    g_w = std::max(170, std::min(400, static_cast<int>(GetPrivateProfileIntA("HUD", "Width", g_w, ini.c_str()))));
    g_h = std::max(170, std::min(430, static_cast<int>(GetPrivateProfileIntA("HUD", "Height", g_h, ini.c_str()))));
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
    g_hooksInstalled = true;
    return true;
}
extern "C" int APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
