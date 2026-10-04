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
#include "../src/EternalCameraHook.h"
#include "../src/EternalPresentation.h"

namespace {
using namespace std::literals;
struct Signature {uintptr_t rva;std::string_view bytes;};
constexpr Signature signatures[]{
    {0x133daa0,"\x40\x55\x53\x56\x57\x48\x8d\xac\x24\x58\xff\xff\xff\x48\x81\xec\xa8\x01\0\0"sv},
    {0x12c07c0,"\x48\x89\x5c\x24\x08\x57\x48\x83\xec\x30"sv},
    {0x12c0620,"\x48\x89\x5c\x24\x08\x48\x89\x6c\x24\x10\x48\x89\x74\x24\x18\x48\x89\x7c\x24\x20"sv}
};
struct Hook {unsigned char* target{};void* detour{};void** original{};unsigned char byte{};bool owned{},enabled{};};
std::array<Hook,3> hooks{};
std::mutex hookGuard;
unsigned char* imageBytes{};
bool failCreate{},failEnable{},failRemove{},throwLogs{},missingDependencies{};
unsigned createCalls{},enableCalls{},invalidCleanup{},logCalls{},logFailures{},allocationFailures{};
std::atomic<int> failAfter{-1};
std::atomic<bool> blockCreate{};
HANDLE createEntered{},releaseCreate{},secondFinished{};
uintptr_t failRead{};
unsigned nativeCalls{},views{},axes{},physics{};
uint64_t buttons{},oldButtons{};
bool aimReady{true},invalidAim{},invalidDelta{},nativeThrows{},viewThrows{},basisThrows{};
const void* nativePrevious{};const void* nativeCurrent{};void* nativeObject{};
ULONGLONG testTick(){return 10000;}
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
unsigned owned(){unsigned count{};for(const auto& hook:hooks)count+=hook.target&&hook.owned;return count;}
BOOL WINAPI testRead(HANDLE process,LPCVOID address,LPVOID output,SIZE_T size,SIZE_T* got){
    if(uintptr_t(address)==failRead){if(got)*got=0;return FALSE;}
    return ReadProcessMemory(process,address,output,size,got);
}
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
void log(const std::string&){++logCalls;if(throwLogs){++logFailures;throw std::bad_alloc{};}}
}
namespace argent::camera {
void publishPhysics(uintptr_t,XrVector3f) noexcept {++physics;}
bool revenantAim(uintptr_t,float* axis) noexcept {
    const float basis[]{0,.8660254f,.5f,-1,0,0,0,-.5f,.8660254f};std::memcpy(axis,basis,sizeof(basis));if(invalidAim)axis[0]=NAN;return aimReady;
}
}
#define GetTickCount64 testTick
#define ReadProcessMemory testRead
#ifdef ARGENT_REVENANT_BASELINE
#include "../build-perf/revenant-hooks-before/EternalRevenant.cpp"
#else
#include "../src/EternalRevenant.cpp"
#endif
#undef ReadProcessMemory
#undef GetTickCount64

