#pragma once
#include "StyledPanel.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
// Original 1.10a byte offsets, kept separate from v2::Player's late layout.
template<class T> inline T effectField(const void *player,size_t offset) {
    T value{};std::memcpy(&value,static_cast<const char*>(player)+offset,sizeof(value));return value;
}
inline void collectDamageEffects(PanelView &v,const void *own, const void *enemy) {
    auto sane=[](double n){return std::isfinite(n)&&n>0&&n<=50.;};
    if(v.character<0||v.character>19||v.opponentCharacter<0||v.opponentCharacter>19||
       !sane(v.attack)||!sane(v.defense)||!sane(v.skillMultiplier)||!sane(v.spellMultiplier))return;
    // +0x198 is GameObjectBase::counterHitDmgMultiplier (NOT +0x530). It is
    // published as a plain signal: only FirstHit.hpp may fold it into the
    // common percentage, and only when every conservative condition holds.
    const float chLocal=effectField<float>(own,0x198);
    v.firstHitMultiplier=(std::isfinite(chLocal)&&chLocal>1.008&&chLocal<10.)?
        static_cast<double>(chLocal):1.;
    // Meiling update: +0x890 is the buff timer; +0x1A2 counts forced
    // first-hit CHs. Unlike an attack box, these remain valid while idle.
    // Require both: a leftover multiplier alone must never arm the display.
    v.forcedFirstHitArmed=v.character==17&&
        effectField<unsigned short>(own,0x890)>0&&
        effectField<short>(own,0x1a2)>0;
    // A sealed effective weather belongs to the affected player (0x526).
    v.weatherSealed=effectField<unsigned short>(own,0x526)>0;
    double accountedAttack=1.,accountedDefense=1.;
    // Same formatter as the big percentage / grey guts number: no 0.01 drift.
    auto factor=[](double n){return "×"+whm::hudFactor(n,2);};
    auto add=[&](int ch,int card,int copies,const char *name,const std::string &value){v.effects.push_back({ch,card,copies,name,value});};
    if(v.rods>0){double n=1.+.1*v.rods;accountedAttack*=n;add(v.character,18,v.rods,"制御棒",factor(n));}
    int enemyRods=std::clamp(static_cast<int>(effectField<float>(enemy,0x844)),0,4);
    if(enemyRods){double n=1.+.075*enemyRods;accountedDefense*=n;add(v.opponentCharacter,18,enemyRods,"对手制御棒",factor(n));}
    static const double dolls[]={1.,.97,.93,.88,.8};
    if(v.dolls>0){double n=dolls[std::clamp(v.dolls,0,4)];accountedDefense*=n;add(v.opponentCharacter,4,v.dolls,"对手替身人偶",factor(n));}
    static const double elixirAttack[]={1.,1.05,1.125,1.25};
    static const double elixirDefense[]={1.,.95,.875,.8};
    if(v.character==10){int count=std::clamp<int>(effectField<unsigned short>(own,0x8a0),0,3);if(count){accountedAttack*=elixirAttack[count];add(10,209,count,"国士无双",factor(elixirAttack[count]));}}
    if(v.opponentCharacter==10){int count=std::clamp<int>(effectField<unsigned short>(enemy,0x8a0),0,3);if(count){accountedDefense*=elixirDefense[count];add(10,209,count,"对手国士无双",factor(elixirDefense[count]));}}
    if(v.opponentCharacter==6&&effectField<short>(enemy,0x890)>0){accountedDefense*=1.2;add(6,208,1,"千年吸血鬼",factor(1.2));}
    if(v.opponentCharacter==14&&effectField<int>(enemy,0x924)>0){accountedDefense*=.75;add(14,206,1,"无念无想减伤",factor(.75));}
    // Suwako's three-color curse state lives on the caster; red changes the
    // other player's defense. Orange's active interval changes caster attack.
    if(v.character==19) {
        const int level=std::max<int>(effectField<signed char>(own,0x6a7),effectField<signed char>(own,0x6ab));
        const int card=effectField<signed char>(own,0x6ab)>=0?107:103;
        int type=effectField<int>(own,0x894),timer=effectField<int>(own,0x8a0);
        if(timer>0&&type==1){double n=1.1+.05*std::clamp(level,0,4);accountedDefense*=n;add(19,card,1,"红色诅咒",factor(n));}
        if(timer>0&&type==2)add(19,card,1,"绿色诅咒","错防破防");
        int orangeLevel=effectField<signed char>(own,0x6af);
        if(orangeLevel>0&&orangeLevel<=4&&effectField<int>(own,0x8a4)>0){double n=1.1+.1*orangeLevel;accountedAttack*=n;add(19,111,1,"御射军神诅咒",factor(n));}
    }
    const double reflected=effectField<float>(own,0x558);
    const double enemyReflected=effectField<float>(enemy,0x558);
    if(std::isfinite(reflected)&&reflected>0&&reflected<=1)add(v.character,v.character==19?103:-1,1,"伤害反射",factor(reflected)+" 另计");
    if(std::isfinite(enemyReflected)&&enemyReflected>0&&enemyReflected<=1)add(v.opponentCharacter,v.opponentCharacter==19?103:-1,1,"对手反伤",factor(enemyReflected)+" 另计");
    if(effectField<unsigned char>(enemy,0x56d)&&v.character!=19)add(v.opponentCharacter,-1,1,"对手错防状态","错防破防");
    if(std::abs(v.attack/accountedAttack-1.)>.012)add(v.character,-1,1,"其他攻击修正",factor(v.attack/accountedAttack));
    if(std::abs(v.defense/accountedDefense-1.)>.012)add(v.opponentCharacter,-1,1,"其他承伤修正",factor(v.defense/accountedDefense));
    // Type multipliers already include weather. Show each once, not a second
    // weather factor. CH and reflection are annotations, never whole-move buffs.
    if(std::abs(v.skillMultiplier-1.)>.008)add(v.character,v.weather==4?1004:-1,1,v.weather==4?"冰雹·必杀":"必杀技修正",factor(v.skillMultiplier));
    if(std::abs(v.spellMultiplier-1.)>.008)add(v.character,v.weather==1?1001:-1,1,v.weather==1?"雾雨·符卡":"符卡修正",factor(v.spellMultiplier));
    if(v.forcedFirstHitArmed)add(17,v.firstHitMultiplier>=2?211:109,1,
        v.firstHitMultiplier>=2?"猛虎内劲首击":"虎劲首击",
        factor(v.firstHitMultiplier*(4./3.))+" 仅首段");
    else if(v.firstHitMultiplier>1.)add(v.character,-1,1,"CH首段强化",factor(v.firstHitMultiplier)+" 仅首段");
    if(effectField<unsigned short>(enemy,0x526))add(14,201,1,"目标天气封印","按有效倍率");
    const float absorb=effectField<float>(own,0x550);
    if(std::isfinite(absorb)&&absorb>0&&absorb<=1)add(v.character,v.character==6?208:1006,1,"伤害吸血",factor(absorb)+" 另计");
    if(v.weather==16)add(v.character,1016,1,"黄沙CH","可CH首段×1.33");
    // Reuse the exact label rendered beside the big percentage. Never format
    // hpFactor a second time here: that was the source of the 0.01 discrepancy.
    if(v.hpFactor<.999)add(v.opponentCharacter,-1,1,"根性补正",
                           v.gutsText.empty()?factor(v.hpFactor):v.gutsText);
}
