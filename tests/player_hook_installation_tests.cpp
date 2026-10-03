#include <windows.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include "../src/EternalBuildProfile.h"

namespace {
using namespace std::literals;
struct Signature {uintptr_t rva;size_t offset;std::string_view bytes;};
constexpr Signature signatures[]{
    {0x134b00e,0,"\x0f\x28\x45\xd0\x0f\x11\x8e\x68\x02\0\0"sv},
    {0x16b8351,0,"\x48\x8d\x95\x58\x01\0\0\x48\x8d\x8d\x90\0\0\0"sv},
    {0x16b8e76,0,"\xf3\x0f\x10\x45\x68\xf3\x0f\x10\x6d\x30\xf3\x0f\x10\x5d\x34"sv},
    {0x13891f0,0,"\x40\x53\x48\x83\xec\x20\x48\x8b\xd9"sv},
    {0x13693f0,0,"\x48\x83\xec\x38\x0f\x29\x74\x24\x20\x0f\x28\xf1"sv},
    {0x16c4080,0,"\x40\x53\x48\x83\xec\x20\x48\x8b\xd9"sv},
    {0x16c4280,0,"\x48\x83\xec\x38\x48\x83\xb9\x58\x01\0\0\0\x4c\x8b\xc1"sv},
    {0x138ab20,0,"\x40\x55\x56\x41\x55\x41\x56\x48\x8d\x6c\x24\xc1\x48\x81\xec\xd8\0\0\0"sv},
    {0x135aa00,0,"\x48\x89\x5c\x24\x18\x48\x89\x6c\x24\x20\x56\x48\x81\xec\x80\0\0\0"sv},
    {0x135ab80,0,"\x48\x89\x5c\x24\x18\x48\x89\x6c\x24\x20\x56\x48\x81\xec\x80\0\0\0"sv},
    {0x135ad00,0,"\x48\x89\x5c\x24\x18\x48\x89\x6c\x24\x20\x57\x48\x81\xec\x80\0\0\0"sv},
    {0x1981bc0,0,"\x48\x83\xec\x48\x48\x8b\x44\x24\x78\x45\x8b\xd0"sv},
    {0x13807ea,0,"\x0f\xb6\x86\xb0\0\0\0\x0f\x10\x45\xd0\x24\x0c\x3c\x0c"sv},
    {0x137fd60,0,"\x48\x8b\xc4\x48\x89\x58\x10\x48\x89\x70\x18"sv},
    {0x1390b50,0,"\x41\x54\x41\x55\x41\x56\x48\x81\xec\x90\0\0\0"sv},
    {0x1398420,0,"\x48\x89\x5c\x24\x10\x57\x48\x83\xec\x40"sv},
    {0x13bc530,0,"\x80\xbf\x50\x17\0\0\0"sv},
    {0x13b98f8,0,"\x0f\x28\xc8\x0f\xc6\xc9\x55"sv},
    {0x1389020,0,"\x48\x89\x6c\x24\x10\x48\x89\x74\x24\x18\x48\x89\x7c\x24\x20\x41\x56"sv},
    {0x1389910,0,"\x4d\x85\xc0\x0f\x84\x35\x01\0\0\x55\x56\x57\x41\x56\x41\x57"sv},
    {0x135f280,0,"\x4c\x8b\xdc\x55\x53\x56\x57\x41\x55\x41\x56\x41\x57"sv},
    {0x135f280,0xf6,"\x49\x8b\x45\x38\x48\x8b\x8b\x58\x03\0\0\x44\x8b\xa0\x08\x02\0\0"sv},
    {0x135f130,0,"\x48\x89\x5c\x24\x08\x48\x89\x74\x24\x10\x57\x48\x83\xec\x20"sv},
    {0x18dbf20,0,"\x80\xa1\xb0\0\0\0\xfe"sv},
    {0x138a0e0,0,"\x40\x53\x48\x83\xec\x20\x48\x8b\xd9"sv},
    {0x53a9f0,0,"\x48\x89\x74\x24\x20\x57\x48\x81\xec\xf0\0\0\0"sv}
};
struct Hook {unsigned char* target{};void* detour{};void** original{};unsigned char byte{};bool owned{},enabled{};};
std::array<Hook,24> hooks{};
std::mutex hookGuard;
unsigned char* imageBytes{};
uintptr_t failCreate{},failEnable{},failRemove{};
unsigned createCalls{},enableCalls{},invalidCleanup{},monkeyCalls{},logCalls{},logFailures{},allocationFailures{};
unsigned logFailureAt{};
bool throwLogs{},throwMonkey{},monkeyResult{true},formatFailure{},earlyEnable{},earlyOptional{},missingImage{},missingOriginal{};
std::atomic<int> failAfter{-1};
std::atomic<bool> blockCreate{};
HANDLE createEntered{},releaseCreate{},secondFinished{};
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
unsigned owned(){unsigned count{};for(const auto& hook:hooks)count+=hook.target&&hook.owned;return count;}
unsigned requiredOwned(){unsigned count{};for(const auto& hook:hooks)count+=hook.target&&hook.owned&&hook.target!=imageBytes+argent::build::rva(0x53a9f0);return count;}
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
void log(const std::string&){++logCalls;if(throwLogs||logCalls==logFailureAt){++logFailures;throw std::bad_alloc{};}}
}
extern "C" {
void* argentHandsResume{};void argentHandsBridge(){}
void* argentFocusResume{};void argentFocusBridge(){}
void* argentMeathookResume{};void argentMeathookBridge(){}
void* argentMeathookGateResume{};void argentMeathookGateBridge(){}
void* argentWallGateResume{};void argentWallGateBridge(){}
void* argentWallImpulseResume{};void argentWallImpulseBridge(){}
}
namespace argent::player {
namespace {
unsigned char* image{};
void* originalHideItem{};void hideItem(){}
void* originalZoomBlend{};void zoomBlend(){}
void* originalZoomMode{};void zoomMode(){}
void* originalZoomFov{};void zoomFov(){}
void* originalItemAnimation{};void updateItemAnimation(){}
using CrucibleEvent=uintptr_t(__fastcall*)(void*,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
CrucibleEvent originalCrucibleEvents[3]{};
template<int> uintptr_t __fastcall crucibleEvent(void*,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t){return 0;}
void* originalAttachmentJoint{};void attachmentJoint(){}
void* originalUpdateHands{};void updateHands(){}
void* originalPlayTraversal{};void playTraversal(){}
void* originalEndBar{};void endBar(){}
void* originalItemTransform{};void itemTransform(){}
void* originalThrowItem{};void throwItem(){}
void* originalFire{};void fire(){}
void* originalWaterMove{};void waterMove(){}
}
}
namespace argent::monkey {
bool install(unsigned char* base){++monkeyCalls;earlyOptional|=requiredOwned()!=21;missingImage|=base!=imageBytes;if(throwMonkey)throw std::bad_alloc{};return monkeyResult;}
}
namespace argent::player {
#ifdef ARGENT_PLAYER_INSTALLATION_BASELINE
#include "../build-perf/player-installation-before/PlayerHookInstallation.inc"
#else
#include "../src/PlayerHookInstallation.inc"
#endif
}

extern "C" MH_STATUS WINAPI MH_CreateHook(void* target,void* detour,void** original){
    if(blockCreate.exchange(false)){SetEvent(createEntered);if(WaitForSingleObject(releaseCreate,5000)!=WAIT_OBJECT_0)return MH_ERROR_MEMORY_ALLOC;}
    std::lock_guard<std::mutex> lock(hookGuard);++createCalls;
    for(const auto& hook:hooks)if(hook.target==target)return MH_ERROR_ALREADY_CREATED;
    if(uintptr_t(target)==failCreate)return MH_ERROR_MEMORY_ALLOC;
    for(auto& hook:hooks)if(!hook.target){
        hook={static_cast<unsigned char*>(target),detour,original,*static_cast<unsigned char*>(target),true,false};
        if(original)*original=reinterpret_cast<void*>(uintptr_t(target)+0x100);
        if(formatFailure&&target==imageBytes+argent::build::rva(0x135f280))failAfter=1;
        return MH_OK;
    }
    return MH_ERROR_MEMORY_ALLOC;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(void* target){
    std::lock_guard<std::mutex> lock(hookGuard);++enableCalls;
    earlyEnable|=requiredOwned()!=21;missingImage|=argent::player::image!=imageBytes;
    for(const auto& hook:hooks)if(hook.target&&hook.owned)missingOriginal|=!hook.original||!*hook.original;
    for(auto& hook:hooks)if(hook.target==target){
        if(!hook.owned){++invalidCleanup;return MH_ERROR_ENABLED;}
        if(uintptr_t(target)==failEnable)return MH_ERROR_MEMORY_PROTECT;
        hook.enabled=true;*hook.target=0xe9;return MH_OK;
    }
    ++invalidCleanup;return MH_ERROR_NOT_CREATED;
}
extern "C" MH_STATUS WINAPI MH_RemoveHook(void* target){
    std::lock_guard<std::mutex> lock(hookGuard);
    for(auto& hook:hooks)if(hook.target==target){
        if(!hook.owned){++invalidCleanup;return MH_ERROR_NOT_CREATED;}
        if(uintptr_t(target)==failRemove)return MH_ERROR_MEMORY_PROTECT;
        *hook.target=hook.byte;hook={};return MH_OK;
    }
    ++invalidCleanup;return MH_ERROR_NOT_CREATED;
}

namespace {
struct Image {
    unsigned char* bytes=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1a00000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Image(){check(bytes!=nullptr,"Test image allocation failed");for(const auto& signature:signatures)std::memcpy(bytes+argent::build::rva(signature.rva)+signature.offset,signature.bytes.data(),signature.bytes.size());}
    ~Image(){VirtualFree(bytes,0,MEM_RELEASE);}
};
void* const originalSentinel=reinterpret_cast<void*>(uintptr_t(0x76543210));
auto originalSlots(){
    using namespace argent::player;
    return std::array<void**,22>{&argentFocusResume,&argentMeathookResume,&argentMeathookGateResume,&originalHideItem,&originalZoomBlend,&originalZoomMode,&originalZoomFov,&originalItemAnimation,
        reinterpret_cast<void**>(&originalCrucibleEvents[0]),reinterpret_cast<void**>(&originalCrucibleEvents[1]),reinterpret_cast<void**>(&originalCrucibleEvents[2]),&originalAttachmentJoint,
        &argentHandsResume,&originalUpdateHands,&originalPlayTraversal,&originalEndBar,&argentWallGateResume,&argentWallImpulseResume,&originalItemTransform,&originalThrowItem,&originalFire,&originalWaterMove};
}
auto detours(){
    using namespace argent::player;
    return std::array<void*,22>{reinterpret_cast<void*>(&argentFocusBridge),reinterpret_cast<void*>(&argentMeathookBridge),reinterpret_cast<void*>(&argentMeathookGateBridge),
        reinterpret_cast<void*>(&hideItem),reinterpret_cast<void*>(&zoomBlend),reinterpret_cast<void*>(&zoomMode),reinterpret_cast<void*>(&zoomFov),reinterpret_cast<void*>(&updateItemAnimation),
        reinterpret_cast<void*>(&crucibleEvent<0>),reinterpret_cast<void*>(&crucibleEvent<1>),reinterpret_cast<void*>(&crucibleEvent<2>),reinterpret_cast<void*>(&attachmentJoint),
        reinterpret_cast<void*>(&argentHandsBridge),reinterpret_cast<void*>(&updateHands),reinterpret_cast<void*>(&playTraversal),reinterpret_cast<void*>(&endBar),
        reinterpret_cast<void*>(&argentWallGateBridge),reinterpret_cast<void*>(&argentWallImpulseBridge),reinterpret_cast<void*>(&itemTransform),reinterpret_cast<void*>(&throwItem),
        reinterpret_cast<void*>(&fire),reinterpret_cast<void*>(&waterMove)};
}
DWORD WINAPI installationWorker(void* second){const bool result=argent::player::install(imageBytes);if(second)SetEvent(secondFinished);return result?0:1;}
void concurrentCase(){
    createEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);releaseCreate=CreateEventW(nullptr,TRUE,FALSE,nullptr);secondFinished=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    check(createEntered&&releaseCreate&&secondFinished,"Installation events unavailable");blockCreate=true;
    const auto first=CreateThread(nullptr,0,installationWorker,nullptr,0,nullptr);check(first!=nullptr,"First installer unavailable");
    const auto entered=WaitForSingleObject(createEntered,5000);const auto second=CreateThread(nullptr,0,installationWorker,reinterpret_cast<void*>(1),0,nullptr);
    const auto premature=second?WaitForSingleObject(secondFinished,100):WAIT_FAILED;SetEvent(releaseCreate);
    const auto firstWait=WaitForSingleObject(first,5000),secondWait=second?WaitForSingleObject(second,5000):WAIT_FAILED;DWORD firstCode{},secondCode{};
    const bool exited=GetExitCodeThread(first,&firstCode)&&second&&GetExitCodeThread(second,&secondCode);
    CloseHandle(first);if(second)CloseHandle(second);CloseHandle(createEntered);CloseHandle(releaseCreate);CloseHandle(secondFinished);
    check(entered==WAIT_OBJECT_0&&premature==WAIT_TIMEOUT&&firstWait==WAIT_OBJECT_0&&secondWait==WAIT_OBJECT_0&&exited&&!firstCode&&!secondCode,"Concurrent installation exposed incomplete ownership");
    check(owned()==22&&createCalls==22&&enableCalls==22&&monkeyCalls==1,"Concurrent installation duplicated committed hooks");
}
void runCase(std::string_view scenario){
    using namespace argent::player;
    hooks[0]={imageBytes+0x100,nullptr,nullptr,0,false,true};
    for(const auto slot:originalSlots())*slot=originalSentinel;
    if(scenario=="concurrent"){concurrentCase();return;}
    bool succeeds=true,waterExpected=true;
    if(scenario=="null")succeeds=false;
    if(scenario=="logs")throwLogs=true;
    if(scenario=="format-oom")formatFailure=true;
    if(scenario=="monkey-refused")monkeyResult=false;
    if(scenario=="monkey-throws")throwMonkey=true;
    if(scenario.rfind("log-",0)==0)logFailureAt=unsigned(std::stoul(std::string(scenario.substr(4))));
    if(scenario=="water-signature"){waterExpected=false;imageBytes[argent::build::rva(0x53a9f0)]^=1;}
    if(scenario=="water-create"){waterExpected=false;failCreate=uintptr_t(imageBytes)+argent::build::rva(0x53a9f0);}
    if(scenario=="water-enable"||scenario=="water-remove-fatal"){waterExpected=false;failEnable=uintptr_t(imageBytes)+argent::build::rva(0x53a9f0);if(scenario=="water-remove-fatal")failRemove=failEnable;}
    if(scenario=="water-conflict"){waterExpected=false;hooks[1]={imageBytes+argent::build::rva(0x53a9f0),nullptr,nullptr,0,false,true};}
    for(const auto prefix:{"signature-"sv,"create-"sv,"enable-"sv,"conflict-"sv})if(scenario.rfind(prefix,0)==0){
        succeeds=false;const auto index=size_t(std::stoul(std::string(scenario.substr(prefix.size()))));const auto& signature=signatures[index];const auto target=imageBytes+argent::build::rva(signature.rva)+signature.offset;
        if(prefix=="signature-")*target^=1;
        else if(prefix=="create-")failCreate=uintptr_t(target);
        else if(prefix=="enable-")failEnable=uintptr_t(target);
        else hooks[1]={target,nullptr,nullptr,*target,false,true};
    }
    if(scenario=="remove-fatal"){succeeds=false;failEnable=uintptr_t(imageBytes)+argent::build::rva(0x135f280);failRemove=uintptr_t(imageBytes)+argent::build::rva(0x134b00e);}
    check(install(scenario=="null"?nullptr:imageBytes)==succeeds,"Installer result disagrees with required ownership");failAfter=-1;
    check(!invalidCleanup&&hooks[0].target==imageBytes+0x100&&hooks[0].enabled,"Installer changed a foreign hook");
    if(succeeds){
        check(owned()==21+unsigned(waterExpected)&&image==imageBytes,"Successful installation lost owned state");
        for(const auto& hook:hooks)if(hook.owned)check(hook.enabled&&hook.original&&*hook.original==reinterpret_cast<void*>(uintptr_t(hook.target)+0x100),"Enabled hook has no retained trampoline");
        const auto slots=originalSlots();const auto expectedDetours=detours();
        for(size_t i=0;i<21+size_t(waterExpected);++i){
            const auto rva=signatures[i==21?25:i].rva;bool found{};
            for(const auto& hook:hooks)if(hook.target==imageBytes+argent::build::rva(rva)){found=true;check(hook.original==slots[i]&&hook.detour==expectedDetours[i],"Hook target received the wrong detour or trampoline slot");}
            check(found,"Expected hook target was not installed");
        }
        if(!waterExpected)check(originalWaterMove==originalSentinel,"Failed optional hook left a stale trampoline");
        if(scenario=="prepared")check(!earlyEnable&&!earlyOptional&&!missingImage&&!missingOriginal,"Activation began before required dependencies were prepared");
        if(scenario=="repeat"||scenario=="different-image"){
            const auto before=createCalls;check(install(imageBytes)&&createCalls==before,"Repeated installation inspected patched signatures or duplicated hooks");
            if(scenario=="different-image"){Image other;check(!install(other.bytes)&&image==imageBytes&&createCalls==before,"Another image replaced committed ownership");}
        }
    }else{
        check(!owned()&&!image&&!monkeyCalls,"Failed required setup retained partial hooks or optional state");
        for(const auto slot:originalSlots())check(*slot==originalSentinel,"Rollback retained a freed trampoline");
        if(scenario.rfind("signature-",0)==0)check(!createCalls&&!enableCalls,"Signature refusal changed native hooks");
        if(scenario.rfind("create-",0)==0)check(!enableCalls,"A trampoline creation failure activated a partial group");
        if(scenario.rfind("create-",0)==0||scenario.rfind("enable-",0)==0){failCreate=failEnable=0;check(install(imageBytes)&&owned()==22,"Clean installation retry failed");}
        if(scenario.rfind("conflict-",0)==0)check(hooks[1].target&&hooks[1].enabled&&!hooks[1].owned,"Collision rollback removed the foreign hook");
    }
    if(throwLogs||logFailureAt)check(logFailures>0,"Diagnostic fault was not exercised");
    if(formatFailure)check(allocationFailures==1,"Diagnostic allocation fault was not exercised");
}
bool child(const wchar_t* executable,const std::wstring& scenario,bool store){
    std::wstring command=L"\""+std::wstring(executable)+L"\" "+scenario+(store?L" store":L"");STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Installer child unavailable");
    const auto wait=WaitForSingleObject(process.hProcess,15000);DWORD code{};if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,INFINITE);}
    const DWORD expected=scenario.find(L"-fatal")!=std::wstring::npos?0xc0000602u:0u;
    const bool passed=wait==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&code)&&code==expected;
    CloseHandle(process.hThread);CloseHandle(process.hProcess);if(!passed)std::wcerr<<scenario<<L" store="<<store<<L" exit="<<code<<L'\n';return passed;
}
}

