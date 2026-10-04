#include <windows.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include "../src/EternalCameraHook.h"
#include "../src/sfs/NativeSfs.h"

namespace {
using namespace std::literals;
constexpr uintptr_t targetRvas[]{0x2268b30,0x1cc7aa0,0x2268f40};
constexpr std::string_view common="\x48\x89\x5c\x24\x08\x48\x89\x6c\x24\x10\x48\x89\x74\x24\x18"sv;
constexpr std::string_view releaseBytes="\x48\x89\x5c\x24\x08\x57\x48\x83\xec\x20\x48\x8b\x1d\x2f\x91\xa5\x04"sv;
constexpr std::string_view storeReleaseBytes="\x48\x89\x5c\x24\x08\x57\x48\x83\xec\x20\x48\x8b\x1d\x6f\x3f\xa7\x04"sv;
struct Hook {unsigned char* target{};void* detour{};void** original{};unsigned char byte{};bool owned{},enabled{},queued{};};
std::array<Hook,5> hooks{};
std::mutex hookGuard;
unsigned char* imageBytes{};
uintptr_t failCreate{},failEnable{},failQueue{},failRemove{};
unsigned createCalls{},enableCalls{},applyCalls{},invalidCleanup{},logCalls{},logFailures{},allocationFailures{};
bool throwLogs{},missingDependencies{},cameraReady{true},vrReady{true},moduleMissing{},resolveReady{true},throwResolve{};
std::atomic<int> failAfter{-1};
std::atomic<bool> blockCreate{};
HANDLE createEntered{},releaseCreate{},secondFinished{};
argent::sfs::FramePose resolvedPose{};
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
unsigned owned(){unsigned count{};for(const auto& hook:hooks)count+=hook.target&&hook.owned;return count;}
HMODULE WINAPI testModule(LPCWSTR name){check(!name,"Installer queried another module");return moduleMissing?nullptr:reinterpret_cast<HMODULE>(imageBytes);}
ULONGLONG testTick(){return 10000;}
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
Stats stats() noexcept {return {0,0,0,cameraReady};}
}
namespace argent::sfs {
bool vrEnabled(){return vrReady;}
bool dlssEyeResources(VkCommandBuffer,const std::array<const dlss::Resource*,30>& input,std::array<dlss::EyeParameters,2>& output,FramePose& pose){
    if(throwResolve)throw std::bad_alloc{};if(!resolveReady)return false;pose=resolvedPose;
    for(size_t i=0;i<input.size();++i)if(input[i])for(unsigned eye=0;eye<2;++eye){output[eye].resources[i]=*input[i];output[eye].resources[i].range.baseArrayLayer=eye;}
    return true;
}
}
#define GetModuleHandleW testModule
#define GetTickCount64 testTick
#ifdef ARGENT_DLSS_BASELINE
#include "../build-perf/dlss-hooks-before/EternalDlssHook.cpp"
#else
#include "../src/EternalDlssHook.cpp"
#endif
#undef GetTickCount64
#undef GetModuleHandleW

