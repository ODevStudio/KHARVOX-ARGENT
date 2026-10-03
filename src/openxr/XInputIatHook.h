#pragma once
#include <windows.h>
#include <atomic>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>

namespace argent::input {
inline bool readXInputImage(unsigned char* base,size_t limit,size_t offset,void* output,size_t size) noexcept {
    const auto address=reinterpret_cast<uintptr_t>(base),maximum=std::numeric_limits<uintptr_t>::max();
    if(!base||!size||offset>limit||size>limit-offset||offset>maximum-address||size-1>maximum-address-offset)return false;
    const auto first=reinterpret_cast<const void*>(address+offset),last=reinterpret_cast<const void*>(address+offset+size-1);
    MEMORY_BASIC_INFORMATION begin{},end{};SIZE_T read{};
    return VirtualQuery(first,&begin,sizeof(begin))&&VirtualQuery(last,&end,sizeof(end))&&
        begin.AllocationBase==base&&end.AllocationBase==base&&begin.State==MEM_COMMIT&&end.State==MEM_COMMIT&&
        ReadProcessMemory(GetCurrentProcess(),first,output,size,&read)&&read==size;
}
struct XInputImport {
    unsigned char* base{};size_t size{};void** slot{};void* native{};
};
inline XInputImport findXInputImport(const char* exportName) noexcept {
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    if(!readXInputImage(base,std::numeric_limits<size_t>::max(),0,&dos,sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<LONG(sizeof(dos))||
       !readXInputImage(base,std::numeric_limits<size_t>::max(),size_t(dos.e_lfanew),&nt,sizeof(nt))||nt.Signature!=IMAGE_NT_SIGNATURE||
       nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.FileHeader.SizeOfOptionalHeader<sizeof(IMAGE_OPTIONAL_HEADER64)||
       nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||nt.OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_IMPORT||
       nt.OptionalHeader.NumberOfRvaAndSizes>IMAGE_NUMBEROF_DIRECTORY_ENTRIES)return {};
    const size_t size=nt.OptionalHeader.SizeOfImage,headers=nt.OptionalHeader.SizeOfHeaders;
    const size_t ntSize=sizeof(nt.Signature)+sizeof(nt.FileHeader)+nt.FileHeader.SizeOfOptionalHeader;
    if(headers>size||size_t(dos.e_lfanew)>headers||ntSize>headers-size_t(dos.e_lfanew))return {};
    const auto directory=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!directory.VirtualAddress||directory.VirtualAddress>size||directory.Size>size-directory.VirtualAddress||directory.Size<sizeof(IMAGE_IMPORT_DESCRIPTOR))return {};
    XInputImport selected{};
    for(size_t index=0;index<directory.Size/sizeof(IMAGE_IMPORT_DESCRIPTOR);++index){
        IMAGE_IMPORT_DESCRIPTOR row{};
        if(!readXInputImage(base,size,directory.VirtualAddress+index*sizeof(row),&row,sizeof(row)))return {};
        if(!row.Name){if(row.OriginalFirstThunk||row.TimeDateStamp||row.ForwarderChain||row.FirstThunk)return {};return selected;}
        char name[MAX_PATH]{};bool terminated{};
        for(size_t character=0;character<sizeof(name);++character){
            if(!readXInputImage(base,size,size_t(row.Name)+character,name+character,1))return {};
            if(!name[character]){terminated=true;break;}
        }
        if(!terminated)return {};
        if(_stricmp(name,"xinput1_3.dll")&&_stricmp(name,"xinput1_4.dll")&&_stricmp(name,"xinput9_1_0.dll"))continue;
        const auto library=GetModuleHandleA(name);const auto expected=library?GetProcAddress(library,exportName):nullptr;
        if(!expected)continue;
        if(!row.FirstThunk||row.FirstThunk>size||row.FirstThunk%alignof(void*))return {};
        terminated=false;
        for(size_t thunk=0;thunk<(size-row.FirstThunk)/sizeof(IMAGE_THUNK_DATA64);++thunk){
            IMAGE_THUNK_DATA64 entry{};const auto offset=row.FirstThunk+thunk*sizeof(entry);
            if(!readXInputImage(base,size,offset,&entry,sizeof(entry)))return {};
            if(!entry.u1.Function){terminated=true;break;}
            if(!selected.slot&&entry.u1.Function==reinterpret_cast<uintptr_t>(expected))
                selected={base,size,reinterpret_cast<void**>(base+offset),reinterpret_cast<void*>(expected)};
        }
        if(!terminated)return {};
    }
    return {};
}
[[noreturn]] inline void failXInputIatRollback() noexcept {RaiseFailFastException(nullptr,nullptr,0);std::terminate();}
struct XInputIatWrite {
    void** slot;void* native;void* callback;DWORD protection{};bool writable{},patched{},committed{};
    bool restoreProtection() noexcept {
        if(!writable)return true;DWORD ignored{};
        if(!VirtualProtect(slot,sizeof(void*),protection,&ignored))return false;
        writable=false;return true;
    }
    ~XInputIatWrite(){
        if(committed)return;
        if(patched)InterlockedCompareExchangePointer(slot,native,callback);
        if(!restoreProtection())failXInputIatRollback();
    }
};
inline std::mutex xinputInstallationMutex;
template<class Function> bool installXInputIatHook(const char* exportName,Function callback,std::atomic<Function>& original) noexcept {try{
    std::lock_guard<std::mutex> lock(xinputInstallationMutex);static XInputImport installed{};
    const auto replacement=reinterpret_cast<void*>(callback);
    if(installed.slot){
        void* current{};
        return GetModuleHandleW(nullptr)==reinterpret_cast<HMODULE>(installed.base)&&
            readXInputImage(installed.base,installed.size,reinterpret_cast<uintptr_t>(installed.slot)-reinterpret_cast<uintptr_t>(installed.base),&current,sizeof(current))&&current==replacement;
    }
    const auto target=findXInputImport(exportName);
    if(!target.slot||target.native==replacement)return false;
    const auto native=reinterpret_cast<Function>(target.native),previous=original.load();
    if(previous&&previous!=native)return false;
    HMODULE pinned{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(callback),&pinned))return false;
    XInputIatWrite write{target.slot,target.native,replacement};
    if(!VirtualProtect(target.slot,sizeof(void*),PAGE_READWRITE,&write.protection))return false;
    write.writable=true;original.store(native);
    if(InterlockedCompareExchangePointer(target.slot,replacement,target.native)!=target.native)return false;
    write.patched=true;
    if(!write.restoreProtection())return false;
    installed=target;write.committed=true;return true;
}catch(...){return false;}}
}
