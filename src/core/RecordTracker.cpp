#include "RecordTracker.hpp"
#include <algorithm>
namespace whm {
void RecordTracker::reset() { best_ = {}; active_ = {}; lastFrame_ = 0; unattributedDamage_ = 0; }
void RecordTracker::commit() { if (active_.damage > best_.damage) best_ = active_; active_ = {}; }
void RecordTracker::finish() { commit(); }
void RecordTracker::observe(int beforeHP, int afterHP, int action, unsigned frame) {
    // Frame counter may restart on the next ROUND. Preserve the match record.
    if (frame < lastFrame_) commit();
    const int delta = std::max(0, beforeHP - afterHP);
    // A contiguous action episode: only count hits when action is valid and is
    // unmistakably an attack animation. This is not sufficient for projectiles.
    const bool attributed = (action >= 300 && action < 690);
    if (active_.damage > 0 && (action != active_.actionId || frame > lastFrame_ + 15 || !attributed)) commit();
    if (delta > 0) {
        if (!attributed) { unattributedDamage_ += delta; }
        else {
            active_.attributed = true;
            active_.actionId = action;
            active_.isSpell = action >= 600;
            active_.damage += delta;
            if (active_.damage > best_.damage) best_ = active_; // live preview
        }
    }
    lastFrame_ = frame;
}
}
