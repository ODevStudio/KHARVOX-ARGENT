#include <windows.h>
#include <MinHook.h>
#include <bcrypt.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <intrin.h>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include "../src/EternalCameraHook.h"
#include "../src/EternalBuildProfile.h"

namespace {
unsigned char* imageBytes{};
void** importSlot{};
uintptr_t failCreate{},failEnable{},failRemove{},testCaller{};
unsigned createCalls{},invalidCleanup{},refreshCalls{},logFailures{},protectCalls{},allocationFailures{};
int failProtection{};
bool failAllRestores{},raceImport{},replaceImport{},throwRefresh{},changeOutput{},approveHash{true},failInitialize{},failPin{},formatFailure{};
std::atomic<int> failAfter{-1};
std::atomic<bool> blockCreate{};
HANDLE createEntered{},releaseCreate{},secondFinished{};
bool concurrentExtent{};
std::string_view logPrefix;
struct Hook {void* target{};bool owned{},enabled{};};
std::array<Hook,8> hooks{};
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
BOOL WINAPI nativeRect(HWND,LPRECT rect){if(rect)*rect={10,20,810,620};return TRUE;}
BOOL WINAPI foreignRect(HWND,LPRECT rect){if(rect)*rect={30,40,830,640};return TRUE;}
HMODULE WINAPI moduleHandle(LPCWSTR name){return name?GetModuleHandleW(name):reinterpret_cast<HMODULE>(imageBytes);}
BOOL WINAPI pinModule(DWORD,LPCWSTR,HMODULE* result){if(failPin)return FALSE;*result=GetModuleHandleW(nullptr);return TRUE;}
NTSTATUS WINAPI finishHash(BCRYPT_HASH_HANDLE,PUCHAR output,ULONG bytes,ULONG){
    constexpr unsigned char expected[]{0x69,0xdc,0x13,0xe8,0x8d,0x1c,0x19,0x13,0x3e,0xad,0x79,0x50,0xdc,0x64,0xeb,0xcb,0xd4,0xa5,0xa3,0xf6,0xbd,0x6f,0x9c,0x33,0x6e,0xbf,0xfe,0x56,0xdf,0x6a,0x1c,0x11};
    check(bytes==sizeof(expected),"Hash output length changed");std::memset(output,0,bytes);if(approveHash)std::memcpy(output,expected,bytes);return 0;
}
BOOL WINAPI protectMemory(LPVOID address,SIZE_T size,DWORD protection,PDWORD previous){
    if(address==importSlot){
        ++protectCalls;
        if(int(protectCalls)==failProtection||(failAllRestores&&protection!=PAGE_READWRITE))return FALSE;
    }
    return VirtualProtect(address,size,protection,previous);
}
void* compareImport(void* volatile* address,void* replacement,void* expected){
    if(raceImport&&address==importSlot){raceImport=false;InterlockedExchangePointer(address,reinterpret_cast<void*>(&foreignRect));}
    return InterlockedCompareExchangePointer(address,replacement,expected);
}
void* exchangeImport(void* volatile* address,void* replacement){
    if(raceImport&&address==importSlot){raceImport=false;InterlockedExchangePointer(address,reinterpret_cast<void*>(&foreignRect));}
    return InterlockedExchangePointer(address,replacement);
}
void __fastcall nativeRefresh();
void __fastcall nativeCamera(void*,const float*,const float*){}
void __fastcall nativeFinalize(void*){}
int __fastcall nativePoll(void*,int){return 73;}
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
void log(const std::string& message){if(!logPrefix.empty()&&std::string_view(message).rfind(logPrefix,0)==0){++logFailures;throw std::bad_alloc{};}}
}
namespace argent::hud {
bool weaponWheelVisible()noexcept{return false;}
int calibrationPlaceholderRole(){return -1;}
kharvox::hands::HandHudPanels calibrationPlaceholder(int,bool,const float*,const float*,float,const float*){return {};}
}

