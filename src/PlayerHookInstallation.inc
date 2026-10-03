namespace {
std::mutex playerInstallationGuard;
bool playerHooksInstalled{};
struct PlayerHook {
 void* target;
 void* detour;
 void** original;
 void* previousOriginal;
 const unsigned char* expected;
 size_t size;
};
template<size_t N,class Detour,class Original>
PlayerHook playerHook(unsigned char* base,uintptr_t rva,Detour detour,Original* original,const unsigned char(&expected)[N]) {
 const auto slot=reinterpret_cast<void**>(original);
 return {base+build::rva(rva),reinterpret_cast<void*>(detour),slot,*slot,expected,N};
}
void removeOwnedPlayerHook(void* target) noexcept {
 if(MH_RemoveHook(target)!=MH_OK){RaiseFailFastException(nullptr,nullptr,0);std::terminate();}
}
struct PlayerHookAttempt {
 const std::array<PlayerHook,21>& hooks;
 unsigned char* previousImage;
 size_t created{};
 bool committed{};
 ~PlayerHookAttempt(){
  if(committed)return;
  for(size_t i=created;i>0;--i)removeOwnedPlayerHook(hooks[i-1].target);
  for(size_t i=0;i<created;++i)*hooks[i].original=hooks[i].previousOriginal;
  image=previousImage;
 }
};
template<size_t N>
bool playerContract(unsigned char* base,uintptr_t rva,const unsigned char(&expected)[N],size_t offset=0) {
 return !std::memcmp(base+build::rva(rva)+offset,expected,N);
}
bool installWaterHook() {
 constexpr unsigned char waterBytes[]={0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x81,0xec,0xf0,0,0,0};
 if(!playerContract(image,0x53a9f0,waterBytes))return false;
 const auto target=image+build::rva(0x53a9f0);
 const auto previous=originalWaterMove;
 if(MH_CreateHook(target,reinterpret_cast<void*>(&waterMove),reinterpret_cast<void**>(&originalWaterMove))!=MH_OK)return false;
 if(MH_EnableHook(target)==MH_OK)return true;
 removeOwnedPlayerHook(target);originalWaterMove=previous;return false;
}
}
bool install(unsigned char* verifiedImage) noexcept {try{
 std::lock_guard<std::mutex> lock(playerInstallationGuard);
 if(playerHooksInstalled)return verifiedImage==image;
 if(!verifiedImage)return false;
 constexpr unsigned char equipmentContract[]={0x49,0x8b,0x45,0x38,0x48,0x8b,0x8b,0x58,0x03,0x00,0x00,0x44,0x8b,0xa0,0x08,0x02,0x00,0x00};
 constexpr unsigned char transformBytes[]={0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56};
 constexpr unsigned char throwBytes[]={0x4d,0x85,0xc0,0x0f,0x84,0x35,0x01,0,0,0x55,0x56,0x57,0x41,0x56,0x41,0x57};
 constexpr unsigned char wallGateBytes[]={0x80,0xbf,0x50,0x17,0,0,0};
 constexpr unsigned char wallImpulseBytes[]={0x0f,0x28,0xc8,0x0f,0xc6,0xc9,0x55};
 constexpr unsigned char focusBytes[]={0x0f,0x28,0x45,0xd0,0x0f,0x11,0x8e,0x68,0x02,0x00,0x00};
 constexpr unsigned char meathookBytes[]={0x48,0x8d,0x95,0x58,0x01,0,0,0x48,0x8d,0x8d,0x90,0,0,0};
 constexpr unsigned char gateBytes[]={0xf3,0x0f,0x10,0x45,0x68,0xf3,0x0f,0x10,0x6d,0x30,0xf3,0x0f,0x10,0x5d,0x34};
 constexpr unsigned char blendBytes[]={0x48,0x83,0xec,0x38,0x0f,0x29,0x74,0x24,0x20,0x0f,0x28,0xf1};
 constexpr unsigned char modeBytes[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9};
 constexpr unsigned char fovBytes[]={0x48,0x83,0xec,0x38,0x48,0x83,0xb9,0x58,0x01,0x00,0x00,0x00,0x4c,0x8b,0xc1};
 constexpr unsigned char parentShowBytes[]={0x80,0xa1,0xb0,0x00,0x00,0x00,0xfe};
 constexpr unsigned char hideBytes[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9};
 constexpr unsigned char itemAnimationBytes[]={0x40,0x55,0x56,0x41,0x55,0x41,0x56,0x48,0x8d,0x6c,0x24,0xc1,0x48,0x81,0xec,0xd8,0,0,0};
 constexpr unsigned char stateBytes[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
 constexpr unsigned char eventBytes[]={0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20,0x56,0x48,0x81,0xec,0x80,0,0,0};
 constexpr unsigned char eventEndBytes[]={0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20,0x57,0x48,0x81,0xec,0x80,0,0,0};
 constexpr unsigned char attachmentBytes[]={0x48,0x83,0xec,0x48,0x48,0x8b,0x44,0x24,0x78,0x45,0x8b,0xd0};
 constexpr unsigned char rootBytes[]={0x0f,0xb6,0x86,0xb0,0x00,0x00,0x00,0x0f,0x10,0x45,0xd0,0x24,0x0c,0x3c,0x0c};
 constexpr unsigned char fireBytes[]={0x4c,0x8b,0xdc,0x55,0x53,0x56,0x57,0x41,0x55,0x41,0x56,0x41,0x57};
 constexpr unsigned char updateBytes[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18};
 constexpr unsigned char playBytes[]={0x41,0x54,0x41,0x55,0x41,0x56,0x48,0x81,0xec,0x90,0,0,0};
 constexpr unsigned char endBytes[]={0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x40};
 if(!playerContract(verifiedImage,0x135f280,equipmentContract,0xf6)||
    !playerContract(verifiedImage,0x135f130,stateBytes)||
    !playerContract(verifiedImage,0x18dbf20,parentShowBytes)||
    !playerContract(verifiedImage,0x138a0e0,hideBytes))return false;
 const std::array<PlayerHook,21> hooks{
  playerHook(verifiedImage,0x134b00e,&argentFocusBridge,&argentFocusResume,focusBytes),
  playerHook(verifiedImage,0x16b8351,&argentMeathookBridge,&argentMeathookResume,meathookBytes),
  playerHook(verifiedImage,0x16b8e76,&argentMeathookGateBridge,&argentMeathookGateResume,gateBytes),
  playerHook(verifiedImage,0x13891f0,&hideItem,&originalHideItem,hideBytes),
  playerHook(verifiedImage,0x13693f0,&zoomBlend,&originalZoomBlend,blendBytes),
  playerHook(verifiedImage,0x16c4080,&zoomMode,&originalZoomMode,modeBytes),
  playerHook(verifiedImage,0x16c4280,&zoomFov,&originalZoomFov,fovBytes),
  playerHook(verifiedImage,0x138ab20,&updateItemAnimation,&originalItemAnimation,itemAnimationBytes),
  playerHook(verifiedImage,0x135aa00,&crucibleEvent<0>,&originalCrucibleEvents[0],eventBytes),
  playerHook(verifiedImage,0x135ab80,&crucibleEvent<1>,&originalCrucibleEvents[1],eventBytes),
  playerHook(verifiedImage,0x135ad00,&crucibleEvent<2>,&originalCrucibleEvents[2],eventEndBytes),
  playerHook(verifiedImage,0x1981bc0,&attachmentJoint,&originalAttachmentJoint,attachmentBytes),
  playerHook(verifiedImage,0x13807ea,&argentHandsBridge,&argentHandsResume,rootBytes),
  playerHook(verifiedImage,0x137fd60,&updateHands,&originalUpdateHands,updateBytes),
  playerHook(verifiedImage,0x1390b50,&playTraversal,&originalPlayTraversal,playBytes),
  playerHook(verifiedImage,0x1398420,&endBar,&originalEndBar,endBytes),
  playerHook(verifiedImage,0x13bc530,&argentWallGateBridge,&argentWallGateResume,wallGateBytes),
  playerHook(verifiedImage,0x13b98f8,&argentWallImpulseBridge,&argentWallImpulseResume,wallImpulseBytes),
  playerHook(verifiedImage,0x1389020,&itemTransform,&originalItemTransform,transformBytes),
  playerHook(verifiedImage,0x1389910,&throwItem,&originalThrowItem,throwBytes),
  playerHook(verifiedImage,0x135f280,&fire,&originalFire,fireBytes)
 };
 for(const auto& hook:hooks)if(std::memcmp(hook.target,hook.expected,hook.size))return false;
 PlayerHookAttempt attempt{hooks,image};
 for(const auto& hook:hooks){
  if(MH_CreateHook(hook.target,hook.detour,hook.original)!=MH_OK)return false;
  ++attempt.created;
 }
 image=verifiedImage;
 for(const auto& hook:hooks)if(MH_EnableHook(hook.target)!=MH_OK)return false;
 attempt.committed=true;playerHooksInstalled=true;
 try{log("ETERNAL_FOCUS installed=1 origin=HMD direction=HMD reach=native");}catch(...){}
 try{log("ETERNAL_MEATHOOK targetView=weapon-controller rangeAndLOS=native");}catch(...){}
 try{log("ETERNAL_MEATHOOK candidateAngleGate=weapon-controller nativeLimits=preserved");}catch(...){}
 try{log("ETERNAL_MELEE weaponVisibility=retain-primary-normal-punch sync=native");}catch(...){}
 try{log("ETERNAL_PLAYER zoomPresentation=no-hand-animation scope=off handsFov=world zoomTarget=gameplay-fov");}catch(...){}
 try{log("ETERNAL_CRUCIBLE restAttachmentHook=1 velocityOnly=1 hitAndTrigger=native");}catch(...){}
 try{log("ETERNAL_MONKEYBAR skip=first-person-play-call native-completion=invalid-handle");}catch(...){}
 try{if(!monkey::install(image))log("ETERNAL_MONKEYBAR HMD adapter unavailable: hook contract rejected");}catch(...){}
 const bool waterOk=installWaterHook();
 try{log("ETERNAL_SWIMMING installed="+std::to_string(waterOk)+" scope=native-water-move pitch=HMD yaw=existing-input");}catch(...){}
 try{log("ETERNAL_PLAYER arms=authored-camera-only projection=world-xy movement=head-relative");}catch(...){}
 try{log("ETERNAL_WALLCLIMB installed=1 gate=HMD impulse=HMD-yaw-pitch launchBias=native");}catch(...){}
 try{log("ETERNAL_EQUIPMENT installed=1 paths=joint-transform,throw,fire-info slots=5,6,9,10 pitch=HMD");}catch(...){}
 try{log("ETERNAL_PLAYER hands=1 fire=1 physics=read-native-origin");}catch(...){}
 return true;
 }catch(...){return false;}}
