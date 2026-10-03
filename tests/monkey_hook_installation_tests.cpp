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
#ifdef ARGENT_MONKEY_BASELINE
#include "../build-perf/monkey-hooks-before/src/EternalCameraHook.h"
#include "../build-perf/monkey-hooks-before/src/EternalBuildProfile.h"
#else
#include "../src/EternalCameraHook.h"
#include "../src/EternalBuildProfile.h"
#endif

namespace {
using namespace std::literals;
struct Signature {uintptr_t rva;ptrdiff_t offset;std::string_view bytes;};
constexpr Signature signatures[]{
    {0x13e8950,0,"\x48\x8b\x15"sv},{0x13e8950,18,"\x83\x7a\x08\0\x41\x0f\x44\xc0\x48\x03\xc1\xc3"sv},
    {0x13e8970,0,"\x48\x8b\x15"sv},{0x13e8970,18,"\x83\x7a\x08\0\x41\x0f\x44\xc0\x48\x03\xc1\xc3"sv},
    {0x1399bf7,-6,"\xff\x90\x70\x04\0\0"sv},{0x139a842,-6,"\xff\x90\x70\x04\0\0"sv},{0x139a8f0,-6,"\xff\x90\x70\x04\0\0"sv},
    {0x1397a7c,-6,"\xff\x90\x70\x04\0\0"sv},{0x1397aef,-6,"\xff\x90\x70\x04\0\0"sv},
    {0x139a79a,-6,"\xff\x90\x78\x04\0\0"sv},{0x1399c83,-6,"\xff\x90\x78\x04\0\0"sv},{0x139a295,-6,"\xff\x90\x78\x04\0\0"sv},
    {0x139a85f,-6,"\xff\x90\x78\x04\0\0"sv},{0x139a90e,-6,"\xff\x90\x78\x04\0\0"sv},
    {0x1398d5d,0,"\x0f\x28\x15"sv},{0x13989b0,0,"\x40\x53\x48\x83\xec\x20\x48\x8b\x01\x48\x8b\xd9"sv},
    {0xfbdc3a,-5,"\xe8"sv},{0xfbddc9,-5,"\xe8"sv},
    {0x13e5be0,0,"\x48\x8b\x89\x88\xce\x04\0\x48\x85\xc9\x0f\x85"sv},{0x13e5be0,16,"\xc3"sv},
    {0xfd6790,0,"\x40\x53\x48\x83\xec\x20\x48\x83\xb9\x98\x02\0\0\0"sv},{0xfd6790,0x13,"\x84\xd2\x74\x38"sv},{0xfd6790,0x5b,"\xe9"sv},
    {0xb420e0,0,"\x48\x89\x5c\x24\x08\x48\x89\x74\x24\x18\x57\x48\x83\xec\x20"sv},{0x1398a8c,-5,"\xe8"sv}
};
struct Branch {uintptr_t rva;ptrdiff_t offset,end;uintptr_t target;};
constexpr Branch branches[]{{0xfbdc3a,-4,0,0x13989b0},{0xfbddc9,-4,0,0x13989b0},{0x13e5be0,12,16,0xfd6790},{0xfd6790,0x5c,0x60,0xfbbf60},{0x1398a8c,-4,0,0xb420e0}};
constexpr uintptr_t targetRvas[]{0x13e8950,0x13e8970,0x1398d5d,0x13989b0,0xb420e0};
struct Hook {unsigned char* target{};void* detour{};void** original{};unsigned char byte{};bool owned{},enabled{};};
std::array<Hook,7> hooks{};
std::mutex hookGuard;
unsigned char* imageBytes{};
uintptr_t failCreate{},failEnable{},failRemove{};
unsigned createCalls{},enableCalls{},invalidCleanup{},logCalls{},logFailures{},allocationFailures{};
unsigned logFailureAt{};
bool throwLogs{},missingDependencies{};
std::atomic<int> failAfter{-1};
std::atomic<bool> blockCreate{};
HANDLE createEntered{},releaseCreate{},secondFinished{};
ULONGLONG testTick(){return 10000;}
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
unsigned owned(){unsigned count{};for(const auto& hook:hooks)count+=hook.target&&hook.owned;return count;}
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
namespace argent::camera {
bool wallClimbView(float* axis) noexcept {const float value[]{0,.8f,.6f,-1,0,0,0,-.6f,.8f};std::memcpy(axis,value,sizeof(value));return true;}
bool monkeyBarPose(float* origin,float* axis,float* offset) noexcept {
    const float value[]{1,2,3};std::memcpy(origin,value,sizeof(value));if(offset)std::memcpy(offset,value,sizeof(value));return wallClimbView(axis);
}
}
extern "C" void argentMonkeyBridge(){}
#define GetTickCount64 testTick
#ifdef ARGENT_MONKEY_BASELINE
#include "../build-perf/monkey-hooks-before/src/EternalMonkeyBar.cpp"
#else
#include "../src/EternalMonkeyBar.cpp"
#endif
#undef GetTickCount64

extern "C" MH_STATUS WINAPI MH_CreateHook(void* target,void* detour,void** original){
    if(blockCreate.exchange(false)){SetEvent(createEntered);if(WaitForSingleObject(releaseCreate,5000)!=WAIT_OBJECT_0)return MH_ERROR_MEMORY_ALLOC;}
    std::lock_guard<std::mutex> lock(hookGuard);++createCalls;
    for(const auto& hook:hooks)if(hook.target==target)return MH_ERROR_ALREADY_CREATED;
    if(uintptr_t(target)==failCreate)return MH_ERROR_MEMORY_ALLOC;
    for(auto& hook:hooks)if(!hook.target){hook={static_cast<unsigned char*>(target),detour,original,*static_cast<unsigned char*>(target),true,false};if(original)*original=reinterpret_cast<void*>(uintptr_t(target)+0x100);return MH_OK;}
    return MH_ERROR_MEMORY_ALLOC;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(void* target){
    std::lock_guard<std::mutex> lock(hookGuard);++enableCalls;
    missingDependencies|=owned()!=5||argent::monkey::image!=imageBytes||reinterpret_cast<void*>(argent::monkey::nativeStopDash)!=imageBytes+argent::build::rva(0x13e5be0);
    for(const auto& hook:hooks)if(hook.owned)missingDependencies|=!hook.original||!*hook.original;
    for(auto& hook:hooks)if(hook.target==target){
        if(!hook.owned){++invalidCleanup;return MH_ERROR_ENABLED;}if(uintptr_t(target)==failEnable)return MH_ERROR_MEMORY_PROTECT;
        hook.enabled=true;*hook.target=0xe9;return MH_OK;
    }
    ++invalidCleanup;return MH_ERROR_NOT_CREATED;
}
extern "C" MH_STATUS WINAPI MH_DisableHook(void* target){
    std::lock_guard<std::mutex> lock(hookGuard);
    for(auto& hook:hooks)if(hook.target==target){if(!hook.owned){++invalidCleanup;return MH_ERROR_NOT_CREATED;}hook.enabled=false;*hook.target=hook.byte;return MH_OK;}
    ++invalidCleanup;return MH_ERROR_NOT_CREATED;
}
extern "C" MH_STATUS WINAPI MH_RemoveHook(void* target){
    std::lock_guard<std::mutex> lock(hookGuard);
    for(auto& hook:hooks)if(hook.target==target){
        if(!hook.owned){++invalidCleanup;return MH_ERROR_NOT_CREATED;}if(uintptr_t(target)==failRemove)return MH_ERROR_MEMORY_PROTECT;
        *hook.target=hook.byte;hook={};return MH_OK;
    }
    ++invalidCleanup;return MH_ERROR_NOT_CREATED;
}

namespace {
struct Image {
    unsigned char* bytes=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1400000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Image(){
        check(bytes!=nullptr,"Test image unavailable");
        for(const auto& signature:signatures)std::memcpy(bytes+argent::build::rva(signature.rva)+signature.offset,signature.bytes.data(),signature.bytes.size());
        for(const auto& branch:branches){const auto delta=int(argent::build::rva(branch.target)-argent::build::rva(branch.rva)-branch.end);std::memcpy(bytes+argent::build::rva(branch.rva)+branch.offset,&delta,4);}
        std::memcpy(bytes+argent::build::rva(0xd9d168)-6,"\xff\x90\x70\x04\0\0",6);std::memcpy(bytes+argent::build::rva(0xd9d185)-6,"\xff\x90\x78\x04\0\0",6);
    }
    ~Image(){VirtualFree(bytes,0,MEM_RELEASE);}
};
void* const originalSentinel=reinterpret_cast<void*>(uintptr_t(0x76543210));
auto originalSlots(){using namespace argent::monkey;return std::array<void**,5>{reinterpret_cast<void**>(&nativeAxis),reinterpret_cast<void**>(&nativeOrigin),&argentMonkeyResume,reinterpret_cast<void**>(&nativeCancel),reinterpret_cast<void**>(&nativeCompletion)};}
auto detours(){using namespace argent::monkey;return std::array<void*,5>{reinterpret_cast<void*>(&axis),reinterpret_cast<void*>(&origin),reinterpret_cast<void*>(&argentMonkeyBridge),reinterpret_cast<void*>(&cancel),reinterpret_cast<void*>(&completion)};}
DWORD WINAPI installationWorker(void* second){const bool result=argent::monkey::install(imageBytes);if(second)SetEvent(secondFinished);return result?0:1;}
void concurrentCase(){
    createEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);releaseCreate=CreateEventW(nullptr,TRUE,FALSE,nullptr);secondFinished=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    check(createEntered&&releaseCreate&&secondFinished,"Installer events unavailable");blockCreate=true;
    const auto first=CreateThread(nullptr,0,installationWorker,nullptr,0,nullptr);check(first!=nullptr,"First installer unavailable");
    const auto entered=WaitForSingleObject(createEntered,5000);const auto second=CreateThread(nullptr,0,installationWorker,reinterpret_cast<void*>(1),0,nullptr);
    const auto premature=second?WaitForSingleObject(secondFinished,100):WAIT_FAILED;SetEvent(releaseCreate);
    const auto firstWait=WaitForSingleObject(first,5000),secondWait=second?WaitForSingleObject(second,5000):WAIT_FAILED;DWORD firstCode{},secondCode{};
    const bool exited=GetExitCodeThread(first,&firstCode)&&second&&GetExitCodeThread(second,&secondCode);
    CloseHandle(first);if(second)CloseHandle(second);CloseHandle(createEntered);CloseHandle(releaseCreate);CloseHandle(secondFinished);
    check(entered==WAIT_OBJECT_0&&premature==WAIT_TIMEOUT&&firstWait==WAIT_OBJECT_0&&secondWait==WAIT_OBJECT_0&&exited&&!firstCode&&!secondCode,"Concurrent installer exposed partial state");
    check(owned()==5&&createCalls==5&&enableCalls==5,"Concurrent installer duplicated hooks");
}
void runCase(std::string_view scenario){
    using namespace argent::monkey;
    hooks[0]={imageBytes+0x100,nullptr,nullptr,0,false,true};
    for(const auto slot:originalSlots())*slot=originalSentinel;nativeStopDash=reinterpret_cast<StopDash>(originalSentinel);facingEnabled=true;
    if(scenario=="concurrent"){concurrentCase();return;}
    bool succeeds=scenario!="null";
    if(scenario=="logs")throwLogs=true;
    if(scenario=="log-first")logFailureAt=1;
    if(scenario=="log-final")logFailureAt=2;
    if(scenario=="format-oom")failAfter=1;
    if(scenario=="optional-facing")imageBytes[argent::build::rva(0xd9d168)-6]^=1;
    if(scenario.rfind("signature-",0)==0){
        succeeds=false;const auto index=size_t(std::stoul(std::string(scenario.substr(10))));
        if(index<std::size(signatures)){const auto& signature=signatures[index];imageBytes[argent::build::rva(signature.rva)+signature.offset]^=1;}
        else {const auto& branch=branches[index-std::size(signatures)];imageBytes[argent::build::rva(branch.rva)+branch.offset]^=1;}
    }
    for(const auto prefix:{"create-"sv,"enable-"sv,"conflict-"sv,"remove-fatal-"sv})if(scenario.rfind(prefix,0)==0){
        succeeds=false;const auto index=size_t(std::stoul(std::string(scenario.substr(prefix.size()))));const auto target=imageBytes+argent::build::rva(targetRvas[index]);
        if(prefix=="create-")failCreate=uintptr_t(target);
        else if(prefix=="enable-")failEnable=uintptr_t(target);
        else if(prefix=="remove-fatal-"){failEnable=uintptr_t(imageBytes)+argent::build::rva(targetRvas[4]);failRemove=uintptr_t(target);}
        else hooks[1]={target,nullptr,nullptr,*target,false,true};
    }
    const bool result=install(scenario=="null"?nullptr:imageBytes);failAfter=-1;
    if(result!=succeeds)std::cerr<<"result="<<result<<" creates="<<createCalls<<" enables="<<enableCalls<<" logs="<<logCalls<<'\n';
    check(result==succeeds,"Installer result disagrees with owned state");
    check(!invalidCleanup&&hooks[0].target==imageBytes+0x100&&hooks[0].enabled,"Installer changed unrelated hooks");
    if(succeeds){
        check(owned()==5&&image==imageBytes&&facingEnabled==(scenario!="optional-facing")&&reinterpret_cast<void*>(nativeStopDash)==imageBytes+argent::build::rva(0x13e5be0),"Committed adapter state is incomplete");
        const auto slots=originalSlots();const auto expectedDetours=detours();
        for(size_t i=0;i<5;++i){bool found{};for(const auto& hook:hooks)if(hook.target==imageBytes+argent::build::rva(targetRvas[i])){found=true;check(hook.enabled&&hook.original==slots[i]&&hook.detour==expectedDetours[i]&&*slots[i]==reinterpret_cast<void*>(uintptr_t(hook.target)+0x100),"Hook wiring or retained trampoline is wrong");}check(found,"Expected target is missing");}
        if(scenario=="prepared")check(!missingDependencies,"Activation preceded native dependency publication");
        if(scenario=="repeat"||scenario=="different-image"){
            const auto before=createCalls;check(install(imageBytes)&&createCalls==before,"Repeat installation rechecked patched bytes");
            if(scenario=="different-image"){Image other;check(!install(other.bytes)&&image==imageBytes&&createCalls==before,"Another image replaced the adapter");}
        }
    }else{
        check(!owned()&&!image&&facingEnabled&&reinterpret_cast<void*>(nativeStopDash)==originalSentinel,"Failed setup retained published state");
        for(const auto slot:originalSlots())check(*slot==originalSentinel,"Rollback retained a freed trampoline");
        if(scenario.rfind("signature-",0)==0)check(!createCalls&&!enableCalls,"Contract refusal changed native hooks");
        if(scenario.rfind("create-",0)==0)check(!enableCalls,"Creation failure enabled an incomplete group");
        if(scenario.rfind("create-",0)==0||scenario.rfind("enable-",0)==0){failCreate=failEnable=0;check(install(imageBytes)&&owned()==5,"Clean retry failed");}
        if(scenario.rfind("conflict-",0)==0)check(hooks[1].target&&hooks[1].enabled&&!hooks[1].owned,"Rollback changed foreign ownership");
    }
    if(throwLogs||logFailureAt)check(logFailures>0,"Logging fault was not exercised");
    if(scenario=="format-oom")check(allocationFailures==1,"Formatting fault was not exercised");
}
unsigned nativeCalls{},stops{},cancellations{};
bool nativeComplete{true};
void* stoppedOwner{};
bool refill{};
const float nativeView[]{4,5,6,1,0,0,0,1,0};
const float* __fastcall nativeGetter(void*){return nativeView;}
void __fastcall nativeStop(void* owner,bool refillMeter){++stops;stoppedOwner=owner;refill=refillMeter;}
bool __fastcall nativeCompleted(void*,unsigned short*,int){++nativeCalls;return nativeComplete;}
void __fastcall nativeCancelled(void*){++cancellations;}
void runCallback(std::string_view scenario){
    using namespace argent;using namespace argent::monkey;
    std::array<unsigned char,0x50000> playerBytes{};std::array<unsigned char,0x2b0> controller{};std::array<unsigned char,0x120> dash{};std::array<unsigned char,32> fsm{};
    const auto owner=uintptr_t(playerBytes.data()),controllerAddress=uintptr_t(controller.data()),dashAddress=uintptr_t(dash.data()),fsmAddress=uintptr_t(fsm.data());
    const auto mechanic=playerBytes.data()+0x36e48;const auto handle=reinterpret_cast<unsigned short*>(mechanic+0x108);const int state=2;
    std::memcpy(playerBytes.data()+0x4ce88,&controllerAddress,8);std::memcpy(controller.data()+0x290,&owner,8);std::memcpy(controller.data()+0x298,&dashAddress,8);dash[0x118]=1;
    std::memcpy(mechanic+0x18,&owner,8);std::memcpy(mechanic+0x50,&fsmAddress,8);std::memcpy(fsm.data()+12,&state,4);
    presentation::player=owner;presentation::skippedMonkeyBarOwner=owner;presentation::worldPresentation=true;
    nativeAxis=nativeOrigin=&nativeGetter;nativeStopDash=&nativeStop;nativeCancel=&nativeCancelled;nativeCompletion=&nativeCompleted;
    entryOwner=owner;entryTick=testTick();facingEnabled=true;
    throwLogs=scenario.find("-log")!=std::string_view::npos;const bool allocation=scenario.find("-oom")!=std::string_view::npos;if(allocation)failAfter=1;
    if(scenario.rfind("callback-accepted-",0)==0){acceptedBar(owner);check(stops==1&&stoppedOwner==reinterpret_cast<void*>(owner)&&!refill&&entryOwner.load()==owner&&entryTick.load()==testTick(),"Accepted-bar diagnostic changed native dash handoff");}
    else if(scenario.rfind("callback-waiting-",0)==0)check(!completionFor(nullptr,handle,1,0x1398a8c)&&nativeCalls==1&&stops==1&&!refill,"Waiting diagnostic changed completion or skipped native stop");
    else if(scenario.rfind("callback-timeout-",0)==0){entryTick=testTick()-100;check(completionFor(nullptr,handle,1,0x1398a8c)&&nativeCalls==1&&!stops,"Timeout diagnostic changed native completion");}
    else if(scenario.rfind("callback-facing-",0)==0){
        const auto forward=axisFor(reinterpret_cast<void*>(owner),0xd9d168);check(forward!=nativeView&&forward[1]==.8f&&forward[2]==.6f,"Facing diagnostic lost the HMD result");
        check(originFor(reinterpret_cast<void*>(owner),0xd9d185)[0]==4&&!facing.valid&&originFor(reinterpret_cast<void*>(owner),0xd9d185)==nativeView,"Facing diagnostic changed native-origin snapshot consumption");
    }
    else if(scenario.rfind("callback-cancel-",0)==0){cancelFor(mechanic,0xfbdc3a);check(!cancellations,"Cancellation diagnostic abandoned protected dash handoff");}
    else if(scenario.rfind("callback-launch-",0)==0){float x=9,y=10;argentMonkeyLaunch(mechanic,&x,&y);check(x==0&&y==1,"Launch diagnostic changed the selected HMD direction");}
    else if(scenario=="callback-native-false"){nativeComplete=false;check(!completionFor(nullptr,handle,1,0x1398a8c)&&nativeCalls==1&&!stops&&!logCalls,"Adapter changed a native incomplete result");}
    else if(scenario=="callback-remote-facing")check(axisFor(reinterpret_cast<void*>(owner+1),0xd9d168)==nativeView&&!facing.valid&&!logCalls,"Adapter changed a remote native getter result");
    else check(false,"Unknown callback scenario");
    failAfter=-1;
    if(throwLogs)check(logFailures==1,"Callback logging failure was not exercised");
    if(allocation)check(allocationFailures==1,"Callback formatting failure was not exercised");
}
bool child(const wchar_t* executable,const std::wstring& scenario,bool store){
    std::wstring command=L"\""+std::wstring(executable)+L"\" "+scenario+(store?L" store":L"");STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Installer child unavailable");
    const auto wait=WaitForSingleObject(process.hProcess,15000);DWORD code{};if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,INFINITE);}
    const DWORD expected=scenario.rfind(L"remove-fatal-",0)==0?0xc0000602u:0u;
    const bool passed=wait==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&code)&&code==expected;CloseHandle(process.hThread);CloseHandle(process.hProcess);
    if(!passed)std::wcerr<<scenario<<L" store="<<store<<L" exit="<<code<<L'\n';return passed;
}
}

