#include "DamageEngine.hpp"
#include "ActionIdentity.hpp"
#include "MoveCatalog.hpp"
#include "WikiMax.hpp"
#include "RecordTracker.hpp"
#include "LocalPlayerSide.hpp"
#include "HudPalette.hpp"
#include "HudText.hpp"
#include "FirstHit.hpp"
#include "HitAttribution.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <vector>

static void testProjectileRecords() {
    using namespace whm;
    HitAttribution attribution;
    ActorContact cast;cast.action=561;cast.animationFrame=10;
    ActorContact idle;
    ProjectileContact bullet{1,0,10,0,1,0,true,true};
    const auto launched=attribution.observe(cast,cast,{}, {bullet});
    assert(launched.valid()&&launched.action==561);
    attribution.observe(cast,idle,{bullet},{bullet});
    auto hitBullet=bullet;hitBullet.collision=1;hitBullet.limit=0;
    const auto landed=attribution.observe(idle,idle,{bullet},{hitBullet});
    assert(landed==launched); // delayed 236C hit retains the casting action

    RecordTracker records;
    records.observeDamage(500,landed.action,10,landed.instance);
    // An idle body and long spacing must not split one cast's full damage.
    records.observeDamage(366,landed.action,40,landed.instance);
    records.observeDamage(336,landed.action,70,landed.instance);
    records.observeDamage(310,landed.action,100,landed.instance);
    records.observeDamage(285,landed.action,130,landed.instance);
    assert(records.best().damage==1797&&records.best().actionId==561);
    attribution.observe(idle,idle,{hitBullet},{});
    bullet.generation=11;
    const auto second=attribution.observe(cast,cast,{}, {bullet});
    assert(second.instance!=launched.instance);
    records.observeDamage(500,second.action,160,second.instance);
    assert(records.best().damage==1797); // same command twice is not one move

    ActorContact held=cast;held.sequence=1;held.animationFrame=40;
    attribution.observe(cast,held,{bullet},{bullet});
    ActorContact recast=cast;recast.sequence=0;recast.animationFrame=0;
    ProjectileContact recastBullet{3,0,15,0,1,0,true,true};
    attribution.observe(held,recast,{bullet},{bullet});
    attribution.observe(recast,recast,{bullet},{bullet,recastBullet});
    ActorContact rain;rain.action=571;
    ProjectileContact rainBullet{2,0,12,0,1,0,true,true};
    const auto rainCast=attribution.observe(rain,rain,{bullet},{bullet,rainBullet});
    auto rainHit=rainBullet;rainHit.collision=1;rainHit.limit=0;
    auto bothHit=bullet;bothHit.collision=1;bothHit.limit=0;
    const auto ambiguous=attribution.observe(rain,rain,{bullet,rainBullet},{bothHit,rainHit});
    assert(!ambiguous.valid()); // cannot steal an older bullet for the new cast
    records.observeDamage(242,571,180,100);
    records.observeDamage(233,571,181,100);
    records.observeDamage(2300,571,220,100);
    assert(records.best().damage==2775&&records.best().actionId==571);

    HitAttribution foreign;
    bullet.owned=false;bothHit.owned=false;
    assert(!foreign.observe(cast,cast,{bullet},{bothHit}).valid());
    assert(observedDamage(10000,10000,{18,4416,true},{18,4416,true})==0);
    assert(observedDamage(10000,10000,{18,4416,true},{1,242,false})==242);
    assert(observedDamage(9758,9525,{1,242,false},{2,475,false})==233);
    assert(observedDamage(10000,9900,{0,0,true},{0,0,true})==100); // chip
    assert(observedDamage(20,0,{0,0,true},{1,500,false})==20); // HP clamp
    RecordTracker unknown;
    unknown.observeDamage(500,-1,1,0);
    assert(unknown.best().damage==500&&!unknown.best().attributed);
}

