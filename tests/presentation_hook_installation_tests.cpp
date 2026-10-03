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
#include "../src/EternalPresentation.h"

namespace {
using namespace std::literals;
struct Signature {uintptr_t rva;std::string_view bytes;};
constexpr Signature signatures[]{
    {0xf88d10,"\x48\x89\x5c\x24\x08\x57\x48\x83\xec\x30\x8b\xfa\x48\x8b\xd9"sv},
    {0xf87400,"\x48\x89\x5c\x24\x08\x57\x48\x83\xec\x30\x8b\xda\x48\x8b\xf9"sv},
    {0xf854e0,"\x40\x53\x48\x83\xec\x30\x48\x8b\x41\x10\x48\x8b\xd9"sv},
    {0xf83ab0,"\x48\x89\x5c\x24\x08\x57\x48\x83\xec\x20\x48\x8b\xd9"sv},
    {0xf37330,"\x4c\x8b\xdc\x53\x56\x57\x48\x81\xec\xa0\0\0\0"sv},
    {0xf36780,"\x40\x53\x48\x83\xec\x20\x48\x8b\xd9"sv},
    {0xf38450,"\x40\x53\x48\x83\xec\x20\x48\x8b\xd9"sv},
    {0xf38560,"\x48\x89\x5c\x24\x10\x48\x89\x74\x24\x18\x48\x89\x7c\x24\x20"sv},
    {0x17e8740,"\x4c\x8b\xdc\x49\x89\x5b\x20\x55\x56\x57\x41\x56\x41\x57\x49\x8d\xab\xe8\xfe\xff\xff\x48\x81\xec\xf0\x01\0\0"sv}
};
struct Hook {unsigned char* target{};void* detour{};void** original{};unsigned char byte{};bool owned{},enabled{};};
std::array<Hook,11> hooks{};
std::mutex hookGuard;
unsigned char* imageBytes{};
uintptr_t failCreate{},failEnable{},failRemove{};
unsigned createCalls{},enableCalls{},invalidCleanup{},logCalls{},logFailures{},allocationFailures{};
unsigned logFailureAt{},nativeCalls{};
bool throwLogs{},missingDependencies{},throwRead{};
std::atomic<int> failAfter{-1};
std::atomic<bool> blockCreate{};
HANDLE createEntered{},releaseCreate{},secondFinished{};
void* nativeObject{};void* nativeEvent{};int nativeTransition{};
ULONGLONG testTick(){return 10000;}
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
unsigned owned(){unsigned count{};for(const auto& hook:hooks)count+=hook.target&&hook.owned;return count;}
BOOL WINAPI testRead(HANDLE process,LPCVOID address,LPVOID output,SIZE_T size,SIZE_T* got){
    if(throwRead){throwRead=false;throw std::bad_alloc{};}
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
void log(const std::string&){++logCalls;if(throwLogs||logCalls==logFailureAt){++logFailures;throw std::bad_alloc{};}}
}
#define GetTickCount64 testTick
#define ReadProcessMemory testRead
#ifdef ARGENT_PRESENTATION_BASELINE
#include "../build-perf/presentation-hooks-before/EternalPresentation.cpp"
#else
#include "../src/EternalPresentation.cpp"
#endif
#undef ReadProcessMemory
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
    missingDependencies|=owned()!=9||argent::presentation::playerVtable.load()!=uintptr_t(imageBytes)+argent::build::rva(0x2db5698)||argent::presentation::storeConsumer.load()!=argent::build::microsoftStore;
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
    unsigned char* bytes=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Image(){check(bytes!=nullptr,"Test image unavailable");for(const auto& signature:signatures)std::memcpy(bytes+argent::build::rva(signature.rva),signature.bytes.data(),signature.bytes.size());}
    ~Image(){VirtualFree(bytes,0,MEM_RELEASE);}
};
void* const originalSentinel=reinterpret_cast<void*>(uintptr_t(0x76543210));
constexpr uintptr_t previousType=0x12345678;
auto originalSlots(){using namespace argent::presentation;return std::array<void**,9>{reinterpret_cast<void**>(&originalPauseShow),reinterpret_cast<void**>(&originalPauseHide),reinterpret_cast<void**>(&originalUpgradeShow),reinterpret_cast<void**>(&originalUpgradeHide),reinterpret_cast<void**>(&originalDeathShow),reinterpret_cast<void**>(&originalDeathHide),reinterpret_cast<void**>(&originalDossierOpen),reinterpret_cast<void**>(&originalDossierClose),reinterpret_cast<void**>(&original)};}
auto detours(){using namespace argent::presentation;return std::array<void*,9>{reinterpret_cast<void*>(&pauseShow),reinterpret_cast<void*>(&pauseHide),reinterpret_cast<void*>(&upgradeShow),reinterpret_cast<void*>(&upgradeHide),reinterpret_cast<void*>(&deathShow),reinterpret_cast<void*>(&deathHide),reinterpret_cast<void*>(&dossierOpen),reinterpret_cast<void*>(&dossierClose),reinterpret_cast<void*>(&consumeFrame)};}
DWORD WINAPI installationWorker(void* second){const bool result=argent::presentation::install(imageBytes);if(second)SetEvent(secondFinished);return result?0:1;}
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
    check(owned()==9&&createCalls==9&&enableCalls==9,"Concurrent installer duplicated hooks");
}
void runCase(std::string_view scenario){
    using namespace argent::presentation;
    hooks[0]={imageBytes+0x100,nullptr,nullptr,0,false,true};
    for(const auto slot:originalSlots())*slot=originalSentinel;playerVtable=previousType;storeConsumer=!argent::build::microsoftStore;
    if(scenario=="concurrent"){concurrentCase();return;}
    bool succeeds=scenario!="null";
    if(scenario=="logs")throwLogs=true;
    if(scenario=="format-oom")failAfter=1;
    if(scenario.rfind("log-",0)==0)logFailureAt=unsigned(std::stoul(std::string(scenario.substr(4))));
    if(scenario.rfind("refusal-log-",0)==0){succeeds=false;imageBytes[argent::build::rva(signatures[0].rva)]^=1;throwLogs=scenario=="refusal-log-throw";if(!throwLogs)failAfter=1;}
    if(scenario.rfind("signature-",0)==0){succeeds=false;const auto index=size_t(std::stoul(std::string(scenario.substr(10))));imageBytes[argent::build::rva(signatures[index].rva)]^=1;}
    for(const auto prefix:{"create-"sv,"enable-"sv,"conflict-"sv,"remove-fatal-"sv})if(scenario.rfind(prefix,0)==0){
        succeeds=false;const auto index=size_t(std::stoul(std::string(scenario.substr(prefix.size()))));const auto target=imageBytes+argent::build::rva(signatures[index].rva);
        if(prefix=="create-")failCreate=uintptr_t(target);
        else if(prefix=="enable-")failEnable=uintptr_t(target);
        else if(prefix=="remove-fatal-"){failEnable=uintptr_t(imageBytes)+argent::build::rva(signatures[8].rva);failRemove=uintptr_t(target);}
        else hooks[1]={target,nullptr,nullptr,*target,false,true};
    }
    const bool result=install(scenario=="null"?nullptr:imageBytes);failAfter=-1;
    check(result==succeeds,"Installer result disagrees with owned state");
    check(!invalidCleanup&&hooks[0].target==imageBytes+0x100&&hooks[0].enabled,"Installer changed unrelated hooks");
    if(succeeds){
        check(owned()==9&&playerVtable.load()==uintptr_t(imageBytes)+argent::build::rva(0x2db5698)&&storeConsumer.load()==argent::build::microsoftStore,"Committed adapter state is incomplete");
        const auto slots=originalSlots();const auto expectedDetours=detours();
        for(size_t i=0;i<9;++i){bool found{};for(const auto& hook:hooks)if(hook.target==imageBytes+argent::build::rva(signatures[i].rva)){found=true;check(hook.enabled&&hook.original==slots[i]&&hook.detour==expectedDetours[i]&&*slots[i]==reinterpret_cast<void*>(uintptr_t(hook.target)+0x100),"Hook wiring or retained trampoline is wrong");}check(found,"Expected target is missing");}
        check(!missingDependencies,"Activation preceded native dependency publication");
        if(scenario=="repeat"||scenario=="different-image"){
            const auto before=createCalls;check(install(imageBytes)&&createCalls==before,"Repeat installation rechecked patched bytes");
            if(scenario=="different-image"){Image other;check(!install(other.bytes)&&playerVtable.load()==uintptr_t(imageBytes)+argent::build::rva(0x2db5698)&&createCalls==before,"Another image replaced the adapter");}
        }
    }else{
        check(!owned()&&playerVtable.load()==previousType&&storeConsumer.load()!=argent::build::microsoftStore,"Failed setup retained published state");
        for(const auto slot:originalSlots())check(*slot==originalSentinel,"Rollback retained a freed trampoline");
        if(scenario.rfind("signature-",0)==0||scenario.rfind("refusal-log-",0)==0)check(!createCalls&&!enableCalls,"Contract refusal changed native hooks");
        if(scenario.rfind("create-",0)==0)check(!enableCalls,"Creation failure enabled an incomplete group");
        if(scenario.rfind("create-",0)==0||scenario.rfind("enable-",0)==0){failCreate=failEnable=0;check(install(imageBytes)&&owned()==9,"Clean retry failed");}
        if(scenario.rfind("conflict-",0)==0)check(hooks[1].target&&hooks[1].enabled&&!hooks[1].owned,"Rollback changed foreign ownership");
    }
    if(throwLogs||logFailureAt)check(logFailures>0,"Logging fault was not exercised");
    if(scenario=="format-oom"||scenario=="refusal-log-oom")check(allocationFailures==1,"Formatting fault was not exercised");
}
void __fastcall nativeMenu(void* object,int transition){++nativeCalls;nativeObject=object;nativeTransition=transition;}
void __fastcall nativeOpen(void* object,void* event){++nativeCalls;nativeObject=object;nativeEvent=event;}
void __fastcall nativeClose(void* object){++nativeCalls;nativeObject=object;}
void runCallback(std::string_view scenario){
    using namespace argent::presentation;
    const auto kind=scenario.substr(9,scenario.rfind('-')-9);
    throwLogs=scenario.substr(scenario.rfind('-'))=="-log";const bool allocation=scenario.substr(scenario.rfind('-'))=="-oom";
    std::array<unsigned char,0x3000> screen{};std::array<unsigned char,0x8300> frame{};std::array<unsigned char,0x50000> playerBytes{};
    const auto owner=screen.data();const auto frameAddress=uintptr_t(frame.data()),playerAddress=uintptr_t(playerBytes.data());
    originalPauseShow=originalPauseHide=originalUpgradeShow=originalUpgradeHide=originalDeathShow=originalDeathHide=&nativeMenu;
    originalDossierOpen=&nativeOpen;originalDossierClose=original=&nativeClose;player=playerAddress;
    if(allocation)failAfter=1;
    if(kind=="pause-show"){pauseShow(owner,0);check(pauseSession.active()&&pauseRootVisible(),"Pause-show diagnostic changed visibility");}
    else if(kind=="pause-hide-child"||kind=="pause-hide-force"){pauseSession.show(uintptr_t(owner));pauseHide(owner,kind=="pause-hide-force"?2:0);check(pauseSession.active()==(kind=="pause-hide-child")&&!pauseRootVisible(),"Pause-hide diagnostic changed session ownership");}
    else if(kind=="upgrade-show"){upgradeShow(owner,0);check(upgradeScreen.load()==uintptr_t(owner)&&upgradeAnimationOwner.load()==playerAddress,"Upgrade-show diagnostic lost animation ownership");}
    else if(kind=="upgrade-hide"){upgradeScreen=uintptr_t(owner);upgradeHide(owner,2);check(!upgradeScreen.load(),"Upgrade-hide diagnostic retained the menu");}
    else if(kind=="death-show"){deathShow(owner,0);check(deathScreen.load()==uintptr_t(owner),"Death-show diagnostic lost the menu");}
    else if(kind=="death-hide"){deathScreen=uintptr_t(owner);deathHide(owner,2);check(!deathScreen.load(),"Death-hide diagnostic retained the menu");}
    else if(kind=="dossier-open"){dossierOpen(owner,frame.data());check(dossierScreen.load()==uintptr_t(owner)&&nativeEvent==frame.data(),"Dossier-open diagnostic changed the event or menu");}
    else if(kind=="dossier-close"){dossierScreen=uintptr_t(owner);dossierClose(owner);check(!dossierScreen.load(),"Dossier-close diagnostic retained the menu");}
    else {
        frame[0x40]=frame[0x41]=1;frame[0x8219]=1;std::memcpy(screen.data()+0x2a50,&frameAddress,8);player=0;
        if(kind=="frame-null"){storeConsumer=true;std::memset(screen.data()+0x2a50,0,8);}
        else if(kind=="frame-invalid"){storeConsumer=true;frame[0x40]=2;}
        else if(kind=="frame-first")storeConsumer=true;
        else if(kind=="frame-drone"){player=playerAddress;playerVtable=previousType;std::memcpy(playerBytes.data(),&previousType,8);upgradeAnimationOwner=playerAddress;playerBytes[0xd2c8+0x8da5]=0x80;}
        else if(kind=="frame-report-final"){if(throwLogs)logFailureAt=2;}
        else if(kind=="frame-observer"){throwRead=true;throwLogs=false;failAfter=-1;}
        else check(kind=="frame-report-first","Unknown callback scenario");
        if(kind=="frame-report-final"&&throwLogs)throwLogs=false;
        consumeFrame(owner);
        if(kind!="frame-null"&&kind!="frame-invalid"&&kind!="frame-observer")check(latest.tick==testTick()&&latest.valid&&latest.inGame&&latest.paused,"Frame diagnostic skipped sample publication");
        if(kind=="frame-drone")check(latest.interaction&&droneAnimation.load(),"Drone diagnostic lost native animation classification");
    }
    failAfter=-1;
    check(nativeCalls==1&&nativeObject==owner,"Observer diagnostic skipped or changed the native callback");
    if(kind=="pause-hide-force"||kind=="upgrade-hide"||kind=="death-hide")check(nativeTransition==2,"Menu callback changed the native transition");
    if(throwLogs||logFailureAt)check(logFailures>0,"Callback logging failure was not exercised");
    if(allocation)check(allocationFailures==1,"Callback formatting failure was not exercised");
    if(kind=="frame-observer")check(!throwRead,"Observer exception was not exercised");
}
bool child(const wchar_t* executable,const std::wstring& scenario,bool store){
    std::wstring command=L"\""+std::wstring(executable)+L"\" "+scenario+(store?L" store":L"");STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Presentation child unavailable");
    const auto wait=WaitForSingleObject(process.hProcess,15000);DWORD code{};if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,INFINITE);}
    const DWORD expected=scenario.rfind(L"remove-fatal-",0)==0?0xc0000602u:0u;
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
                for(const auto scenario:{L"normal",L"null",L"repeat",L"different-image",L"logs",L"format-oom",L"refusal-log-throw",L"refusal-log-oom",L"concurrent"}){++cases;failures+=!child(executable,scenario,store);}
                for(unsigned i=1;i<=5;++i){++cases;failures+=!child(executable,L"log-"+std::to_wstring(i),store);}
                for(unsigned i=0;i<9;++i){++cases;failures+=!child(executable,L"signature-"+std::to_wstring(i),store);for(const auto prefix:{L"create-",L"enable-",L"conflict-",L"remove-fatal-"}){++cases;failures+=!child(executable,prefix+std::to_wstring(i),store);}}
            }
            if(!installationOnly){
                for(const auto kind:{L"pause-show",L"pause-hide-child",L"pause-hide-force",L"upgrade-show",L"upgrade-hide",L"death-show",L"death-hide",L"dossier-open",L"dossier-close",L"frame-null",L"frame-invalid",L"frame-first",L"frame-drone",L"frame-report-first"})for(const auto mode:{L"normal",L"log",L"oom"}){++cases;failures+=!child(executable,L"callback-"+std::wstring(kind)+L"-"+mode,store);}
                for(const auto scenario:{L"callback-frame-report-final-normal",L"callback-frame-report-final-log",L"callback-frame-observer-normal"}){++cases;failures+=!child(executable,scenario,store);}
            }
        }
        std::cout<<cases<<" production presentation scenarios, "<<failures<<" failures\n";return failures?1:0;
    }catch(const std::exception& error){failAfter=-1;std::cerr<<error.what()<<'\n';return 1;}
}
