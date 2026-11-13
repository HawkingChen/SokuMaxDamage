#pragma once
#include "DamageEngine.hpp"
#include <istream>
#include <string>
#include <vector>

namespace whm {
// Published, neutral-state, extreme FULL-move damage (not damage per hit).
// Character/action/skill availability and card state are resolved by the mod.
struct WikiMax {
    int character = -1;
    MoveKind kind = MoveKind::Spell;
    int id = -1;
    int level = -1;
    int neutralMax = 0;
    std::string name;
    std::string source;
};

class WikiMaxCatalog {
public:
    // character|normal/skill/spell|action_or_card_id|level|neutral_max|name|sourceURL
    bool read(std::istream &input, std::vector<std::string> *errors = nullptr);
    const WikiMax *find(int character, MoveKind kind, int id, int level) const;
    std::vector<const WikiMax *> forCharacter(int character, MoveKind kind) const;
    size_t size() const { return records_.size(); }
private:
    std::vector<WikiMax> records_;
};

// Approximate value, not an exact simulated multi-hit value. Wiki neutralMax
// normally already contains the neutral full-HP combo-rate/guts/rounding.
// Do not claim exactness when current HP or dynamic buffs change.
int rescaleWikiMax(const WikiMax &entry, const Fighter &own, const Fighter &enemy);
}
