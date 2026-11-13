#pragma once
#include <cstdint>
#include <string>
#include <map>
namespace whm {

struct ActualRecord {
    int damage = 0;
    int actionId = -1;
    bool attributed = false;
    bool isSpell = false;
};

// Single-cast damage records. The runtime supplies observed damage and a
// source instance from HitAttribution, so delayed/multi-hit projectiles keep
// their casting action. observe() retains the older HP-only API for callers
// without source snapshots; it cannot attribute delayed projectiles.
class RecordTracker {
public:
    void reset();
    void observe(int beforeHP, int afterHP, int attackAction, unsigned frame);
    void observeDamage(int damage, int action, unsigned frame, std::uint64_t instance);
    void finish();
    const ActualRecord &best() const { return best_; }
    int unattributedDamage() const { return unattributedDamage_; }
private:
    void commit();
    std::map<std::uint64_t,ActualRecord> casts_;
    ActualRecord best_{};
    ActualRecord active_{};
    unsigned lastFrame_ = 0;
    unsigned lastHitFrame_ = 0;
    int unattributedDamage_ = 0;
};
}
