#include <windows.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <intrin.h>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "../src/EternalCameraHook.h"
#include "../src/hud/EternalHitMarkers.h"

namespace {
std::atomic<int> failAfter{-1};
unsigned allocationFailures{},logCalls{},nativeCalls{},invalidCleanup{};
bool throwLog{},throwPublish{},cameraAvailable{},throwNative{};
uintptr_t imageBase{},failCreate{},failEnable{};
uintptr_t canvasCaller{};
struct Hook {void* target{};bool owned{},enabled{};};
std::array<Hook,9> hooks{};
constexpr std::array<uintptr_t,5> required{0x15ef980,0x157ca10,0x194f840,0x194cb20,0x182e610};
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
unsigned ownedHooks(){unsigned count{};for(const auto& h:hooks)count+=h.target&&h.owned;return count;}
}

void* operator new(size_t size){
    if(failAfter.load()>0&&failAfter.fetch_sub(1)==1){failAfter=-1;++allocationFailures;throw std::bad_alloc{};}
    if(auto result=std::malloc(size?size:1))return result;
    throw std::bad_alloc{};
}
void* operator new[](size_t size){return ::operator new(size);}
void operator delete(void* value)noexcept{std::free(value);}
void operator delete[](void* value)noexcept{std::free(value);}
void operator delete(void* value,size_t)noexcept{std::free(value);}
void operator delete[](void* value,size_t)noexcept{std::free(value);}

namespace argent {
void log(const std::string&){++logCalls;if(throwLog)throw std::bad_alloc{};}
}
namespace argent::camera {
float unitsPerMeter()noexcept{return 1.f;}
bool hudCamera(float* origin,float* axis,uint64_t* frame)noexcept{
    if(!cameraAvailable)return false;
    std::memset(origin,0,12);const float basis[]{1,0,0,0,1,0,0,0,1};std::memcpy(axis,basis,36);
    if(frame)*frame=42;return true;
}
bool hudOffhand(float*,float*,bool&)noexcept{return false;}
bool hudProjection(float& x,float& y)noexcept{x=y=1.f;return true;}
void publishHudPanels(uint64_t,int,const kharvox::hands::HandHudPanels&){if(throwPublish)throw std::bad_alloc{};}
}

#define _ReturnAddress() reinterpret_cast<void*>(canvasCaller)
#include "../src/hud/EternalWeaponWheel.cpp"
#undef _ReturnAddress
#include "../src/hud/TutorialBindingHook.cpp"

