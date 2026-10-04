#include <windows.h>
#include <Xinput.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
unsigned char* image{};
std::string_view fault;
bool haptics{};
std::atomic<unsigned> protectionCalls{},patchCalls{},pins{},nativeCalls{};
void** slot{};
void (*observeInflight)(){};
DWORD WINAPI nativeGet(DWORD user,XINPUT_STATE* output){++nativeCalls;if(output)output->dwPacketNumber=123;return 41+user;}
DWORD WINAPI nativeSet(DWORD user,XINPUT_VIBRATION*){++nativeCalls;return 71+user;}
DWORD WINAPI foreignGet(DWORD,XINPUT_STATE*){return 91;}
DWORD WINAPI foreignSet(DWORD,XINPUT_VIBRATION*){return 92;}
void* nativeTarget(){return haptics?reinterpret_cast<void*>(&nativeSet):reinterpret_cast<void*>(&nativeGet);}
void* foreignTarget(){return haptics?reinterpret_cast<void*>(&foreignSet):reinterpret_cast<void*>(&foreignGet);}
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
HMODULE WINAPI moduleW(LPCWSTR){return fault=="null-module"?nullptr:reinterpret_cast<HMODULE>(image);}
HMODULE WINAPI moduleA(LPCSTR){return fault=="missing-library"?nullptr:reinterpret_cast<HMODULE>(1);}
FARPROC WINAPI procedure(HMODULE,LPCSTR name){
    if(fault=="missing-export")return nullptr;
    return reinterpret_cast<FARPROC>(std::strcmp(name,"XInputSetState")==0?reinterpret_cast<void*>(&nativeSet):reinterpret_cast<void*>(&nativeGet));
}
BOOL WINAPI pinModule(DWORD flags,LPCWSTR,HMODULE* module){
    ++pins;check(flags==(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN),"Callback module was not pinned");
    if(fault=="pin-failure")return FALSE;*module=reinterpret_cast<HMODULE>(2);return TRUE;
}
SIZE_T WINAPI queryMemory(LPCVOID address,PMEMORY_BASIC_INFORMATION info,SIZE_T size){
    if(fault=="query-failure")return 0;return VirtualQuery(address,info,size);
}
BOOL WINAPI readMemory(HANDLE process,LPCVOID address,LPVOID output,SIZE_T size,SIZE_T* read){
    if(fault=="read-failure")return FALSE;return ReadProcessMemory(process,address,output,size,read);
}
BOOL WINAPI protectMemory(LPVOID address,SIZE_T size,DWORD protection,DWORD* previous){
    const auto call=++protectionCalls;
    if(fault=="concurrent"||fault=="both-concurrent"){
        if(protection==PAGE_READWRITE)Sleep(30);*previous=protection==PAGE_READWRITE?PAGE_READONLY:PAGE_READWRITE;return TRUE;
    }
    if(fault=="write-failure"&&call==1)return FALSE;
    if(fault=="inflight-rollback"&&call==2){std::thread observer(observeInflight);observer.join();return FALSE;}
    if((fault=="restore-failure"||fault=="foreign-rollback")&&call==2){if(fault=="foreign-rollback")*slot=foreignTarget();return FALSE;}
    if(fault=="restore-terminal"&&call>=2)return FALSE;
    const auto result=VirtualProtect(address,size,protection,previous);
    if(result&&fault=="foreign-race"&&call==1)*slot=foreignTarget();
    return result;
}
void* exchangePointer(void* volatile* address,void* replacement){++patchCalls;return _InterlockedExchangePointer(address,replacement);}
void* comparePointer(void* volatile* address,void* replacement,void* expected){++patchCalls;return _InterlockedCompareExchangePointer(address,replacement,expected);}
}

