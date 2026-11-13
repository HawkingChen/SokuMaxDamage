#pragma once
#include <SokuLib.hpp>
#include "DamageEngine.hpp"
#include <algorithm>
#include <vector>

namespace whm::probe {

// Extracts *plausible direct-hit frames* from the current character's loaded
// animation data. This DOES NOT include spawned objects/projectiles, command
// follow-ups, throw scripts or positional constraints; it is NEVER full-hit
// verified. The output should be labelled "approx/partial" in the HUD.
inline bool directFrames(const SokuLib::v2::Player &player, MoveKind kind,
                         int actionId, int cardId, Move &out) {
    auto *map = player.gameData.patternMap;
    if (!map || map->size < 0 || map->size > 4000) return false;
    const auto it = map->find(actionId);
    if (it == map->end() || !it->second) return false;
    out = {};
    out.kind = kind;
    out.character = static_cast<int>(player.characterIndex);
    out.id = kind == MoveKind::Spell ? cardId : actionId;
    out.level = -1;
    out.name = kind == MoveKind::Spell ? "SC#" + std::to_string(cardId) :
               "ACTION#" + std::to_string(actionId);
    out.verifiedFullHit = false;
    const auto *first = it->second;
    const auto *seq = first;
    int sequenceGuard = 0;
    do {
        if (!seq || ++sequenceGuard > 32) return false;
        const auto &frames = seq->frames;
        if (frames.size() > 2048) return false;
        bool active = false;
        for (const auto &frame : frames) {
            const bool hit = frame.damage > 0 && frame.attackBoxes.size() > 0;
            if (!hit) { active = false; continue; }
            if (active) continue; // a contiguous active interval normally hits once
            active = true;
            if (out.hits.size() >= 32) return false;
            Hit part;
            part.baseDamage = frame.damage;
            part.rateAfter = frame.ratio > 0 && frame.ratio <= 100 ? frame.ratio / 100.0 : 1.0;
            part.limit = std::clamp(static_cast<int>(frame.limit), 0, 100);
            out.hits.push_back(part);
        }
        seq = seq->next;
    } while (seq != first && seq);
    return !out.hits.empty();
}
} // namespace whm::probe
