#pragma once
#include <string>
namespace whm {
// SokuLib Action.hpp: each skill slot occupies 20 action IDs; variants
// start at +0/+5/+10. Card IDs group all slots before the next variant.
constexpr int skillCardForAction(int character,int action) {
    if(action<500||action>=600)return -1;
    int relative=action-500,slot=relative/20,variant=(relative%20)/5;
    int slots=character==4?5:4; // Patchouli has five skill slots.
    if(slot>=slots||variant>=3||relative%5>=4)return -1;
    return 100+variant*slots+slot;
}
inline std::string mergeSkillButtons(const std::string &a,const std::string &b) {
    if(a==b)return a;
    if(a.size()!=b.size()||a.empty()||a.substr(0,a.size()-1)!=b.substr(0,b.size()-1))return a;
    if((a.back()=='B'&&b.back()=='C')||(a.back()=='C'&&b.back()=='B'))return a.substr(0,a.size()-1)+"B/C";
    return a;
}
constexpr int cardForAction(int character,int action) {
    if(action>=600&&action<620)return action-400;
    return skillCardForAction(character,action);
}
}
