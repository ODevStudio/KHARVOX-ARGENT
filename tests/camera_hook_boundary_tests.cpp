#include <windows.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include "../src/EternalCameraHook.h"

namespace {
std::atomic<int> failAfter{-1};
unsigned allocationFailures{},nativeCalls{},setCalls{},logFailures{};
bool nativeThrows{},setThrows{},reenter{},wheelShown{};
std::string_view logPrefix;
ULONGLONG testNow=100000;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}

void* operator new(size_t size){
    if(failAfter.load()>0&&failAfter.fetch_sub(1)==1){failAfter=-1;++allocationFailures;throw std::bad_alloc{};}
    if(auto result=std::malloc(size?size:1))return result;throw std::bad_alloc{};
}
void* operator new[](size_t size){return ::operator new(size);}
void operator delete(void* value)noexcept{std::free(value);}
void operator delete[](void* value)noexcept{std::free(value);}
void operator delete(void* value,size_t)noexcept{std::free(value);}
void operator delete[](void* value,size_t)noexcept{std::free(value);}

namespace argent {
void log(const std::string& message){
    if(!logPrefix.empty()&&std::string_view(message).rfind(logPrefix,0)==0){++logFailures;throw std::bad_alloc{};}
}
}
namespace argent::hud {
bool weaponWheelVisible()noexcept{return wheelShown;}
int calibrationPlaceholderRole(){return -1;}
kharvox::hands::HandHudPanels calibrationPlaceholder(int,bool,const float*,const float*,float,const float*){return {};}
}

#define GetTickCount64() testNow
#include "../src/EternalCameraHook.cpp"
#undef GetTickCount64

