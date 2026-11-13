#include "DamageEngine.hpp"
#include <algorithm>
#include <cmath>

namespace whm {
namespace {
int truncMul(int value, double factor) {
    return static_cast<int>(std::floor(static_cast<double>(value) * factor + 1e-9));
}
bool sane(double n) { return std::isfinite(n) && n > 0.0 && n <= 100.0; }
}

double gutsFactor(int hp) {
    // In Soku at 10000 HP -> 1.00, at 0 HP -> 0.70.
    const int clamped = std::clamp(hp, 0, 10000);
    return std::floor(70.0 + static_cast<double>(clamped) * 3.0 / 1000.0) / 100.0;
}

Estimate estimate(const Move &move, const Fighter &attacker, const Fighter &defender) {
    Estimate result;
    if (move.hits.empty()) { result.reason = "no hits"; return result; }
    if (!sane(attacker.attack) || !sane(defender.defense) ||
        !sane(attacker.spellMultiplier) || !sane(attacker.skillMultiplier)) {
        result.reason = "invalid multiplier"; return result;
    }
    double runningRate = 1.0;
    int hp = std::clamp(defender.hp, 0, 10000);
    int limit = 0;
    for (const auto &hit : move.hits) {
        if (hit.baseDamage < 0 || hit.baseDamage > 32767 ||
            !sane(hit.rateAfter) || hit.rateAfter > 1.0 || !sane(hit.special) ||
            hit.limit < 0 || hit.limit > 100) {
            result.reason = "invalid hit data";
            return result;
        }
        // Base * attack * defense, truncating after the combined multiplier.
        int dealt = truncMul(hit.baseDamage, attacker.attack * defender.defense);
        dealt = truncMul(dealt, runningRate);
        dealt = truncMul(dealt, hit.special);
        const double typeBonus = move.kind == MoveKind::Spell ? attacker.spellMultiplier :
                                 move.kind == MoveKind::Skill ? attacker.skillMultiplier : 1.0;
        dealt = truncMul(dealt, typeBonus);
        // Separate W term in the published Hisoutensoku damage formula.
        // Needs practice-mode verification to exclude overlap with type multipliers.
        const double weatherBonus =
            (attacker.activeWeather == 1 && move.kind == MoveKind::Spell) ||
            (attacker.activeWeather == 4 && move.kind == MoveKind::Skill) ? 1.25 : 1.0;
        dealt = truncMul(dealt, weatherBonus);
        dealt = truncMul(dealt, gutsFactor(hp));
        result.damage += dealt;
        hp = std::max(0, hp - dealt);
        runningRate *= hit.rateAfter;
        limit += hit.limit;
        if (limit >= 100) break;
    }
    result.valid = true;
    result.complete = move.verifiedFullHit;
    if (!result.complete) result.reason = "hit sequence not verified";
    return result;
}
} // namespace whm
