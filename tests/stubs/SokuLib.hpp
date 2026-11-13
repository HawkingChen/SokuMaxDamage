#pragma once
#include "windows.h"
#include <string>
#include <vector>
#include <array>
#include <cstdint>
namespace SokuLib {
using Vector2i = struct Vec2i { int x,y; };
using Vector2u = struct Vec2u { unsigned x,y; };
struct Sprite { void setTexture2(int,int,int,int,int){} void renderScreen(float,float,float,float){} };
struct Card { unsigned short id; Sprite sprite; };
struct String { operator const char*() const { return "stub"; } };
struct CardInfo { String name; int texIdB=0; };
struct FontDescription { char faceName[256]; int r1,g1,b1,r2,g2,b2,height,weight,shadow,bufferSize;};
struct SWRFont { void create(){} void setIndirect(const FontDescription&){} };
namespace DrawUtils {
struct DxSokuColor{ DxSokuColor(int,int,int,int){} };
struct Texture{ bool createFromText(const char*,SWRFont&,Vector2u,Vector2i*){return true;} };
struct Sprite{ Texture texture; struct Rect {int left,top,width,height;} rect; void setPosition(Vector2i){} void setSize(Vector2u){} void draw(){} };
struct RectangleShape{void setPosition(Vector2i){} void setSize(Vector2u){} void setFillColor(DxSokuColor){} void setBorderColor(DxSokuColor){} void draw(){} };
}
enum BattleMode {BATTLE_MODE_VSCOM, BATTLE_MODE_VSPLAYER,BATTLE_MODE_VSCLIENT,BATTLE_MODE_VSSERVER,BATTLE_MODE_PRACTICE};
extern BattleMode mainMode;
extern int activeWeather;
extern BYTE targetHash[16];

namespace v2 {
struct CharacterFrameData {short damage{},ratio{},limit{};std::vector<int> attackBoxes;};
struct CharacterSequenceData {std::vector<CharacterFrameData> frames;CharacterSequenceData *next=nullptr;};
struct PatternMap {
 struct IterValue {int first; CharacterSequenceData *second;};
 struct Iter {IterValue value; IterValue *operator->() const {return const_cast<IterValue*>(&value);} bool operator==(const Iter &other) const{return value.first == other.value.first;} bool operator!=(const Iter &other) const{return !(*this==other);} };
 int size = 0; Iter find(int) {return {{0,nullptr}};} Iter end(){return {{-1,nullptr}};}
};
struct Player {
 int characterIndex=0;
 short HP=10000;
 float attackPower=1,defensePower=1,spellDmgMultiplier=1,specialDmgMultiplier=1;
 struct { PatternMap *patternMap; } gameData;
 struct { std::vector<Card> hand; } handInfo;
 struct {short skilledSkillLevel[32],effectiveSkillLevel[32];} levels;
 short skilledSkillLevel[32],effectiveSkillLevel[32];
 struct {unsigned char hitBoxCount;} boxData;
 struct {CardInfo* cardLookup(unsigned short){return nullptr;}} deckInfo;
};
}
struct ObjectManager { short hp{}; int action{}; };
struct CharacterManager { ObjectManager objectBase; };
struct BattleManager {unsigned frameCount{}; CharacterManager &leftCharacterManager,&rightCharacterManager; int onProcess(){return 0;} void onRender(){} void onExit(){} };
struct BattleManager_VTABLE {int (BattleManager::*onProcess)(); void (BattleManager::*onRender)(); void (BattleManager::*onExit)();};
extern BattleManager_VTABLE VTable_BattleManager;
template<typename T,typename U> T TamperDword(T *slot, U) {return *slot;}
}