extern "C" MH_STATUS WINAPI MH_Initialize(){return MH_OK;}
extern "C" MH_STATUS WINAPI MH_CreateHook(void*,void*,void**){return MH_OK;}
extern "C" MH_STATUS WINAPI MH_EnableHook(void*){return MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(void*){return MH_OK;}
extern "C" MH_STATUS WINAPI MH_RemoveHook(void*){return MH_OK;}

namespace {
using namespace argent::camera;
template<class T>void put(unsigned char* bytes,size_t offset,const T& value){std::memcpy(bytes+offset,&value,sizeof(value));}
struct Slot {RenderControl control{};std::array<unsigned char,0x30> data{};};
std::array<Slot,32> slots{};size_t slotCount{};
Slot& slot(uintptr_t rva){for(size_t i=0;i<slotCount;++i)if(slots[i].control.rva==rva)return slots[i];throw std::runtime_error("Unknown render control");}
int value(uintptr_t rva){int result{};std::memcpy(&result,slot(rva).data.data()+8,4);return result;}
void setValue(uintptr_t rva,int next){put(slot(rva).data.data(),8,next);put(slot(rva).data.data(),12,float(next));}
struct Controls {
    unsigned char* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x8000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Controls(){
        check(image!=nullptr,"Control image allocation failed");cvarBase=uintptr_t(image);
        auto add=[&](RenderControl control,int initial){
            auto& next=slots[slotCount++];next.control=control;put(next.data.data(),8,initial);put(next.data.data(),12,float(initial));
            put(next.data.data(),0x28,control.name);put(image,argent::build::rva(control.rva),next.data.data());
        };
        for(const auto& control:renderControls)add(control,control.integer?0:1);
        add(wheelBloomControl,1);add(lightCullingControl,0);add({0x6685ce0,"r_antialiasing","",0},0);
        add({0x66e8ab0,"r_enableResolutionScale","1",1},0);add({0x66ec860,"rs_enable","0",0},1);
        add({0x66ec8e0,"rs_forceResolution","1",1,true},0);add({0x66de720,"r_TAASafeMode","0",0},0);
    }
    ~Controls(){setCvar=nullptr;cvarBase=0;VirtualFree(image,0,MEM_RELEASE);}
};
bool __fastcall controlSetter(void* wrapper,const char* text,bool){
    ++setCalls;
    if(setThrows){setThrows=false;throw std::bad_alloc{};}
    if(reenter){reenter=false;maintainRenderControls();}
    for(size_t i=0;i<slotCount;++i)if(uintptr_t(wrapper)==cvarBase+argent::build::rva(slots[i].control.rva)){
        const auto fraction=std::strtof(text,nullptr);put(slots[i].data.data(),8,int(fraction));put(slots[i].data.data(),12,fraction);return true;
    }
    throw std::runtime_error("Unknown native control wrapper");
}
int __fastcall nativePoll(void*,int user){++nativeCalls;check(user==0,"Native poll user changed");if(nativeThrows)throw std::runtime_error("Native poll failure");return 73;}
void __fastcall nativeSetter(void* object,const float* position,const float* basis){
    ++nativeCalls;if(position)std::memcpy(static_cast<unsigned char*>(object)+0x124,position,12);
    if(basis)std::memcpy(static_cast<unsigned char*>(object)+0x130,basis,36);
    if(nativeThrows)throw std::runtime_error("Native camera failure");
}
void __fastcall nativeFinalize(void*){++nativeCalls;if(nativeThrows)throw std::runtime_error("Native finalizer failure");}
void __fastcall failedDiscovery(void*){throw std::bad_alloc{};}
bool poll(){try{check(pollController(nullptr,0)==73,"Native poll result changed");return false;}catch(...){return true;}}

void maintenanceCase(std::string_view scenario){
    Controls controls;setCvar=controlSetter;originalPoll=nativePoll;
    if(scenario=="maintenance-log")logPrefix="ETERNAL_";
    if(scenario=="maintenance-control-log")logPrefix="ETERNAL_CONTROL ";
    if(scenario=="maintenance-scale-log")logPrefix="ETERNAL_SCALE ";
    if(scenario=="maintenance-temporal-log")logPrefix="ETERNAL_TEMPORAL_STATE ";
    if(scenario=="maintenance-perf-log")logPrefix="PERF_FOV_STATE ";
    if(scenario=="maintenance-aa-log"){
        argent::presentation::latest={testNow,true,true};check(!poll(),"Initial AA context failed");
        testNow+=1600;argent::presentation::latest.tick=testNow;controlTicks=0;setValue(0x6685ce0,1);logPrefix="ETERNAL_AA_ENFORCED ";
    }
    if(scenario=="maintenance-fov-log"){
        active=true;setValue(renderControls[0].rva,90);observedFovX=observedFovY=1.f;requiredFovX=requiredFovY=2.f;observedFovTick=testNow;logPrefix="HEADSET_FOV ";
    }
    if(scenario=="maintenance-setter")setThrows=true;
    if(scenario=="maintenance-reenter")reenter=true;
    if(scenario=="maintenance-format-oom")failAfter=1;
    if(scenario=="maintenance-native")nativeThrows=true;
    const auto previousCalls=nativeCalls;const bool escaped=poll();failAfter=-1;
    check(nativeCalls==previousCalls+1&&escaped==nativeThrows,"Control maintenance changed native poll execution");
    if(!logPrefix.empty())check(logFailures>0,"Maintenance diagnostic not exercised");
    if(scenario=="maintenance-format-oom")check(allocationFailures==1,"Control formatting allocation failure not exercised");
    logPrefix={};nativeThrows=false;controlTicks=0;
    check(!poll()&&nativeCalls==previousCalls+2,"Control maintenance failed to recover");
    for(const auto& control:renderControls)check(value(control.rva)==(control.rva==renderControls[0].rva?automaticFov:control.integer),"Control maintenance retained a stale engine override");
    check(value(0x66e8ab0)==1&&value(0x66ec860)==0&&value(0x66ec8e0)==1,"Launcher scale maintenance interrupted");
}

void scopeCase(std::string_view scenario){
    Controls controls;setCvar=controlSetter;originalPoll=nativePoll;active=true;
    if(scenario=="bloom-restore-log"){
        argent::presentation::gameplayInput=true;wheelShown=true;logPrefix="ETERNAL_WHEEL_BLOOM ";
        check(!poll()&&nativeCalls==1&&wheelBloom.held&&value(wheelBloomControl.rva)==0,"Bloom entry diagnostic interrupted native polling");
        stop();wheelShown=false;controlTicks=1;
        check(!poll()&&nativeCalls==2&&!wheelBloom.held&&value(wheelBloomControl.rva)==1,"Stopped camera retained temporary bloom override");
    }else{
        active=false;lightCulling={true,0};setValue(lightCullingControl.rva,1);logPrefix="ETERNAL_LIGHT_CULL ";
        check(!poll()&&nativeCalls==1&&!lightCulling.held&&value(lightCullingControl.rva)==0,"Culling restoration diagnostic interrupted native polling");
        setValue(rayTracingControl.rva,1);controlTicks=1;logPrefix={};
        check(!poll()&&nativeCalls==2&&value(rayTracingControl.rva)==0,"Temporary scope exception disabled later maintenance");
    }
    check(logFailures>0,"Temporary override diagnostic not exercised");
}

void finalizeCase(std::string_view scenario){
    std::array<unsigned char,0x160> object{};auto fov=reinterpret_cast<float*>(object.data()+0xb8);fov[0]=60;fov[1]=70;
    active=animationActive=true;animationCamera=object.data();updated=animationCameraTick=testNow;requiredFovX=requiredFovY=2.f;
    originalFinalizeCamera=nativeFinalize;
    if(scenario=="finalize-log")logPrefix="ETERNAL_ANIMATION_FOV ";
    if(scenario=="finalize-oom")failAfter=1;
    if(scenario.rfind("finalize-oom-",0)==0)failAfter=int(scenario.back()-'0');
    if(scenario=="finalize-format-oom")failAfter=4;
    if(scenario=="finalize-unchanged"){requiredFovX=requiredFovY=.5f;failAfter=1;}
    if(scenario=="finalize-native")nativeThrows=true;
    bool escaped=false;try{finalizeCamera(object.data());}catch(...){escaped=true;}failAfter=-1;
    check(nativeCalls==1&&escaped==nativeThrows,"Optional FOV work changed native finalization");
    if(scenario.rfind("finalize-oom",0)==0)check(allocationFailures==1&&fov[0]==60&&fov[1]==70,"Failed FOV ownership allocation modified native state");
    else if(scenario=="finalize-unchanged")check(!allocationFailures&&fov[0]==60&&fov[1]==70,"Unchanged animation FOV allocated or modified native state");
    else if(!nativeThrows)check(fov[0]>120&&fov[1]>120,"Animation FOV protection not exercised");
    if(scenario=="finalize-format-oom")check(allocationFailures==1,"FOV diagnostic formatting failure not exercised");
    if(scenario=="finalize-external")fov[0]=100;
    logPrefix={};nativeThrows=false;stop();finalizeCamera(object.data());
    check(nativeCalls==2&&fov[0]==(scenario=="finalize-external"?100.f:60.f)&&fov[1]==70,"Animation FOV restoration lost native or externally changed state");
}

void cameraCase(std::string_view scenario){
    std::array<unsigned char,0x160> object{};const float position[]{1,2,3};const Basis basis{1,0,0,0,1,0,0,0,1};
    original=nativeSetter;active=true;updated=testNow;installed=true;
    std::optional<Controls> controls;
    if(scenario=="camera-maintenance-setter"){controls.emplace();setCvar=controlSetter;setThrows=true;}
    if(scenario=="camera-entry-log"){argent::presentation::scriptedMovement=true;logPrefix="ETERNAL_KILL_CAMERA ";}
    if(scenario=="camera-exit-log"){animationActive=true;logPrefix="ETERNAL_KILL_CAMERA ";}
    if(scenario=="camera-native")nativeThrows=true;
    bool escaped=false;try{cameraSetter(object.data(),position,basis.data());}catch(...){escaped=true;}
    check(nativeCalls==1&&escaped==nativeThrows,"Camera diagnostic changed native setter execution");
    if(!logPrefix.empty())check(logFailures>0,"Camera transition diagnostic not exercised");
    logPrefix={};nativeThrows=false;stop();cameraSetter(object.data(),position,basis.data());
    check(nativeCalls==2&&!std::memcmp(object.data()+0x124,position,12)&&!std::memcmp(object.data()+0x130,basis.data(),36),"Stopped hook changed native camera state");
}

void scenarioCase(std::string_view scenario){
    if(scenario.rfind("maintenance-",0)==0){maintenanceCase(scenario);return;}
    if(scenario=="bloom-restore-log"||scenario=="culling-restore-log"){scopeCase(scenario);return;}
    if(scenario.rfind("finalize-",0)==0){finalizeCase(scenario);return;}
    if(scenario.rfind("camera-",0)==0){cameraCase(scenario);return;}
    if(scenario=="discovery-throw"){
        originalPoll=nativePoll;discover=failedDiscovery;argent::input::state.active=true;argent::input::state.tick=testNow;
        check(!poll()&&nativeCalls==1,"Discovery exception changed native polling");testNow+=2000;argent::input::state.tick=testNow;
        check(!poll()&&nativeCalls==2,"Discovery failure blocked native polling retry");return;
    }
    if(scenario=="actor-physics-log"||scenario=="actor-pose-log"){
        argent::revenant::actor=7;logPrefix="ETERNAL_CONTROLLED_ACTOR ";
        if(scenario=="actor-physics-log")publishPhysics(7,{1,2,3});else updatePose({{0,0,0,1},{0,1,0}},false,false,true);
        check(viewActor==7&&logFailures>0,"Actor transition lost despite diagnostic failure");return;
    }
    if(scenario=="body-yaw-log"){
        installed=true;logPrefix="ETERNAL_BODY_YAW ";
        for(unsigned i=0;i<300;++i)updatePose({{0,0,0,1},{0,1,0}},true,false,true);
        check(logFailures==1,"Body yaw diagnostic not exercised");return;
    }
    throw std::runtime_error("Unknown camera scenario");
}

bool child(const wchar_t* executable,const wchar_t* scenario,bool store){
    std::wstring command=L"\""+std::wstring(executable)+L"\" "+scenario+(store?L" store":L"");
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Camera child process creation failed");
    const auto wait=WaitForSingleObject(process.hProcess,15000);DWORD code{};
    if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,INFINITE);}
    const bool passed=wait==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&code)&&!code;
    CloseHandle(process.hThread);CloseHandle(process.hProcess);if(!passed)std::wcerr<<scenario<<L" store="<<store<<L" exit="<<code<<L'\n';return passed;
}
}

