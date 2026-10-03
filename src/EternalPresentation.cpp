#include "EternalBuildProfile.h"
#include "EternalPresentation.h"
#include "Diagnostics.h"
#include "EternalPresentationFrame.h"
#include "PauseMenuSession.h"
#include "QuadRuntime.h"
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <exception>
#include <cstring>
namespace argent::presentation {namespace {
using ConsumeFrame=void(__fastcall*)(void*);
ConsumeFrame original{};
std::atomic<bool> storeConsumer{};
std::mutex presentationInstallationGuard;
unsigned char* installedImage{};
struct PresentationHook {void* target;void* detour;void** original;void* previous;};
struct PresentationHookAttempt {
 const std::array<PresentationHook,9>& hooks;
 const uintptr_t previousType;
 const bool previousStore;
 size_t created{};
 bool committed{};
 ~PresentationHookAttempt(){
  if(committed)return;
  for(size_t i=created;i>0;--i)if(MH_RemoveHook(hooks[i-1].target)!=MH_OK){RaiseFailFastException(nullptr,nullptr,0);std::terminate();}
  for(size_t i=0;i<created;++i)*hooks[i].original=hooks[i].previous;
  playerVtable=previousType;storeConsumer=previousStore;
 }
};
using MenuTransition=void(__fastcall*)(void*,int);
MenuTransition originalPauseShow{},originalPauseHide{};
MenuTransition originalUpgradeShow{},originalUpgradeHide{};
MenuTransition originalDeathShow{},originalDeathHide{};
std::atomic<uintptr_t> upgradeScreen{};
std::atomic<uintptr_t> deathScreen{};
using DossierOpen=void(__fastcall*)(void*,void*);
using DossierClose=void(__fastcall*)(void*);
DossierOpen originalDossierOpen{};DossierClose originalDossierClose{};
std::atomic<uintptr_t> dossierScreen{};
void __fastcall dossierOpen(void* owner,void* event){
 originalDossierOpen(owner,event);dossierScreen=uintptr_t(owner);
 try{log("ETERNAL_DOSSIER event=open");}catch(...){}
}
void __fastcall dossierClose(void* owner){
 originalDossierClose(owner);auto expected=uintptr_t(owner);dossierScreen.compare_exchange_strong(expected,0);
 try{log("ETERNAL_DOSSIER event=close");}catch(...){}
}
std::mutex pauseMutex;
PauseMenuSession pauseSession;
template<class T> bool read(uintptr_t p,T& value){SIZE_T got{};return p&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),&value,sizeof(value),&got)&&got==sizeof(value);}
void logPause(void* screen,int transition,const char* event) noexcept {try{
 uintptr_t menu{};int active=-99,next=-99;
 if(read(uintptr_t(screen)+0xd8,menu)&&menu){read(menu+0x90,active);read(menu+0x94,next);}
 log(std::string("ETERNAL_PAUSE event=")+event+" transition="+std::to_string(transition)+" activeScreen="+std::to_string(active)+" nextScreen="+std::to_string(next));
}catch(...){}
}
void __fastcall pauseShow(void* screen,int transition){
 try{std::lock_guard<std::mutex> lock(pauseMutex);pauseSession.show(uintptr_t(screen));}catch(...){}
 originalPauseShow(screen,transition);
 logPause(screen,transition,"show");
}
void __fastcall pauseHide(void* screen,int transition){
 originalPauseHide(screen,transition);
 try{std::lock_guard<std::mutex> lock(pauseMutex);pauseSession.hide(uintptr_t(screen),transition);}catch(...){}
 logPause(screen,transition,"hide");
}
void __fastcall upgradeShow(void* screen,int transition){
 upgradeScreen=uintptr_t(screen);
 originalUpgradeShow(screen,transition);
 upgradeAnimationOwner=player.load();
 try{log("ETERNAL_UPGRADE event=show transition="+std::to_string(transition));}catch(...){}
}
void __fastcall upgradeHide(void* screen,int transition){
 originalUpgradeHide(screen,transition);
 auto expected=uintptr_t(screen);upgradeScreen.compare_exchange_strong(expected,0);
 try{log("ETERNAL_UPGRADE event=hide transition="+std::to_string(transition));}catch(...){}
}
void __fastcall deathShow(void* screen,int transition){
 originalDeathShow(screen,transition);
 deathScreen=uintptr_t(screen);
 try{log("ETERNAL_DEATH event=show transition="+std::to_string(transition));}catch(...){}
}
void __fastcall deathHide(void* screen,int transition){
 originalDeathHide(screen,transition);
 auto expected=uintptr_t(screen);deathScreen.compare_exchange_strong(expected,0);
 try{log("ETERNAL_DEATH event=hide transition="+std::to_string(transition));}catch(...){}
}
void publishFrame(uintptr_t frame){
 if(!frame){
  if(storeConsumer){static std::atomic<bool> reported{};if(!reported.exchange(true))try{log("ETERNAL_PRESENTATION_STORE frame pointer unavailable");}catch(...){}}
  return;
 }
 Sample next;
 if(!decodeFrame([&](size_t offset,void* value,size_t size){SIZE_T got{};return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(frame+offset),value,size,&got)&&got==size;},GetTickCount64(),next)){
  if(storeConsumer){static std::atomic<bool> reported{};if(!reported.exchange(true))try{log("ETERNAL_PRESENTATION_STORE frame decode failed");}catch(...){}}
  return;
 }
 if(storeConsumer){static std::atomic<bool> reported{};if(!reported.exchange(true))try{log("ETERNAL_PRESENTATION_STORE first-frame valid="+std::to_string(next.valid)+" inGame="+std::to_string(next.inGame)+" paused="+std::to_string(next.paused)+" cutscene="+std::to_string(next.cutscene));}catch(...){}}
 if(!next.valid||!next.inGame){player=0;playerTick=0;upgradeScreen=0;dossierScreen=0;deathScreen=0;upgradeAnimationOwner=0;}
 {std::lock_guard<std::mutex> lock(pauseMutex);
  if(!next.valid||!next.inGame)pauseSession.reset();
  else next.paused=next.paused||pauseSession.active();
 }
 // +c9 is only the show/hide animation flag: it clears while the menu remains
 // open. Hold the native Show/Hide session, not that transient widget flag.
 next.upgradeMenu=upgradeScreen.load()!=0;
 next.dossierMenu=dossierScreen.load()!=0;
 next.deathMenu=deathScreen.load()!=0;
 refreshAnimationCamera();next.sync=syncAttack.load();
 if(extendedLogging()){
  static std::atomic<bool> lastDrone{};const bool active=droneAnimation.load();
  if(lastDrone.exchange(active)!=active)try{log(std::string("ETERNAL_DRONE_ANIMATION active=")+(active?"1":"0")+" source=modChangeAnimPlaying");}catch(...){}
 }
 next.interaction=interactionAnimation.load();next.monkeyBar=monkeyBarAnimation.load();next.meatHook=meatHookAnimation.load();
 auto owner=player.load();uintptr_t table{};
 // Hands-root transforms can stop during a sync animation. Validate the live
 // player's type instead of timing out that still-current owner after 250 ms.
 if(!revenant::active()&&owner&&read(owner,table)&&table==playerVtable.load()){
  uintptr_t sync{};int ledge{};
  // ManagedClassPtr's ptr member is +8. Native fields, not animation guesses.
  if(read(owner+0x7db0,sync))next.sync=next.sync||sync!=0;
  if(read(owner+0x2fb70+0x5278,ledge))next.ledge=activeLedge(ledge);
 }
 {std::lock_guard<std::mutex> lock(mutex);latest=next;}
 if(extendedLogging()){static std::atomic<uint64_t> count{};const auto sequence=++count;
  if(sequence==1)try{log("ETERNAL_PRESENTATION_FRAME address="+std::to_string(frame));}catch(...){}
  if(sequence==1||sequence%120==0)try{log("ETERNAL_PRESENTATION valid="+std::to_string(next.valid)+" inGame="+std::to_string(next.inGame)+" paused="+std::to_string(next.paused)+" deathMenu="+std::to_string(next.deathMenu)+" cutscene="+std::to_string(next.cutscene)+" sync="+std::to_string(next.sync)+" ledge="+std::to_string(next.ledge)+" interaction="+std::to_string(next.interaction)+" monkeyBar="+std::to_string(next.monkeyBar)+" meatHook="+std::to_string(next.meatHook));}catch(...){}
 }
}
void __fastcall consumeFrame(void* viewBuilder){
 // 17e8740 consumes gameFrameReturn_t from this+2a50 when assembling the
 // render views. Producer 439a60 only SCHEDULES the job; returning from it
 // does not mean the frame is populated. Read at this actual consumer.
 try{uintptr_t frame{};if(read(uintptr_t(viewBuilder)+0x2a50,frame))publishFrame(frame);}catch(...){}
 original(viewBuilder);
}

}
bool pauseRootVisible(){std::lock_guard<std::mutex> lock(pauseMutex);return pauseSession.active()&&pauseSession.rootVisible;}
bool install(unsigned char* image) noexcept {
 try{
 std::lock_guard<std::mutex> lock(presentationInstallationGuard);
 if(installedImage)return image==installedImage;
 const auto type=build::rva(0x2db5698);
 if(!image||!type)return false;
 for(const auto rva:{0xf88d10,0xf87400,0xf854e0,0xf83ab0,0xf37330,0xf36780,0xf38450,0xf38560,0x17e8740})if(!build::rva(rva))return false;
 // Called only after the camera installer has verified the complete EXE hash.
 constexpr unsigned char bytes[]={0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x20,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x49,0x8d,0xab,0xe8,0xfe,0xff,0xff,0x48,0x81,0xec,0xf0,0x01,0x00,0x00};
 constexpr unsigned char showBytes[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x30,0x8b,0xfa,0x48,0x8b,0xd9};
 constexpr unsigned char hideBytes[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x30,0x8b,0xda,0x48,0x8b,0xf9};
 constexpr unsigned char upgradeShowBytes[]={0x40,0x53,0x48,0x83,0xec,0x30,0x48,0x8b,0x41,0x10,0x48,0x8b,0xd9};
 constexpr unsigned char upgradeHideBytes[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9};
 constexpr unsigned char dossierOpenBytes[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9};
 constexpr unsigned char dossierCloseBytes[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20};
 constexpr unsigned char deathShowBytes[]={0x4c,0x8b,0xdc,0x53,0x56,0x57,0x48,0x81,0xec,0xa0,0x00,0x00,0x00};
 constexpr unsigned char deathHideBytes[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9};
 if(!image||std::memcmp(image+build::rva(0x17e8740),bytes,sizeof(bytes))||
    std::memcmp(image+build::rva(0xf88d10),showBytes,sizeof(showBytes))||
    std::memcmp(image+build::rva(0xf87400),hideBytes,sizeof(hideBytes))||
    std::memcmp(image+build::rva(0xf854e0),upgradeShowBytes,sizeof(upgradeShowBytes))||
    std::memcmp(image+build::rva(0xf83ab0),upgradeHideBytes,sizeof(upgradeHideBytes))||
    std::memcmp(image+build::rva(0xf38450),dossierOpenBytes,sizeof(dossierOpenBytes))||
    std::memcmp(image+build::rva(0xf38560),dossierCloseBytes,sizeof(dossierCloseBytes))||
    std::memcmp(image+build::rva(0xf37330),deathShowBytes,sizeof(deathShowBytes))||
    std::memcmp(image+build::rva(0xf36780),deathHideBytes,sizeof(deathHideBytes))){try{log("ETERNAL_PRESENTATION refused: native signature mismatch");}catch(...){}return false;}
 const std::array<PresentationHook,9> hooks{{
  {image+build::rva(0xf88d10),reinterpret_cast<void*>(&pauseShow),reinterpret_cast<void**>(&originalPauseShow),reinterpret_cast<void*>(originalPauseShow)},
  {image+build::rva(0xf87400),reinterpret_cast<void*>(&pauseHide),reinterpret_cast<void**>(&originalPauseHide),reinterpret_cast<void*>(originalPauseHide)},
  {image+build::rva(0xf854e0),reinterpret_cast<void*>(&upgradeShow),reinterpret_cast<void**>(&originalUpgradeShow),reinterpret_cast<void*>(originalUpgradeShow)},
  {image+build::rva(0xf83ab0),reinterpret_cast<void*>(&upgradeHide),reinterpret_cast<void**>(&originalUpgradeHide),reinterpret_cast<void*>(originalUpgradeHide)},
  {image+build::rva(0xf37330),reinterpret_cast<void*>(&deathShow),reinterpret_cast<void**>(&originalDeathShow),reinterpret_cast<void*>(originalDeathShow)},
  {image+build::rva(0xf36780),reinterpret_cast<void*>(&deathHide),reinterpret_cast<void**>(&originalDeathHide),reinterpret_cast<void*>(originalDeathHide)},
  {image+build::rva(0xf38450),reinterpret_cast<void*>(&dossierOpen),reinterpret_cast<void**>(&originalDossierOpen),reinterpret_cast<void*>(originalDossierOpen)},
  {image+build::rva(0xf38560),reinterpret_cast<void*>(&dossierClose),reinterpret_cast<void**>(&originalDossierClose),reinterpret_cast<void*>(originalDossierClose)},
  {image+build::rva(0x17e8740),reinterpret_cast<void*>(&consumeFrame),reinterpret_cast<void**>(&original),reinterpret_cast<void*>(original)}
 }};
 PresentationHookAttempt attempt{hooks,playerVtable.load(),storeConsumer.load()};
 for(const auto& hook:hooks){if(MH_CreateHook(hook.target,hook.detour,hook.original)!=MH_OK)return false;++attempt.created;}
 playerVtable=uintptr_t(image)+type;storeConsumer=build::microsoftStore;
 for(const auto& hook:hooks)if(MH_EnableHook(hook.target)!=MH_OK)return false;
 attempt.committed=true;installedImage=image;
 try{log("ETERNAL_DOSSIER source=native-lifecycle installed=1");}catch(...){}
 try{log("ETERNAL_DEATH source=native-lifecycle installed=1");}catch(...){}
 try{log("ETERNAL_UPGRADE source=modbot-native-lifecycle installed=1");}catch(...){}
 try{log("ETERNAL_PAUSE source=native-menu-lifecycle installed=1");}catch(...){}
 try{log("ETERNAL_PRESENTATION source=render-view-consumer installed=1");}catch(...){}
 return true;
 }catch(...){return false;}
}
}
