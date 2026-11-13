#pragma once
// All fixed companion-window strings live here as one keyed dictionary.
// Move/card names are NOT keyed by their localized text: they are matched by
// character, card id and notation through config/MoveNames.tsv.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

namespace whm {

enum class HudLanguage { Chinese = 0, Japanese = 1, English = 2 };

enum class HudTextKey {
    PanelTitle, Strongest, Record, Hand, SpellMax,
    NoEffects, NoHand, NoSkill, NoRecord,
    Theoretical, MatchBest, Weather, Unknown,
    SpecialType, SpellType, Guts, FirstHit,
    KeyCount
};

struct HudTextEntry {
    const char *zh, *ja, *en;
};

inline constexpr HudTextEntry kHudTexts[] = {
    /* PanelTitle  */ {"伤害影响 · 总修正", "ダメージ補正", "DAMAGE · TOTAL MODS"},
    /* Strongest   */ {"最痛必杀技", "最大ダメージ必殺技", "STRONGEST SPECIAL"},
    /* Record      */ {"最痛记录", "最大ダメージ記録", "BEST HIT"},
    /* Hand        */ {"当前手牌", "手札", "CURRENT HAND"},
    /* SpellMax    */ {"SC 最大伤害", "スペカ最大値", "SC MAX DAMAGE"},
    /* NoEffects   */ {"当前无额外效果", "追加効果なし", "NO EXTRA EFFECTS"},
    /* NoHand      */ {"目前手牌中没有 SC", "手札にスペルカードなし", "NO SC IN HAND"},
    /* NoSkill     */ {"暂无可靠必杀技资料", "必殺技データなし", "NO VERIFIED SPECIAL DATA"},
    /* NoRecord    */ {"尚未记录命中", "ヒット記録なし", "NO HIT RECORDED"},
    /* Theoretical */ {"理论最大值 · 估算", "理論最大値・推定", "THEORETICAL MAX · ESTIMATE"},
    /* MatchBest   */ {"本局最高 · 命中", "この対戦の最大ダメージ", "MATCH BEST · HIT"},
    /* Weather     */ {"天气", "天気", "WEATHER"},
    /* Unknown     */ {"未知", "不明", "UNKNOWN"},
    /* SpecialType */ {"必杀技", "必殺技", "SPECIAL"},
    /* SpellType   */ {"SC", "スペカ", "SC"},
    /* Guts        */ {"根性修正", "根性補正", "GUTS"},
    /* FirstHit    */ {"首击", "初段", "1ST HIT"},
};
static_assert(sizeof(kHudTexts) / sizeof(kHudTexts[0]) ==
              static_cast<std::size_t>(HudTextKey::KeyCount),
              "Every HUD text key needs Chinese, Japanese and English entries.");

inline const char *hudText(HudTextKey key, HudLanguage lang) {
    const auto &e = kHudTexts[static_cast<std::size_t>(key)];
    switch (lang) {
        case HudLanguage::Japanese: return e.ja;
        case HudLanguage::English: return e.en;
        default: return e.zh;
    }
}

// ONE formatter for every visible factor. The big percentage, the grey guts
// number and the explanatory effect rows must never disagree by a rounding
// step: iostream and snprintf can round a value such as 0.705 differently.
inline std::string hudFactor(double value, int decimals) {
    if (!(value == value) || value < -1e9 || value > 1e9) value = 0.;
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return std::string(buffer);
}

// One shared, already-formatted guts label. The grey factor next to the big
// percentage and the "根性补正" effect row MUST use this exact same string:
// formatting them independently previously produced 0.70 vs 0.71 at a
// rounding boundary, and two snapshots could drift apart for other reasons.
inline std::string gutsLabel(double hpFactor) {
    return "×" + hudFactor(hpFactor, 2);
}

// j/f prefixes become upper case and the separator dot is removed:
// j.2A -> J2A, f.5A -> F5A, j.623B/C -> J623B/C. Full English skill names
// are deliberately left untouched, and ground/air variants stay distinct.
inline std::string normalizeNotation(const std::string &s) {
    if (s.empty()) return s;
    const char first = s[0];
    if (first != 'j' && first != 'J' && first != 'f' && first != 'F') return s;
    std::size_t i = 1;
    if (i < s.size() && s[i] == '.') ++i;
    // A prefix must lead into a command (digit), otherwise this is a word
    // such as "jab" or a localized skill name.
    if (i >= s.size() || s[i] < '0' || s[i] > '9') return s;
    std::string out;
    out += (first == 'j' || first == 'J') ? 'J' : 'F';
    out += s.substr(i);
    return out;
}

inline char asciiUpper(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}
// A normal's "name" is often its own command still in the source spelling
// (FrameData.txt / MoveNames.tsv use j.2A), while PanelCard::notation is
// already normalized (J2A). Only UTF-8 high bytes outside a-z are left alone.
inline bool sameNotation(const std::string &a, const std::string &b) {
    const std::string na = normalizeNotation(a), nb = normalizeNotation(b);
    if (na.size() != nb.size()) return false;
    for (std::size_t i = 0; i < na.size(); ++i)
        if (asciiUpper(na[i]) != asciiUpper(nb[i])) return false;
    return true;
}
// One card caption: when the localized name IS the command, print the
// ALL-CAPS normalized command exactly once; otherwise name + command.
inline std::string cardDisplayName(const std::string &name,
                                   const std::string &notation) {
    const std::string command = normalizeNotation(notation);
    const std::string caption = normalizeNotation(name);
    if (command.empty()) return caption;
    if (sameNotation(name, command)) return command;
    return caption + " · " + command;
}

// Weather text is shown only when it can actually affect damage, and never
// when the effective weather is sealed. An unexplained effective multiplier
// still counts, so independent buffs are not silently dropped.
inline bool weatherLabelVisible(int effectiveWeather, bool weatherSealed,
                                double skillMultiplier, double spellMultiplier) {
    if (weatherSealed || effectiveWeather < 0 || effectiveWeather == 21) return false;
    switch (effectiveWeather) {
        case 1:  // 霧雨 / spell cards
        case 4:  // 雹 / specials
        case 9:  // 疎雨 / skill levels
        case 16: // 黄砂 / counter hits
            return true;
        default:
            break;
    }
    return std::abs(skillMultiplier - 1.) > .008 ||
           std::abs(spellMultiplier - 1.) > .008;
}

inline const char *weatherName(int weather, HudLanguage lang) {
    static const char *const zh[] = {
        "快晴", "雾雨", "阴天", "苍天", "冰雹", "春霭", "浓雾", "雪",
        "太阳雨", "疏雨", "风雨", "晴岚", "川雾", "台风", "凪", "钻石尘",
        "黄沙", "烈日", "梅雨", "极光", "黄昏", "无天气"};
    static const char *const ja[] = {
        "快晴", "霧雨", "曇天", "蒼天", "雹", "春靄", "濃霧", "雪",
        "太陽雨", "疎雨", "風雨", "晴嵐", "川霧", "台風", "凪",
        "ダイヤモンドダスト", "黄砂", "烈日", "梅雨", "オーロラ", "黄昏", "無天気"};
    static const char *const en[] = {
        "Clear", "Drizzle", "Cloudy", "Blue Sky", "Hail", "Spring Haze",
        "Dense Fog", "Snow", "Sunshower", "Sprinkle", "Wind & Rain",
        "Mountain Wind", "River Mist", "Typhoon", "Calm", "Diamond Dust",
        "Sandstorm", "Scorching Sun", "Plum Rain", "Aurora", "Twilight",
        "No Weather"};
    if (weather < 0 || weather >= 22) return hudText(HudTextKey::Unknown, lang);
    switch (lang) {
        case HudLanguage::Japanese: return ja[weather];
        case HudLanguage::English: return en[weather];
        default: return zh[weather];
    }
}

// Effect rows are collected with stable Chinese labels in DamageEffects.hpp.
// This table translates those fixed labels without changing the snapshot.
struct EffectLabelEntry {
    const char *zh, *ja, *en;
};
inline constexpr EffectLabelEntry kEffectLabels[] = {
    {"自身制御棒", "自分の制御棒", "OWN CONTROL RODS"},
    {"对手制御棒", "相手の制御棒", "RIVAL CONTROL RODS"},
    {"对手替身人偶", "相手の身代わり人形", "RIVAL DOLLS"},
    {"自身国士无双", "自分の国士無双の薬", "OWN ELIXIR"},
    {"对手国士无双", "相手の国士無双の薬", "RIVAL ELIXIR"},
    {"千年吸血鬼", "ミレニアムの吸血鬼", "MILLENNIUM VAMPIRE"},
    {"无念无想减伤", "無念無想の境地", "TENSHI DAMAGE CUT"},
    {"红色诅咒", "赤い祟り", "RED CURSE"},
    {"绿色诅咒", "緑の祟り", "GREEN CURSE"},
    {"御射军神诅咒", "ミシャグジさまの祟り", "MISHAGUJI CURSE"},
    {"自身反伤", "自分のダメージ反射", "OWN REFLECTION"},
    {"对手反伤", "相手のダメージ反射", "RIVAL REFLECTION"},
    {"对手错防状态", "相手の誤ガード状態", "RIVAL GUARD CRUSH"},
    {"其他攻击修正", "その他攻撃補正", "OTHER ATTACK MODS"},
    {"其他承伤修正", "その他被ダメ補正", "OTHER DEFENSE MODS"},
    {"冰雹·必杀", "雹・スキル", "HAIL · SPECIAL"},
    {"必杀技修正", "必殺技補正", "SPECIAL MODIFIER"},
    {"雾雨·符卡", "霧雨・スペルカード", "DRIZZLE · SPELL"},
    {"符卡修正", "スペルカード補正", "SPELL MODIFIER"},
    {"CH首段强化", "CH初段強化", "CH FIRST-HIT BONUS"},
    {"目标天气封印", "天気封印", "WEATHER SEALED"},
    {"伤害吸血", "体力吸収", "LIFE STEAL"},
    {"黄沙CH", "黄砂CH", "SANDSTORM CH"},
    {"根性补正", "根性補正", "GUTS"},
};

inline const char *localizeEffectLabel(const char *zh, HudLanguage lang) {
    if (lang == HudLanguage::Chinese || !zh) return zh;
    for (const auto &e : kEffectLabels) {
        if (std::strcmp(e.zh, zh) == 0)
            return lang == HudLanguage::Japanese ? e.ja : e.en;
    }
    return zh; // Unknown rows keep their official label rather than an id.
}

inline std::string replaceAll(std::string value, const char *from, const char *to) {
    const std::size_t length = std::strlen(from);
    if (!length) return value;
    std::size_t at = 0;
    while ((at = value.find(from, at)) != std::string::npos) {
        value.replace(at, length, to);
        at += std::strlen(to);
    }
    return value;
}

inline std::string localizeEffectValue(const std::string &value, HudLanguage lang) {
    if (lang == HudLanguage::Chinese) return value;
    struct Suffix { const char *zh, *ja, *en; };
    static const Suffix suffixes[] = {
        {"可CH首段×1.33", "CH初段×1.33", "CH 1ST HIT ×1.33"},
        {"错防破防", "誤ガードでクラッシュ", "GUARD CRUSH"},
        {"按有效倍率", "有効倍率に反映", "IN EFFECTIVE MULT."},
        {"仅首段", "初段のみ", "1ST HIT ONLY"},
        {"另计", "別途", "SEPARATE"},
    };
    std::string out = value;
    for (const auto &s : suffixes)
        out = replaceAll(out, s.zh,
                         lang == HudLanguage::Japanese ? s.ja : s.en);
    return out;
}

inline const wchar_t *uiFontFamily(HudLanguage lang) {
    switch (lang) {
        case HudLanguage::Japanese: return L"Yu Gothic";
        case HudLanguage::English: return L"Segoe UI";
        default: return L"Microsoft YaHei";
    }
}

} // namespace whm
