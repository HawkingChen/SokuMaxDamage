#pragma once
#include <cstdint>
#include <map>
#include <vector>
#include <utility>

namespace whm {
struct AttackOrigin {
    int action = -1;
    std::uint64_t instance = 0;
    bool valid() const { return action >= 300 && action < 690 && instance; }
    bool operator==(const AttackOrigin &) const = default;
};
struct ActorContact {
    int action = 0, sequence = 0;
    unsigned animationFrame = 0;
    int collision = 0, limit = 0, hitStop = 0;
    bool hitBoxes = false;
};
struct ProjectileContact {
    std::uintptr_t id = 0, parent = 0;
    unsigned generation = 0;
    int collision = 0, limit = 0, hitStop = 0;
    bool hitBoxes = false, owned = false;
};
// Read-only snapshots, taken on either side of the original engine update.
// Remember the casting action at birth; never relabel an old projectile when
// its owner walks, attacks again, or returns to idle. Handle generation avoids
// confusing a recycled game-object address with the previous projectile.
class HitAttribution {
    struct Tracked { ProjectileContact sample; AttackOrigin origin; };
    std::map<std::uintptr_t,Tracked> objects_;
    ActorContact previousActor_{};
    AttackOrigin actorOrigin_{};
    std::uint64_t serial_ = 0;
    static bool contact(int now,int before,int limit,int oldLimit,int stop,int oldStop) {
        return (now==1||now==2||now==7) &&
               (now!=before || limit<oldLimit || stop>oldStop);
    }
    AttackOrigin actor(const ActorContact &s) {
        const bool attack=s.action>=300&&s.action<690;
        const bool restarted=(s.sequence==previousActor_.sequence &&
            s.animationFrame<previousActor_.animationFrame) ||
            (s.sequence==0 && previousActor_.sequence>0);
        if(!attack)actorOrigin_={};
        else if(s.action!=previousActor_.action||restarted||!actorOrigin_.valid())
            actorOrigin_={s.action,++serial_};
        previousActor_=s;
        return actorOrigin_;
    }
    AttackOrigin born(const ProjectileContact &s,AttackOrigin casting) const {
        const auto parent=objects_.find(s.parent);
        return parent!=objects_.end()&&parent->second.origin.valid()?
            parent->second.origin:casting;
    }
public:
    void reset() { objects_.clear();previousActor_={};actorOrigin_={};serial_=0; }
    AttackOrigin observe(const ActorContact &before,const ActorContact &after,
                         const std::vector<ProjectileContact> &pre,
                         const std::vector<ProjectileContact> &post) {
        const auto beforeOrigin=actor(before);
        for(const auto &s:pre) {
            if(!s.owned)continue;
            auto it=objects_.find(s.id);
            if(it==objects_.end()||it->second.sample.generation!=s.generation)
                objects_[s.id]={s,born(s,beforeOrigin)};
            else it->second.sample=s;
        }
        const auto afterOrigin=actor(after);
        const auto casting=beforeOrigin.valid()?beforeOrigin:afterOrigin;
        std::vector<AttackOrigin> hits,possible;
        bool unknownHit=false;
        auto add=[&](std::vector<AttackOrigin> &v,AttackOrigin origin) {
            if(!origin.valid())return;
            for(auto old:v)if(old==origin)return;
            v.push_back(origin);
        };
        if(before.hitBoxes)add(possible,beforeOrigin);
        if(after.hitBoxes)add(possible,afterOrigin);
        if((before.hitBoxes||after.hitBoxes)&&contact(after.collision,before.collision,
             after.limit,before.limit,after.hitStop,before.hitStop))add(hits,casting);
        for(const auto &[id,t]:objects_)
            if(t.sample.owned&&t.sample.hitBoxes&&t.sample.limit>0)add(possible,t.origin);
        std::map<std::uintptr_t,Tracked> next;
        for(const auto &s:post) {
            if(!s.owned)continue;
            const auto it=objects_.find(s.id);
            const bool known=it!=objects_.end()&&it->second.sample.generation==s.generation;
            const AttackOrigin origin=known?it->second.origin:born(s,casting);
            const ProjectileContact prior=known?it->second.sample:ProjectileContact{};
            if(contact(s.collision,prior.collision,s.limit,prior.limit,s.hitStop,prior.hitStop)) {
                if(origin.valid())add(hits,origin);else unknownHit=true;
            }
            if(s.hitBoxes&&s.limit>0)add(possible,origin);
            next[s.id]={s,origin};
        }
        objects_=std::move(next);
        if(unknownHit||hits.size()>1)return {};
        if(hits.size()==1)return hits.front();
        // Some projectiles disappear in the same update as their last hit.
        // Use the sole evidenced active cast; never choose between two casts.
        return possible.size()==1?possible.front():AttackOrigin{};
    }
};

struct ComboReading { int hits=0,damage=0; bool freshTarget=false; };
inline int observedDamage(int beforeHP,int afterHP,ComboReading before,ComboReading after) {
    int damage=0;
    if(after.hits>0 && after.damage>0 &&
       (after.hits!=before.hits || after.damage!=before.damage)) {
        if(before.freshTarget||after.hits<=before.hits||after.damage<before.damage)
            damage=after.damage;
        else damage=after.damage-before.damage;
    }
    // Includes chip; combo totals also retain hits when Practice restores HP
    // within this engine update. Stale combo totals alone never add damage.
    if(damage<=0)damage=beforeHP>afterHP?beforeHP-afterHP:0;
    if(beforeHP>0&&damage>beforeHP)damage=beforeHP;
    return damage;
}
} // namespace whm
