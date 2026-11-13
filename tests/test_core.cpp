#include "DamageEngine.hpp"
#include "MoveCatalog.hpp"
#include "WikiMax.hpp"
#include "RecordTracker.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
#include <vector>

int main() {
    using namespace whm;
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
    std::cout << "All core tests passed\n";
}