#define GetModuleHandleW moduleW
#define GetModuleHandleA moduleA
#define GetModuleHandleExW pinModule
#define GetProcAddress procedure
#define VirtualProtect protectMemory
#define VirtualQuery queryMemory
#define ReadProcessMemory readMemory
#undef InterlockedExchangePointer
#define InterlockedExchangePointer exchangePointer
#undef InterlockedCompareExchangePointer
#define InterlockedCompareExchangePointer comparePointer
#include "../src/openxr/HapticBridge.h"
#undef GetModuleHandleW
#undef GetModuleHandleA
#undef GetModuleHandleExW
#undef GetProcAddress
#undef VirtualProtect
#undef VirtualQuery
#undef ReadProcessMemory
#undef InterlockedExchangePointer
#undef InterlockedCompareExchangePointer

namespace {
struct Image {
    unsigned char* bytes=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Image(const char* library){
        check(bytes!=nullptr,"Fixture image unavailable");image=bytes;slot=reinterpret_cast<void**>(bytes+0x2000);
        auto& dos=*reinterpret_cast<IMAGE_DOS_HEADER*>(bytes);dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=0x80;
        auto& nt=*reinterpret_cast<IMAGE_NT_HEADERS64*>(bytes+0x80);nt.Signature=IMAGE_NT_SIGNATURE;nt.FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
        nt.FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        nt.OptionalHeader.SizeOfImage=0x4000;nt.OptionalHeader.SizeOfHeaders=0x1000;nt.OptionalHeader.NumberOfRvaAndSizes=IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
        nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]={0x1000,3*sizeof(IMAGE_IMPORT_DESCRIPTOR)};
        auto rows=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(bytes+0x1000);rows[0].Name=0x1100;rows[0].FirstThunk=0x2100;
        rows[1].Name=0x1120;rows[1].FirstThunk=0x2000;rows[1].OriginalFirstThunk=0x1200;
        std::strcpy(reinterpret_cast<char*>(bytes+0x1100),"other.dll");std::strcpy(reinterpret_cast<char*>(bytes+0x1120),library);
        *reinterpret_cast<ULONGLONG*>(bytes+0x1200)=IMAGE_ORDINAL_FLAG64|(haptics?3:2);
        *slot=nativeTarget();*reinterpret_cast<void**>(bytes+0x2100)=nativeTarget();
        if(fault=="foreign-existing")*slot=foreignTarget();
        if(fault=="bad-dos")dos.e_magic=0;
        if(fault=="negative-nt")dos.e_lfanew=-1;
        if(fault=="outside-nt")dos.e_lfanew=0x4000;
        if(fault=="bad-nt")nt.Signature=0;
        if(fault=="wrong-machine")nt.FileHeader.Machine=IMAGE_FILE_MACHINE_I386;
        if(fault=="wrong-optional")nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;
        if(fault=="short-optional")nt.FileHeader.SizeOfOptionalHeader=0;
        if(fault=="large-optional")nt.FileHeader.SizeOfOptionalHeader=0xffff;
        if(fault=="no-directories")nt.OptionalHeader.NumberOfRvaAndSizes=0;
        if(fault=="zero-image")nt.OptionalHeader.SizeOfImage=0;
        if(fault=="short-headers")nt.OptionalHeader.SizeOfHeaders=1;
        if(fault=="large-headers")nt.OptionalHeader.SizeOfHeaders=0x5000;
        if(fault=="outside-directory")nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress=0x4000;
        if(fault=="short-directory")nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size=sizeof(IMAGE_IMPORT_DESCRIPTOR);
        if(fault=="large-directory")nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size=0xffffffff;
        if(fault=="missing-terminator")nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size=2*sizeof(IMAGE_IMPORT_DESCRIPTOR);
        if(fault=="invalid-terminator")rows[2].FirstThunk=0x2000;
        if(fault=="outside-name")rows[1].Name=0x4000;
        if(fault=="outside-thunk")rows[1].FirstThunk=0x4000;
        if(fault=="unaligned-thunk")rows[1].FirstThunk=0x2001;
        if(fault=="unreadable-thunk")rows[1].FirstThunk=0x3000;
        if(fault=="unterminated-thunk"){
            rows[1].FirstThunk=0x2ff8;*reinterpret_cast<void**>(bytes+0x2ff8)=nativeTarget();
        }
        if(fault=="bound-import")rows[1].OriginalFirstThunk=0;
        if(fault=="both-concurrent"){slot[0]=reinterpret_cast<void*>(&nativeGet);slot[1]=reinterpret_cast<void*>(&nativeSet);}
        DWORD old{};check(VirtualProtect(bytes+0x3000,0x1000,PAGE_NOACCESS,&old),"Fixture guard page unavailable");
        if(fault!="concurrent"&&fault!="both-concurrent")check(VirtualProtect(bytes+0x2000,0x1000,PAGE_READONLY,&old),"Fixture IAT protection unavailable");
    }
    ~Image(){VirtualFree(bytes,0,MEM_RELEASE);}
};
bool install(){return haptics?argent::input::installHaptics():argent::input::install();}
void* callback(){return haptics?reinterpret_cast<void*>(&argent::input::setState):reinterpret_cast<void*>(&argent::input::getState);}
void originalPrepared(){if(haptics)argent::input::originalSetState=&nativeSet;else argent::input::originalGetState=&nativeGet;}
void observeCallback(){
    XINPUT_STATE state{};XINPUT_VIBRATION vibration{123,456};
    const auto code=haptics?argent::input::setState(1,&vibration):argent::input::getState(1,&state);
    check(code==(haptics?72:42),"In-flight callback lost its native dispatch during rollback");
}
void checkProtection(){MEMORY_BASIC_INFORMATION memory{};check(VirtualQuery(slot,&memory,sizeof(memory))&&memory.Protect==PAGE_READONLY,"Installer did not restore IAT page protection");}
void runCase(std::string_view scenario,const char* library){
    fault=scenario;Image allocation(library);const auto before=*slot;
    observeInflight=&observeCallback;
    if(fault=="prepared-native")originalPrepared();
    if(fault=="foreign-native"){if(haptics)argent::input::originalSetState=&foreignSet;else argent::input::originalGetState=&foreignGet;}
    if(fault=="concurrent"||fault=="both-concurrent"){
        std::array<bool,8> results{};std::atomic<unsigned> ready{};std::atomic<bool> start{};std::vector<std::thread> workers;
        for(size_t i=0;i<results.size();++i)workers.emplace_back([&,i]{++ready;while(!start.load())Sleep(1);results[i]=fault=="both-concurrent"?(i%2?argent::input::install():argent::input::installHaptics()):install();});
        while(ready!=results.size())Sleep(1);start=true;for(auto& worker:workers)worker.join();
        for(const auto result:results)check(result,"Concurrent installer rejected matching reuse");
        if(fault=="both-concurrent")check(protectionCalls==4&&pins==2&&slot[0]==reinterpret_cast<void*>(&argent::input::getState)&&slot[1]==reinterpret_cast<void*>(&argent::input::setState),"Concurrent state/rumble installation lost shared IAT page ownership");
        else check(protectionCalls==2&&pins==1&&*slot==callback(),"Concurrent installation patched or pinned more than once");return;
    }
    const auto result=install();
    if(fault=="normal"||fault=="bound-import"||fault=="prepared-native"||fault=="foreign-after"||fault=="module-after"){
        check(result&&*slot==callback()&&*reinterpret_cast<void**>(image+0x2100)==nativeTarget(),"Installer failed or patched an unrelated DLL");
        check(pins==1,"Successful hook did not pin its callback module");checkProtection();
        const auto protections=protectionCalls.load();check(install()&&protectionCalls==protections&&pins==1,"Matching installed hook repeated native setup");
        if(fault=="foreign-after"){
            DWORD old{};check(VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old),"Foreign fixture write unavailable");*slot=foreignTarget();DWORD ignored{};
            check(VirtualProtect(slot,sizeof(void*),old,&ignored),"Foreign fixture protection unavailable");
            check(!install()&&*slot==foreignTarget()&&protectionCalls==protections,"Cached success concealed a replaced IAT hook");return;
        }
        if(fault=="module-after"){fault="null-module";check(!install()&&*slot==callback()&&protectionCalls==protections,"Cached success accepted a different main module");return;}
        XINPUT_STATE state{};XINPUT_VIBRATION vibration{123,456};
        const auto code=haptics?argent::input::setState(1,&vibration):argent::input::getState(1,&state);
        check(code==(haptics?72:42)&&nativeCalls==1,"Hook did not preserve native callback arguments/result");return;
    }
    check(!result,"Installer accepted a failed or malformed setup");
    check(*slot==(fault=="foreign-race"||fault=="foreign-rollback"?foreignTarget():before),"Failure rollback overwrote foreign ownership or retained its hook");
    if(fault=="foreign-native")check(haptics?argent::input::originalSetState==&foreignSet:argent::input::originalGetState==&foreignGet,"Installer overwrote previously published native dispatch");
    if(fault=="inflight-rollback"){checkProtection();observeCallback();check(nativeCalls==2,"Rollback interrupted in-flight native callback delivery");}
    if(fault=="restore-failure"||fault=="foreign-rollback"||fault=="foreign-race"||fault=="write-failure")checkProtection();
    if(fault=="restore-failure"||fault=="write-failure"||fault=="pin-failure"){
        fault="normal";check(install()&&*slot==callback(),"Failed installer could not retry");checkProtection();
    }
}
bool child(const wchar_t* executable,const std::wstring& scenario,bool rumble,const wchar_t* library){
    std::wstring command=L"\""+std::wstring(executable)+L"\" "+scenario+(rumble?L" rumble ":L" state ")+library;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"XInput fixture child unavailable");
    const auto wait=WaitForSingleObject(process.hProcess,15000);DWORD code{};
    if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,INFINITE);}
    const bool expected=wait==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&code)&&code==(scenario==L"restore-terminal"?DWORD(0xc0000602):0);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    if(!expected)std::wcerr<<scenario<<L" rumble="<<rumble<<L" dll="<<library<<L" exit="<<std::hex<<code<<std::dec<<L'\n';return expected;
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    try{
        if(argc>1){haptics=std::string_view(argv[2])=="rumble";runCase(argv[1],argv[3]);return 0;}
        wchar_t executable[32768]{};check(GetModuleFileNameW(nullptr,executable,32768),"XInput fixture path unavailable");unsigned cases{},failures{};
        for(const bool rumble:{false,true})for(const auto library:{L"xinput1_3.dll",L"XINPUT1_4.DLL",L"xinput9_1_0.dll"})
            for(const auto scenario:{L"normal",L"bound-import",L"prepared-native",L"foreign-native",L"foreign-after",L"module-after",L"foreign-existing",L"foreign-race",L"foreign-rollback",L"inflight-rollback",L"concurrent",L"both-concurrent",L"null-module",L"missing-library",L"missing-export",L"bad-dos",L"negative-nt",L"outside-nt",L"bad-nt",L"wrong-machine",L"wrong-optional",L"short-optional",L"large-optional",L"no-directories",L"zero-image",L"short-headers",L"large-headers",L"outside-directory",L"short-directory",L"large-directory",L"missing-terminator",L"invalid-terminator",L"outside-name",L"outside-thunk",L"unaligned-thunk",L"unreadable-thunk",L"unterminated-thunk",L"query-failure",L"read-failure",L"pin-failure",L"write-failure",L"restore-failure",L"restore-terminal"}){
                ++cases;failures+=!child(executable,scenario,rumble,library);
            }
        std::cout<<cases<<" production XInput installation scenarios, "<<failures<<" failures\n";return failures?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
