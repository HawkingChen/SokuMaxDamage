#include "DamageEffects.hpp"
#include <array>
#include <cassert>
#include <iostream>
// The 2x2 language/collapse pad belongs at the bottom-right of the hand
// strip, clear of every SC card and its damage caption.
static_assert(kHudButtons[0].x >= 900 && kHudButtons[3].x >= 900);
static_assert(kHudButtons[3].y > kHudButtons[0].y);
static_assert(kHudButtons[3].x + kHudButtons[3].w <= kPanelVirtualWidth);
template<class T> void put(std::array<unsigned char,0x940> &bytes,size_t offset,T value){std::memcpy(bytes.data()+offset,&value,sizeof(value));}
int main(){
 std::array<unsigned char,0x940> own{},enemy{};
 PanelView v;v.character=0;v.opponentCharacter=15;v.rods=1;v.dolls=4;v.attack=1.1;v.defense=.8;v.skillMultiplier=1.25;v.weather=4;
 collectDamageEffects(v,own.data(),enemy.data());
 assert(v.effects.size()==3);assert(v.effects[0].card==18&&v.effects[0].copies==1);assert(v.effects[1].card==4&&v.effects[1].copies==4);assert(v.effects[2].card==1004);
 // Explanatory rows never reapply their factors to the live totals.
 assert(std::abs(v.attack*v.defense*v.skillMultiplier-1.1)<1e-9);
 PanelView drug;drug.character=10;drug.opponentCharacter=0;drug.attack=1.125;put<unsigned short>(own,0x8a0,2);collectDamageEffects(drug,own.data(),enemy.data());assert(drug.effects.size()==1&&drug.effects[0].card==209&&drug.effects[0].copies==2);
 own={};PanelView curse;curse.character=19;curse.opponentCharacter=0;curse.attack=1.5;curse.defense=1.3;
 put<signed char>(own,0x6a7,4);put<signed char>(own,0x6ab,-1);put<int>(own,0x894,1);put<int>(own,0x8a0,120);put<signed char>(own,0x6af,4);put<int>(own,0x8a4,60);
 collectDamageEffects(curse,own.data(),enemy.data());assert(curse.effects.size()==2);assert(curse.effects[0].card==103&&curse.effects[1].card==111);
 own={};enemy={};PanelView conditional;conditional.character=17;conditional.opponentCharacter=0;put<float>(own,0x198,2.f);put<float>(enemy,0x558,.3f);collectDamageEffects(conditional,own.data(),enemy.data());assert(conditional.effects.size()==2);assert(conditional.attack==1.&&conditional.defense==1.); // No whole-move CH/reflection multiplication.
 PanelView invalid;invalid.character=-1;collectDamageEffects(invalid,own.data(),enemy.data());assert(invalid.effects.empty());
 std::cout<<"Effect snapshots, stack counts, attribution and no-double-counting passed\n";
}