int main() {
    testProjectileRecords();
    using namespace whm;
    assert(mergeSkillButtons("623B","623C")=="623B/C");
    assert(mergeSkillButtons("623B","j.623C")=="623B");
    assert(mergeSkillButtons("236B","623C")=="236B");
    static_assert(skillCardForAction(18,505)==104);
    static_assert(skillCardForAction(18,520)==101);
    static_assert(skillCardForAction(4,580)==104);
    static_assert(skillCardForAction(0,580)==-1);
    static_assert(skillCardForAction(18,504)==-1);
    static_assert(cardForAction(18,600)==200);

    // Regression: networking roles are the OPPOSITE of the old implementation.
    // SokuMods DiscordIntegration uses VSCLIENT (4) as local left and
    // VSSERVER (5) as local right. A wrong side leaks opponent's hand/skills.
    static_assert(localPlayerSide(4, 1) == 0); // client -> own P1/left
    static_assert(localPlayerSide(5, 1) == 1); // server -> own P2/right
    static_assert(localPlayerSide(8, 0) == 0); // practice
    static_assert(localPlayerSide(2, 0) == 0); // VS COM
    static_assert(localPlayerSide(3, 1) == 0); // offline P1
    static_assert(localPlayerSide(6, 1) == kNoLocalPlayer); // spectator
    static_assert(localPlayerSide(5, 2) == kNoLocalPlayer); // replay
    static_assert(localPlayerSide(4, 2) == kNoLocalPlayer); // replay
    static_assert(localPlayerSide(0, 1) == kNoLocalPlayer); // story
    static_assert(localPlayerSide(99, 1) == kNoLocalPlayer); // unknown
    assert(localPlayerSide(4, 1) != localPlayerSide(5, 1));

    assert(std::abs(gutsFactor(10000) - 1.0) < 0.001);
    assert(std::abs(gutsFactor(0) - 0.7) < 0.001);
    assert(std::abs(gutsFactor(5000) - 0.85) < 0.001);
    Move simple{0, MoveKind::Normal, 300, -1, "5A", {{500, 1., 1., 0}}, true};
    Fighter attacker{}, victim{};
    assert(estimate(simple, attacker, victim).damage == 500);
    attacker.attack = 1.4;
    victim.defense = .8;
    assert(estimate(simple, attacker, victim).damage == 560);
    victim.hp = 0;
    assert(estimate(simple, attacker, victim).damage == 392);
    Move multihit{0, MoveKind::Spell, 200, -1, "testSC", {{1000, .8, 1., 0}, {1000, 1., 1., 0}}, true};
    attacker.attack = 1.0; attacker.spellMultiplier = 1.25;
    victim = {};
    auto e = estimate(multihit, attacker, victim);
    assert(e.valid && e.damage > 2000 && e.complete);
    // Weather should affect only the corresponding attack category.
    Fighter standard{};
    Move singleSpell{0, MoveKind::Spell, 200, -1, "spell", {{1000, 1., 1., 0}}, true};
    Move singleSkill{0, MoveKind::Skill, 500, -1, "skill", {{1000, 1., 1., 0}}, true};
    standard.activeWeather = 1;
    assert(estimate(singleSpell, standard, victim).damage == 1250);
    assert(estimate(singleSkill, standard, victim).damage == 1000);
    standard.activeWeather = 4;
    assert(estimate(singleSpell, standard, victim).damage == 1000);
    assert(estimate(singleSkill, standard, victim).damage == 1250);
    // An incomplete profile must never be silently tagged complete.
    singleSpell.verifiedFullHit = false;
    assert(!estimate(singleSpell, standard, victim).complete);
    MoveCatalog db;
    std::istringstream in("#comment\n0|normal|300|-1|5A|1|500:1:1:0\n0|spell|201|-1|foo|0|1000:.9:1:0;500:1:1:0\n");
    std::vector<std::string> err;
    assert(db.read(in, &err));
    assert(db.size() == 2);
    assert(db.find(0, MoveKind::Normal, 300, 2) != nullptr);
    assert(db.find(0, MoveKind::Spell, 201, 0) != nullptr);
    assert(db.find(1, MoveKind::Normal, 300, 0) == nullptr);
    // Published full-move maxima are neutral reference totals; dynamically
    // scaling them is approximate, especially for multi-hit guts/truncation.
    WikiMaxCatalog wiki;
    std::istringstream wsrc("14|spell|207|-1|5851|zenjinrui|https://example.org/tenchi\n");
    assert(wiki.read(wsrc));
    auto *known = wiki.find(14, MoveKind::Spell, 207, -1);
    assert(known != nullptr && known->neutralMax == 5851);
    Fighter a{}, b{};
    assert(rescaleWikiMax(*known, a, b) == 5851);
    a.attack = 1.4;
    b.defense = .8;
    assert(rescaleWikiMax(*known, a, b) == 6553);
    RecordTracker record;
    record.observe(10000, 9800, 500, 1);
    record.observe(9800, 9400, 500, 2);
    assert(record.best().damage == 600);
    record.observe(9400, 9300, 100, 3);
    assert(record.best().damage == 600);
    assert(record.unattributedDamage() == 100);
    record.observe(9300, 9000, 605, 4);
    assert(record.best().damage == 600);
    record.reset();
    assert(record.best().damage == 0);
    record.observe(10000, 9900, 500, 1);
    for (unsigned frame = 2; frame <= 20; ++frame)
        record.observe(9900, 9900, 500, frame);
    record.observe(9900, 9750, 500, 21);
    assert(record.best().damage == 150);
    // --- Big percentage heat gradient: 100% keeps the existing warm gold.
    const auto baseline = paletteForPercent(100.);
    assert(baseline.core.r == kBaselineNumberPalette.core.r &&
           baseline.core.g == kBaselineNumberPalette.core.g &&
           baseline.core.b == kBaselineNumberPalette.core.b);
    assert(baseline.outline.r == kBaselineNumberPalette.outline.r &&
           baseline.outline.g == kBaselineNumberPalette.outline.g &&
           baseline.outline.b == kBaselineNumberPalette.outline.b);
    assert(baseline.glow.r == kBaselineNumberPalette.glow.r &&
           baseline.glow.g == kBaselineNumberPalette.glow.g &&
           baseline.glow.b == kBaselineNumberPalette.glow.b);
    assert(paletteForPercent(10.).core.b == paletteForPercent(50.).core.b);
    assert(paletteForPercent(400.).outline.r == paletteForPercent(250.).outline.r);
    // Interpolation stays continuous across every node: no step changes.
    for (double p = 50.; p < 250.; p += .25) {
        const auto a = paletteForPercent(p), b = paletteForPercent(p + .25);
        assert(std::abs(int(a.core.r) - int(b.core.r)) <= 4);
        assert(std::abs(int(a.outline.r) - int(b.outline.r)) <= 4);
        assert(std::abs(int(a.glow.g) - int(b.glow.g)) <= 4);
    }
    // 110% sits between gold anchors; 160% between orange-gold and orange-red.
    assert(paletteForPercent(110.).outline.r > paletteForPercent(100.).outline.r);
    assert(paletteForPercent(160.).outline.g < paletteForPercent(150.).outline.g);
    assert(paletteForPercent(std::nan("")).core.r == baseline.core.r);

    // --- Fixed UI strings and notation normalization.
    for (int k = 0; k < int(HudTextKey::KeyCount); ++k) {
        const auto key = static_cast<HudTextKey>(k);
        assert(hudText(key, HudLanguage::Chinese)[0] != '\0');
        assert(hudText(key, HudLanguage::Japanese)[0] != '\0');
        assert(hudText(key, HudLanguage::English)[0] != '\0');
    }
    assert(std::string(hudText(HudTextKey::Strongest, HudLanguage::Chinese)) == "最痛必杀技");
    assert(std::string(hudText(HudTextKey::Record, HudLanguage::Chinese)) == "最痛记录");
    assert(std::string(normalizeNotation("j.2A")) == "J2A");
    assert(std::string(normalizeNotation("f.5A")) == "F5A");
    assert(normalizeNotation("j.236B") == "J236B");
    assert(normalizeNotation("j5A") == "J5A");
    assert(normalizeNotation("J2A") == "J2A"); // idempotent
    assert(normalizeNotation("623B/C") == "623B/C");
    assert(normalizeNotation("Hakurei Amulet") == "Hakurei Amulet");
    assert(std::string(localizeEffectLabel("制御棒", HudLanguage::English)) ==
           "CONTROL RODS");
    assert(localizeEffectLabel("根性补正", HudLanguage::Japanese) ==
           std::string("根性補正"));
    assert(localizeEffectValue("×1.20 另计", HudLanguage::Japanese) == "×1.20 別途");
    assert(weatherName(4, HudLanguage::English) == std::string("Hail"));
    // Regression: the big percentage and the "根性补正" row used different
    // formatters and could show 0.70 vs 0.71 for the same hpFactor.
    char probe[64]{}; std::snprintf(probe, sizeof(probe), "%.2f", 0.705);
    assert(hudFactor(0.705, 2) == probe);
    assert(hudFactor(0.705, 2) == hudFactor(0.705, 2));
    assert(hudFactor(1., 1) == "1.0");
    assert(hudFactor(0.7, 2) == "0.70");
    // Regression: the grey factor and the "根性补正" row share ONE formatted
    // string, so a rounding boundary can never show 0.70 vs 0.71 again.
    assert(gutsLabel(0.705) == std::string("×") + hudFactor(0.705, 2));
    assert(gutsLabel(1.) == "×1.00");

    // Regression: normal attacks keep the source spelling (j.2A) as their
    // "name" while notation is normalized (J2A). The caption must show one
    // ALL-CAPS command, never "j.2A · J2A" / "j6A · J6A".
    assert(sameNotation("j.2A", "J2A"));
    assert(sameNotation("j6A", "J6A"));
    assert(!sameNotation("Hakurei Amulet", "236B"));
    assert(!sameNotation("6A", "6B"));
    assert(cardDisplayName("j.2A", "J2A") == "J2A");
    assert(cardDisplayName("j.6A", "J6A") == "J6A");
    assert(cardDisplayName("j2A", "J2A") == "J2A");
    assert(cardDisplayName("J2A", "J2A") == "J2A");
    assert(cardDisplayName("6A", "6A") == "6A");
    assert(cardDisplayName("j.2A", "") == "J2A");
    assert(cardDisplayName("Hakurei Amulet", "236B") ==
           std::string("Hakurei Amulet · 236B"));
    assert(cardDisplayName("博丽护符", "236B") ==
           std::string("博丽护符 · 236B"));

    // --- Weather text only when effective weather can influence damage.
    assert(weatherLabelVisible(4, false, 1.25, 1.));
    assert(weatherLabelVisible(1, false, 1., 1.25));
    assert(!weatherLabelVisible(0, false, 1., 1.));  // clear sky, no effect
    assert(!weatherLabelVisible(21, false, 1., 1.)); // no weather
    assert(!weatherLabelVisible(4, true, 1.25, 1.));  // sealed
    assert(weatherLabelVisible(7, false, 1.1, 1.));   // unexplained effective buff

    // --- First-hit-only bonus: conservative gating, no whole-move inflation.
    FirstHitSignals ready{1.3, true, true, true, true, false, false, true};
    auto hit = evaluateFirstHit(ready);
    assert(hit.applies && std::abs(hit.multiplier - 1.3) < 1e-9);
    assert(std::abs(commonPercent(1.2, 1., hit) - 156.) < 1e-9);
    assert(std::abs(commonPercent(1.2, 1., {}) - 120.) < 1e-9);
    FirstHitSignals unconfirmed = ready; unconfirmed.sourceConfirmed = false;
    assert(!evaluateFirstHit(unconfirmed).applies);
    FirstHitSignals stale = ready; stale.localMultiplier = 1.;
    assert(!evaluateFirstHit(stale).applies);
    FirstHitSignals wild = ready; wild.localMultiplier = 12.;
    assert(!evaluateFirstHit(wild).applies);
    FirstHitSignals capped = ready; capped.localMultiplier = 8.;
    assert(evaluateFirstHit(capped).multiplier == 4.);
    FirstHitSignals consumed = ready; consumed.firstHitConsumed = true;
    assert(!evaluateFirstHit(consumed).applies);
    FirstHitSignals combo = ready; combo.comboIdle = false;
    assert(!evaluateFirstHit(combo).applies);
    FirstHitSignals hitstun = ready; hitstun.targetOutOfHitstun = false;
    assert(!evaluateFirstHit(hitstun).applies);
    FirstHitSignals projectile = ready; projectile.attackConfirmed = false;
    assert(!evaluateFirstHit(projectile).applies);
    // Regression: armed Tiger Chi Release must show the NEXT first hit
    // while standing idle, not only flash during active hitbox frames.
    FirstHitSignals tigerIdle = ready;
    tigerIdle.attackConfirmed = false;
    tigerIdle.forcedFirstHitArmed = true;
    assert(evaluateFirstHit(tigerIdle).applies);
    tigerIdle.firstHitConsumed = true;
    assert(!evaluateFirstHit(tigerIdle).applies);
    tigerIdle.firstHitConsumed = false;
    tigerIdle.comboIdle = false;
    assert(!evaluateFirstHit(tigerIdle).applies);
    tigerIdle.comboIdle = true;
    tigerIdle.sourceConfirmed = false;
    assert(!evaluateFirstHit(tigerIdle).applies);
    assert(canStartCombo(0,0)); // stale combo/untech counters do not gate idle
    assert(!canStartCombo(50,0));
    assert(!canStartCombo(98,0));
    assert(!canStartCombo(0,1));
    FirstHitSignals tiger=ready;tiger.attackConfirmed=false;
    tiger.forcedFirstHitArmed=true;
    for(int level=1;level<=4;++level) {
        tiger.localMultiplier=1.+.1*(level-1);
        const auto result=evaluateFirstHit(tiger);
        assert(result.applies);
        assert(std::abs(result.multiplier-(4./3.)*(1.+.1*(level-1)))<1e-9);
    }
    tiger.localMultiplier=2.;
    assert(std::abs(evaluateFirstHit(tiger).multiplier-8./3.)<1e-9);
    FirstHitSignals duplicate = ready; duplicate.attackPowerAlreadyIncludes = true;
    assert(!evaluateFirstHit(duplicate).applies);
    assert(std::abs(commonPercent(0., 1., {}) - 100.) < 1e-9);

    std::cout << "All core tests passed\n";
}