int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);SetEnvironmentVariableW(L"ARGENT_EXTENDED_LOGGING",L"1");
    try{
        const bool callbacksOnly=argc>1&&std::string_view(argv[1])=="--callbacks";
        if(argc>1&&!callbacksOnly){argent::build::microsoftStore=argc>2;Image image;imageBytes=image.bytes;const std::string_view scenario=argv[1];if(scenario.rfind("callback-",0)==0)runCallback(scenario);else runCase(scenario);return 0;}
        wchar_t executable[32768]{};check(GetModuleFileNameW(nullptr,executable,32768)!=0,"Executable path unavailable");unsigned cases{},failures{};
        for(const bool store:{false,true}){
            if(!callbacksOnly){
            for(const auto scenario:{L"normal",L"null",L"prepared",L"repeat",L"different-image",L"logs",L"log-first",L"log-final",L"format-oom",L"optional-facing",L"concurrent"}){++cases;failures+=!child(executable,scenario,store);}
            for(unsigned i=0;i<std::size(signatures)+std::size(branches);++i){++cases;failures+=!child(executable,L"signature-"+std::to_wstring(i),store);}
            for(unsigned i=0;i<5;++i)for(const auto prefix:{L"create-",L"enable-",L"conflict-",L"remove-fatal-"}){++cases;failures+=!child(executable,prefix+std::to_wstring(i),store);}
            }
            for(const auto kind:{L"accepted",L"waiting",L"timeout",L"facing",L"cancel",L"launch"})for(const auto mode:{L"normal",L"log",L"oom"}){++cases;failures+=!child(executable,L"callback-"+std::wstring(kind)+L"-"+mode,store);}
            for(const auto scenario:{L"callback-native-false",L"callback-remote-facing"}){++cases;failures+=!child(executable,scenario,store);}
        }
        std::cout<<cases<<" production monkey hook scenarios, "<<failures<<" failures\n";return failures?1:0;
    }catch(const std::exception& error){failAfter=-1;std::cerr<<error.what()<<'\n';return 1;}
}