extern "C" MH_STATUS WINAPI MH_CreateHook(void* target,void*,void** original){
    for(const auto& h:hooks)if(h.target==target)return MH_ERROR_ALREADY_CREATED;
    if(uintptr_t(target)==failCreate)return MH_ERROR_MEMORY_ALLOC;
    for(auto& h:hooks)if(!h.target){h={target,true,false};if(original)*original=nullptr;return MH_OK;}
    return MH_ERROR_MEMORY_ALLOC;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(void* target){
    if(uintptr_t(target)==failEnable)return MH_ERROR_MEMORY_PROTECT;
    for(auto& h:hooks)if(h.target==target){h.enabled=true;return MH_OK;}
    return MH_ERROR_NOT_CREATED;
}
extern "C" MH_STATUS WINAPI MH_DisableHook(void* target){
    for(auto& h:hooks)if(h.target==target){if(!h.owned)++invalidCleanup;h.enabled=false;return MH_OK;}
    ++invalidCleanup;return MH_ERROR_NOT_CREATED;
}
extern "C" MH_STATUS WINAPI MH_RemoveHook(void* target){
    for(auto& h:hooks)if(h.target==target){if(!h.owned)++invalidCleanup;h={};return MH_OK;}
    ++invalidCleanup;return MH_ERROR_NOT_CREATED;
}

namespace {
using namespace argent::hud;
template<class T>void put(unsigned char* bytes,size_t offset,const T& value){std::memcpy(bytes+offset,&value,sizeof(value));}
struct Image {
    unsigned char* bytes=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Image(){
        check(bytes!=nullptr,"Image allocation failed");imageBase=uintptr_t(bytes);
        auto signature=[&](uintptr_t rva,std::initializer_list<unsigned char> data){std::memcpy(bytes+argent::build::rva(rva),data.begin(),data.size());};
        signature(0x15ef980,{0x40,0x53,0x55,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0xc0,0,0,0});
        signature(0x157ca10,{0x4c,0x8b,0xdc,0x55,0x53,0x41,0x54,0x41,0x56});
        signature(0x182e610,{0x4c,0x8b,0xdc,0x49,0x89,0x53,0x10,0x55,0x53,0x41,0x55,0x41,0x56});
        signature(0x194f840,{0x40,0x53,0x48,0x83,0xec,0x60});
        signature(0x194cb20,{0x48,0x8b,0x01,0x4c,0x8b,0xca,0x48,0x89,0x82,0x20,0x46,0,0});
        signature(0x157c400,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18});
        signature(0xeee970,{0x40,0x55,0x56,0x41,0x55,0x41,0x56,0x41,0x57});
        signature(0xefd230,{0x40,0x53,0x57,0x41,0x57,0x48,0x83,0xec,0x20,0x48,0x8d,0xb9,0xf8,0,0,0,0x4c,0x8b,0xfa});
        signature(0xefd4c2,{0x48,0x8b,0x83,0xc8,1,0,0,0x49,0x39,0x07});
        if(argent::build::microsoftStore)signature(0x360bb0,{0x48,0x83,0xec,0x58,0x48,0x8b,0x15,0x7d,0x84,0xfc,0x03,0x48,0x8d,0x05,0x16,0xb5,0x78,0x02});
        else signature(0x360bb0,{0x48,0x83,0xec,0x58,0x48,0x8b,0x15,0x0d,0x10,0xf1,0x03,0x48,0x8d,0x05,0x26,0xc7,0x6e,0x02});
        put(bytes,argent::build::rva(0x2d03ea8)+39*8,imageBase+argent::build::rva(0xecfea0));
    }
    ~Image(){VirtualFree(bytes,0,MEM_RELEASE);}
};
uintptr_t __fastcall nativeSubmit(void*,void*,int){++nativeCalls;if(throwNative)throw std::runtime_error("Native submit failure");return 73;}
void __fastcall nativeUpdate(void*,const float*,const float*,void*,void*){++nativeCalls;if(throwNative)throw std::runtime_error("Native update failure");}
void __fastcall nativeDraw(void*,void*,uint64_t){++nativeCalls;}
void __fastcall createWorldCanvas(void* gui,int,bool,float){put(static_cast<unsigned char*>(gui),8,reinterpret_cast<void*>(3));}
const char* __fastcall nativeLookup(const void*){return "Press _jump to jump";}
void __fastcall nativeCanvas(void*,int width,int height,float,float){++nativeCalls;check(width==100&&height==50,"Native canvas dimensions changed");if(throwNative)throw std::runtime_error("Native canvas failure");}
void __fastcall nativeRender(void* gui,void*,void*,bool,bool,bool){
    ++nativeCalls;auto bytes=static_cast<unsigned char*>(gui);
    check(*reinterpret_cast<void**>(bytes+0x10)==reinterpret_cast<void*>(3)&&*reinterpret_cast<float*>(bytes+0x6c)==-1.f&&pending.role==0,"HUD render overrides not exercised");
    if(throwNative)throw std::runtime_error("Native render failure");
}
void __fastcall nativePoi(void*,void*,void* entry,void*,void*){
    ++nativeCalls;check(*reinterpret_cast<float*>(static_cast<unsigned char*>(entry)+0x60)==2.f,"POI scale override not exercised");
    if(throwNative)throw std::runtime_error("Native POI failure");
}

void submitCase(std::string_view scenario){
    std::vector<unsigned char> buffer(0xd0018);std::array<unsigned char,0x88> surface{};
    const std::array<std::array<float,2>,3> xy{{{0,0},{10,0},{0,10}}};
    for(size_t i=0;i<xy.size();++i){std::memcpy(buffer.data()+i*48,xy[i].data(),8);buffer[i*48+31]=255;}
    const uint16_t indices[]{0,1,2};std::memcpy(buffer.data()+0xc0000,indices,sizeof(indices));
    put(surface.data(),0x14,3);put(surface.data(),0x1c,3);
    put(buffer.data(),0xd0000,3);put(buffer.data(),0xd0004,3);put(buffer.data(),0xd0008,surface.data());put(buffer.data(),0xd0010,1);
    void* entity=reinterpret_cast<void*>(9);const int role=scenario=="submit-format-oom"?0:1;
    geometryOwners[role]={entity,0,10,10,.5f,.5f,GetTickCount64(),42,{},{1,0,0,0,1,0,0,0,1},1,1,1};
    originalSubmit=nativeSubmit;throwLog=scenario=="submit-log";throwPublish=scenario=="submit-publish";
    throwNative=scenario=="submit-native";if(scenario=="submit-oom"||scenario=="submit-format-oom")failAfter=1;
    auto context=buffer.data();bool escaped=false;uintptr_t returned{};
    try{returned=submitGeometry(&context,entity,4);}catch(...){escaped=true;}
    failAfter=-1;
    check(nativeCalls==1&&escaped==throwNative&&(throwNative||returned==73),"HUD observer changed native submission");
    if(throwLog)check(logCalls>0,"Submit diagnostic not exercised");
    if(scenario=="submit-oom"||scenario=="submit-format-oom")check(allocationFailures==1,"HUD allocation failure not exercised");
    check(!std::memcmp(buffer.data()+0xc0000,indices,sizeof(indices)),"HUD correction changed native indices");
    for(size_t i=0;i<xy.size();++i)check(buffer[i*48+31]==255,"HUD correction changed vertex alpha");
}

void updateCase(std::string_view scenario){
    std::array<unsigned char,0xc0> owner{};image=0x100000;
    put(owner.data(),0,image+argent::build::rva(roles[0].vtable));owner[0xb8]=1;
    put(owner.data(),0x10,reinterpret_cast<void*>(2));createCanvas=createWorldCanvas;originalUpdate=nativeUpdate;
    cameraAvailable=true;throwLog=scenario=="update-log";throwNative=scenario=="update-native";
    const float origin[]{0,0,0},axis[]{1,0,0,0,1,0,0,0,1};
    if(scenario=="update-oom")failAfter=1;
    bool escaped=false;try{update(owner.data(),origin,axis,nullptr,nullptr);}catch(...){escaped=true;}
    failAfter=-1;check(nativeCalls==1&&escaped==throwNative,"HUD observer changed native update");
    if(!throwNative)check(weaponWheelVisible(),"HUD observer lost wheel visibility after update");
    if(throwLog)check(logCalls>0,"Update diagnostic not exercised");
    if(scenario=="update-oom")check(allocationFailures==1,"Owner allocation failure not exercised");
}

void scenarioCase(std::string_view scenario){
    if(scenario.rfind("submit-",0)==0){submitCase(scenario);return;}
    if(scenario.rfind("update-",0)==0){updateCase(scenario);return;}
    if(scenario.rfind("canvas-",0)==0){
        std::array<unsigned char,0x188> entity{};originalCanvas=nativeCanvas;image=0x100000;
        pending={};pending.entity=entity.data();pending.role=0;pending.frame=42;
        const float axis[]{1,0,0,0,1,0,0,0,1};std::memcpy(pending.head,axis,36);
        canvasCaller=image+argent::build::rva(0x157cf4a);throwLog=scenario=="canvas-log";throwNative=scenario=="canvas-native";
        bool escaped=false;try{canvas(entity.data(),100,50,1.f,1.f);}catch(...){escaped=true;}
        check(nativeCalls==1&&escaped==throwNative,"HUD observer changed native canvas call");
        if(throwLog)check(logCalls>0,"Canvas diagnostic not exercised");return;
    }
    if(scenario.rfind("render-",0)==0){
        std::array<unsigned char,0xc0> owner{};auto gui=owner.data()+0x10;image=0x100000;
        const auto table=image+argent::build::rva(roles[0].vtable);put(owner.data(),0,table);
        put(gui,0,reinterpret_cast<void*>(2));put(gui,8,reinterpret_cast<void*>(3));put(gui,0x10,reinterpret_cast<void*>(4));put(gui,0x6c,1.f);
        animationHudOwners[uintptr_t(gui)]={table,GetTickCount64()};sources[0].owner=uintptr_t(owner.data());sources[0].tick=GetTickCount64();
        pending={};pending.role=15;pending.entity=reinterpret_cast<void*>(11);hiddenSwf=reinterpret_cast<void*>(6);
        originalRender=nativeRender;cameraAvailable=true;throwNative=scenario=="render-native";
        bool escaped=false;try{render(gui,nullptr,nullptr,false,true,true);}catch(...){escaped=true;}
        check(nativeCalls==1&&escaped==throwNative&&*reinterpret_cast<void**>(gui+0x10)==reinterpret_cast<void*>(4)&&*reinterpret_cast<float*>(gui+0x6c)==1.f&&pending.role==15&&pending.entity==reinterpret_cast<void*>(11)&&hiddenSwf==reinterpret_cast<void*>(6),"HUD render did not restore native and thread-local state");return;
    }
    if(scenario.rfind("poi-",0)==0&&scenario!="poi-conflict"&&scenario!="poi-enable"){
        std::array<unsigned char,0x64> entry{};std::array<unsigned char,16> declaration{};
        const char* name="objective_marker_major";put(entry.data(),0x60,6.f);put(declaration.data(),8,name);
        argent::presentation::gameplayInput=true;originalPoiEntry=nativePoi;throwLog=scenario=="poi-log";throwNative=scenario=="poi-native";
        bool escaped=false;try{updatePoiEntry(nullptr,nullptr,entry.data(),declaration.data(),nullptr);}catch(...){escaped=true;}
        check(nativeCalls==1&&escaped==throwNative&&*reinterpret_cast<float*>(entry.data()+0x60)==6.f,"POI did not restore native scale");
        if(throwLog)check(logCalls>0,"POI diagnostic not exercised");return;
    }
    if(scenario=="hidden-log"){
        originalDrawSwf=nativeDraw;hiddenSwf=reinterpret_cast<void*>(5);throwLog=true;
        drawSwf(hiddenSwf,nullptr,7);check(!nativeCalls&&logCalls,"Hidden SWF diagnostic changed suppression");return;
    }
    if(scenario=="localized-log"){
        originalLookup=nativeLookup;throwLog=true;const auto first=localizedText(nullptr),second=localizedText(nullptr);
        check(std::string_view(first)=="Press [R B] to jump"&&first==second&&logCalls,"Diagnostic changed cached tutorial text");return;
    }
    Image fixture;
    if(scenario=="wheel-ready-log"||scenario=="wheel-ready"){
        throwLog=scenario=="wheel-ready-log";check(installWeaponWheel(fixture.bytes)&&ownedHooks()==7&&!invalidCleanup,"HUD hook publication changed");return;
    }
    if(scenario=="wheel-refused-log"){
        throwLog=true;check(!installWeaponWheel(nullptr)&&!ownedHooks(),"Invalid HUD image accepted");return;
    }
    if(scenario.rfind("wheel-oom-",0)==0){
        failAfter=int(scenario.back()-'0');const auto installed=installWeaponWheel(fixture.bytes);failAfter=-1;
        check(allocationFailures==1&&!invalidCleanup&&ownedHooks()==(installed?7:0),"HUD setup allocation failure stranded hooks");return;
    }
    if(scenario.rfind("wheel-create-",0)==0||scenario.rfind("wheel-enable-",0)==0){
        const auto target=imageBase+argent::build::rva(required[size_t(scenario.back()-'0')]);
        if(scenario.rfind("wheel-create-",0)==0)hooks[0]={reinterpret_cast<void*>(target),false,true};else failEnable=target;
        check(!installWeaponWheel(fixture.bytes)&&!ownedHooks()&&!invalidCleanup,"HUD rollback changed unowned hooks or retained owned hooks");
        if(!failEnable)check(hooks[0].target==reinterpret_cast<void*>(target)&&hooks[0].enabled,"Existing hook lost");return;
    }
    if(scenario=="poi-conflict"||scenario=="poi-enable"){
        const auto target=imageBase+argent::build::rva(0xeee970);
        if(scenario=="poi-conflict")hooks[0]={reinterpret_cast<void*>(target),false,true};else failEnable=target;
        check(installWeaponWheel(fixture.bytes)&&ownedHooks()==6&&!invalidCleanup,"Optional POI setup changed hook ownership");
        if(scenario=="poi-conflict")check(hooks[0].target&&hooks[0].enabled,"Existing POI hook lost");return;
    }
    if(scenario.rfind("tutorial-",0)==0||scenario.rfind("hit-",0)==0){
        const bool tutorial=scenario.rfind("tutorial-",0)==0;
        const auto target=imageBase+argent::build::rva(tutorial?0x360bb0:0xefd230);
        throwLog=scenario.find("log")!=std::string_view::npos;
        if(scenario.find("create")!=std::string_view::npos)failCreate=target;
        if(scenario.find("enable")!=std::string_view::npos)failEnable=target;
        const bool refused=scenario.find("refused")!=std::string_view::npos;
        const auto result=tutorial?installTutorialBindings(refused?nullptr:fixture.bytes):installHitMarkerSuppression(refused?nullptr:fixture.bytes);
        const bool expected=!refused&&!failCreate&&!failEnable;
        check(result==expected&&ownedHooks()==unsigned(expected)&&!invalidCleanup,"Optional hook result or rollback changed");return;
    }
    throw std::runtime_error("Unknown HUD scenario");
}

bool child(const wchar_t* executable,const std::wstring& scenario,bool store){
    std::wstring command=L"\""+std::wstring(executable)+L"\" "+scenario+(store?L" store":L"");
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"HUD child process creation failed");
    const auto wait=WaitForSingleObject(process.hProcess,15000);DWORD code{};
    if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,INFINITE);}
    const bool passed=wait==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&code)&&!code;
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    if(!passed)std::wcerr<<scenario<<L" store="<<store<<L" exit="<<code<<L'\n';return passed;
}
}