#define GetModuleHandleW moduleHandle
#define GetModuleHandleExW pinModule
#define BCryptFinishHash finishHash
#define VirtualProtect protectMemory
#define _ReturnAddress() reinterpret_cast<void*>(testCaller)
#undef InterlockedCompareExchangePointer
#undef InterlockedExchangePointer
#define InterlockedCompareExchangePointer compareImport
#define InterlockedExchangePointer exchangeImport
#include "../src/EternalCameraHook.cpp"
#undef GetModuleHandleW
#undef GetModuleHandleExW
#undef BCryptFinishHash
#undef VirtualProtect
#undef _ReturnAddress
#undef InterlockedCompareExchangePointer
#undef InterlockedExchangePointer

extern "C" MH_STATUS WINAPI MH_Initialize(){return failInitialize?MH_ERROR_MEMORY_ALLOC:MH_OK;}
extern "C" MH_STATUS WINAPI MH_CreateHook(void* target,void*,void** original){
    ++createCalls;
    if(blockCreate.exchange(false)){SetEvent(createEntered);check(WaitForSingleObject(releaseCreate,5000)==WAIT_OBJECT_0,"Installation creation barrier timed out");}
    for(const auto& hook:hooks)if(hook.target==target)return MH_ERROR_ALREADY_CREATED;
    if(uintptr_t(target)==failCreate)return MH_ERROR_MEMORY_ALLOC;
    for(auto& hook:hooks)if(!hook.target){
        hook={target,true,false};
        if(original){
            const auto rva=argent::build::semanticRva(uintptr_t(target)-uintptr_t(imageBytes));
            *original=rva==0x1cbfa60?reinterpret_cast<void*>(&nativeRefresh):rva==0x1481d30?reinterpret_cast<void*>(&nativeFinalize):rva==0x1dc57a0?reinterpret_cast<void*>(&nativePoll):reinterpret_cast<void*>(&nativeCamera);
        }
        return MH_OK;
    }
    return MH_ERROR_MEMORY_ALLOC;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(void* target){
    if(uintptr_t(target)==failEnable){
        if(replaceImport){DWORD old{};check(VirtualProtect(importSlot,sizeof(void*),PAGE_READWRITE,&old),"Foreign patch could not acquire import page");*importSlot=reinterpret_cast<void*>(&foreignRect);DWORD ignored{};check(VirtualProtect(importSlot,sizeof(void*),old,&ignored),"Foreign patch could not restore import page");}
        return MH_ERROR_MEMORY_PROTECT;
    }
    for(auto& hook:hooks)if(hook.target==target){hook.enabled=true;if(formatFailure){formatFailure=false;failAfter=1;}return MH_OK;}
    return MH_ERROR_NOT_CREATED;
}
extern "C" MH_STATUS WINAPI MH_DisableHook(void* target){
    for(auto& hook:hooks)if(hook.target==target){if(!hook.owned)++invalidCleanup;hook.enabled=false;return MH_OK;}
    ++invalidCleanup;return MH_ERROR_NOT_CREATED;
}
extern "C" MH_STATUS WINAPI MH_RemoveHook(void* target){
    if(uintptr_t(target)==failRemove)return MH_ERROR_MEMORY_PROTECT;
    for(auto& hook:hooks)if(hook.target==target){if(!hook.owned)++invalidCleanup;hook={};return MH_OK;}
    ++invalidCleanup;return MH_ERROR_NOT_CREATED;
}

namespace {
using namespace argent::camera;
constexpr uint32_t width=1408,height=1536;
unsigned owned(){unsigned count{};for(const auto& hook:hooks)count+=hook.target&&hook.owned;return count;}
void __fastcall nativeRefresh(){
    ++refreshCalls;
    if(changeOutput)reinterpret_cast<uint32_t*>(imageBytes+argent::build::rva(0x39aabe4))[1]=777;
    if(throwRefresh)throw std::runtime_error("Native refresh failed");
}
struct Image {
    Image(bool store){
        imageBytes=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x8000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));check(imageBytes!=nullptr,"Image allocation failed");
        argent::build::microsoftStore=store;
        auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(imageBytes);dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;
        auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(imageBytes+0x80);nt->Signature=IMAGE_NT_SIGNATURE;nt->FileHeader.NumberOfSections=1;nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
        if(store){nt->FileHeader.TimeDateStamp=0x69bc663d;nt->OptionalHeader.SizeOfImage=0x74f1000;for(const auto& contract:argent::build::storeCode)std::memcpy(imageBytes+contract.rva,contract.bytes,sizeof(contract.bytes));}
        auto section=IMAGE_FIRST_SECTION(nt);section->VirtualAddress=0x1000;section->Misc.VirtualSize=0x100;section->Characteristics=IMAGE_SCN_MEM_EXECUTE;
        std::memcpy(imageBytes+0x1000,signature,sizeof(signature));
        auto bytes=[&](uintptr_t rva,std::initializer_list<unsigned char> data){std::memcpy(imageBytes+argent::build::rva(rva),data.begin(),data.size());};
        bytes(0x376020,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0x09,0x41,0x0f,0xb6,0xf0,0x48,0x8b,0xfa,0x48,0x85,0xd2,0x75});
        bytes(0x1481d30,{0x48,0x8b,0xc4,0x57,0x48,0x83,0xec,0x70,0x48,0x89,0x58,0x08,0x48,0x8b,0xf9,0x48,0x89,0x70,0x18});
        bytes(0x1dc57a0,{0x48,0x89,0x5c,0x24,0x18,0x57,0x48,0x83,0xec,0x40});
        bytes(0x1dc48f0,{0x80,0xb9,0xfc,0x01,0,0,0,0x75,0x07,0xc6,0x81,0xfd,0x01,0,0,0x01});
        if(store){
            bytes(0x1cbf8d0,{0x8b,0x05,0x8e,0x0e,0xd1,0x01,0xc3,0xcc});bytes(0x1cbf8c0,{0x8b,0x05,0xa2,0x0e,0xd1,0x01,0xc3,0xcc});
            bytes(0x1d0912e,{0xff,0x15,0x34,0x5c,0xd2,0});bytes(0x1d091b4,{0xff,0x15,0xae,0x5b,0xd2,0});bytes(0x1cbfa60,{0x48,0x83,0xec,0x28,0x80,0x3d,0x57,0xa6,0x9e,0x04,0x01,0x75,0x16});
        }else{
            bytes(0x1cbf8d0,{0x8b,0x05,0x0e,0xb3,0xce,0x01,0xc3,0xcc});bytes(0x1cbf8c0,{0x8b,0x05,0x22,0xb3,0xce,0x01,0xc3,0xcc});
            bytes(0x1d0912e,{0xff,0x15,0x14,0x2f,0xd1,0});bytes(0x1d091b4,{0xff,0x15,0x8e,0x2e,0xd1,0});bytes(0x1cbfa60,{0x48,0x83,0xec,0x28,0x80,0x3d,0xe7,0xf1,0x9b,0x04,0x01,0x75,0x16});
        }
        auto output=reinterpret_cast<uint32_t*>(imageBytes+argent::build::rva(0x39aabe4));output[0]=900;output[1]=600;
        importSlot=reinterpret_cast<void**>(imageBytes+argent::build::rva(0x2a1c048));*importSlot=reinterpret_cast<void*>(&nativeRect);DWORD old{};check(VirtualProtect(importSlot,sizeof(void*),PAGE_READONLY,&old),"Import read-only setup failed");
    }
    ~Image(){VirtualFree(imageBytes,0,MEM_RELEASE);imageBytes=nullptr;importSlot=nullptr;}
};
void setImport(void* value){DWORD old{},ignored{};check(VirtualProtect(importSlot,sizeof(void*),PAGE_READWRITE,&old),"Import setup failed");*importSlot=value;check(VirtualProtect(importSlot,sizeof(void*),old,&ignored),"Import setup protection failed");}
DWORD WINAPI installationWorker(void* second){
    const bool result=concurrentExtent?installRenderExtent(width,height):install();if(second)SetEvent(secondFinished);return result?0:1;
}
void concurrentCase(bool extent){
    concurrentExtent=extent;createEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);releaseCreate=CreateEventW(nullptr,TRUE,FALSE,nullptr);secondFinished=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    check(createEntered&&releaseCreate&&secondFinished,"Installation event setup failed");blockCreate=true;
    const auto first=CreateThread(nullptr,0,installationWorker,nullptr,0,nullptr);check(first!=nullptr,"First installation thread creation failed");
    const auto entered=WaitForSingleObject(createEntered,5000);const auto second=CreateThread(nullptr,0,installationWorker,reinterpret_cast<void*>(1),0,nullptr);
    const auto premature=second?WaitForSingleObject(secondFinished,100):WAIT_FAILED;SetEvent(releaseCreate);
    const auto firstWait=WaitForSingleObject(first,5000);const auto secondWait=second?WaitForSingleObject(second,5000):WAIT_FAILED;DWORD firstCode{},secondCode{};
    const bool exited=GetExitCodeThread(first,&firstCode)&&second&&GetExitCodeThread(second,&secondCode);
    CloseHandle(first);if(second)CloseHandle(second);CloseHandle(createEntered);CloseHandle(releaseCreate);CloseHandle(secondFinished);
    check(entered==WAIT_OBJECT_0&&premature==WAIT_TIMEOUT&&firstWait==WAIT_OBJECT_0&&secondWait==WAIT_OBJECT_0&&exited&&!firstCode&&!secondCode,"Concurrent installation exposed partial ownership");
    check(owned()==3&&createCalls==3&&!invalidCleanup&&(extent?refreshCalls==1&&eyeWidth==width:installed.load()),"Concurrent installation duplicated or cleared committed hooks");
}
void extentCase(std::string_view scenario){
    bool succeeds=scenario=="extent-normal"||scenario=="extent-log"||scenario=="extent-format-oom"||scenario=="extent-foreign-import"||scenario=="extent-repeat";
    if(scenario=="extent-log")logPrefix="ETERNAL_RENDER_EXTENT ";
    if(scenario=="extent-format-oom")formatFailure=true;
    if(scenario=="extent-init")failInitialize=true;
    if(scenario=="extent-protect")failProtection=1;
    if(scenario=="extent-restore")failProtection=2;
    if(scenario=="extent-restore-fatal")failAllRestores=true;
    if(scenario=="extent-refresh")throwRefresh=true;
    if(scenario=="extent-refresh-external")throwRefresh=changeOutput=true;
    if(scenario=="extent-cas-race")raceImport=true;
    if(scenario=="extent-null-import")setImport(nullptr);
    if(scenario=="extent-recursive-import")setImport(reinterpret_cast<void*>(&renderClientRect));
    if(scenario=="extent-foreign-import")setImport(reinterpret_cast<void*>(&foreignRect));
    if(scenario=="extent-hash"){approveHash=false;imageBytes[0]=0;}
    if(scenario.rfind("extent-signature-",0)==0){const uintptr_t rvas[]{0x1cbf8d0,0x1cbf8c0,0x1cbfa60,0x1d0912e,0x1d091b4};imageBytes[argent::build::rva(rvas[scenario.back()-'0'])]^=1;approveHash=false;}
    if(scenario.rfind("extent-create-",0)==0||scenario.rfind("extent-conflict-",0)==0||scenario.rfind("extent-enable-",0)==0){
        const uintptr_t rvas[]{0x1cbf8d0,0x1cbf8c0,0x1cbfa60};const auto target=uintptr_t(imageBytes)+argent::build::rva(rvas[scenario.back()-'0']);
        if(scenario.rfind("extent-create-",0)==0)failCreate=target;
        else if(scenario.rfind("extent-enable-",0)==0)failEnable=target;
        else hooks[0]={reinterpret_cast<void*>(target),false,true};
    }
    if(scenario=="extent-foreign-rollback"){replaceImport=true;failEnable=uintptr_t(imageBytes)+argent::build::rva(0x1cbfa60);}
    if(scenario=="extent-remove-fatal"){failEnable=uintptr_t(imageBytes)+argent::build::rva(0x1cbfa60);failRemove=uintptr_t(imageBytes)+argent::build::rva(0x1cbf8c0);}
    if(scenario=="extent-rollback-protect-fatal"){failEnable=uintptr_t(imageBytes)+argent::build::rva(0x1cbfa60);failProtection=3;}
    const auto initial=*importSlot;check(installRenderExtent(scenario=="extent-overflow-width"?0x80000000u:width,scenario=="extent-overflow-height"?0x80000000u:height)==succeeds,"Render extent installation result disagrees with owned state");failAfter=-1;
    check(!invalidCleanup,"Installer cleaned a hook it did not own");
    MEMORY_BASIC_INFORMATION memory{};check(VirtualQuery(importSlot,&memory,sizeof(memory))&&memory.Protect==PAGE_READONLY,"Import page protection was not restored");
    if(succeeds){
        check(owned()==3&&eyeWidth==width&&eyeHeight==height&&*importSlot==reinterpret_cast<void*>(&renderClientRect)&&refreshCalls==1,"Successful extent installation lost ownership");
        RECT rect{};testCaller=uintptr_t(imageBytes)+argent::build::rva(0x1d0912e)+6;check(renderClientRect(nullptr,&rect)&&rect.right-rect.left==LONG(width)&&rect.bottom-rect.top==LONG(height),"Audited swapchain caller did not receive eye extent");
        testCaller=uintptr_t(imageBytes)+0x100;check(renderClientRect(nullptr,&rect)&&rect.right-rect.left==800&&rect.bottom-rect.top==600,"Unaudited window caller received virtual extent");
        if(scenario=="extent-repeat"){check(installRenderExtent(width,height)&&!installRenderExtent(width+1,height)&&createCalls==3&&refreshCalls==1,"Repeated extent installation changed live ownership");}
    }else{
        const auto expected=scenario=="extent-cas-race"||scenario=="extent-foreign-rollback"?reinterpret_cast<void*>(&foreignRect):initial;
        check(!owned()&&!eyeWidth&&!eyeHeight&&*importSlot==expected,"Failed extent installation retained or overwrote owned state");
        const auto output=reinterpret_cast<const uint32_t*>(imageBytes+argent::build::rva(0x39aabe4));
        check(output[0]==900&&output[1]==(changeOutput?777u:600u),"Failed extent refresh retained owned dimensions or overwrote native changes");
        if(scenario=="extent-create-1"||scenario=="extent-create-2"||scenario=="extent-enable-0"||scenario=="extent-enable-1"||scenario=="extent-enable-2"||scenario=="extent-protect"||scenario=="extent-restore"||scenario=="extent-refresh"){
            failCreate=failEnable=0;failProtection=0;throwRefresh=false;check(installRenderExtent(width,height)&&owned()==3,"Clean extent retry failed");
        }
    }
    if(!logPrefix.empty())check(logFailures>0,"Extent diagnostic failure was not exercised");
    if(scenario=="extent-format-oom")check(allocationFailures==1,"Extent formatting failure was not exercised");
}
void cameraCase(std::string_view scenario){
    if(scenario=="camera-log")logPrefix="ETERNAL_";
    if(scenario=="camera-format-oom")formatFailure=true;
    if(scenario=="camera-pin")failPin=true;
    if(scenario=="camera-init")failInitialize=true;
    if(scenario=="camera-create")failCreate=uintptr_t(imageBytes)+0x1000;
    if(scenario=="camera-enable")failEnable=uintptr_t(imageBytes)+0x1000;
    if(scenario=="camera-conflict")hooks[0]={imageBytes+0x1000,false,true};
    if(scenario=="camera-poll-enable")failEnable=uintptr_t(imageBytes)+argent::build::rva(0x1dc57a0);
    if(scenario=="camera-poll-conflict")hooks[0]={imageBytes+argent::build::rva(0x1dc57a0),false,true};
    if(scenario=="camera-fov-enable")failEnable=uintptr_t(imageBytes)+argent::build::rva(0x1481d30);
    if(scenario=="camera-fov-conflict")hooks[0]={imageBytes+argent::build::rva(0x1481d30),false,true};
    if(scenario=="camera-remove-fatal"){failEnable=failRemove=uintptr_t(imageBytes)+0x1000;}
    if(scenario=="camera-fov-remove-fatal"){failEnable=failRemove=uintptr_t(imageBytes)+argent::build::rva(0x1481d30);}
    if(scenario=="camera-poll-remove-fatal"){failEnable=failRemove=uintptr_t(imageBytes)+argent::build::rva(0x1dc57a0);}
    const bool succeeds=scenario=="camera-normal"||scenario=="camera-log"||scenario=="camera-format-oom"||scenario=="camera-poll-enable"||scenario=="camera-poll-conflict"||scenario=="camera-fov-enable"||scenario=="camera-fov-conflict"||scenario=="camera-fov-remove-fatal"||scenario=="camera-poll-remove-fatal";
    check(install()==succeeds&&installed.load()==succeeds,"Camera installation result disagrees with activation");failAfter=-1;
    const bool optionalMissing=scenario=="camera-poll-enable"||scenario=="camera-poll-conflict"||scenario=="camera-fov-enable"||scenario=="camera-fov-conflict";
    check(!invalidCleanup,"Camera installer cleaned a foreign hook");check(owned()==(succeeds?(optionalMissing?2u:3u):0u),"Camera installer leaked partial hooks");
    if(succeeds){const auto before=createCalls;check(install()&&createCalls==before,"Camera retry duplicated active hooks");}
    if(!logPrefix.empty())check(logFailures>0,"Camera installation logging failure was not exercised");
    if(scenario=="camera-format-oom")check(allocationFailures==1,"Camera installation formatting failure was not exercised");
}
bool child(const wchar_t* executable,const wchar_t* scenario,bool store){
    std::wstring command=L"\""+std::wstring(executable)+L"\" "+scenario+(store?L" store":L"");STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Installation child creation failed");
    const auto wait=WaitForSingleObject(process.hProcess,15000);DWORD code{};if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,INFINITE);}
    const DWORD expected=std::wstring_view(scenario).find(L"-fatal")!=std::wstring_view::npos?0xc0000602u:0u;
    const bool passed=wait==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&code)&&code==expected;
    CloseHandle(process.hThread);CloseHandle(process.hProcess);if(!passed)std::wcerr<<scenario<<L" store="<<store<<L" exit="<<code<<L'\n';return passed;
}
}

