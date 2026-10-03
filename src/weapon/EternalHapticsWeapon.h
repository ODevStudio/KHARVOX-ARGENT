#pragma once
#include "LegacyHapticsWeaponKind.h"
#include <array>
#include <string_view>
namespace argent {
inline const char* eternalCalibrationProfile(KharvoxWeaponKind kind,bool crucible=false,bool sentinelHammer=false){
 if(sentinelHammer)return "sentinel_hammer";
 if(crucible)return "crucible";
 switch(kind){
 case KharvoxWeaponKind::Shotgun:return "combat_shotgun";
 case KharvoxWeaponKind::HeavyAssaultRifle:return "heavy_cannon";
 case KharvoxWeaponKind::PlasmaRifle:return "plasma_rifle";
 case KharvoxWeaponKind::RocketLauncher:return "rocket_launcher";
 case KharvoxWeaponKind::SuperShotgun:return "super_shotgun";
 case KharvoxWeaponKind::GaussCannon:return "ballista";
 case KharvoxWeaponKind::Chaingun:return "chaingun";
 case KharvoxWeaponKind::Bfg:return "bfg";
 case KharvoxWeaponKind::Chainsaw:return "chainsaw";
 case KharvoxWeaponKind::Fists:return "fists";
 default:return "default";
 }
}
inline bool normalizedWeaponNameContains(std::string_view name,std::string_view part) noexcept {
 if(part.empty())return true;
 for(size_t start=0;start<name.size();++start){
  if(name[start]=='_'||name[start]=='-'||name[start]==' ')continue;
  size_t matched{};
  for(size_t i=start;i<name.size();++i){
   const auto c=static_cast<unsigned char>(name[i]);if(c=='_'||c=='-'||c==' ')continue;
   if(char(c>='A'&&c<='Z'?c+32:c)!=part[matched])break;
   if(++matched==part.size())return true;
  }
 }
 return false;
}
inline KharvoxWeaponKind eternalHapticsWeapon(std::string_view name) noexcept {
 std::array<char,256> key;
 size_t length{};
 if(name.size()<=key.size())for(unsigned char c:name)if(c!='_'&&c!='-'&&c!=' ')key[length++]=char(c>='A'&&c<='Z'?c+32:c);
 const std::string_view normalized(key.data(),length);
 const auto has=[&](std::string_view part){return name.size()<=key.size()?normalized.find(part)!=std::string_view::npos:normalizedWeaponNameContains(name,part);};
 if(has("supershotgun")||has("doublebarrel"))return KharvoxWeaponKind::SuperShotgun;
 if(has("shotgun"))return KharvoxWeaponKind::Shotgun;
 if(has("heavycannon")||has("heavyassaultrifle"))return KharvoxWeaponKind::HeavyAssaultRifle;
 if(has("plasmarifle"))return KharvoxWeaponKind::PlasmaRifle;
 if(has("rocketlauncher"))return KharvoxWeaponKind::RocketLauncher;
 if(has("ballista")||has("gauss"))return KharvoxWeaponKind::GaussCannon;
 if(has("chaingun"))return KharvoxWeaponKind::Chaingun;
 if(has("bfg"))return KharvoxWeaponKind::Bfg;
 if(has("chainsaw"))return KharvoxWeaponKind::Chainsaw;
 if(has("fists"))return KharvoxWeaponKind::Fists;
 return KharvoxWeaponKind::Unknown;
}
}
