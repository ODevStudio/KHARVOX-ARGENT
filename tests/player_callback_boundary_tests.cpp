#include <windows.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "../src/EternalCameraHook.h"
#include "../src/EternalBuildProfile.h"

namespace {
const std::array<float,9> headBasis{0,.8f,.6f,-1,0,0,0,-.6f,.8f};
bool throwLogs{},throwNative{};
unsigned logFailures{},allocationFailures{},nativeCalls{},hideCalls{},showCalls{},laserPublications{};
ULONGLONG currentTick=10000;
uint64_t currentContext=1;
uintptr_t attachmentResult=0x123401;
unsigned attachmentStateCalls{};
int attachmentState{};
int failAfter=-1;
void* caller{};
void* testCaller(){return caller;}
ULONGLONG testTick(){return currentTick;}
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
void* operator new(size_t size){
    if(failAfter>0&&!--failAfter){failAfter=-1;++allocationFailures;throw std::bad_alloc{};}
    if(auto result=std::malloc(size?size:1))return result;throw std::bad_alloc{};
}
void* operator new[](size_t size){return ::operator new(size);}
void operator delete(void* value)noexcept{std::free(value);}
void operator delete[](void* value)noexcept{std::free(value);}
void operator delete(void* value,size_t)noexcept{std::free(value);}
void operator delete[](void* value,size_t)noexcept{std::free(value);}

namespace argent {
void log(const std::string&){if(throwLogs){++logFailures;throw std::bad_alloc{};}}
}
namespace argent::camera {
bool swimmingView(float* axis) noexcept {std::memcpy(axis,headBasis.data(),sizeof(headBasis));return true;}
bool wallClimbView(float* axis) noexcept {return swimmingView(axis);}
bool hudCamera(float* origin,float* axis,uint64_t*) noexcept {const float position[]{1,2,3};std::memcpy(origin,position,sizeof(position));return swimmingView(axis);}
bool controllerPlacement(XrPosef hand,float* origin,float* axis,const input::Snapshot*) noexcept {std::memcpy(origin,&hand.position,sizeof(hand.position));return swimmingView(axis);}
uint64_t weaponContext() noexcept {return currentContext;}
float unitsPerMeter() noexcept {return 39.37f;}
void publishLaser(const float*,const float*,const char*) noexcept {++laserPublications;}
void publishPhysics(uintptr_t,XrVector3f) noexcept {}
}
namespace argent::monkey {
bool install(unsigned char*){return true;}
void acceptedBar(uintptr_t){}
}
extern "C" {
void argentHandsBridge(){}
void argentFocusBridge(){}
void argentMeathookBridge(){}
void argentMeathookGateBridge(){}
void argentWallGateBridge(){}
void argentWallImpulseBridge(){}
}
#define GetTickCount64 testTick
#define _ReturnAddress testCaller
#include "../src/EternalPlayerHooks.cpp"
#undef _ReturnAddress
#undef GetTickCount64

extern "C" MH_STATUS WINAPI MH_CreateHook(void*,void*,void**){return MH_ERROR_NOT_INITIALIZED;}
extern "C" MH_STATUS WINAPI MH_EnableHook(void*){return MH_ERROR_NOT_INITIALIZED;}
extern "C" MH_STATUS WINAPI MH_RemoveHook(void*){return MH_ERROR_NOT_INITIALIZED;}

namespace {
template<class T>void put(void* object,size_t offset,const T& value){std::memcpy(static_cast<unsigned char*>(object)+offset,&value,sizeof(value));}
template<class T>T get(void* object,size_t offset){T value{};std::memcpy(&value,static_cast<unsigned char*>(object)+offset,sizeof(value));return value;}
void nativeFailure(){if(throwNative)throw std::bad_alloc{};}
uintptr_t __fastcall nativeEvent(void*,uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e){++nativeCalls;check(a==1&&b==2&&c==3&&d==4&&e==5,"Event changed native arguments");nativeFailure();return 0x1234;}
void __fastcall nativeWater(void* object,void* context){
    ++nativeCalls;check(context==reinterpret_cast<void*>(0x1234),"Swimming changed native context");
    check(std::abs(get<float>(object,0x469c+8)+.6f)<.0001f,"Swimming did not apply temporary HMD tilt");nativeFailure();
}
void __fastcall nativeAnimation(void* item,void* hands){
    ++nativeCalls;using namespace argent::player;
    check(animationItem==uintptr_t(item)&&animationHands==uintptr_t(hands),"Animation did not expose the current callback context");nativeFailure();
}
void* observedRoot{};
void* nestedRoot{};
bool nestedFailure{};
void __fastcall nativeHands(void*){
    ++nativeCalls;using namespace argent::player;check(collectingVisibility,"Hands update did not collect native visibility requests");
    finalVisibility[finalVisibilityCount++]={observedRoot,false};nativeFailure();
}
void __fastcall nativeNestedHands(void* hands){
    using namespace argent::player;++nativeCalls;
    if(nativeCalls==2){finalVisibility[finalVisibilityCount++]={nestedRoot,false};if(nestedFailure)throw std::bad_alloc{};return;}
    finalVisibility[finalVisibilityCount++]={observedRoot,false};bool escaped{};
    try{updateHands(hands);}catch(const std::bad_alloc&){escaped=true;}
    check(escaped==nestedFailure&&collectingVisibility&&finalVisibilityCount==1&&finalVisibility[0].root==observedRoot,"Nested hands update replaced its caller's visibility requests");
}
short* __fastcall nativeFindJoint(void*,short* joint,const char*){*joint=0;return joint;}
bool __fastcall nativeJoint(void*,void*,int,unsigned short,float* origin,float* axis){std::memset(origin,0,3*sizeof(float));std::memcpy(axis,headBasis.data(),sizeof(headBasis));return true;}
uintptr_t __fastcall nativeAttachment(void* model,void*,int mode,void* joint,float* origin,float* axis){
    ++nativeCalls;check(model==reinterpret_cast<void*>(0x2345)&&mode==1&&joint==reinterpret_cast<void*>(0x1234),"Attachment changed native arguments");nativeFailure();
    if(attachmentResult&0xff){const float position[]{4,5,6};std::memcpy(origin,position,sizeof(position));std::memcpy(axis,headBasis.data(),sizeof(headBasis));}return attachmentResult;
}
int __fastcall nativeHandsState(void* hands,bool flag){++attachmentStateCalls;check(uintptr_t(hands)==argent::player::animationHands&&!flag,"Attachment changed native animation-state arguments");return attachmentState;}
void __fastcall nativeHide(void* root,int surface){++hideCalls;const auto mask=get<uint64_t>(root,0x518);put(root,0x518,mask&~(uint64_t(1)<<surface));}
void __fastcall nativeShow(void* root,int surface){++showCalls;const auto mask=get<uint64_t>(root,0x518);put(root,0x518,mask|(uint64_t(1)<<surface));}
bool __fastcall nativeTransform(void*,void*,float* origin,float* axis){++nativeCalls;nativeFailure();const float position[]{4,5,6};std::memcpy(origin,position,sizeof(position));std::memset(axis,0,9*sizeof(float));return true;}
void __fastcall nativeThrow(void*,void*,void*,float force,void*,const float* origin,const float* axis){
    ++nativeCalls;check(force==7&&origin[0]==4&&std::memcmp(axis,headBasis.data(),sizeof(headBasis))==0,"Throw changed native force/origin or lost HMD direction");nativeFailure();
}
void __fastcall nativeFire(void*,void*,void*,void*,void*,float* origin,float* axis){++nativeCalls;nativeFailure();const float position[]{4,5,6};std::memcpy(origin,position,sizeof(position));std::memset(axis,0,9*sizeof(float));}
struct Image {
    unsigned char* bytes=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x7000000,MEM_RESERVE,PAGE_NOACCESS));
    Image(){check(bytes!=nullptr,"Player callback image reservation failed");argent::player::image=bytes;stub(0x19cf620,&nativeHide);stub(0x19d0340,&nativeShow);}
    template<class T>void stub(uintptr_t rva,T function){
        auto target=bytes+argent::build::rva(rva);auto page=reinterpret_cast<void*>(uintptr_t(target)&~uintptr_t(4095));
        check(VirtualAlloc(page,4096,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"Callback stub page unavailable");
        const unsigned char jump[]{0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};std::memcpy(target,jump,sizeof(jump));
        const auto address=reinterpret_cast<uintptr_t>(function);std::memcpy(target+2,&address,sizeof(address));DWORD old{};
        check(VirtualProtect(page,4096,PAGE_EXECUTE_READ,&old)&&FlushInstructionCache(GetCurrentProcess(),page,4096),"Callback stub protection failed");
    }
    ~Image(){VirtualFree(bytes,0,MEM_RELEASE);}
};
struct Objects {
    std::vector<unsigned char> owner=std::vector<unsigned char>(0x40000),hands=std::vector<unsigned char>(0x10000),weapon=std::vector<unsigned char>(0x5000),decl=std::vector<unsigned char>(0x2200),root=std::vector<unsigned char>(0x600),item=std::vector<unsigned char>(0x100);
    Objects(const Image& image){
        using namespace argent;const auto player=uintptr_t(owner.data());presentation::player=player;presentation::playerVtable=0;
        put(hands.data(),0x358,player);put(hands.data(),0x29b0+0x30,uint32_t(1));put(hands.data(),0x29b0+0x34,uint32_t(1));put(hands.data(),0x29b0+0x38,uintptr_t(weapon.data()));
        put(weapon.data(),0,uintptr_t(image.bytes)+build::rva(0x2e0dbc8));put(weapon.data(),0x38,uintptr_t(decl.data()));
        put(decl.data(),0,uintptr_t(image.bytes)+build::rva(0x2b1c348));put(decl.data(),8,uintptr_t("weapon/player/crucible"));put(decl.data(),0x208,int(5));
        put(root.data(),0,uintptr_t(image.bytes)+0x1000);put(root.data(),0x518,uint64_t(5));
        put(item.data(),0,int(5));put(item.data(),8,uintptr_t(decl.data()));
        observedRoot=root.data();player::originalCrucibleEvents[0]=player::originalCrucibleEvents[1]=player::originalCrucibleEvents[2]=&nativeEvent;
        player::originalWaterMove=&nativeWater;player::originalItemAnimation=&nativeAnimation;player::originalUpdateHands=&nativeHands;
        player::originalItemTransform=&nativeTransform;player::originalThrowItem=&nativeThrow;player::originalFire=&nativeFire;
    }
};
void arm(std::string_view mode){throwLogs=mode=="log";if(mode=="oom")failAfter=1;}
void disarm(){throwLogs=false;failAfter=-1;}
void runCase(std::string_view scenario){
    using namespace argent;using namespace argent::player;
    const auto split=scenario.rfind('-');const auto kind=scenario.substr(0,split),mode=scenario.substr(split+1);
    Image image;Objects objects(image);float origin[3]{4,5,6},axis[9]{};
    if(kind.rfind("crucible",0)==0){
        arm(mode);const int type=kind.back()-'0';const auto result=type==0?crucibleEvent<0>(objects.hands.data(),1,2,3,4,5):type==1?crucibleEvent<1>(objects.hands.data(),1,2,3,4,5):crucibleEvent<2>(objects.hands.data(),1,2,3,4,5);disarm();
        check(result==0x1234&&nativeCalls==1,"Crucible observer changed a native result");
        check(mode=="oom"||crucibleEvents.at(uintptr_t(objects.hands.data())).serial==1,"Crucible diagnostic lost the observed native event");
    }else if(kind=="water"||kind=="water-native"){
        const auto address=objects.owner.data()+0x8a50;put(objects.owner.data(),0,uintptr_t(1));presentation::playerVtable=1;
        const std::array<float,6> saved{0,1,0,-1,0,0};std::memcpy(address+0x469c,saved.data(),sizeof(saved));const float gravity[]{0,0,-1};std::memcpy(address+0x4668,gravity,sizeof(gravity));
        arm(mode);throwNative=kind=="water-native";bool escaped{};try{waterMove(address,reinterpret_cast<void*>(0x1234));}catch(const std::bad_alloc&){escaped=true;}disarm();
        check(escaped==throwNative&&nativeCalls==1&&std::memcmp(address+0x469c,saved.data(),sizeof(saved))==0,"Swimming exception lost the native basis or changed exception propagation");
    }else if(kind=="animation"||kind=="animation-native"){
        animationItem=0x123;animationHands=0x456;throwNative=kind=="animation-native";bool escaped{};
        try{updateItemAnimation(objects.item.data(),objects.hands.data());}catch(const std::bad_alloc&){escaped=true;}
        check(escaped==throwNative&&nativeCalls==1&&animationItem==0x123&&animationHands==0x456,"Animation exception retained a stale callback context");
    }else if(kind=="hands"||kind=="hands-native"){
        presentation::scriptedMovement=true;
        rootVisibility(objects.hands.data(),objects.root.data(),true);check(get<uint64_t>(objects.root.data(),0x518)==0,"Hands fixture could not hide the root");
        crucibleHeld=true;hapticKind=static_cast<KharvoxWeaponKind>(2);hapticTick=1;
        put(objects.hands.data(),0x29b0+0x30,uint32_t(2));arm(mode);throwNative=kind=="hands-native";bool escaped{};
        try{updateHands(objects.hands.data());}catch(const std::bad_alloc&){escaped=true;}disarm();
        check(escaped==throwNative&&nativeCalls==1&&!collectingVisibility,"Hands failure left native visibility collection active");
        if(!throwNative)check(hapticKind==KharvoxWeaponKind::Unknown&&hapticTick==10000&&get<uint64_t>(objects.root.data(),0x518)==5&&showCalls==2&&laserPublications>=2,"Hands diagnostics skipped state publication or final visibility recovery");
    }else if(kind=="haptic"){
        crucibleHeld=false;hapticKind=static_cast<KharvoxWeaponKind>(2);hapticTick=1;arm(mode);updateHapticWeapon(objects.hands.data());disarm();
        check(crucibleHeld&&hapticKind==KharvoxWeaponKind::Unknown&&hapticTick==10000&&std::strcmp(weaponProfile(),"crucible")==0,"Haptic diagnostic skipped persistent weapon state publication");
    }else if(kind=="visibility"){
        rootVisibility(objects.hands.data(),objects.root.data(),false);arm(mode);rootVisibility(objects.hands.data(),objects.root.data(),true);disarm();
        check(get<uint64_t>(objects.root.data(),0x518)==0&&hideCalls==2,"Visibility diagnostic skipped native hiding");rootVisibility(objects.hands.data(),objects.root.data(),false);
        check(get<uint64_t>(objects.root.data(),0x518)==5&&showCalls==2,"Visibility diagnostic lost the native restore mask");
    }else if(kind=="visibility-interleaved"||kind=="visibility-shared"){
        std::array<unsigned char,0x600> second{};put(second.data(),0,uintptr_t(image.bytes)+0x1000);put(second.data(),0x518,uint64_t(9));
        const auto otherHands=objects.owner.data();const auto otherRoot=kind=="visibility-shared"?objects.root.data():second.data();
        rootVisibility(objects.hands.data(),objects.root.data(),true);rootVisibility(otherHands,otherRoot,true);
        rootVisibility(objects.hands.data(),objects.root.data(),true);rootVisibility(otherHands,otherRoot,true);
        rootVisibility(objects.hands.data(),objects.root.data(),false);rootVisibility(otherHands,otherRoot,false);
        check(get<uint64_t>(objects.root.data(),0x518)==5&&get<uint64_t>(second.data(),0x518)==9,"Interleaved hands discarded a live root's saved visibility");
    }else if(kind=="visibility-replaced-entity"||kind=="visibility-replaced-model"||kind=="visibility-invalid"){
        rootVisibility(objects.hands.data(),objects.root.data(),true);
        if(kind=="visibility-invalid"){
            put(objects.root.data(),0,uintptr_t(1));rootVisibility(objects.hands.data(),objects.root.data(),false);
            check(showCalls==0,"Rejected root identity still reached native visibility");put(objects.root.data(),0,uintptr_t(image.bytes)+0x1000);
        }else put(objects.root.data(),kind=="visibility-replaced-entity"?0x4d8:0x4e0,uintptr_t(objects.decl.data()));
        put(objects.root.data(),0x518,uint64_t(9));rootVisibility(objects.hands.data(),objects.root.data(),false);
        check(get<uint64_t>(objects.root.data(),0x518)==9&&showCalls==0,"Replaced root inherited an earlier object's saved visibility");
        rootVisibility(objects.hands.data(),objects.root.data(),true);rootVisibility(objects.hands.data(),objects.root.data(),false);
        check(get<uint64_t>(objects.root.data(),0x518)==9&&showCalls==2,"Replaced root could not capture and restore its own mask");
    }else if(kind=="visibility-allocation"||kind=="visibility-reserve"||kind=="visibility-initialize"){
        if(kind=="visibility-initialize"){
            failAfter=1;bool escaped{};try{rootVisibility(objects.hands.data(),objects.root.data(),true);}catch(const std::bad_alloc&){escaped=true;}disarm();
            check(!escaped&&allocationFailures==1&&hideCalls==0&&get<uint64_t>(objects.root.data(),0x518)==5,"Initial mask-map allocation modified a root or escaped");
            rootVisibility(objects.hands.data(),objects.root.data(),true);rootVisibility(objects.hands.data(),objects.root.data(),false);
            check(get<uint64_t>(objects.root.data(),0x518)==5&&showCalls==2,"Initial mask-map allocation could not retry");
        }else if(kind=="visibility-allocation"){
            failAfter=1;bool escaped{};try{rootVisibility(objects.hands.data(),objects.root.data(),false);}catch(const std::bad_alloc&){escaped=true;}disarm();
            check(!escaped&&allocationFailures==0&&get<uint64_t>(objects.root.data(),0x518)==5,"Showing an untracked root allocated or escaped");
        }else{
            rootVisibility(objects.hands.data(),objects.root.data(),true);rootVisibility(objects.hands.data(),objects.root.data(),false);
            std::array<unsigned char,0x600> second{};put(second.data(),0,uintptr_t(image.bytes)+0x1000);put(second.data(),0x518,uint64_t(9));
            const auto initialHides=hideCalls;failAfter=1;bool escaped{};try{rootVisibility(objects.hands.data(),second.data(),true);}catch(const std::bad_alloc&){escaped=true;}disarm();
            check(!escaped&&allocationFailures==1&&hideCalls==initialHides&&get<uint64_t>(second.data(),0x518)==9,"Failed mask reservation modified an untracked root or escaped");
            rootVisibility(objects.hands.data(),second.data(),true);rootVisibility(objects.hands.data(),second.data(),false);
            check(get<uint64_t>(second.data(),0x518)==9,"Mask reservation could not retry and restore");
        }
    }else if(kind=="nested"||kind=="nested-native"){
        presentation::scriptedMovement=true;std::array<unsigned char,0x600> second{};nestedRoot=second.data();nestedFailure=kind=="nested-native";
        put(second.data(),0,uintptr_t(image.bytes)+0x1000);put(second.data(),0x518,uint64_t(9));
        rootVisibility(objects.hands.data(),objects.root.data(),true);rootVisibility(objects.hands.data(),second.data(),true);
        originalUpdateHands=&nativeNestedHands;updateHands(objects.hands.data());
        check(nativeCalls==2&&!collectingVisibility&&finalVisibilityCount==1&&finalVisibility[0].root==observedRoot&&get<uint64_t>(objects.root.data(),0x518)==5,"Nested hands update lost outer visibility recovery");
    }else if(kind=="idle-allocation"){
        std::array<unsigned char,0x400> model{},definition{},node{},skeleton{};
        put(objects.root.data(),0x4e0,uintptr_t(model.data()));put(model.data(),8,uintptr_t(definition.data()));put(definition.data(),0x80,uintptr_t(node.data()));put(node.data(),0x310,uintptr_t(skeleton.data()));
        std::memcpy(objects.root.data()+0x164,headBasis.data(),sizeof(headBasis));image.stub(0x19bfe00,&nativeFindJoint);image.stub(0x1981990,&nativeJoint);
        presentation::gameplayInput=true;input::state.active=true;input::state.tick=10000;input::state.weaponValid=true;input::state.weapon.position={1,2,3};input::state.weaponPivot={};
        collectingVisibility=true;placedHands=uintptr_t(objects.hands.data());placedTick=1;failAfter=1;
        argentHandsTransform(objects.hands.data(),objects.root.data(),origin,axis);disarm();
        check(allocationFailures==1&&origin[0]==1&&origin[1]==2&&origin[2]==3&&placedTick==10000&&finalVisibilityCount==1&&finalVisibility[0].ready,"Idle cache allocation interrupted completed placement bookkeeping");
        argentHandsTransform(objects.hands.data(),objects.root.data(),origin,axis);input::state.weaponValid=false;origin[0]=99;
        argentHandsTransform(objects.hands.data(),objects.root.data(),origin,axis);
        check(origin[0]==1&&placedTick==10000&&finalVisibilityCount==3&&finalVisibility[2].ready,"Idle cache retry did not preserve an untracked weapon pose");
        presentation::scriptedMovement=true;failAfter=1;origin[0]=99;argentHandsTransform(objects.hands.data(),objects.root.data(),origin,axis);disarm();
        check(allocationFailures==1&&origin[0]==99&&placedTick==0&&!finalVisibility[3].ready,"Authored animation retained the idle pose or allocated a new entry");
        presentation::scriptedMovement=false;origin[0]=99;argentHandsTransform(objects.hands.data(),objects.root.data(),origin,axis);
        check(origin[0]==99&&!finalVisibility[4].ready,"Gameplay reused an idle pose across authored animation");
    }else if(kind.rfind("attachment",0)==0){
        std::array<unsigned char,0x600> childRoot{};put(objects.hands.data(),0x370,uintptr_t(objects.root.data()));put(objects.hands.data(),0x29b0+0x78,uintptr_t(childRoot.data()));
        animationHands=uintptr_t(objects.hands.data());animationItem=animationHands+0x29b0;caller=image.bytes+build::rva(0x138a64e);
        originalAttachmentJoint=reinterpret_cast<AttachmentJoint>(&nativeAttachment);image.stub(0x135f130,&nativeHandsState);presentation::gameplayInput=true;
        input::state.active=true;input::state.tick=currentTick;input::state.weaponValid=true;input::state.weaponBase.position={1,2,3};input::state.weapon.position={2,4,6};
        input::state.weaponCorrection=kind.rfind("attachment-rest",0)!=0;std::strcpy(input::state.weaponProfile.data(),"crucible");input::weaponApplied(input::state);
        placedHands=animationHands;placedTick=currentTick;hapticTick=currentTick;std::memcpy(axis,headBasis.data(),sizeof(headBasis));
        const auto invoke=[&](){return attachmentJoint(reinterpret_cast<void*>(0x2345),objects.root.data(),1,reinterpret_cast<void*>(0x1234),origin,axis);};
        if(kind.rfind("attachment-rest",0)==0){
            check(bool(invoke())&&!crucibleVisual.pose.cached,"Attachment captured rest pose before idle settled");currentTick+=400;
            input::state.tick=currentTick;input::weaponApplied(input::state);placedTick=currentTick;hapticTick=currentTick;
            check(bool(invoke())&&crucibleVisual.pose.cached,"Attachment did not capture a settled native rest pose");
            if(kind=="attachment-rest-swing"){
                input::state.crucibleSwing=currentTick;crucibleEvent<0>(objects.hands.data(),1,2,3,4,5);attachmentState=1;
                input::state.weapon.position={10,20,30};input::weaponApplied(input::state);check(bool(invoke()),"Rest attachment changed native success");
                check(origin[0]==12&&origin[1]==21&&origin[2]==30&&crucibleVisual.pose.attacking,"Physical swing lost its cached controller-relative rest attachment");
            }else if(kind=="attachment-rest-failed"){
                attachmentResult=0x123400;input::state.manualWeaponTrigger=true;const auto reads=attachmentStateCalls;origin[0]=99;
                check(!bool(invoke())&&origin[0]==99&&attachmentStateCalls==reads&&crucibleVisual.pose.cached,"Failed native attachment mutated outputs or consumed rest-pose state");
            }else{
                if(kind=="attachment-rest-context")++currentContext;else input::state.manualWeaponTrigger=true;
                check(bool(invoke())&&!crucibleVisual.pose.cached&&origin[0]==4,"Rest attachment survived a changed context or manual native trigger");
            }
        }else{
            if(kind=="attachment-failed"||kind=="attachment-failed-high")attachmentResult=kind=="attachment-failed"?0:0x123400;
            if(kind=="attachment-native")throwNative=true;
            if(kind=="attachment-guard-caller")caller=image.bytes+build::rva(0x138a64e)+1;
            if(kind=="attachment-guard-owner")presentation::player=0;
            if(kind=="attachment-guard-item")animationItem+=0xd78;
            if(kind=="attachment-guard-root")put(objects.hands.data(),0x370,uintptr_t(childRoot.data()));
            if(kind=="attachment-guard-stale")input::weaponRendered.tick=1;
            if(kind=="attachment-guard-profile")std::strcpy(input::weaponRendered.weaponProfile.data(),"default");
            if(kind=="attachment-guard-placement")placedTick=1;
            if(kind=="attachment-guard-gameplay")presentation::gameplayInput=false;
            if(kind=="attachment-guard-context")currentContext=0;
            if(kind=="attachment-guard-scripted")presentation::scriptedMovement=true;
            if(kind=="attachment-guard-monkey")presentation::monkeyBarAnimation=true;
            if(kind=="attachment-guard-drone")presentation::droneAnimation=true;
            if(kind=="attachment-guard-authored"){
                put(objects.owner.data(),0,uintptr_t(1));presentation::playerVtable=1;put(objects.owner.data(),0x167e9,static_cast<unsigned char>(1));
            }
            arm(mode);bool result{},escaped{};try{result=bool(invoke());}catch(const std::bad_alloc&){escaped=true;}disarm();
            check(escaped==throwNative&&nativeCalls==1&&result==(!throwNative&&bool(attachmentResult&0xff)),"Attachment lost the native AL result or exception");
            const bool corrected=kind=="attachment";check(std::abs(origin[0]-(corrected?5:4))<.0001f&&std::abs(origin[1]-(corrected?7:5))<.0001f&&std::abs(origin[2]-(corrected?9:6))<.0001f,"Attachment modified a rejected native pose or lost calibration");
            for(size_t k=0;k<headBasis.size();++k)check(std::abs(axis[k]-headBasis[k])<.0001f,"Attachment lost the native/calibrated basis");
            if(!attachmentResult||kind=="attachment-failed-high"||throwNative)check(attachmentStateCalls==0,"Failed native attachment still dispatched observer calls");
        }
    }else if(kind=="transform"||kind=="throw"||kind=="fire"){
        arm(mode);
        if(kind=="transform")check(itemTransform(objects.item.data(),objects.hands.data(),origin,axis),"Transform diagnostic changed native success");
        else if(kind=="throw")throwItem(objects.item.data(),objects.owner.data(),nullptr,7,nullptr,origin,axis);
        else {float muzzle[9]{};fire(objects.hands.data(),objects.weapon.data(),objects.decl.data(),origin,muzzle,origin,axis);check(std::memcmp(muzzle,headBasis.data(),sizeof(headBasis))==0,"Fire diagnostic lost native muzzle direction");}
        disarm();check(nativeCalls==1&&origin[0]==4&&(kind=="throw"||std::memcmp(axis,headBasis.data(),sizeof(headBasis))==0),"Equipment diagnostic changed native calls, origins or HMD direction");
    }else if(kind=="target"||kind=="candidate"){
        presentation::gameplayInput=true;input::state.active=true;input::state.tick=10000;input::state.weaponValid=true;
        std::strcpy(input::state.weaponProfile.data(),"super_shotgun");input::state.weapon.position={1,2,3};
        const auto handle=objects.owner.data()+0xd2c8+0x29b0;put(handle,0x30,uint32_t(1));put(handle,0x34,uint32_t(1));put(handle,0x38,uintptr_t(objects.weapon.data()));
        if(kind=="candidate")check(argentMeathookTargetView(objects.weapon.data(),objects.owner.data(),origin,axis),"Meathook fixture could not seed the query pose");
        arm(mode);const bool result=kind=="target"?argentMeathookTargetView(objects.weapon.data(),objects.owner.data(),origin,axis):argentMeathookCandidateView(objects.weapon.data(),objects.owner.data(),origin,axis);disarm();
        check(result&&meathookQueryPose.valid&&origin[0]==1,"Meathook diagnostic changed a completed pose result");
    }else throw std::runtime_error("Unknown player callback scenario");
    if(mode=="log")check(logFailures>0,"Player callback logging failure was not exercised");
    if(mode=="oom")check(allocationFailures==1,"Player callback allocation failure was not exercised");
}
bool child(const wchar_t* executable,const std::wstring& scenario,bool store,bool timing){
    std::wstring command=L"\""+std::wstring(executable)+L"\" "+scenario+(store?L" store":L" steam")+(timing?L" timing":L" quiet");STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Player callback child unavailable");
    const auto wait=WaitForSingleObject(process.hProcess,15000);DWORD code{};if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,INFINITE);}
    const bool passed=wait==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&code)&&code==0;CloseHandle(process.hThread);CloseHandle(process.hProcess);
    if(!passed)std::wcerr<<scenario<<L" store="<<store<<L" timing="<<timing<<L" exit="<<code<<L'\n';return passed;
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);SetEnvironmentVariableW(L"ARGENT_SFS_PROFILE_TIMING",nullptr);
    try{
        if(argc>1){argent::build::microsoftStore=argc>2&&std::string_view(argv[2])=="store";SetEnvironmentVariableW(L"ARGENT_EXTENDED_LOGGING",argc>3&&std::string_view(argv[3])=="timing"?L"1":L"0");runCase(argv[1]);return 0;}
        wchar_t executable[32768]{};check(GetModuleFileNameW(nullptr,executable,32768),"Player callback path unavailable");unsigned cases{},failures{};
        for(const bool store:{false,true})for(const bool timing:{false,true}){
            for(const auto kind:{L"crucible0",L"crucible1",L"crucible2",L"water",L"hands",L"haptic",L"visibility",L"transform",L"throw",L"fire",L"target",L"candidate",L"attachment"})for(const auto mode:{L"normal",L"log",L"oom"}){
                if(!timing&&std::wstring_view(mode)!=L"normal")continue;++cases;failures+=!child(executable,std::wstring(kind)+L"-"+mode,store,timing);
            }
            for(const auto kind:{L"animation",L"water-native",L"animation-native",L"hands-native",L"visibility-allocation",L"visibility-reserve",L"visibility-initialize",L"visibility-interleaved",L"visibility-shared",L"visibility-replaced-entity",L"visibility-replaced-model",L"visibility-invalid",L"nested",L"nested-native",L"idle-allocation"}){if(timing&&std::wstring_view(kind)==L"visibility-initialize")continue;++cases;failures+=!child(executable,std::wstring(kind)+L"-normal",store,timing);}
            for(const auto kind:{L"attachment-failed",L"attachment-failed-high",L"attachment-native",L"attachment-guard-caller",L"attachment-guard-owner",L"attachment-guard-item",L"attachment-guard-root",L"attachment-guard-stale",L"attachment-guard-profile",L"attachment-guard-placement",L"attachment-rest-swing",L"attachment-rest-failed",L"attachment-rest-context",L"attachment-rest-manual"}){++cases;failures+=!child(executable,std::wstring(kind)+L"-normal",store,timing);}
            for(const auto kind:{L"attachment-guard-gameplay",L"attachment-guard-context",L"attachment-guard-scripted",L"attachment-guard-monkey",L"attachment-guard-drone",L"attachment-guard-authored"}){++cases;failures+=!child(executable,std::wstring(kind)+L"-normal",store,timing);}
        }
        std::cout<<cases<<" production player callback scenarios, "<<failures<<" failures\n";return failures?1:0;
    }catch(const std::exception& error){disarm();std::cerr<<error.what()<<'\n';return 1;}
}
