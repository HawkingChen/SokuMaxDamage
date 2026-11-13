#include "WikiMax.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace whm {
namespace {
std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> result;
    std::istringstream stream(s);
    std::string item;
    while (std::getline(stream, item, delim)) result.push_back(item);
    return result;
}
bool parseInteger(const std::string &s, int &v) {
    if (s.empty()) return false;
    char *end;
    const long n = std::strtol(s.c_str(), &end, 10);
    if (*end || n < -100000 || n > 100000) return false;
    v = static_cast<int>(n);
    return true;
}
}

bool WikiMaxCatalog::read(std::istream &input, std::vector<std::string> *errors) {
    records_.clear();
    bool good = true;
    std::string line;
    int number = 0;
    while (std::getline(input, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto parts = split(line, '|');
        WikiMax value;
        bool valid = parts.size() == 7;
        if (valid) {
            valid &= parseInteger(parts[0], value.character);
            valid &= parseInteger(parts[2], value.id);
            valid &= parseInteger(parts[3], value.level);
            valid &= parseInteger(parts[4], value.neutralMax);
            if (parts[1] == "normal") value.kind = MoveKind::Normal;
            else if (parts[1] == "skill") value.kind = MoveKind::Skill;
            else if (parts[1] == "spell") value.kind = MoveKind::Spell;
            else valid = false;
            value.name = parts[5]; value.source = parts[6];
            valid &= value.character >= 0 && value.character <= 19 && value.id >= 0 &&
                value.level >= -1 && value.level <= 4 && value.neutralMax > 0 &&
                value.neutralMax <= 20000 && !value.name.empty() &&
                value.source.rfind("https://", 0) == 0;
            const auto duplicate = std::find_if(records_.begin(), records_.end(), [&](const WikiMax &old) {
                return old.character == value.character && old.kind == value.kind &&
                    old.id == value.id && old.level == value.level;
            });
            if (duplicate != records_.end()) valid = false;
        }
        if (valid) records_.push_back(std::move(value));
        else {
            good = false;
            if (errors) errors->push_back("bad Wiki max at line " + std::to_string(number));
        }
    }
    return good;
}

const WikiMax *WikiMaxCatalog::find(int character, MoveKind kind, int id, int level) const {
    const WikiMax *fallback = nullptr;
    for (const auto &r : records_) {
        if (r.character != character || r.kind != kind || r.id != id) continue;
        if (r.level == level) return &r;
        if (r.level == -1) fallback = &r;
    }
    return fallback;
}

std::vector<const WikiMax *> WikiMaxCatalog::forCharacter(int character, MoveKind kind) const {
    std::vector<const WikiMax *> out;
    for (const auto &r : records_)
        if (r.character == character && r.kind == kind) out.push_back(&r);
    return out;
}

int rescaleWikiMax(const WikiMax &entry, const Fighter &own, const Fighter &enemy) {
    if (entry.neutralMax <= 0) return 0;
    const double type = entry.kind == MoveKind::Spell ? own.spellMultiplier :
                        entry.kind == MoveKind::Skill ? own.skillMultiplier : 1.0;
    const double weather = (entry.kind == MoveKind::Spell && own.activeWeather == 1) ||
                           (entry.kind == MoveKind::Skill && own.activeWeather == 4) ? 1.25 : 1.0;
    const double multiplier = own.attack * enemy.defense * type * weather;
    if (!std::isfinite(multiplier) || multiplier <= 0.0 || multiplier > 50.0) return 0;
    // Wiki maximum generally assumes a fresh target at 10k HP. The ratio
    // below is a coarse approximation, because each hit changes guts.
    const double hpApprox = enemy.hp < 10000 ? gutsFactor(enemy.hp) : 1.0;
    return static_cast<int>(std::floor(entry.neutralMax * multiplier * hpApprox));
}
}
