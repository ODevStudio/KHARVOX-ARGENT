#include "EternalBuildProfile.h"
#include "EternalPlayerHooks.h"
#include "weapon/EternalHapticsWeapon.h"
#include "Diagnostics.h"
#include "WeaponIdlePose.h"
#include "WeaponAttachmentCalibration.h"
#include "CrucibleRestPose.h"
#include "WeaponHandoffVisibility.h"
#include "WeaponZoomPolicy.h"
#include "MeleeWeaponVisibility.h"
#include "EternalCameraHook.h"
#include "LaserSightPolicy.h"
#include "FocusTrace.h"
#include "EquipmentAimPolicy.h"
#include "WallClimbAim.h"
#include "SwimmingAim.h"
#include "MeathookAim.h"
#include "MonkeyBarAim.h"
#include "EternalRenderControls.h"
#include "EternalCameraMath.h"
#include "EternalPresentation.h"
#include "QuadRuntime.h"
#include "openxr/ControllerInput.h"
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <mutex>
#include <map>
#include <optional>
extern "C" {void* argentHandsResume{};void argentHandsBridge();}
extern "C" {void* argentMeathookResume{};void argentMeathookBridge();}
extern "C" {void* argentMeathookGateResume{};void argentMeathookGateBridge();}
namespace {
struct MeathookQueryPose {void* weapon{};void* owner{};uint64_t tick{};float origin[3]{},forward[3]{};bool valid{};};
thread_local MeathookQueryPose meathookQueryPose;
}
extern "C" {void* argentFocusResume{};void argentFocusBridge();}
extern "C" {void* argentWallGateResume{};void* argentWallImpulseResume{};void argentWallGateBridge();void argentWallImpulseBridge();}
namespace argent::player {namespace {
unsigned char* image{};
using FindJoint=short*(__fastcall*)(void*,short*,const char*);
using Joint=bool(__fastcall*)(void*,void*,int,unsigned short,float*,float*);
using Surface=void(__fastcall*)(void*,int);
using Fire=void(__fastcall*)(void*,void*,void*,void*,void*,float*,float*);
Fire originalFire{};
using ItemTransform=bool(__fastcall*)(void*,void*,float*,float*);
ItemTransform originalItemTransform{};
using ThrowItem=void(__fastcall*)(void*,void*,void*,float,void*,const float*,const float*);
ThrowItem originalThrowItem{};
using UpdateHands=void(__fastcall*)(void*);
UpdateHands originalUpdateHands{};
using UpdateItemAnimation=void(__fastcall*)(void*,void*);
UpdateItemAnimation originalItemAnimation{};
using AttachmentJoint=bool(__fastcall*)(void*,void*,int,void*,float*,float*);
AttachmentJoint originalAttachmentJoint{};
struct CrucibleVisual {
 uintptr_t hands{},root{},owner{},context{};int dominant{-1};XrVector3f pivot{};
 CrucibleRestPose pose;bool hammer{};
};
std::mutex crucibleVisualGuard;
CrucibleVisual crucibleVisual;
thread_local uintptr_t animationItem{},animationHands{};
using CrucibleEvent=uintptr_t(__fastcall*)(void*,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
CrucibleEvent originalCrucibleEvents[3]{};
struct CrucibleEvents {uint64_t serial{};CrucibleRestPose::Events value;};
std::mutex crucibleEventsGuard;
std::map<uintptr_t,CrucibleEvents> crucibleEvents;
template<int Kind> uintptr_t __fastcall crucibleEvent(void* hands,uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e){
 const auto result=originalCrucibleEvents[Kind](hands,a,b,c,d,e);
 try{
 std::lock_guard<std::mutex> lock(crucibleEventsGuard);
 if(crucibleEvents.size()>32)crucibleEvents.clear();
 auto& events=crucibleEvents[uintptr_t(hands)];
 const auto serial=++events.serial;
 if constexpr(Kind==2)events.value.end=serial;
 else {events.value.begin=serial;events.value.tick=GetTickCount64();}
 if(extendedLogging())log("ETERNAL_CRUCIBLE nativeSwingEvent="+std::to_string(Kind)+" serial="+std::to_string(serial));
 }catch(...){}
 return result;
}
using ZoomBlend=void(__fastcall*)(void*,float);
using ZoomMode=int(__fastcall*)(void*);
ZoomBlend originalZoomBlend{};
ZoomMode originalZoomMode{};
using ZoomFov=float(__fastcall*)(void*);
ZoomFov originalZoomFov{};
using HideItem=void(__fastcall*)(void*);
HideItem originalHideItem{};
std::atomic<uintptr_t> zoomHands{},zoomWeapons[2]{};
std::atomic<ULONGLONG> zoomTick{};
std::atomic<uint64_t> transforms{},placed{},aimUpdates{},physicsReads{},missingPivot{},hidden{};
std::atomic<uintptr_t> placedHands{};std::atomic<ULONGLONG> placedTick{};
bool read(uintptr_t p,void* out,size_t n){SIZE_T done{};return p&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),out,n,&done)&&done==n;}
uintptr_t ptr(uintptr_t p){uintptr_t value{};return read(p,&value,sizeof(value))?value:0;}
using WaterMove=void(__fastcall*)(void*,void*);
WaterMove originalWaterMove{};
void __fastcall waterMove(void* object,void* context){
 const auto address=uintptr_t(object),owner=presentation::player.load();
 float saved[6]{},replacement[6]{},gravity[3]{};camera::Basis head{};
 // Entry is the native water-only dispatch (waterLevel > native threshold).
 // Never force a water state, velocity, buoyancy, damage, or collision result.
 const bool use=owner&&address==owner+0x8a50&&ptr(owner)==presentation::playerVtable.load()&&
  camera::swimmingView(head.data())&&read(address+0x469c,saved,sizeof(saved))&&
  read(address+0x4668,gravity,sizeof(gravity))&&std::abs(gravity[0])<.01f&&std::abs(gravity[1])<.01f&&std::abs(gravity[2]+1.f)<.01f&&
  swimmingBasis(saved+3,head.data(),replacement,replacement+3);
 if(!use){originalWaterMove(object,context);return;}
 {
  struct RestoreBasis {void* target;const float* previous;~RestoreBasis(){std::memcpy(target,previous,6*sizeof(float));}} restore{reinterpret_cast<void*>(address+0x469c),saved};
  std::memcpy(restore.target,replacement,sizeof(replacement));
  originalWaterMove(object,context);
 }
 static std::atomic<uint64_t> updates{};
 if(extendedLogging()&&updates.fetch_add(1)%120==0)try{log("ETERNAL_SWIMMING active=1 headZ="+std::to_string(head[2])+" forwardZ="+std::to_string(replacement[2])+" rightZ="+std::to_string(replacement[5])+" restore=1");}catch(...){}
}
struct WallHeading {uintptr_t mechanic{};ULONGLONG tick{};camera::Basis head{};};
thread_local WallHeading wallHeading;
bool localWallClimb(uintptr_t mechanic){
 const auto owner=presentation::player.load();int state{};
 return owner&&mechanic==owner+0x35130&&ptr(mechanic+0x18)==owner&&
  read(ptr(mechanic+0x138)+0xc,&state,sizeof(state))&&attachedWallClimbState(state);
}
std::atomic<KharvoxWeaponKind> hapticKind{KharvoxWeaponKind::Unknown};
std::atomic<ULONGLONG> hapticTick{};
std::atomic<bool> crucibleHeld{};
std::atomic<const char*> calibrationWeaponProfile{"default"};
struct WeaponIdentity {KharvoxWeaponKind kind{KharvoxWeaponKind::Unknown};bool crucible{},hammer{};char name[256]{};const char* profile{"default"};};
// MSVC x64 RTTI identifies the DLC weapon independently of localized/decl names.
// All reads are guarded; an unavailable or different native type stays unclassified.
bool sentinelHammerWeapon(uintptr_t weapon){
 uintptr_t table{},locator{};uint32_t signature{},typeRva{},selfRva{};
 if(!weapon||!read(weapon,&table,8)||table<8||!read(table-8,&locator,8)||!locator||
    !read(locator,&signature,4)||signature!=1||!read(locator+12,&typeRva,4)||
    !read(locator+20,&selfRva,4)||locator<selfRva)return false;
 const auto base=locator-selfRva;
 if(base!=uintptr_t(image)||typeRva>0x10000000)return false;
 char name[sizeof(".?AVidHammerWeapon@@")]{};
 return read(base+typeRva+16,name,sizeof(name))&&std::memcmp(name,".?AVidHammerWeapon@@",sizeof(name))==0;
}
WeaponIdentity readWeaponIdentity(void* hands){
 // Read the native primary handle after the game has resolved it. Never invoke
 // entity resolution from the XR thread or use the calibration profile.
 const auto handle=uintptr_t(hands)+0x29b0;
 uint32_t generation{},cached{};uintptr_t weapon{},decl{},table{},name{};
 WeaponIdentity identity;auto& text=identity.name;auto& kind=identity.kind;
 if(read(handle+0x30,&generation,4)&&read(handle+0x34,&cached,4)&&generation==cached&&generation!=0x1fffffe&&
    (weapon=ptr(handle+0x38))&&read(weapon+0x38,&decl,8)&&decl&&
    read(decl,&table,8)&&table==uintptr_t(image)+build::rva(0x2b1c348)&&read(decl+8,&name,8)&&name){
  char* end{};
  if(read(name,text,sizeof(text)-1))end=static_cast<char*>(std::memchr(text,0,sizeof(text)-1));
  else{
   std::memset(text,0,sizeof(text));
   for(size_t i=0;i<sizeof(text)-1;++i){if(!read(name+i,text+i,1))break;if(!text[i]){end=text+i;break;}}
  }
  if(end){std::memset(end,0,sizeof(text)-(end-text));kind=eternalHapticsWeapon(text);}
  else std::memset(text,0,sizeof(text));
 }
 identity.crucible=std::strcmp(text,"weapon/player/crucible")==0;
 identity.hammer=sentinelHammerWeapon(weapon);
 identity.profile=eternalCalibrationProfile(kind,identity.crucible,identity.hammer);return identity;
}
void updateHapticWeapon(void* hands){
 const auto identity=readWeaponIdentity(hands);const auto kind=identity.kind;const auto crucible=identity.crucible;const auto& text=identity.name;
 calibrationWeaponProfile=identity.profile;
 const auto previousCrucible=crucibleHeld.exchange(crucible);
 const auto previous=hapticKind.exchange(kind);hapticTick=GetTickCount64();
 if(previousCrucible!=crucible)try{log("ETERNAL_CRUCIBLE equipped="+std::to_string(crucible));}catch(...){}
 if(previous!=kind&&extendedLogging())try{log("ETERNAL_HAPTIC_WEAPON kind="+std::to_string(int(kind))+" decl="+text);}catch(...){}
}
// The only skipped call is GorillaBar entry at 0x1397c2c. The native
// completion query (0xb420e0) treats handle 0xffff as completed and advances
// its own FSM to the launch state. No physics/FSM timers are overwritten.
using PlayTraversal=bool(__fastcall*)(void*,void*,unsigned short*,bool,void*,void*,float);
PlayTraversal originalPlayTraversal{};
UpdateHands originalEndBar{};
bool __fastcall playTraversal(void* player,void* entity,unsigned short* handle,bool loop,void* anim,void* transform,float rate){
 const auto owner=uintptr_t(player);
 if(uintptr_t(_ReturnAddress())==uintptr_t(image)+build::rva(0x1397c31)&&owner==presentation::player.load()&&
    uintptr_t(handle)==owner+0x36e48+0x108&&(presentation::worldPresentation.load()||presentation::gameplayInput.load())){
  *handle=0xffff;
  presentation::skippedMonkeyBarOwner=owner;
  monkey::acceptedBar(owner);
  if(extendedLogging())try{log("ETERNAL_MONKEYBAR firstPersonAnimation=skipped completionHandle=65535");}catch(...){}
  return true;
 }
 return originalPlayTraversal(player,entity,handle,loop,anim,transform,rate);
}
void __fastcall endBar(void* mechanic){
 const auto owner=ptr(uintptr_t(mechanic)+0x18);
 originalEndBar(mechanic);
 if(uintptr_t(mechanic)==owner+0x36e48){
  auto expected=owner;
  if(presentation::skippedMonkeyBarOwner.compare_exchange_strong(expected,0)&&extendedLogging())
   try{log("ETERNAL_MONKEYBAR nativeExit=1");}catch(...){}
 }
}
void __fastcall hideItem(void* item){
 const auto hands=placedHands.load();int meleeType{};
 const bool fresh=GetTickCount64()-placedTick.load()<100&&camera::weaponContext()!=0;
 if(hands&&fresh&&uintptr_t(item)==hands+0x29b0&&read(hands+0x8db8,&meleeType,sizeof(meleeType))&&
    keepMeleeWeaponVisible(presentation::gameplayInput.load(),presentation::refreshSyncAttack(),
       presentation::scriptedMovement.load(),fresh,hands,uintptr_t(item),meleeType)){
  if(extendedLogging()){static std::atomic<unsigned> reports{};if(reports.fetch_add(1)<32)
   try{log("ETERNAL_MELEE weaponVisible=1 type="+std::to_string(meleeType)+" caller="+std::to_string(reinterpret_cast<uintptr_t>(_ReturnAddress())-uintptr_t(image)));}catch(...){} }
  return;
 }
 originalHideItem(item);
}
bool zoomPresentationActive(){
 const auto tick=zoomTick.load();
 return tick&&camera::weaponContext()!=0&&presentation::gameplayInput.load()&&!presentation::syncAttack.load()&&!presentation::nativeAnimation.load()&&!presentation::droneAnimation.load()&&
        !presentation::scriptedMovement.load()&&!presentation::monkeyBarAnimation.load()&&GetTickCount64()-tick<100;
}
void __fastcall zoomBlend(void* hands,float blend){
 originalZoomBlend(hands,vrWeaponZoomBlend(blend,zoomPresentationActive()&&zoomHands.load()==uintptr_t(hands)));
}
int __fastcall zoomMode(void* weapon){
 const int native=originalZoomMode(weapon);
 const bool ours=zoomWeapons[0].load()==uintptr_t(weapon)||zoomWeapons[1].load()==uintptr_t(weapon);
 return vrWeaponZoomMode(native,ours&&zoomPresentationActive());
}
float __fastcall zoomFov(void* weapon){
 const auto caller=build::semanticRva(reinterpret_cast<uintptr_t>(_ReturnAddress())-uintptr_t(image));
 const float native=originalZoomFov(weapon);
 // Other callers calculate zoom progress for weapon logic. Override only
 // the three verified player-view interpolation call sites.
 if(caller!=0x1462d36&&caller!=0x1462f6d&&caller!=0x1462f95)return native;
 const bool ours=zoomWeapons[0].load()==uintptr_t(weapon)||zoomWeapons[1].load()==uintptr_t(weapon);
 if(!ours||!zoomPresentationActive())return native;
 int integer{};float gameplay{};
 if(!camera::readRenderControl(uintptr_t(image),camera::renderControls[0],read,integer,&gameplay))return native;
 const float result=vrWeaponWorldFov(native,gameplay,true);
 if(extendedLogging()&&result!=native){static std::atomic<unsigned> reports{};if(reports.fetch_add(1)<16)
  try{log("ETERNAL_ALT_FIRE worldFovNative="+std::to_string(native)+" worldFovVR="+std::to_string(result));}catch(...){} }
 return result;
}
// Same presentation-only policy as KHARVOX's patchVrSniperPresentation.
// Eternal offsets and enum values are independently verified in its PE.
// No changes to zoomShootState, fire mode, ammunition or zoom timing.
bool prepareZoomDecl(uintptr_t decl) noexcept {
 __try {
  if(!decl||*reinterpret_cast<uintptr_t*>(decl)!=uintptr_t(image)+build::rva(0x2b1c348))return false;
  auto fov=*reinterpret_cast<float*>(decl+0x1700);
  auto& handsFov=*reinterpret_cast<float*>(decl+0x1704);
  auto& hide=*reinterpret_cast<unsigned char*>(decl+0x1750);
  auto& scope=*reinterpret_cast<int*>(decl+0x176c);
  auto& mode=*reinterpret_cast<int*>(decl+0x1788);
  if(!validWeaponZoomPresentation(fov,handsFov,hide,scope,mode))return false;
  auto& reticle=*reinterpret_cast<uintptr_t*>(decl+0x2160);
  const int replacement=vrWeaponZoomMode(mode,true);
  const float stableHandsFov=vrWeaponHandsFov(handsFov,fov,true);
  const bool changed=hide||scope||reticle||mode!=replacement||handsFov!=stableHandsFov;
  if(!changed)return false;
  hide=0;scope=0;reticle=0;mode=replacement;handsFov=stableHandsFov;
  // hideHandsOnZoomDelay is a 16-byte milliToGameTime_t in Eternal,
  // unlike DOOM 2016. Leave it intact: hideHandsOnZoom=false bypasses it.
  return changed;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool precisionBoltActive(void* hands) {
 const auto weapon=zoomWeapons[0].load();int selected=-1;
 if(!weapon||zoomHands.load()!=uintptr_t(hands)||!zoomPresentationActive()||!read(weapon+0x19e8,&selected,4)||selected!=1)return false;
 const auto handle=uintptr_t(hands)+0x29b0;uint32_t generation{},cached{};
 if(!read(handle+0x30,&generation,4)||!read(handle+0x34,&cached,4)||generation!=cached||generation==0x1fffffe||ptr(handle+0x38)!=weapon)return false;
 using Decl=void*(__fastcall*)(void*,int);
 const auto decl=reinterpret_cast<uintptr_t>(reinterpret_cast<Decl>(image+build::rva(0x16c0f90))(reinterpret_cast<void*>(weapon),1));
 constexpr char expected[]="weapon/player/heavy_cannon_bolt_action";char name[sizeof(expected)]{};
 return read(ptr(decl+8),name,sizeof(name))&&std::memcmp(name,expected,sizeof(name))==0;
}
void prepareZoom(void* hands,bool active) noexcept {
 zoomTick=0;zoomHands=0;zoomWeapons[0]=0;zoomWeapons[1]=0;
 if(!active)return;
 __try {
  using Resolve=void*(__fastcall*)(void*);
  using Decl=void*(__fastcall*)(void*,int);
  const size_t offsets[]={0x29b0,0x3728};
  for(int i=0;i<2;++i){
   auto handle=uintptr_t(hands)+offsets[i];
   if(!*reinterpret_cast<uintptr_t*>(handle+8))continue;
   auto weapon=reinterpret_cast<Resolve>(image+build::rva(0x135f230))(reinterpret_cast<void*>(handle));
   if(!weapon)continue;
   // Meathook is an alternate-fire traversal action. The Precision Bolt zoom
   // presentation work must not rewrite its declarations or reset its blend.
   if(*reinterpret_cast<uintptr_t*>(weapon)==uintptr_t(image)+build::rva(0x2e0dbc8)){
    if(i==0)return;
    continue;
   }
   zoomWeapons[i]=uintptr_t(weapon);
   for(int slot=0;slot<2;++slot){
    auto decl=reinterpret_cast<Decl>(image+build::rva(0x16c0f90))(weapon,slot);
    prepareZoomDecl(uintptr_t(decl));
   }
  }
  zoomHands=uintptr_t(hands);zoomTick=GetTickCount64();
  // Also clears a blend that was already active on entering immersive mode.
  originalZoomBlend(hands,0.f);
 }__except(EXCEPTION_EXECUTE_HANDLER){zoomTick=0;zoomHands=0;zoomWeapons[0]=0;zoomWeapons[1]=0;}
}
// Exact native accessor: idPlayer+8a50 -> physics vtable slot 78h, origin(0).
bool physics(void* hands,uintptr_t& owner,XrVector3f& origin){
 __try {
  owner=ptr(uintptr_t(hands)+0x358);if(!owner)return false;
  auto object=owner+0x8a50;auto fn=ptr(ptr(object)+0x78);
  if(fn<uintptr_t(image)||fn>=uintptr_t(image)+0x2a00000)return false;
  using GetOrigin=const float*(__fastcall*)(void*,int);
  const auto p=reinterpret_cast<GetOrigin>(fn)(reinterpret_cast<void*>(object),0);
  return read(uintptr_t(p),&origin,sizeof(origin))&&camera::validPosition(origin);
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool pivot(void* root,float* local){
 __try {
  auto model=ptr(uintptr_t(root)+0x4e0);
  auto skeleton=ptr(ptr(ptr(model+8)+0x80)+0x310);if(!skeleton)return false;
  short joint=-1;reinterpret_cast<FindJoint>(image+build::rva(0x19bfe00))(reinterpret_cast<void*>(skeleton),&joint,"righthandattach");
  if(joint<0)return false;
  float world[3]{},jointAxis[9]{},oldOrigin[3]{};camera::Basis oldAxis{};
  if(!read(uintptr_t(root)+0x158,oldOrigin,sizeof(oldOrigin))||!read(uintptr_t(root)+0x164,oldAxis.data(),sizeof(oldAxis))||!camera::validBasis(oldAxis))return false;
  if(!reinterpret_cast<Joint>(image+build::rva(0x1981990))(reinterpret_cast<void*>(model),root,1,static_cast<unsigned short>(joint),world,jointAxis))return false;
  for(int r=0;r<3;++r){local[r]=0;for(int k=0;k<3;++k)local[r]+=(world[k]-oldOrigin[k])*oldAxis[r*3+k];if(!std::isfinite(local[r])||std::abs(local[r])>2)return false;}
  return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Read the actual weapon child after native attachment/calibration updates.
// Eternal stores muzzle in the model definition's _info locator group, not
// in the skeleton's bone-name table. Follow the native FireWeapon locator
// path, then transform it through the current calibrated weapon root.
bool laserMuzzle(uintptr_t hands,float* origin,float* axis) noexcept {
 __try {
  const auto item=hands+0x29b0,root=ptr(item+0x78),model=ptr(root+0x4e0);
  unsigned char hidden{},flags{};uint64_t mask{};
  if(!root||!model||!read(item+0x54,&hidden,1)||hidden||!read(root+0xb0,&flags,1)||(flags&1)||
     !read(root+0x518,&mask,sizeof(mask))||!mask)return false;
  const auto definition=ptr(model+8),groups=ptr(definition+0x278);
  int groupCount{};if(!groups||!read(definition+0x280,&groupCount,4)||groupCount<1||groupCount>256)return false;
  uintptr_t locator{};
  for(int g=0;g<groupCount&&!locator;++g){
   const auto group=groups+g*0x150;char name[6]{};
   if(!read(ptr(group+8),name,sizeof(name))||std::memcmp(name,"_info",sizeof(name)))continue;
   const auto entries=ptr(group+0x130);int count{};
   if(!entries||!read(group+0x138,&count,4)||count<1||count>1024)return false;
   for(int n=0;n<count;++n){const auto entry=entries+n*0x28;
    if(read(ptr(entry),name,sizeof(name))&&!std::memcmp(name,"muzzle",sizeof(name))){
     // Include the terminator: never select muzzle_light or muzzle_burst.
     char end{};if(read(ptr(entry)+6,&end,1)&&!end){locator=entry+8;break;}
    }
   }
  }
  if(!locator)return false;
  using LocatorTransform=bool(__fastcall*)(void*,void*,int,const void*,float*,float*);
  if(!reinterpret_cast<LocatorTransform>(image+build::rva(0x1981bc0))(
   reinterpret_cast<void*>(model),reinterpret_cast<void*>(root),1,reinterpret_cast<const void*>(locator),origin,axis))return false;
  camera::Basis basis{};std::memcpy(basis.data(),axis,sizeof(basis));
  return camera::validBasis(basis)&&camera::validPosition({origin[0],origin[1],origin[2]});
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct RootVisibilityRequest {void* root{};bool ready{};};
thread_local std::array<RootVisibilityRequest,32> finalVisibility{};
thread_local size_t finalVisibilityCount{};
thread_local bool collectingVisibility{};
void rootVisibility(void*,void* root,bool hide){
 // Roots come from the verified transform or reflected idHands/idHandsItem fields.
 // Arms stay hidden in VR; weapon roots are hidden only while awaiting their
 // first VR pose. Keep each root's original mask independently.
 static std::mutex guard;std::lock_guard<std::mutex> lock(guard);
 struct Mask {uint64_t saved{};std::array<uintptr_t,3> identity{};bool held{};};
 static std::optional<std::map<uintptr_t,Mask>> storage;
 auto model=uintptr_t(root);
 // Native FindMesh reads render entity+4d8, HideMesh changes +518.
 // The animation-event receiver at +2a28 is not this UpdatePosition owner.
 std::array<uintptr_t,3> identity{};
 if(!model||!read(model,identity.data(),sizeof(uintptr_t))||identity[0]<uintptr_t(image)||identity[0]>=uintptr_t(image)+0x5000000||
    !read(model+0x4d8,identity.data()+1,2*sizeof(uintptr_t))){if(storage)storage->erase(model);return;}
 int surfaceCount{};const bool countRead=read(identity[1]+0x80,&surfaceCount,sizeof(surfaceCount));
 static unsigned visibilityReport{};if(extendedLogging()&&hide&&visibilityReport++%600==0)try{log("ETERNAL_ARMS currentRoot=1 surfaces="+std::to_string(surfaceCount)+" readable="+std::to_string(countRead));}catch(...){}
 // Match KHARVOX: clear every bit of the native 64-bit mesh mask.
 // Material/surface counts are not the visibility-mask capacity.
 surfaceCount=64;
 uint64_t visible{};if(!read(model+0x518,&visible,sizeof(visible)))return;
 if(!storage){if(!hide)return;try{storage.emplace();}catch(...){return;}}
 auto& masks=*storage;
 auto found=masks.find(model);
 if(found!=masks.end()&&found->second.identity!=identity){masks.erase(found);found=masks.end();}
 if(found==masks.end()){
  if(!hide)return;
  try{found=masks.try_emplace(model,Mask{0,identity,false}).first;}catch(...){return;}
 }
 auto& mask=found->second;
 if(hide){if(!mask.held){mask.saved=visible;mask.held=true;}for(int i=0;i<surfaceCount;++i)if(visible&(uint64_t(1)<<i))reinterpret_cast<Surface>(image+build::rva(0x19cf620))(reinterpret_cast<void*>(model),i);++hidden;}
 else if(mask.held){for(int i=0;i<surfaceCount;++i)if(mask.saved&(uint64_t(1)<<i))reinterpret_cast<Surface>(image+build::rva(0x19d0340))(reinterpret_cast<void*>(model),i);masks.erase(found);}
}
int equipmentItemSlot(void* item){
 int slot{},declSlot{};const auto definition=ptr(uintptr_t(item)+8);
 if(!item||!definition||!read(uintptr_t(item),&slot,sizeof(slot))||
    !read(definition+0x208,&declSlot,sizeof(declSlot))||slot!=declSlot)return 0;
 return headAimedEquipment(slot)?slot:0;
}
int equipmentVisualSlot(uintptr_t hands,size_t offset){
 // idHandsItem's cached weapon is at +38. Follow its verified definition
 // rather than hiding every item in the hands array or guessing by offset.
 const auto weapon=ptr(hands+offset+0x38);
 const auto definition=ptr(weapon+0x38);
 int slot{};
 return definition&&read(definition+0x208,&slot,sizeof(slot))&&headAimedEquipment(slot)?slot:0;
}
bool equipmentHead(uintptr_t owner,int slot,camera::Basis& head){
 float position[3]{};
 return owner&&owner==presentation::player.load()&&headAimedEquipment(slot)&&
  camera::hudCamera(position,head.data())&&camera::validBasis(head);
}
bool __fastcall itemTransform(void* item,void* hands,float* origin,float* axis){
 const bool valid=originalItemTransform(item,hands,origin,axis);
 if(!valid||!hands||!origin||!axis)return valid;
 const int slot=equipmentItemSlot(item);camera::Basis head{};
 // GetWeaponFireInfo caches this shoulder matrix internally before returning.
 // Its post-call output override alone misses that cache and ThrowItem's
 // independent joint path. Override the successful native source instead.
 if(applyEquipmentDirection(slot,equipmentHead(ptr(uintptr_t(hands)+0x358),slot,head),head,axis)){
  static std::atomic<unsigned> reported{};const unsigned bit=1u<<slot;
  if(!(reported.fetch_or(bit)&bit))try{log("ETERNAL_EQUIPMENT_TRANSFORM slot="+std::to_string(slot)+" source=HMD yawPitchRoll=1 origin=native");}catch(...){}
 }
 return valid;
}
void __fastcall throwItem(void* item,void* owner,void* entity,float force,void* extra,const float* origin,const float* axis){
 const int slot=equipmentItemSlot(item);camera::Basis head{},selected{};
 const bool use=origin&&axis&&applyEquipmentDirection(slot,equipmentHead(uintptr_t(owner),slot,head),head,selected.data());
 // Native throw copies this argument synchronously, normalizes the basis and
 // builds the projectile event. Keep origin, force and all other fields native.
 originalThrowItem(item,owner,entity,force,extra,origin,use?selected.data():axis);
 if(use){
  static std::atomic<unsigned> reported{};const unsigned bit=1u<<slot;
  if(!(reported.fetch_or(bit)&bit))try{log("ETERNAL_EQUIPMENT_THROW slot="+std::to_string(slot)+" source=HMD forwardZ="+std::to_string(selected[2])+" originForce=native");}catch(...){}
 }
}
void __fastcall fire(void* hands,void* weapon,void* info,void* muzzleOrigin,void* muzzleAxis,float* origin,float* axis){
 originalFire(hands,weapon,info,muzzleOrigin,muzzleAxis,origin,axis);
 if(!hands||!weapon||!info||!muzzleOrigin||!muzzleAxis||!origin||!axis)return;
 // GetWeaponFireInfo itself reads weapon->definition (+38)->equipSlot (+208).
 // Restrict the override to our local player's hands, including during startup.
 const auto owner=ptr(uintptr_t(hands)+0x358);
 int slot{};const auto definition=ptr(uintptr_t(weapon)+0x38);
 if(!owner||owner!=presentation::player.load()||!definition||
    !read(definition+0x208,&slot,sizeof(slot))||slot<=0||slot>=11)return;
 camera::Basis head{},controller{};float position[3]{};bool headReady=false,controllerReady=false;
 if(headAimedEquipment(slot)){
  // Same tracked view as the interaction trace. Independent of controller
  // tracking and placedHands; missing HMD data must never borrow weapon aim.
  headReady=camera::hudCamera(position,head.data());
 }else{
  const auto sample=input::snapshot();
  controllerReady=placedHands.load()==uintptr_t(hands)&&GetTickCount64()-placedTick.load()<100&&
   origin&&sample.weaponValid&&input::fresh(sample,GetTickCount64())&&
   camera::controllerPlacement(sample.weapon,position,controller.data());
 }
 const auto source=applyFireDirection(slot,headReady,head,controllerReady,controller,static_cast<float*>(muzzleAxis),axis);
 if(source!=AimSource::Native)++aimUpdates;
 if(source==AimSource::Head){
  static std::atomic<unsigned> reported{};const unsigned bit=1u<<slot;
  if(!(reported.fetch_or(bit)&bit))try{log("ETERNAL_EQUIPMENT_AIM slot="+std::to_string(slot)+" source=HMD origins=native");}catch(...){}
 }
}
bool __fastcall attachmentJoint(void* model,void* root,int mode,void* joint,float* origin,float* axis){
 const auto result=originalAttachmentJoint(model,root,mode,joint,origin,axis);
 // This one caller attaches a weapon child to the animated hands. Do not
 // substitute skeleton samples or event tracks, or modify the parent root.
 if(!result||uintptr_t(_ReturnAddress())!=uintptr_t(image)+build::rva(0x138a64e)||
    !animationHands||animationItem!=animationHands+0x29b0)return result;
 const auto owner=ptr(animationHands+0x358),child=ptr(animationItem+0x78);
 if(!owner||owner!=presentation::player.load())return result;
 const auto sample=input::weaponSample(),controls=input::snapshot();
 const auto weaponTick=hapticTick.load(),placementTick=placedTick.load();
 const auto now=GetTickCount64();
 const auto context=camera::weaponContext();
 const auto identity=readWeaponIdentity(reinterpret_cast<void*>(animationHands));
 const bool sameWeapon=matchingWeaponCalibration(sample.weaponProfile.data(),identity.profile);
 const bool vrReady=origin&&axis&&child&&context&&sameWeapon&&uintptr_t(root)==ptr(animationHands+0x370)&&
  placedHands.load()==animationHands&&now-placementTick<100&&input::fresh(sample,now)&&sample.weaponValid&&
  presentation::gameplayInput.load()&&!presentation::refreshSyncAttack()&&!presentation::refreshAnimationCamera()&&
  !presentation::scriptedMovement.load()&&!presentation::droneAnimation.load()&&!presentation::monkeyBarAnimation.load();
 if(sample.weaponCorrection&&vrReady){
  float fromPosition[3]{},toPosition[3]{},fromAxis[9]{},toAxis[9]{};
  if(camera::controllerPlacement(sample.weaponBase,fromPosition,fromAxis)&&
     camera::controllerPlacement(sample.weapon,toPosition,toAxis)){
   const bool applied=calibrateWeaponAttachment(fromPosition,fromAxis,toPosition,toAxis,origin,axis);
   if(applied&&extendedLogging()){static thread_local uint64_t last{};if(now-last>=1000){last=now;try{log("ETERNAL_WEAPON_CALIBRATION target="+std::string(identity.profile)+" attachmentOnly=1 sharedRoot=baseline");}catch(...){} }}
  }
 }
 std::lock_guard<std::mutex> lock(crucibleVisualGuard);
 auto& visual=crucibleVisual;
 if(visual.hands!=animationHands||visual.root!=child||visual.owner!=owner||visual.context!=context||
    visual.hammer!=identity.hammer||visual.dominant!=sample.dominant||std::memcmp(&visual.pivot,&sample.weaponPivot,sizeof(visual.pivot))){
  visual={};visual.hands=animationHands;visual.root=child;visual.owner=owner;visual.context=context;
  visual.hammer=identity.hammer;visual.dominant=sample.dominant;visual.pivot=sample.weaponPivot;
 }
 int state{-1},pendingAction{-1};unsigned char hit{};float hand[3]{},desired[9]{};
 if((identity.crucible||identity.hammer)&&owner==presentation::player.load()){
  using HandsState=int(__fastcall*)(void*,bool);
  state=reinterpret_cast<HandsState>(image+build::rva(0x135f130))(reinterpret_cast<void*>(animationHands),false);
  read(animationHands+0x8cd0,&pendingAction,sizeof(pendingAction));
 }
 const bool eligible=vrReady&&owner==presentation::player.load()&&(identity.crucible||identity.hammer)&&
  now-weaponTick<100&&input::fresh(controls,now)&&
  read(animationHands+0x8cb0,&hit,1)&&!hit&&
  camera::controllerPlacement(sample.weapon,hand,desired,&sample);
 CrucibleRestPose::Events events;
 {std::lock_guard<std::mutex> lock(crucibleEventsGuard);const auto it=crucibleEvents.find(animationHands);
  if(it!=crucibleEvents.end())events=it->second.value;}
 const bool replace=identity.hammer
  ?visual.pose.selectPhysical(now,controls.crucibleSwing,controls.manualWeaponTrigger,eligible,state==0)
  :visual.pose.select(now,controls.crucibleSwing,controls.manualWeaponTrigger,eligible,state==0,events);
 if(replace){
  visual.pose.apply(hand,desired,origin,axis);
  if(extendedLogging()){static uint64_t last{};if(now-last>500){last=now;
   try{log("ETERNAL_CRUCIBLE restAttachment=1 state="+std::to_string(state)+" source=velocity nativeEvents=preserved profile="+std::string(identity.profile));}catch(...){} }}
 }else if(eligible&&visual.pose.canCapture(now)){
  const bool had=visual.pose.cached;
  if(visual.pose.capture(hand,desired,origin,axis)&&!had&&extendedLogging())try{log("ETERNAL_CRUCIBLE restCaptured=1 animState="+std::to_string(state));}catch(...){}
 }
 if((identity.crucible||identity.hammer)&&extendedLogging()){
  static uint64_t last{};
  if(now-last>=500){last=now;try{log("ETERNAL_CRUCIBLE poseCheck eligible="+std::to_string(eligible)+" cached="+std::to_string(visual.pose.cached)+
   " applied="+std::to_string(replace)+" animState="+std::to_string(state)+" pendingAction="+std::to_string(pendingAction)+
   " begin="+std::to_string(events.begin)+" end="+std::to_string(events.end)+" hit="+std::to_string(hit)+" manual="+std::to_string(controls.manualWeaponTrigger)+
   " gameplay="+std::to_string(presentation::gameplayInput.load())+" context="+std::to_string(context)+
   " inputFresh="+std::to_string(input::fresh(controls,now))+" poseFresh="+std::to_string(input::fresh(sample,now))+
   " poseValid="+std::to_string(sample.weaponValid)+" placedAge="+std::to_string(now-placementTick));}catch(...){} }
 }
 return result;
}
// Capture again after the native per-item animation/attachment pass. This may
// run after UpdateHands; the earlier capture then contains the previous root.
void publishAnimatedLaser(void* item,void* hands){
 if(!hands||uintptr_t(item)!=uintptr_t(hands)+0x29b0||placedHands.load()!=uintptr_t(hands))return;
 const auto now=GetTickCount64();const auto sample=input::snapshot();
 const auto identity=readWeaponIdentity(hands);
 float origin[3]{},axis[9]{};
 const bool eligible=sample.laserSight&&input::fresh(sample,now)&&now-placedTick.load()<100&&
  presentation::gameplayInput.load()&&!presentation::nativeAnimation.load()&&
  !presentation::syncAttack.load()&&!presentation::scriptedMovement.load()&&
  !presentation::droneAnimation.load()&&!presentation::monkeyBarAnimation.load()&&
  !presentation::attachedWallClimb()&&!revenant::active()&&
  laserWeaponAllowed(kharvox::hands::handProfile(identity.profile));
 const bool found=eligible&&laserMuzzle(uintptr_t(hands),origin,axis);
 camera::publishLaser(found?origin:nullptr,found?axis:nullptr,identity.profile);
 if(eligible&&extendedLogging()){
  static thread_local uint64_t last{};if(now-last>=2000){last=now;
   try{log("ETERNAL_LASER phase=after-item-animation nativeMuzzle="+std::to_string(found)+" profile="+identity.profile);}catch(...){} }
 }
}
void __fastcall updateItemAnimation(void* item,void* hands){
 {
  struct RestoreAnimation {const uintptr_t item,hands;~RestoreAnimation(){animationItem=item;animationHands=hands;}} restore{animationItem,animationHands};
  animationItem=uintptr_t(item);animationHands=uintptr_t(hands);
  originalItemAnimation(item,hands);
 }
 publishAnimatedLaser(item,hands);
 // Publish only after native attachment AND skeletal animation have finished.
 // Native Render::Show queues the entity in its render world's update bitset.
 if(!hands||uintptr_t(item)!=uintptr_t(hands)+0x29b0||!precisionBoltActive(hands)||
    placedHands.load()!=uintptr_t(hands)||GetTickCount64()-placedTick.load()>=100||
    presentation::nativeAnimation.load()||presentation::scriptedMovement.load()||
    presentation::monkeyBarAnimation.load())return;
 const auto root=ptr(uintptr_t(item)+0x78);
 if(root){
  reinterpret_cast<HideItem>(image+build::rva(0x18dbf20))(reinterpret_cast<void*>(root));
  if(extendedLogging()){static std::atomic<unsigned> reports{};if(reports.fetch_add(1)<16)
   try{log("ETERNAL_PRECISION_BOLT renderPublish=1 phase=after-item-animation");}catch(...){} }
 }
}
void __fastcall updateHands(void* hands){
 struct RestoreVisibility {
  const bool collecting=collectingVisibility;const size_t count=finalVisibilityCount;std::array<RootVisibilityRequest,32> pending;
  RestoreVisibility(){if(collecting)pending=finalVisibility;}
  ~RestoreVisibility(){collectingVisibility=collecting;if(collecting){finalVisibility=pending;finalVisibilityCount=count;}}
 } restoreVisibility;
 collectingVisibility=false;
 camera::publishLaser(nullptr,nullptr,nullptr);
 if(revenant::refresh(presentation::player.load(),presentation::playerVtable.load())){originalUpdateHands(hands);return;}
 uintptr_t ownerNow{};XrVector3f foot{};
 if(physics(hands,ownerNow,foot)){
  camera::publishPhysics(ownerNow,foot);presentation::player=ownerNow;presentation::playerTick=GetTickCount64();
  presentation::refreshSyncAttack();
 }
 // Refresh ownership before the native hands update, not one render frame
 // later: its first post-animation transform may already be the flat rig.
 presentation::refreshAnimationCamera();
 prepareZoom(hands,presentation::gameplayInput.load()&&!presentation::syncAttack.load()&&!presentation::nativeAnimation.load()&&!presentation::droneAnimation.load()&&camera::weaponContext()!=0);
 // Hidden items can skip native attachment updates. Reveal BEFORE the native
 // position pass, not after it with the preceding frame's attachment transform.
 const auto controllerSample=input::snapshot();
 const bool zoomPoseReady=controllerSample.weaponValid&&input::fresh(controllerSample,GetTickCount64());
 if(keepPrecisionBoltVisible(presentation::gameplayInput.load()&&!presentation::nativeAnimation.load()&&
      !presentation::syncAttack.load()&&!presentation::scriptedMovement.load()&&!presentation::droneAnimation.load()&&
      !presentation::monkeyBarAnimation.load(),zoomPoseReady,zoomHands.load()==uintptr_t(hands),precisionBoltActive(hands),1)){
  // The weapon remains attached to the native hands animation entity. Native
  // scope hides that parent as well; showing just the child leaves its joint
  // animation/attachment source hidden. Keep arm meshes masked independently.
  const auto animationRoot=ptr(uintptr_t(hands)+0x370);unsigned char animationFlags{};
  if(animationRoot&&read(animationRoot+0xb0,&animationFlags,1)&&(animationFlags&1)){
   rootVisibility(hands,reinterpret_cast<void*>(animationRoot),true);
   reinterpret_cast<HideItem>(image+build::rva(0x18dbf20))(reinterpret_cast<void*>(animationRoot));
   if(extendedLogging()){static std::atomic<unsigned> reports{};if(reports.fetch_add(1)<16)
    try{log("ETERNAL_PRECISION_BOLT animationParentShow=1 armMeshes=masked");}catch(...){} }
  }
  const auto item=uintptr_t(hands)+0x29b0;unsigned char itemHidden{},rootFlags{};
  const auto root=ptr(item+0x78);
  if(root&&read(item+0x54,&itemHidden,1)&&read(root+0xb0,&rootFlags,1)&&(itemHidden||(rootFlags&1))){
   reinterpret_cast<HideItem>(image+build::rva(0x138a0e0))(reinterpret_cast<void*>(item));
   if(extendedLogging()){static std::atomic<unsigned> reports{};if(reports.fetch_add(1)<16)
    try{log("ETERNAL_PRECISION_BOLT nativeShow=1 phase=before-position freshController=1");}catch(...){} }
  }
 }
 finalVisibilityCount=0;collectingVisibility=true;
 originalUpdateHands(hands);
 collectingVisibility=false;
 updateHapticWeapon(hands);
 // UpdatePosition can change mesh visibility after the transform callback.
 // Enforce the handoff mask once native updates have finished.
 // Native UpdatePosition can finish the animation AFTER our first state read
 // or the transform callback. Never replay the callback's stale permission to
 // show the flat rig. Only a root actually placed in VR may be revealed.
 const bool finalNative=presentation::refreshAnimationCamera();
 const bool finalVr=presentation::worldPresentation.load()||presentation::gameplayInput.load();
 size_t waiting{};
 for(size_t i=0;i<finalVisibilityCount;++i){
  const auto request=finalVisibility[i];
  const bool arms=uintptr_t(request.root)==ptr(uintptr_t(hands)+0x370);
  rootVisibility(hands,request.root,hideFirstPersonRoot(finalVr,finalNative,request.ready,arms,presentation::droneAnimation.load()||presentation::monkeyBarAnimation.load()));
  if(finalVr&&!finalNative&&!request.ready)++waiting;
 }
 // The alternate native UpdatePosition branch bypasses our transform hook.
 // Enforce root visibility even when it delivered no transform callback.
 const bool suppress=presentation::droneAnimation.load()||presentation::monkeyBarAnimation.load();
 const bool hideNativeRig=hideFirstPersonRoot(finalVr,finalNative,false,true,suppress);
 rootVisibility(hands,reinterpret_cast<void*>(ptr(uintptr_t(hands)+0x370)),hideNativeRig);
 // The authored first-person sequence uses idPlayer::thirdPersonBody, a
 // separate actor. Live r041: fp_hands mask=0, this actor mask=0x73.
 // Resolve only a generation-matched cached handle and verified getters.
 const auto playerOwner=ptr(uintptr_t(hands)+0x358);
 uint32_t generation{},cached{};
 if(playerOwner&&read(playerOwner+0x7d88,&generation,4)&&read(playerOwner+0x7d8c,&cached,4)&&
    generation==cached&&generation!=0x1fffffe){
  const auto actor=ptr(playerOwner+0x7d90),table=ptr(actor);
  if(actor&&ptr(table+0xa8)==uintptr_t(image)+build::rva(0x6e5250)&&ptr(table+0xf0)==uintptr_t(image)+build::rva(0x6e52d0))
   rootVisibility(hands,reinterpret_cast<void*>(ptr(actor+0x2c0)),hideNativeRig);
 }
 bool readyThisUpdate=false;
 for(size_t i=0;i<finalVisibilityCount;++i)readyThisUpdate|=finalVisibility[i].ready;
 const bool hideItems=hideFirstPersonRoot(finalVr,finalNative,readyThisUpdate,false,suppress);
 for(const auto offset:{0x29b0,0x3728,0x44a0,0x5218,0x5f90,0x6d08,0x7a80}){
  const int slot=offset>=0x5218&&playerOwner==presentation::player.load()?equipmentVisualSlot(uintptr_t(hands),offset):0;
  const bool hideVisual=hideEquipmentVisual(slot,finalVr,finalNative);
  rootVisibility(hands,reinterpret_cast<void*>(ptr(uintptr_t(hands)+offset+0x78)),hideItems||hideVisual);
  if(hideVisual&&extendedLogging()){
   static std::atomic<unsigned> reported{};const unsigned bit=1u<<slot;
   if(!(reported.fetch_or(bit)&bit))try{log("ETERNAL_EQUIPMENT_VISUAL slot="+std::to_string(slot)+" mesh=hidden projectileEffects=native");}catch(...){}
  }
 }
 // Source belongs to this camera-history frame, never a later XR action poll.
 const auto laserProfile=weaponProfile();float laserOrigin[3]{},laserAxis[9]{};
 const bool laserReady=controllerSample.laserSight&&input::fresh(controllerSample,GetTickCount64())&&
  readyThisUpdate&&!hideItems&&!finalNative&&!suppress&&presentation::gameplayInput.load()&&
  !presentation::syncAttack.load()&&!presentation::scriptedMovement.load()&&!presentation::attachedWallClimb()&&
  !revenant::active()&&laserWeaponAllowed(kharvox::hands::handProfile(laserProfile));
 const bool laserFound=laserReady&&laserMuzzle(uintptr_t(hands),laserOrigin,laserAxis);
 camera::publishLaser(laserFound?laserOrigin:nullptr,laserFound?laserAxis:nullptr,laserProfile);
 if(laserReady&&extendedLogging()){
  static thread_local uint64_t last{};const auto now=GetTickCount64();if(now-last>2000){last=now;
   try{log("ETERNAL_LASER nativeMuzzle="+std::to_string(laserFound)+" profile="+laserProfile+" cameraHistory=1");}catch(...){} }
 }
 if(extendedLogging()){
  static std::atomic<int> previous{-1};const int state=finalNative?0:(waiting?1:2);
  if(state!=previous.exchange(state))try{log("ETERNAL_WEAPON_HANDOFF state="+std::to_string(state)+" roots="+std::to_string(finalVisibilityCount)+" awaitingPose="+std::to_string(waiting));}catch(...){}
 }
 if(placedHands.load()!=uintptr_t(hands)||GetTickCount64()-placedTick.load()>=100||!presentation::gameplayInput.load()||presentation::syncAttack.load()||presentation::nativeAnimation.load()||presentation::droneAnimation.load())return;
 // Both projection factors are written by UpdatePosition after placement.
 // Use the native player-view accessor, exactly as the original function does.
 auto owner=ptr(uintptr_t(hands)+0x358);if(!owner)return;
 using View=void*(__fastcall*)(void*);
 auto view=reinterpret_cast<View>(image+build::rva(0x13e88c0))(reinterpret_cast<void*>(owner));
 if(view){auto factors=reinterpret_cast<float*>(uintptr_t(view)+0x3890);factors[0]=1.f;factors[1]=1.f;}
}
}
KharvoxWeaponKind hapticWeapon() noexcept {
 return GetTickCount64()-hapticTick.load()<500?hapticKind.load():KharvoxWeaponKind::Unknown;
}
bool crucibleEquipped() noexcept {return GetTickCount64()-hapticTick.load()<100&&crucibleHeld.load();}
const char* weaponProfile() noexcept {return GetTickCount64()-hapticTick.load()<100?calibrationWeaponProfile.load():"default";}
#include "PlayerHookInstallation.inc"
}
extern "C" bool argentWallClimbGate(void* mechanic,bool nativeBlocked) noexcept {try{
 using namespace argent;using namespace argent::player;
 wallHeading={};const auto address=uintptr_t(mechanic);
 if(!localWallClimb(address))return nativeBlocked;
 camera::Basis head{};float native[3]{},normal[3]{},forward[3]{},angle{};bool blocked{};
 if(!camera::wallClimbView(head.data())||!read(address+0x1380,native,sizeof(native))||
    !read(address+0x140c,normal,sizeof(normal))||!read(address+0x172c,&angle,sizeof(angle))||
    !wallClimbForward(native,head.data(),forward)||!wallClimbBlocked(forward,normal,angle,blocked))return nativeBlocked;
 if(!blocked)wallHeading={address,GetTickCount64(),head};
 return blocked;
 }catch(...){return nativeBlocked;}}
extern "C" void argentWallClimbImpulse(void* mechanic,float* xy,float* z,uintptr_t caller) noexcept {try{
 using namespace argent;using namespace argent::player;
 const auto saved=wallHeading;wallHeading={};const auto address=uintptr_t(mechanic);
 // Only the input-driven jump branch, immediately following the same gate.
 // Forced detaches, ledge handoffs and unrelated callers remain native.
 if(caller!=uintptr_t(image)+build::rva(0x13bc530)+0x63||saved.mechanic!=address||
    GetTickCount64()-saved.tick>=100||!localWallClimb(address)||!xy||!z)return;
 const float native[]{xy[0],xy[1],*z};float forward[3]{};
 if(!wallClimbForward(native,saved.head.data(),forward))return;
 xy[0]=forward[0];xy[1]=forward[1];*z=forward[2]; // Saved XMM0 low lanes and EAX/Z.
 log("ETERNAL_WALLCLIMB jump=HMD heading="+std::to_string(std::atan2(forward[1],forward[0])*57.2957795131f)+" headZ="+std::to_string(*z));
 }catch(...){} }
extern "C" void argentFocusTransform(void* tracker,void* owner) noexcept {try{
 using namespace argent;
 const auto player=presentation::player.load();
 if(!player||uintptr_t(owner)!=player||uintptr_t(tracker)!=player+0x167f8)return;
 float origin[3]{},axis[9]{};
 if(!camera::hudCamera(origin,axis))return;
 auto bytes=static_cast<unsigned char*>(tracker);
 if(camera::headFocusTrace(reinterpret_cast<float*>(bytes+0x1f8),reinterpret_cast<float*>(bytes+0x204),reinterpret_cast<float*>(bytes+0x210),origin,axis)){
  static std::atomic<bool> reported{};
  if(!reported.exchange(true))log("ETERNAL_FOCUS applied=1 player=local nearFar=native");
 }
 }catch(...){}
}
extern "C" bool argentMeathookTargetView(void* weapon,void* owner,float* origin,float* angles) noexcept {try{
 meathookQueryPose.valid=false;
 using namespace argent;using namespace argent::player;
 const auto player=presentation::player.load(),object=uintptr_t(weapon);
 if(!player||uintptr_t(owner)!=player||!origin||!angles||
    ptr(object)!=uintptr_t(image)+build::rva(0x2e0dbc8)||
    !presentation::gameplayInput.load()||presentation::nativeAnimation.load()||
    presentation::syncAttack.load()||revenant::active())return false;
 // The native query also serves other actors/weapons. Require the generation-
 // matched local primary handle; never resolve or change its target here.
 const auto handle=player+0xd2c8+0x29b0;uint32_t generation{},cached{};
 if(!read(handle+0x30,&generation,4)||!read(handle+0x34,&cached,4)||
    generation!=cached||generation==0x1fffffe||ptr(handle+0x38)!=object)return false;
 const auto sample=input::snapshot();const auto now=GetTickCount64();
 if(!sample.weaponValid||!input::fresh(sample,now)||
    std::strcmp(sample.weaponProfile.data(),"super_shotgun"))return false;
 float position[3]{},replacement[3]{};camera::Basis basis{};
 if(!camera::controllerPlacement(sample.weapon,position,basis.data())||
    !meathookViewAngles(basis,replacement))return false;
 std::memcpy(origin,position,sizeof(position));std::memcpy(angles,replacement,sizeof(replacement));
 meathookQueryPose.weapon=weapon;meathookQueryPose.owner=owner;meathookQueryPose.tick=now;
 std::memcpy(meathookQueryPose.origin,position,sizeof(position));std::memcpy(meathookQueryPose.forward,basis.data(),sizeof(position));
 meathookQueryPose.valid=true;
 if(extendedLogging()){static std::atomic<uint64_t> last{};auto previous=last.load();unsigned char targeting{};read(object+0x23d0,&targeting,1);
  if(now-previous>=500&&last.compare_exchange_strong(previous,now))
   try{log("ETERNAL_MEATHOOK viewApplied=1 pitch="+std::to_string(replacement[0])+" yaw="+std::to_string(replacement[1])+" targetingActive="+std::to_string(targeting));}catch(...){} }
 return true;
 }catch(...){return false;}}
extern "C" bool argentMeathookCandidateView(void* weapon,void* owner,float* origin,float* forward) noexcept {try{
 // Use the exact pose that seeded this thread's query, not a newer XR sample
 // for every candidate. Failed/unrelated initial queries invalidate this cache.
 const auto& pose=meathookQueryPose;
 if(!origin||!forward||!pose.valid||pose.weapon!=weapon||pose.owner!=owner||GetTickCount64()-pose.tick>=100)return false;
 std::memcpy(origin,pose.origin,sizeof(pose.origin));std::memcpy(forward,pose.forward,sizeof(pose.forward));
 if(argent::extendedLogging()){static std::atomic<uint64_t> last{};const auto now=GetTickCount64();auto previous=last.load();
  if(now-previous>=500&&last.compare_exchange_strong(previous,now))try{argent::log("ETERNAL_MEATHOOK candidateViewApplied=1");}catch(...){} }
 return true;
 }catch(...){return false;}}
extern "C" void argentHandsTransform(void* hands,void* root,float* origin,float* axis) noexcept {try{
 using namespace argent;using namespace argent::player;
 if(revenant::active())return;
 ++transforms;uintptr_t owner{};XrVector3f foot{};
 if(physics(hands,owner,foot)){camera::publishPhysics(owner,foot);presentation::player=owner;presentation::playerTick=GetTickCount64();++physicsReads;}
 const auto sample=input::snapshot();float hand[3]{},desired[9]{},local[3]{};
 // Re-read after native animation evaluation. The pre-update sample can still
 // say 'authored' when this very callback already contains a flat idle pose.
 const bool nativeAnimation=presentation::refreshAnimationCamera();
 bool ready=!nativeAnimation&&sample.weaponValid&&input::fresh(sample,GetTickCount64());
 auto controller=sample.weaponCorrection?sample.weaponBase:sample.weapon;
 if(ready){ready=pivot(root,local);if(!ready)++missingPivot;}
 ready=ready&&camera::controllerPlacement(controller,hand,desired,&sample);
 if(ready){
  // Move the attachment point in model space, then rotate it with the weapon.
  // The controller/VR hand stays at hand[]; do not offset the XR input pose.
  local[0]+=sample.weaponPivot.x*camera::unitsPerMeter();
  local[1]+=sample.weaponPivot.y*camera::unitsPerMeter();
  local[2]+=sample.weaponPivot.z*camera::unitsPerMeter();
  for(int k=0;k<3;++k){origin[k]=hand[k];for(int r=0;r<3;++r)origin[k]-=local[r]*desired[r*3+k];}std::memcpy(axis,desired,sizeof(desired));input::weaponApplied(sample);++placed;}
 const auto context=camera::weaponContext();
 const bool gameplay=presentation::gameplayInput.load(),cacheActive=context&&gameplay&&!nativeAnimation;
 static std::mutex idleGuard;static uintptr_t savedOwner{};static uint64_t savedContext{};
 try{std::lock_guard<std::mutex> lock(idleGuard);
  static std::map<uintptr_t,WeaponIdlePose> idle;
  if(owner!=savedOwner||context!=savedContext||!gameplay||idle.size()>32){idle.clear();savedOwner=owner;savedContext=context;}
  if(cacheActive)ready=idle[uintptr_t(root)].apply(owner,uintptr_t(hands),uintptr_t(root),true,ready,origin,axis,ptr(uintptr_t(hands)+0x29b0+0x38));
  else{idle.erase(uintptr_t(root));ready=false;}
 }catch(...){ready=ready&&cacheActive;}
 if(ready){placedHands=uintptr_t(hands);placedTick=GetTickCount64();}else if(placedHands.load()==uintptr_t(hands)){placedHands=0;placedTick=0;}
 const bool vrRequested=presentation::worldPresentation.load()||presentation::gameplayInput.load();
 if(collectingVisibility&&finalVisibilityCount<finalVisibility.size())finalVisibility[finalVisibilityCount++]={root,ready};
 rootVisibility(hands,root,hideFirstPersonRoot(vrRequested,nativeAnimation,ready,uintptr_t(root)==ptr(uintptr_t(hands)+0x370),presentation::droneAnimation.load()||presentation::monkeyBarAnimation.load()));
 if(extendedLogging()&&transforms%120==0)log("ETERNAL_PLAYER updates="+std::to_string(transforms.load())+" placed="+std::to_string(placed.load())+" physics="+std::to_string(physicsReads.load())+" missingPivot="+std::to_string(missingPivot.load())+" aimUpdates="+std::to_string(aimUpdates.load())+" hidden="+std::to_string(hidden.load()));
 }catch(...) {}}