int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    SetEnvironmentVariableW(L"ARGENT_LOG",nullptr);SetEnvironmentVariableA("ARGENT_EXTENDED_LOGGING",argc>1&&std::string_view(argv[1])=="canvas-log"?"1":"0");
    try{
        if(argc>1){argent::build::microsoftStore=argc>2;scenarioCase(argv[1]);return 0;}
        wchar_t executable[32768]{};check(GetModuleFileNameW(nullptr,executable,32768)!=0,"Executable path unavailable");
        unsigned cases{},failures{};
        for(const bool store:{false,true}){
            for(const auto scenario:{L"wheel-ready",L"wheel-ready-log",L"wheel-refused-log",L"poi-conflict",L"poi-enable",L"tutorial-ready-log",L"tutorial-refused-log",L"tutorial-create-log",L"tutorial-enable-log",L"hit-ready-log",L"hit-refused-log",L"hit-create-log",L"hit-enable-log",L"submit-normal",L"submit-log",L"submit-publish",L"submit-oom",L"submit-format-oom",L"submit-native",L"update-log",L"update-oom",L"update-native",L"hidden-log",L"localized-log",L"canvas-normal",L"canvas-log",L"canvas-native",L"render-normal",L"render-native",L"poi-normal",L"poi-log",L"poi-native"}){++cases;failures+=!child(executable,scenario,store);}
            for(unsigned i=0;i<5;++i)for(const auto prefix:{L"wheel-create-",L"wheel-enable-"}){++cases;failures+=!child(executable,std::wstring(prefix)+std::to_wstring(i),store);}
            for(unsigned i=1;i<=8;++i){++cases;failures+=!child(executable,L"wheel-oom-"+std::to_wstring(i),store);}
        }
        std::cout<<cases<<" production HUD hook scenarios, "<<failures<<" failures\n";return failures?1:0;
    }catch(const std::exception& error){failAfter=-1;std::cerr<<error.what()<<'\n';return 1;}
}
