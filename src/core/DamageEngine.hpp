#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace whm {

enum class MoveKind { Normal, Skill, Spell };

struct Hit {
    int baseDamage = 0;
    double rateAfter = 1.0;  // Applied to subsequent hits, not this hit.
    double special = 1.0;    // Multiplicative correction specific to this hit.
    int limit = 0;
};

struct Move {
    int character = -1;
    MoveKind kind = MoveKind::Normal;
    int id = -1;                 // Action ID for normal/skill, card ID for spells.
    int level = -1;              // -1 for any level, otherwise exact level.
    std::string name;
    std::vector<Hit> hits;
    bool verifiedFullHit = false; // Explicitly verified that these hits are achievable together.
};

struct Fighter {
    int character = 0;
    int hp = 10000;
    double attack = 1.0;
    double defense = 1.0;
    double spellMultiplier = 1.0;
    double skillMultiplier = 1.0;
    int activeWeather = 0; // Vanilla weather ID: drizzle=1, hail=4.
};

struct Estimate {
    int damage = 0;
    bool valid = false;
    bool complete = false;
    std::string reason;
};

// Implements the community-researched damage pipeline with truncation after each stage.
// Real-game behavior must be compared against practice mode before estimates are certified.
Estimate estimate(const Move &move, const Fighter &attacker, const Fighter &defender);

// Return a two-decimal-step proportional guts factor from target's *current* HP.
double gutsFactor(int defenderHP);

} // namespace whm