extern "C" MH_STATUS WINAPI MH_CreateHook(void* target,void* detour,void** original){
    if(blockCreate.exchange(false)){SetEvent(createEntered);if(WaitForSingleObject(releaseCreate,5000)!=WAIT_OBJECT_0)return MH_ERROR_MEMORY_ALLOC;}
    std::lock_guard<std::mutex> lock(hookGuard);++createCalls;
    for(const auto& hook:hooks)if(hook.target==target)return MH_ERROR_ALREADY_CREATED;
    if(failCreate)return MH_ERROR_MEMORY_ALLOC;
    for(auto& hook:hooks)if(!hook.target){hook={static_cast<unsigned char*>(target),detour,original,*static_cast<unsigned char*>(target),true,false};if(original)*original=reinterpret_cast<void*>(uintptr_t(target)+0x100);return MH_OK;}
    return MH_ERROR_MEMORY_ALLOC;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(void* target){
    using namespace argent::revenant;std::lock_guard<std::mutex> lock(hookGuard);++enableCalls;
    missingDependencies|=owned()!=1||!original||reinterpret_cast<void*>(setBasis)!=imageBytes+argent::build::rva(0x12c07c0)||reinterpret_cast<void*>(setView)!=imageBytes+argent::build::rva(0x12c0620)||expectedType.load()!=uintptr_t(imageBytes)+argent::build::rva(0x2d99918)||installed.load();
    for(auto& hook:hooks)if(hook.target==target){if(!hook.owned){++invalidCleanup;return MH_ERROR_ENABLED;}if(failEnable)return MH_ERROR_MEMORY_PROTECT;hook.enabled=true;*hook.target=0xe9;return MH_OK;}
    ++invalidCleanup;return MH_ERROR_NOT_CREATED;
}
extern "C" MH_STATUS WINAPI MH_RemoveHook(void* target){
    std::lock_guard<std::mutex> lock(hookGuard);
    for(auto& hook:hooks)if(hook.target==target){if(!hook.owned){++invalidCleanup;return MH_ERROR_NOT_CREATED;}if(failRemove)return MH_ERROR_MEMORY_PROTECT;*hook.target=hook.byte;hook={};return MH_OK;}
    ++invalidCleanup;return MH_ERROR_NOT_CREATED;
}

namespace {
struct Image {
    unsigned char* bytes=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Image(){
        check(bytes!=nullptr,"Test image unavailable");for(const auto& signature:signatures)std::memcpy(bytes+argent::build::rva(signature.rva),signature.bytes.data(),signature.bytes.size());
        const auto method=uintptr_t(bytes)+argent::build::rva(0x133daa0);std::memcpy(bytes+argent::build::rva(0x2d99918)+0x1968,&method,8);
    }
    ~Image(){VirtualFree(bytes,0,MEM_RELEASE);}
};
void* const originalSentinel=reinterpret_cast<void*>(uintptr_t(0x76543210));
constexpr uintptr_t previousType=0x12345678;
DWORD WINAPI installationWorker(void* second){const bool result=argent::revenant::install(imageBytes);if(second)SetEvent(secondFinished);return result?0:1;}
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
    check(owned()==1&&createCalls==1&&enableCalls==1,"Concurrent installer duplicated hooks");
}
void runCase(std::string_view scenario){
    using namespace argent::revenant;const auto target=imageBytes+argent::build::rva(0x133daa0);
    hooks[0]={imageBytes+0x100,nullptr,nullptr,0,false,true};
    original=reinterpret_cast<Think>(originalSentinel);setBasis=reinterpret_cast<SetBasis>(originalSentinel);setView=reinterpret_cast<SetView>(originalSentinel);expectedType=previousType;
    if(scenario=="concurrent"){concurrentCase();return;}
    bool succeeds=scenario!="null";
    if(scenario=="logs")throwLogs=true;
    if(scenario=="format-oom")failAfter=1;
    if(scenario.rfind("signature-",0)==0){succeeds=false;const auto index=size_t(std::stoul(std::string(scenario.substr(10))));imageBytes[index<3?argent::build::rva(signatures[index].rva):argent::build::rva(0x2d99918)+0x1968]^=1;}
    if(scenario=="create"){succeeds=false;failCreate=true;}
    if(scenario=="enable"||scenario=="remove-fatal"){succeeds=false;failEnable=true;failRemove=scenario=="remove-fatal";}
    if(scenario=="conflict"){succeeds=false;hooks[1]={target,nullptr,nullptr,*target,false,true};}
    const bool result=install(scenario=="null"?nullptr:imageBytes);failAfter=-1;
    check(result==succeeds,"Installer result disagrees with native ownership");
    check(!invalidCleanup&&hooks[0].target==imageBytes+0x100&&hooks[0].enabled,"Installer changed unrelated hooks");
    if(succeeds){
        check(owned()==1&&installed.load()&&expectedType.load()==uintptr_t(imageBytes)+argent::build::rva(0x2d99918)&&!missingDependencies,"Committed native dependencies are incomplete");
        check(hooks[1].target==target&&hooks[1].enabled&&hooks[1].detour==reinterpret_cast<void*>(&think)&&reinterpret_cast<void*>(original)==reinterpret_cast<void*>(uintptr_t(target)+0x100),"Hook wiring or retained trampoline is wrong");
        if(scenario=="repeat"||scenario=="different-image"){
            const auto before=createCalls;check(install(imageBytes)&&createCalls==before,"Repeat installation rechecked patched bytes");
            if(scenario=="different-image"){Image other;check(!install(other.bytes)&&expectedType.load()==uintptr_t(imageBytes)+argent::build::rva(0x2d99918)&&createCalls==before,"Another image replaced native dependencies");}
        }
    }else{
        check(!owned()&&!installed.load()&&expectedType.load()==previousType&&reinterpret_cast<void*>(original)==originalSentinel&&reinterpret_cast<void*>(setBasis)==originalSentinel&&reinterpret_cast<void*>(setView)==originalSentinel,"Failed setup retained native dependency state");
        if(scenario.rfind("signature-",0)==0||scenario=="null")check(!createCalls&&!enableCalls,"Contract refusal changed native hooks");
        if(scenario=="create")check(!enableCalls,"Failed creation enabled a hook");
        if(scenario=="create"||scenario=="enable"){failCreate=failEnable=false;check(install(imageBytes)&&owned()==1,"Clean retry failed");}
        if(scenario=="conflict")check(hooks[1].target==target&&hooks[1].enabled&&!hooks[1].owned,"Installer changed a foreign hook");
    }
    if(throwLogs)check(logFailures==1,"Logging fault was not exercised");
    if(scenario=="format-oom")check(allocationFailures==1,"Formatting fault was not exercised");
}
void __fastcall nativeBasis(void*,const float*){++axes;if(basisThrows)throw std::bad_alloc{};}
void __fastcall nativeView(void* object,const float*,bool force){
    ++views;check(force,"Native local-view setter lost force semantics");if(viewThrows)throw std::bad_alloc{};
    float delta[]{12,-40,0};if(invalidDelta)delta[0]=NAN;std::memcpy(static_cast<unsigned char*>(object)+0x58e8+0x3fa8,delta,12);
}
void __fastcall nativeThink(void* object,const void* previous,const void* current){
    ++nativeCalls;nativeObject=object;nativePrevious=previous;nativeCurrent=current;if(nativeThrows)throw std::bad_alloc{};
    buttons=oldButtons=0;if(current)std::memcpy(&buttons,static_cast<const unsigned char*>(current)+0x10,8);if(previous)std::memcpy(&oldButtons,static_cast<const unsigned char*>(previous)+0x10,8);
}
void runCallback(std::string_view scenario){
    using namespace argent;using namespace argent::revenant;
    std::array<unsigned char,0x8900> playerBytes{};std::array<unsigned char,0x38000> demon{};std::array<unsigned char,commandSize> prior{},command{};
    const auto p=uintptr_t(playerBytes.data()),d=uintptr_t(demon.data());
    const auto put=[](uintptr_t address,const auto& value){std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));};
    put(p,uintptr_t(42));put(d,uintptr_t(84));put(p+0x88b0,Handle{7,7,d});put(d+0x20a08,Handle{8,8,p});demon[0x20a48]=demon[0x20a49]=1;
    const std::array<uint64_t,5> bindings{1,0x4000000,0x400000,4,0x100000000};put(d+0x37490,bindings);const auto mask=actionButtons(0,bindings,true,true,true,true);
    presentation::player=p;presentation::playerVtable=42;presentation::gameplayInput=true;expectedType=84;
    original=&nativeThink;setBasis=&nativeBasis;setView=&nativeView;
    input::Snapshot sample{};sample.active=true;sample.tick=testTick();sample.revenantActor=d;sample.pad.bLeftTrigger=sample.pad.bRightTrigger=255;sample.pad.wButtons=XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B;input::publish(sample);
    if(scenario=="callback-normal"||scenario=="callback-log"||scenario=="callback-oom"){
        throwLogs=scenario=="callback-log";if(scenario=="callback-oom")failAfter=1;
        think(demon.data(),prior.data(),command.data());failAfter=-1;
        check(nativeCalls==1&&views==1&&axes==1&&physics==1&&buttons==mask&&oldButtons==0,"Diagnostic changed native command or view calls");
        if(throwLogs)check(logFailures==1,"Callback logging fault was not exercised");
        if(scenario=="callback-oom")check(allocationFailures==1,"Callback allocation fault was not exercised");
        think(demon.data(),prior.data(),command.data());check(oldButtons==mask&&buttons==mask,"Diagnostic changed held-action edges");
    }else if(scenario=="callback-native-throw"||scenario=="callback-view-throw"||scenario=="callback-basis-throw"){
        nativeThrows=scenario=="callback-native-throw";viewThrows=scenario=="callback-view-throw";basisThrows=scenario=="callback-basis-throw";
        bool propagated{};try{think(demon.data(),prior.data(),command.data());}catch(const std::bad_alloc&){propagated=true;}check(propagated,"Adapter swallowed a native exception");
        check(nativeCalls==(nativeThrows?1u:0u),"Adapter retried the native thinker after native failure");
    }else {
        think(demon.data(),prior.data(),command.data());check(buttons==mask&&oldButtons==0,"Initial VR command is wrong");
        nativeCalls=views=axes=physics=0;const void* current=command.data();const void* previous=prior.data();
        if(scenario=="callback-foot-invalid")put(d+0x58e8+0xb0,XrVector3f{NAN,0,0});
        else if(scenario=="callback-foot-read")failRead=d+0x58e8+0xb0;
        else if(scenario=="callback-command-read")failRead=uintptr_t(command.data());
        else if(scenario=="callback-bindings-read")failRead=d+0x37490;
        else if(scenario=="callback-inhibited")command[9]=1;
        else if(scenario=="callback-previous-read")failRead=uintptr_t(prior.data());
        else if(scenario=="callback-current-null")current=nullptr;
        else if(scenario=="callback-previous-null")previous=nullptr;
        else if(scenario=="callback-remote")demon[0x20a48]=0;
        else if(scenario=="callback-input-inactive"){sample.active=false;input::publish(sample);}
        else if(scenario=="callback-input-stale"){sample.tick=testTick()-250;input::publish(sample);}
        else if(scenario=="callback-input-actor"){sample.revenantActor=d+8;input::publish(sample);}
        else if(scenario=="callback-gameplay-off")presentation::gameplayInput=false;
        else if(scenario=="callback-aim-missing")aimReady=false;
        else if(scenario=="callback-aim-invalid")invalidAim=true;
        else if(scenario=="callback-delta-invalid")invalidDelta=true;
        else check(false,"Unknown callback scenario");
        think(demon.data(),previous,current);check(nativeCalls==1&&nativeObject==demon.data()&&nativePrevious==previous&&nativeCurrent==current&&!axes&&buttons==0&&oldButtons==0,"Fallback changed or skipped the native command");
        failRead=0;command[9]=0;demon[0x20a48]=1;put(d+0x58e8+0xb0,XrVector3f{});presentation::gameplayInput=true;aimReady=true;invalidAim=invalidDelta=false;
        sample.active=true;sample.tick=testTick();sample.revenantActor=d;input::publish(sample);
        think(demon.data(),prior.data(),command.data());check(buttons==mask&&oldButtons==0,"Fallback retained a VR action mask absent from the native previous frame");
    }
    check(command==std::array<unsigned char,commandSize>{}&&prior==command,"Adapter changed caller-owned command storage");
}
bool child(const wchar_t* executable,const std::wstring& scenario,bool store){
    std::wstring command=L"\""+std::wstring(executable)+L"\" "+scenario+(store?L" store":L"");STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Revenant child unavailable");
    const auto wait=WaitForSingleObject(process.hProcess,15000);DWORD code{};if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,INFINITE);}
    const DWORD expected=scenario==L"remove-fatal"?0xc0000602u:0u;
    const bool passed=wait==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&code)&&code==expected;CloseHandle(process.hThread);CloseHandle(process.hProcess);
    if(!passed)std::wcerr<<scenario<<L" store="<<store<<L" exit="<<code<<L'\n';return passed;
}
}