extern "C" MH_STATUS WINAPI MH_CreateHook(void* target,void* detour,void** original){
    if(blockCreate.exchange(false)){SetEvent(createEntered);if(WaitForSingleObject(releaseCreate,5000)!=WAIT_OBJECT_0)return MH_ERROR_MEMORY_ALLOC;}
    std::lock_guard<std::mutex> lock(hookGuard);++createCalls;
    for(const auto& hook:hooks)if(hook.target==target)return MH_ERROR_ALREADY_CREATED;
    if(uintptr_t(target)==failCreate)return MH_ERROR_MEMORY_ALLOC;
    for(auto& hook:hooks)if(!hook.target){hook={static_cast<unsigned char*>(target),detour,original,*static_cast<unsigned char*>(target),true,false,false};if(original)*original=reinterpret_cast<void*>(uintptr_t(target)+0x100);return MH_OK;}
    return MH_ERROR_MEMORY_ALLOC;
}
namespace {
void checkDependencies(){using namespace argent::dlss;missingDependencies|=owned()!=3||!createOriginal||!evaluateOriginal||!releaseOriginal;}
MH_STATUS activate(void* target){
    checkDependencies();++enableCalls;
    for(auto& hook:hooks)if(hook.target==target){if(uintptr_t(target)==failEnable)return MH_ERROR_MEMORY_PROTECT;hook.enabled=true;hook.queued=false;*hook.target=0xe9;return MH_OK;}
    ++invalidCleanup;return MH_ERROR_NOT_CREATED;
}
}
extern "C" MH_STATUS WINAPI MH_EnableHook(void* target){std::lock_guard<std::mutex> lock(hookGuard);return activate(target);}
extern "C" MH_STATUS WINAPI MH_QueueEnableHook(void* target){
    std::lock_guard<std::mutex> lock(hookGuard);checkDependencies();
    for(auto& hook:hooks)if(hook.target==target){if(uintptr_t(target)==failQueue)return MH_ERROR_MEMORY_PROTECT;hook.queued=true;return MH_OK;}
    ++invalidCleanup;return MH_ERROR_NOT_CREATED;
}
extern "C" MH_STATUS WINAPI MH_ApplyQueued(){
    std::lock_guard<std::mutex> lock(hookGuard);++applyCalls;
    for(auto& hook:hooks)if(hook.queued){const auto result=activate(hook.target);if(result!=MH_OK)return result;}
    return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_DisableHook(void* target){
    std::lock_guard<std::mutex> lock(hookGuard);
    for(auto& hook:hooks)if(hook.target==target){if(!hook.owned){++invalidCleanup;return MH_ERROR_NOT_CREATED;}hook.enabled=false;*hook.target=hook.byte;return MH_OK;}
    ++invalidCleanup;return MH_ERROR_NOT_CREATED;
}
extern "C" MH_STATUS WINAPI MH_RemoveHook(void* target){
    std::lock_guard<std::mutex> lock(hookGuard);
    for(auto& hook:hooks)if(hook.target==target){if(!hook.owned){++invalidCleanup;return MH_ERROR_NOT_CREATED;}if(uintptr_t(target)==failRemove)return MH_ERROR_MEMORY_PROTECT;*hook.target=hook.byte;hook={};return MH_OK;}
    ++invalidCleanup;return MH_ERROR_NOT_CREATED;
}

namespace {
struct Image {
    unsigned char* bytes=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2310000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Image(){
        check(bytes!=nullptr,"Test image unavailable");for(unsigned i=0;i<2;++i)std::memcpy(bytes+argent::build::rva(targetRvas[i]),common.data(),common.size());
        const auto release=argent::build::microsoftStore?storeReleaseBytes:releaseBytes;std::memcpy(bytes+argent::build::rva(targetRvas[2]),release.data(),release.size());
    }
    ~Image(){VirtualFree(bytes,0,MEM_RELEASE);}
};
struct Requests {
    std::filesystem::path directory,request;
    bool owned{};
    Requests(){
        wchar_t inherited[MAX_PATH]{};const auto length=GetEnvironmentVariableW(L"ARGENT_DLSS_TEST_DIR",inherited,MAX_PATH);
        if(length&&length<MAX_PATH)directory=inherited;
        else {
            wchar_t root[MAX_PATH]{},unique[MAX_PATH]{};check(GetTempPathW(MAX_PATH,root)&&GetTempFileNameW(root,L"adl",0,unique),"Temporary request path unavailable");
            check(DeleteFileW(unique)&&CreateDirectoryW(unique,nullptr),"Temporary request directory unavailable");directory=unique;owned=true;
            check(SetEnvironmentVariableW(L"ARGENT_DLSS_TEST_DIR",directory.c_str()),"Request directory environment unavailable");
        }
        request=directory/L"aa-mode.request";
        const auto log=directory/L"argent.log";check(SetEnvironmentVariableW(L"ARGENT_LOG",log.c_str()),"Request environment unavailable");
    }
    ~Requests(){if(owned){DeleteFileW(request.c_str());RemoveDirectoryW(directory.c_str());}}
    bool written() const {std::ifstream stream(request);std::string content;stream>>content;return content=="0";}
};
void* const originalSentinel=reinterpret_cast<void*>(uintptr_t(0x76543210));
auto originalSlots(){using namespace argent::dlss;return std::array<void**,3>{reinterpret_cast<void**>(&createOriginal),reinterpret_cast<void**>(&evaluateOriginal),reinterpret_cast<void**>(&releaseOriginal)};}
auto detours(){using namespace argent::dlss;return std::array<void*,3>{reinterpret_cast<void*>(&create),reinterpret_cast<void*>(&evaluate),reinterpret_cast<void*>(&release)};}
DWORD WINAPI installationWorker(void* second){const bool result=argent::dlss::install();if(second)SetEvent(secondFinished);return result?0:1;}
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
    check(owned()==3&&createCalls==3&&enableCalls==3,"Concurrent installer duplicated hooks");
}
void runCase(std::string_view scenario,const Requests& requests){
    using namespace argent::dlss;hooks[0]={imageBytes+0x100,nullptr,nullptr,0,false,true,false};
    for(const auto slot:originalSlots())*slot=originalSentinel;
    if(scenario=="concurrent"){concurrentCase();return;}
    bool succeeds=true,fallbackExpected{};
    if(scenario=="logs")throwLogs=true;
    if(scenario=="format-oom")failAfter=1;
    if(scenario=="module-null"){succeeds=false;fallbackExpected=true;moduleMissing=true;}
    if(scenario=="camera-off"){succeeds=false;cameraReady=false;}
    if(scenario=="vr-off"){succeeds=false;vrReady=false;}
    if(scenario=="disabled"){succeeds=false;SetEnvironmentVariableW(L"ARGENT_DLSS_STEREO",L"0");}
    if(scenario=="queued-foreign")hooks[1]={imageBytes+0x200,nullptr,nullptr,0,false,false,true};
    if(scenario.rfind("signature-",0)==0){
        succeeds=false;fallbackExpected=true;const auto index=size_t(std::stoul(std::string(scenario.substr(10))));imageBytes[argent::build::rva(targetRvas[index])]^=1;
        if(scenario.find("-log")!=std::string_view::npos)throwLogs=true;if(scenario.find("-oom")!=std::string_view::npos)failAfter=1;
    }
    for(const auto prefix:{"create-"sv,"enable-"sv,"conflict-"sv,"queue-error-"sv,"remove-fatal-"sv})if(scenario.rfind(prefix,0)==0){
        const auto index=size_t(std::stoul(std::string(scenario.substr(prefix.size()))));const auto target=imageBytes+argent::build::rva(targetRvas[index]);
        if(prefix=="queue-error-")failQueue=uintptr_t(target);
        else {
            succeeds=false;fallbackExpected=true;
            if(prefix=="create-")failCreate=uintptr_t(target);
            else if(prefix=="enable-")failEnable=uintptr_t(target);
            else if(prefix=="remove-fatal-"){failEnable=uintptr_t(imageBytes)+argent::build::rva(targetRvas[2]);failRemove=uintptr_t(target);}
            else hooks[1]={target,nullptr,nullptr,*target,false,true,false};
        }
    }
    const bool result=install();failAfter=-1;
    check(result==succeeds,"Installer result disagrees with owned state");
    check(!invalidCleanup&&hooks[0].target==imageBytes+0x100&&hooks[0].enabled,"Installer changed unrelated hooks");
    if(succeeds){
        check(owned()==3&&installed&&!missingDependencies&&!failed(),"Committed installation is incomplete");const auto slots=originalSlots();const auto expectedDetours=detours();
        for(size_t i=0;i<3;++i){bool found{};for(const auto& hook:hooks)if(hook.target==imageBytes+argent::build::rva(targetRvas[i])){found=true;check(hook.enabled&&hook.original==slots[i]&&hook.detour==expectedDetours[i]&&*slots[i]==reinterpret_cast<void*>(uintptr_t(hook.target)+0x100),"Hook wiring or retained trampoline is wrong");}check(found,"Expected target is missing");}
        if(scenario=="repeat"){const auto before=createCalls;check(install()&&createCalls==before,"Repeated installer duplicated patched hooks");}
        if(scenario=="queued-foreign")check(!applyCalls&&hooks[1].queued&&!hooks[1].enabled,"Installer activated another owner's queued hook");
    }else{
        check(!owned()&&!installed&&failed()==fallbackExpected,"Failed installation retained native ownership");for(const auto slot:originalSlots())check(*slot==originalSentinel,"Rollback retained a freed trampoline");
        if(scenario.rfind("signature-",0)==0||scenario=="module-null"||!fallbackExpected)check(!createCalls&&!enableCalls,"Preflight refusal changed hooks");
        if(scenario.rfind("create-",0)==0)check(!enableCalls,"Creation failure enabled an incomplete group");
        if(scenario.rfind("create-",0)==0||scenario.rfind("enable-",0)==0){failCreate=failEnable=0;check(install()&&owned()==3,"Clean hook retry failed");}
        if(scenario.rfind("conflict-",0)==0)check(hooks[1].target&&hooks[1].enabled&&!hooks[1].owned,"Rollback changed foreign hook ownership");
    }
    check(requests.written()==(fallbackExpected&&!argent::cleanRelease),"Diagnostics changed the fallback AA request");
    if(throwLogs)check(logFailures>0,"Logging fault was not exercised");
    if(scenario.find("oom")!=std::string_view::npos)check(allocationFailures==1,"Formatting fault was not exercised");
}
#include "DlssHookCallbacks.inc"
bool child(const wchar_t* executable,const std::wstring& scenario,bool store,const Requests& requests,bool timing=false){
    std::wstring command=L"\""+std::wstring(executable)+L"\" "+scenario+(store?L" store":L" steam")+(timing?L" timing":L" quiet");STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"DLSS child unavailable");
    const auto wait=WaitForSingleObject(process.hProcess,15000);DWORD code{};if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,INFINITE);}
    const DWORD expected=scenario.rfind(L"remove-fatal-",0)==0?0xc0000602u:0u;
    const bool passed=wait==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&code)&&code==expected;CloseHandle(process.hThread);CloseHandle(process.hProcess);
    DeleteFileW(requests.request.c_str());if(!passed)std::wcerr<<scenario<<L" store="<<store<<L" timing="<<timing<<L" exit="<<code<<L'\n';return passed;
}
}

