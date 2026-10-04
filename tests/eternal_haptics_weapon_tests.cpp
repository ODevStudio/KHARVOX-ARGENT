#include "../src/weapon/EternalHapticsWeapon.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>

namespace {
bool rejectAllocations{};
unsigned rejected{};
void check(bool value){if(!value)throw std::runtime_error("Weapon name classification changed");}
}
void* operator new(size_t size){
    if(rejectAllocations){++rejected;throw std::bad_alloc{};}
    if(auto result=std::malloc(size?size:1))return result;throw std::bad_alloc{};
}
void operator delete(void* value)noexcept{std::free(value);}
void operator delete(void* value,size_t)noexcept{std::free(value);}
int main(){try{
    using Kind=KharvoxWeaponKind;
    const std::array<std::pair<std::string_view,Kind>,17> cases{{
        {"weapon/player/SUPER- SHOT_GUN",Kind::SuperShotgun},
        {"double_barrel",Kind::SuperShotgun},{"combat SHOTGUN",Kind::Shotgun},
        {"heavy-cannon",Kind::HeavyAssaultRifle},{"Heavy Assault Rifle",Kind::HeavyAssaultRifle},
        {"plasma_rifle",Kind::PlasmaRifle},{"rocket-launcher",Kind::RocketLauncher},
        {"ballista",Kind::GaussCannon},{"gauss_cannon",Kind::GaussCannon},
        {"CHAIN-GUN",Kind::Chaingun},{"weapon/player/BFG",Kind::Bfg},
        {"chain_saw",Kind::Chainsaw},{"fists",Kind::Fists},{"",Kind::Unknown},
        {"weapon/player/crucible",Kind::Unknown},{"super.shotgun",Kind::Shotgun},
        {"heavycanno",Kind::Unknown}
    }};
    std::string longName(10000,'_');longName+="prefix HEAVY--ASSAULT__RIFLE suffix";
    rejectAllocations=true;
    for(const auto& [name,expected]:cases)check(argent::eternalHapticsWeapon(name)==expected);
    check(argent::eternalHapticsWeapon(longName)==Kind::HeavyAssaultRifle);
    rejectAllocations=false;check(rejected==0);
    std::cout<<"18 weapon name cases preserve normalization and reject no allocations\n";return 0;
}catch(const std::exception& error){rejectAllocations=false;std::cerr<<error.what()<<'\n';return 1;}}