int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);SetEnvironmentVariableW(L"ARGENT_EXTENDED_LOGGING",L"1");
    try{
        const bool callbacksOnly=argc>1&&std::string_view(argv[1])=="--callbacks",installationOnly=argc>1&&std::string_view(argv[1])=="--installation";
        if(argc>1&&!callbacksOnly&&!installationOnly){argent::build::microsoftStore=argc>2;Image image;imageBytes=image.bytes;const std::string_view scenario=argv[1];if(scenario.rfind("callback-",0)==0)runCallback(scenario);else runCase(scenario);return 0;}
        wchar_t executable[32768]{};check(GetModuleFileNameW(nullptr,executable,32768)!=0,"Executable path unavailable");unsigned cases{},failures{};
        for(const bool store:{false,true}){
            if(!callbacksOnly){
                for(const auto scenario:{L"normal",L"null",L"repeat",L"different-image",L"logs",L"format-oom",L"concurrent",L"create",L"enable",L"conflict",L"remove-fatal"}){++cases;failures+=!child(executable,scenario,store);}
                for(unsigned i=0;i<4;++i){++cases;failures+=!child(executable,L"signature-"+std::to_wstring(i),store);}
            }
            if(!installationOnly)for(const auto scenario:{L"callback-normal",L"callback-log",L"callback-oom",L"callback-native-throw",L"callback-view-throw",L"callback-basis-throw",L"callback-foot-invalid",L"callback-foot-read",L"callback-command-read",L"callback-bindings-read",L"callback-inhibited",L"callback-previous-read",L"callback-current-null",L"callback-previous-null",L"callback-remote",L"callback-input-inactive",L"callback-input-stale",L"callback-input-actor",L"callback-gameplay-off",L"callback-aim-missing",L"callback-aim-invalid",L"callback-delta-invalid"}){++cases;failures+=!child(executable,scenario,store);}
        }
        std::cout<<cases<<" production revenant scenarios, "<<failures<<" failures\n";return failures?1:0;
    }catch(const std::exception& error){failAfter=-1;std::cerr<<error.what()<<'\n';return 1;}
}
