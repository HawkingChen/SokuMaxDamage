#pragma once
// First-hit-only bonuses (Meiling's Tiger Strikes / Inner Tiger, Sandstorm CH)
// are deliberately conservative. The game thread publishes plain signals;
// this pure function decides whether the bonus may enter the big percentage.
// Nothing here predicts an uninput future move or touches battle state.
namespace whm {

// Original combo reset tests the victim's hit reaction and reset delay.
// Combo count and untech can retain their previous values while standing idle.
constexpr bool canStartCombo(int targetAction, unsigned resetDelay) {
    return !(targetAction >= 50 && targetAction < 150) && resetDelay == 0;
}


struct FirstHitSignals {
    // GameObjectBase::counterHitDmgMultiplier (+0x198), not +0x530 attackPower.
    double localMultiplier = 1.;
    // A known source is active: Meiling skill, Sandstorm CH or a forced CH.
    // Merely having learned a skill, or a stale field value, is not enough.
    bool sourceConfirmed = false;
    // The target can start a fresh combo; NOT the displayed combo counters.
    bool comboIdle = false;
    // Victim is outside a hit reaction/reset delay, not untech == 0.
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
    // An active Meiling buff guarantees a forced CH on the next first hit.
    // This is readiness, so idle/startup needs no currently active hitbox.
    bool forcedFirstHitArmed = false;
};

struct FirstHitDecision {
    bool applies = false;
    double multiplier = 1.;
};

constexpr FirstHitDecision evaluateFirstHit(const FirstHitSignals &s) {
    const double local = s.localMultiplier;
    if (!(local >= 1.) || !(local < 10.)) return {};
    // Tiger Chi Lv1 has a local multiplier of 1, but guarantees the ordinary
    // 4/3 counter-hit bonus. Higher levels multiply that CH damage again.
    const double m = s.forcedFirstHitArmed ? local * (4. / 3.) : local;
    if (!(m > 1.008)) return {};
    if (!s.sourceConfirmed || !s.comboIdle || !s.targetOutOfHitstun ||
        (!s.attackConfirmed && !s.forcedFirstHitArmed) || s.firstHitConsumed ||
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