int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    SetEnvironmentVariableA("ARGENT_EXTENDED_LOGGING","1");SetEnvironmentVariableA("ARGENT_RENDER_SCALE","1");SetEnvironmentVariableA("ARGENT_FSR1","0");
    SetEnvironmentVariableA("ARGENT_PERFORMANCE_DIAGNOSTICS",argc>1&&std::string_view(argv[1])=="maintenance-perf-log"?"1":"0");
    try{
        if(argc>1){argent::build::microsoftStore=argc>2;scenarioCase(argv[1]);return 0;}
        wchar_t executable[32768]{};check(GetModuleFileNameW(nullptr,executable,32768)!=0,"Executable path unavailable");unsigned scenarios{},failures{};
        for(const bool store:{false,true})for(const auto scenario:{L"maintenance-log",L"maintenance-control-log",L"maintenance-scale-log",L"maintenance-temporal-log",L"maintenance-perf-log",L"maintenance-aa-log",L"maintenance-fov-log",L"maintenance-setter",L"maintenance-reenter",L"maintenance-format-oom",L"maintenance-native",L"bloom-restore-log",L"culling-restore-log",L"finalize-normal",L"finalize-log",L"finalize-oom",L"finalize-oom-2",L"finalize-oom-3",L"finalize-format-oom",L"finalize-unchanged",L"finalize-native",L"finalize-external",L"camera-entry-log",L"camera-exit-log",L"camera-native",L"camera-maintenance-setter",L"actor-physics-log",L"actor-pose-log",L"body-yaw-log",L"discovery-throw"}){
            if constexpr(argent::cleanRelease)if(std::wstring_view(scenario)==L"maintenance-perf-log")continue;
            ++scenarios;failures+=!child(executable,scenario,store);
        }
        std::cout<<scenarios<<" production camera hook scenarios, "<<failures<<" failures\n";return failures?1:0;
    }catch(const std::exception& error){failAfter=-1;std::cerr<<error.what()<<'\n';return 1;}
}