int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    try{
        if(argc>1){argent::build::microsoftStore=argc>2;Image image;imageBytes=image.bytes;runCase(argv[1]);return 0;}
        wchar_t executable[32768]{};check(GetModuleFileNameW(nullptr,executable,32768)!=0,"Executable path unavailable");unsigned cases{},failures{};
        for(const bool store:{false,true}){
            for(const auto scenario:{L"normal",L"null",L"prepared",L"repeat",L"different-image",L"logs",L"format-oom",L"monkey-refused",L"monkey-throws",L"water-signature",L"water-create",L"water-enable",L"water-conflict",L"remove-fatal",L"water-remove-fatal",L"concurrent"}){++cases;failures+=!child(executable,scenario,store);}
            for(unsigned i=0;i<25;++i){++cases;failures+=!child(executable,L"signature-"+std::to_wstring(i),store);}
            for(unsigned i=0;i<21;++i)for(const auto prefix:{L"create-",L"enable-",L"conflict-"}){++cases;failures+=!child(executable,prefix+std::to_wstring(i),store);}
            for(unsigned i=1;i<=12;++i){++cases;failures+=!child(executable,L"log-"+std::to_wstring(i),store);}
        }
        std::cout<<cases<<" production player installation scenarios, "<<failures<<" failures\n";return failures?1:0;
    }catch(const std::exception& error){failAfter=-1;std::cerr<<error.what()<<'\n';return 1;}
}
