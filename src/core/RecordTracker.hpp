#pragma once
#include <cstdint>
#include <string>
namespace whm {

struct ActualRecord {
    int damage = 0;
    int actionId = -1;
    bool attributed = false;
    bool isSpell = false;
};

// Conservative HP-delta observer: exact damage totals but attribution only when an
// unambiguous attack action is continuously observed. Delayed projectiles are UNATTRIBUTED.
class RecordTracker {
public:
    void reset();
    void observe(int beforeHP, int afterHP, int attackAction, unsigned frame);
    void finish();
    const ActualRecord &best() const { return best_; }
    int unattributedDamage() const { return unattributedDamage_; }
private:
    void commit();
    ActualRecord best_{};
    ActualRecord active_{};
    unsigned lastFrame_ = 0;
    int unattributedDamage_ = 0;
};
}
