#pragma once
// First-hit-only bonuses (Meiling's Tiger Strikes / Inner Tiger, Sandstorm CH)
// are deliberately conservative. The game thread publishes plain signals;
// this pure function decides whether the bonus may enter the big percentage.
// Nothing here predicts an uninput future move or touches battle state.
namespace whm {

struct FirstHitSignals {
    // GameObjectBase::counterHitDmgMultiplier (+0x198), not +0x530 attackPower.
    double localMultiplier = 1.;
    // A known source is active: Meiling skill, Sandstorm CH or a forced CH.
    // Merely having learned a skill, or a stale field value, is not enough.
    bool sourceConfirmed = false;
    // BOTH players' comboCount (+0x4B4) are zero. SokuLib does not settle
    // whether that counter belongs to attacker or victim, so requiring both
    // avoids silently reading the wrong owner.
    bool comboIdle = false;
    // Victim untech (+0x4BA) is zero: no ongoing hitstun / combo to extend.
    bool targetOutOfHitstun = false;
    // Own attack boxes exist. Projectile hitboxes live on child objects, so
    // they are intentionally excluded instead of being misattributed.
    bool attackConfirmed = false;
    // This combo already consumed its first hit (multi-hit follow-ups).
    bool firstHitConsumed = false;
    // The effective attackPower already contains this state: never multiply
    // the same effect twice.
    bool attackPowerAlreadyIncludes = false;
    bool moveCategorySupported = true;
};

struct FirstHitDecision {
    bool applies = false;
    double multiplier = 1.;
};

constexpr FirstHitDecision evaluateFirstHit(const FirstHitSignals &s) {
    const double m = s.localMultiplier;
    if (!(m > 1.008) || !(m < 10.)) return {};
    if (!s.sourceConfirmed || !s.comboIdle || !s.targetOutOfHitstun ||
        !s.attackConfirmed || s.firstHitConsumed ||
        s.attackPowerAlreadyIncludes || !s.moveCategorySupported) return {};
    return {true, m > 4. ? 4. : m};
}

// attackPower * target.defensePower * 100%. Guts and special/spell type
// multipliers are intentionally NOT part of this number.
inline double commonPercent(double attack, double defense,
                            const FirstHitDecision &firstHit) {
    const double base = attack * defense * 100.;
    if (!(base == base) || base <= 0. || base > 100000.) return 100.;
    return base * (firstHit.applies ? firstHit.multiplier : 1.);
}

} // namespace whm