int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);SetEnvironmentVariableW(L"ARGENT_DLSS_STEREO",nullptr);SetEnvironmentVariableW(L"ARGENT_EXTENDED_LOGGING",nullptr);SetEnvironmentVariableW(L"ARGENT_SFS_PROFILE_TIMING",nullptr);SetEnvironmentVariableW(L"ARGENT_LOG",nullptr);
    try{
        const bool installationOnly=argc>1&&std::string_view(argv[1])=="--installation",callbacksOnly=argc>1&&std::string_view(argv[1])=="--callbacks";
        if(argc>1&&!installationOnly&&!callbacksOnly){
            argent::build::microsoftStore=argc>2&&std::string_view(argv[2])=="store";if(argc>3&&std::string_view(argv[3])=="timing")SetEnvironmentVariableW(L"ARGENT_EXTENDED_LOGGING",L"1");
            Image image;imageBytes=image.bytes;Requests requests;const std::string_view scenario=argv[1];if(scenario.rfind("callback-",0)==0)runCallback(scenario,requests);else runCase(scenario,requests);return 0;
        }
        SetEnvironmentVariableW(L"ARGENT_DLSS_TEST_DIR",nullptr);Requests requests;
        wchar_t executable[32768]{};check(GetModuleFileNameW(nullptr,executable,32768)!=0,"Executable path unavailable");unsigned cases{},failures{};
        for(const bool store:{false,true}){
            if(!callbacksOnly){
                for(const auto scenario:{L"normal",L"repeat",L"logs",L"format-oom",L"concurrent",L"module-null",L"camera-off",L"vr-off",L"disabled",L"queued-foreign",L"signature-0-log",L"signature-0-oom"}){++cases;failures+=!child(executable,scenario,store,requests);}
                for(unsigned i=0;i<3;++i)for(const auto prefix:{L"signature-",L"create-",L"enable-",L"conflict-",L"queue-error-",L"remove-fatal-"}){++cases;failures+=!child(executable,prefix+std::to_wstring(i),store,requests);}
            }
            if(!installationOnly)for(const bool timing:{false,true}){
                for(const auto kind:{L"create",L"duplicate",L"right-error",L"right-null",L"right-alias",L"release",L"release-left-error",L"missing",L"null-input",L"resources-invalid",L"resolve-error",L"resolve-exception",L"left-eval-error",L"right-eval-error",L"evaluate"})for(const auto mode:{L"normal",L"log",L"oom"}){++cases;failures+=!child(executable,L"callback-"+std::wstring(kind)+L"-"+mode,store,requests,timing);}
                if(timing)for(const auto mode:{L"normal",L"log",L"oom"}){++cases;failures+=!child(executable,L"callback-window-"+std::wstring(mode),store,requests,timing);}
                for(const auto scenario:{L"callback-left-error-normal",L"callback-left-null-normal",L"callback-output-null-normal",L"callback-other-feature-normal",L"callback-create-native-throw-normal",L"callback-release-native-throw-normal",L"callback-evaluate-native-throw-normal",L"callback-registration-cleanup-error-normal",L"callback-release-right-error-normal"}){++cases;failures+=!child(executable,scenario,store,requests,timing);}
                for(const auto failure:{L"left-error",L"right-error",L"null-input",L"resolve-error",L"resolve-exception",L"left-throw",L"right-throw"}){++cases;failures+=!child(executable,L"callback-recovery-"+std::wstring(failure)+L"-normal",store,requests,timing);}
            }
        }
        std::cout<<cases<<" production DLSS hook scenarios, "<<failures<<" failures\n";return failures?1:0;
    }catch(const std::exception& error){failAfter=-1;std::cerr<<error.what()<<'\n';return 1;}
}