int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);SetEnvironmentVariableW(L"ARGENT_LOG",nullptr);
    try{
        if(argc>1){Image image(argc>2);const std::string_view scenario=argv[1];if(scenario=="extent-concurrent"||scenario=="camera-concurrent")concurrentCase(scenario=="extent-concurrent");else if(scenario.rfind("extent-",0)==0)extentCase(scenario);else cameraCase(scenario);return 0;}
        wchar_t executable[32768]{};check(GetModuleFileNameW(nullptr,executable,32768)!=0,"Executable path unavailable");unsigned scenarios{},failures{};
        for(const bool store:{false,true})for(const auto scenario:{L"extent-normal",L"extent-log",L"extent-format-oom",L"extent-init",L"extent-protect",L"extent-restore",L"extent-restore-fatal",L"extent-refresh",L"extent-refresh-external",L"extent-cas-race",L"extent-null-import",L"extent-recursive-import",L"extent-foreign-import",L"extent-foreign-rollback",L"extent-hash",L"extent-repeat",L"extent-signature-0",L"extent-signature-1",L"extent-signature-2",L"extent-signature-3",L"extent-signature-4",L"extent-create-0",L"extent-create-1",L"extent-create-2",L"extent-conflict-0",L"extent-conflict-1",L"extent-conflict-2",L"extent-enable-0",L"extent-enable-1",L"extent-enable-2",L"extent-overflow-width",L"extent-overflow-height",L"extent-remove-fatal",L"extent-rollback-protect-fatal",L"extent-concurrent",L"camera-normal",L"camera-log",L"camera-format-oom",L"camera-pin",L"camera-init",L"camera-create",L"camera-enable",L"camera-conflict",L"camera-poll-enable",L"camera-poll-conflict",L"camera-fov-enable",L"camera-fov-conflict",L"camera-remove-fatal",L"camera-fov-remove-fatal",L"camera-poll-remove-fatal",L"camera-concurrent"}){++scenarios;failures+=!child(executable,scenario,store);}
        std::cout<<scenarios<<" production hook installation scenarios, "<<failures<<" failures\n";return failures?1:0;
    }catch(const std::exception& error){failAfter=-1;std::cerr<<error.what()<<'\n';return 1;}
}
