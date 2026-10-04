#include "../src/QuadRuntime.h"
#include "../src/XrWorker.h"
#include "../src/openxr/OpenXRRuntimePolicy.h"
#include <array>
#include <cstring>
#include <iostream>
#include <new>
#include <sstream>
#include <stdexcept>

namespace argent {
namespace {
enum class Failure { None, Path, Loader, Export, EnumerationCount, EnumerationFill,
    NoGraphics, ManifestWrapper, ManifestImpl, Create, System, Requirements, Properties,
    NativeThrow, ExtensionCount, ExtensionFill, ExtensionThrow };
Failure failure{};
kharvox::OpenXRRuntimeKind expectedKind{};
const char* throwingPrefix{};
bool attempted{},failed{},enable2{},simulator{},vr{},forceEnable1{},hasEnable1{},hasEnable2{},expectedDeviceQuery{};
unsigned loaderCalls{},enumerationCalls{},manifestCalls{},createCalls{},systemCalls{},requirementsCalls{},propertyCalls{},queryCalls{},logsThrown{};
DWORD creationThread{};
HMODULE loader{};
PFN_xrGetInstanceProcAddr get{};
XrInstance instance{};
XrSystemId system{};
kharvox::OpenXRRuntimeKind runtimeKind{};
std::recursive_mutex mutex;
void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
template<class T> T handle(std::uintptr_t value) { return reinterpret_cast<T>(value); }
void check(XrResult result,const char* operation) { if(XR_FAILED(result))throw std::runtime_error(operation); }
unsigned nativeCalls() { return loaderCalls+enumerationCalls+createCalls+systemCalls+requirementsCalls+propertyCalls; }
XRAPI_ATTR XrResult XRAPI_CALL fakeGet(XrInstance,const char*,PFN_xrVoidFunction*) { return XR_ERROR_FUNCTION_UNSUPPORTED; }
HMODULE fakeLoad(LPCWSTR path,HANDLE file,DWORD flags) {
    ++loaderCalls;
    require(std::filesystem::path(path).filename()==L"openxr_loader.dll"&&!file
        &&flags==(LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32),"Loader search security changed");
    return failure==Failure::Loader?nullptr:handle<HMODULE>(1);
}
FARPROC fakeExport(HMODULE module,LPCSTR name) {
    require(module==handle<HMODULE>(1)&&!std::strcmp(name,"xrGetInstanceProcAddr"),"Loader export changed");
    return failure==Failure::Export?nullptr:reinterpret_cast<FARPROC>(fakeGet);
}
DWORD fakeEnvironment(LPCSTR name,LPSTR value,DWORD size) {
    require(!std::strcmp(name,"ARGENT_STEAMVR_FORCE_ENABLE1")&&size>=2,"Unexpected startup environment query");
    if(!forceEnable1)return 0;
    value[0]='1';value[1]=0;return 1;
}
XrResult fake_xrEnumerateInstanceExtensionProperties(const char* layer,uint32_t capacity,uint32_t* count,XrExtensionProperties* out) {
    ++enumerationCalls;require(!layer,"Unexpected extension layer filter");
    if((enumerationCalls==1&&failure==Failure::EnumerationCount)
        ||(enumerationCalls==2&&failure==Failure::EnumerationFill))return XR_ERROR_RUNTIME_FAILURE;
    const auto names=failure==Failure::NoGraphics?std::array<const char*,2>{}:
        std::array<const char*,2>{{hasEnable1?XR_KHR_VULKAN_ENABLE_EXTENSION_NAME:nullptr,
            hasEnable2?XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME:nullptr}};
    *count=unsigned(names[0]!=nullptr)+unsigned(names[1]!=nullptr);
    if(out){
        require(capacity>=*count,"Extension enumeration exceeded capacity");
        unsigned index{};
        for(const auto name:names)if(name){require(out[index].type==XR_TYPE_EXTENSION_PROPERTIES,"Missing extension structure type");strcpy_s(out[index++].extensionName,name);}
    }
    return XR_SUCCESS;
}
bool expectsEnable2() {
    return hasEnable2&&expectedKind!=kharvox::OpenXRRuntimeKind::VirtualDesktop
        &&!(forceEnable1&&kharvox::isSteamBackedOpenXRRuntime(expectedKind)&&hasEnable1);
}
XrResult fake_xrCreateInstance(const XrInstanceCreateInfo* info,XrInstance* out) {
    ++createCalls;creationThread=GetCurrentThreadId();
    const auto extension=expectsEnable2()?XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME:XR_KHR_VULKAN_ENABLE_EXTENSION_NAME;
    require(info->type==XR_TYPE_INSTANCE_CREATE_INFO&&info->applicationInfo.apiVersion==XR_MAKE_VERSION(1,0,0)
        &&!std::strcmp(info->applicationInfo.applicationName,"DOOM Eternal")
        &&!std::strcmp(info->applicationInfo.engineName,"idTech7")
        &&info->enabledExtensionCount==1&&!std::strcmp(info->enabledExtensionNames[0],extension),"Runtime path or application identity changed");
    if(failure==Failure::NativeThrow)throw std::runtime_error("native startup threw");
    if(failure==Failure::Create)return XR_ERROR_RUNTIME_FAILURE;
    *out=handle<XrInstance>(2);return XR_SUCCESS;
}
XrResult fake_xrGetSystem(XrInstance value,const XrSystemGetInfo* info,XrSystemId* out) {
    ++systemCalls;
    require(value==instance&&info->formFactor==XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,"System selection changed");
    if(failure==Failure::System)return XR_ERROR_RUNTIME_FAILURE;
    *out=3;return XR_SUCCESS;
}
XrResult fakeRequirements(XrInstance value,XrSystemId id,XrGraphicsRequirementsVulkanKHR* out,bool runtimeManaged) {
    ++requirementsCalls;
    require(value==instance&&id==system&&out->type==XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR
        &&runtimeManaged==expectsEnable2(),"Graphics requirements routing changed");
    return failure==Failure::Requirements?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;
}
XrResult fake_xrGetVulkanGraphicsRequirementsKHR(XrInstance value,XrSystemId id,XrGraphicsRequirementsVulkanKHR* out) { return fakeRequirements(value,id,out,false); }
XrResult fake_xrGetVulkanGraphicsRequirements2KHR(XrInstance value,XrSystemId id,XrGraphicsRequirementsVulkanKHR* out) { return fakeRequirements(value,id,out,true); }
XrResult fake_xrGetInstanceProperties(XrInstance value,XrInstanceProperties* out) {
    ++propertyCalls;require(value==instance&&out->type==XR_TYPE_INSTANCE_PROPERTIES,"Runtime property query changed");
    if(failure==Failure::Properties)return XR_ERROR_RUNTIME_FAILURE;
    strcpy_s(out->runtimeName,"OpenXR Simulator Runtime");return XR_SUCCESS;
}
XrResult fakeExtensions(XrInstance value,XrSystemId id,uint32_t capacity,uint32_t* count,char* out,bool device) {
    ++queryCalls;require(value==instance&&id==system&&device==expectedDeviceQuery,"Extension query dispatch changed");
    if(failure==Failure::ExtensionThrow)throw std::runtime_error("native extensions threw");
    if((!capacity&&failure==Failure::ExtensionCount)||(capacity&&failure==Failure::ExtensionFill))return XR_ERROR_RUNTIME_FAILURE;
    const auto text=device?"VK_KHR_swapchain VK_KHR_maintenance1":"\tVK_KHR_surface VK_KHR_win32_surface \n";
    *count=uint32_t(std::strlen(text)+1);
    if(out){require(capacity>=*count,"Extension text exceeded capacity");std::memcpy(out,text,*count);}
    return XR_SUCCESS;
}
XrResult fake_xrGetVulkanInstanceExtensionsKHR(XrInstance value,XrSystemId id,uint32_t capacity,uint32_t* count,char* out) { return fakeExtensions(value,id,capacity,count,out,false); }
XrResult fake_xrGetVulkanDeviceExtensionsKHR(XrInstance value,XrSystemId id,uint32_t capacity,uint32_t* count,char* out) { return fakeExtensions(value,id,capacity,count,out,true); }
}
std::filesystem::path runtimePath() {
    if(failure==Failure::Path)throw std::runtime_error("runtime path unavailable");
    return L"C:\\fixture";
}
void log(const std::string& message) {
    if(throwingPrefix&&message.rfind(throwingPrefix,0)==0){++logsThrown;throw std::bad_alloc{};}
}
namespace sfs { bool vrEnabled() { return vr; } }
}
namespace kharvox {
std::string fakeManifest() {
    ++argent::manifestCalls;
    if((argent::failure==argent::Failure::ManifestWrapper&&argent::manifestCalls==1)
        ||(argent::failure==argent::Failure::ManifestImpl&&argent::manifestCalls==2))throw std::bad_alloc{};
    switch(argent::expectedKind){
    case OpenXRRuntimeKind::SteamVR:return "steamxr.json";
    case OpenXRRuntimeKind::VDXR4Steam:return "vdxr4steam.json";
    case OpenXRRuntimeKind::VirtualDesktop:return "virtualdesktop.json";
    case OpenXRRuntimeKind::MetaOculus:return "meta_openxr.json";
    default:return {};
    }
}
}
namespace argent {
#define XR(name) fake_##name
#define LoadLibraryExW fakeLoad
#define GetProcAddress fakeExport
#define GetEnvironmentVariableA fakeEnvironment
#define activeOpenXRRuntimeManifest fakeManifest
#include "../src/XrStartup.inc"
#undef activeOpenXRRuntimeManifest
#undef GetEnvironmentVariableA
#undef GetProcAddress
#undef LoadLibraryExW
#undef XR
namespace {
void reset(Failure value,const char* prefix,kharvox::OpenXRRuntimeKind kind) {
    failure=value;throwingPrefix=prefix;expectedKind=kind;runtimeKind=kharvox::OpenXRRuntimeKind::Unknown;
    attempted=failed=enable2=simulator=forceEnable1=expectedDeviceQuery=false;
    vr=hasEnable1=hasEnable2=true;
    loaderCalls=enumerationCalls=manifestCalls=createCalls=systemCalls=requirementsCalls=propertyCalls=queryCalls=logsThrown=0;
    creationThread=0;loader=nullptr;get=nullptr;instance=XR_NULL_HANDLE;system=0;
}
void runStartup(Failure value,const char* prefix,kharvox::OpenXRRuntimeKind kind,bool enable1=true,bool enable2Available=true,bool force=false,bool vrEnabled=true) {
    reset(value,prefix,kind);hasEnable1=enable1;hasEnable2=enable2Available;forceEnable1=force;vr=vrEnabled;
    bool result{},escaped{};
    try { result=initializeXR(); }catch(...) { escaped=true; }
    const bool expected=value==Failure::None&&(hasEnable1||(hasEnable2&&kind!=kharvox::OpenXRRuntimeKind::VirtualDesktop));
    require(!escaped&&result==expected&&attempted&&failed==!expected,"Startup diagnostic escaped or failure state was not published");
    if(expected){
        require(instance==handle<XrInstance>(2)&&system==3&&simulator&&runtimeKind==kind
            &&enable2==expectsEnable2()&&createCalls==1&&propertyCalls==1,"Successful startup lost runtime ownership or negotiation");
        require((creationThread!=GetCurrentThreadId())==(vr&&kharvox::isSteamBackedOpenXRRuntime(kind)),"Steam startup worker ownership changed");
    }
    if(prefix)require(logsThrown>0,"Requested diagnostic failure was not injected");
    const auto calls=nativeCalls();
    require(initializeXRImpl()==expected&&nativeCalls()==calls,"Cached startup retried failed native setup or accepted partial initialization");
    std::cout<<"startup failure="<<int(value)<<" runtime="<<int(kind)<<" log="<<(prefix?prefix:"none")
        <<" result="<<result<<" native="<<calls<<" logThrows="<<logsThrown<<'\n';
}
void runExtensions(Failure value,const char* prefix,bool device) {
    reset(value,prefix,kharvox::OpenXRRuntimeKind::Unknown);instance=handle<XrInstance>(2);system=3;expectedDeviceQuery=device;
    std::vector<std::string> result;bool escaped{};
    try { result=xrExtensions(device); }catch(...) { escaped=true; }
    const bool expected=value==Failure::None;
    const auto names=device?std::vector<std::string>{"VK_KHR_swapchain","VK_KHR_maintenance1"}:
        std::vector<std::string>{"VK_KHR_surface","VK_KHR_win32_surface"};
    require(!escaped&&failed==!expected&&result==(expected?names:std::vector<std::string>{}),"Extension diagnostics disabled a valid path, escaped or returned partial results");
    if(prefix)require(logsThrown>0,"Requested extension diagnostic failure was not injected");
    if(!expected){const auto calls=queryCalls;require(xrExtensions(device).empty()&&queryCalls==calls,"Failed extension query retried native dispatch");}
    std::cout<<"extensions failure="<<int(value)<<" device="<<device<<" log="<<(prefix?prefix:"none")
        <<" count="<<result.size()<<" logThrows="<<logsThrown<<'\n';
}
void runExtensionGate(unsigned gate,bool device) {
    reset(Failure::None,nullptr,kharvox::OpenXRRuntimeKind::Unknown);
    if(gate!=0)instance=handle<XrInstance>(2);
    if(gate==1)failed=true;
    if(gate==2)enable2=true;
    require(xrExtensions(device).empty()&&!queryCalls,"Disabled or runtime-managed extensions queried legacy dispatch");
    std::cout<<"extension gate="<<gate<<" device="<<device<<" untouched=1\n";
}
}
}

int main() {
    unsigned scenarios{},failures{};
    const auto run=[&](const auto& operation){
        ++scenarios;
        try { operation(); }catch(const std::exception& error){++failures;std::cerr<<"case="<<scenarios<<": "<<error.what()<<'\n';}
    };
    using argent::Failure;using kharvox::OpenXRRuntimeKind;
    for(const auto kind:{OpenXRRuntimeKind::Unknown,OpenXRRuntimeKind::SteamVR,OpenXRRuntimeKind::VDXR4Steam,OpenXRRuntimeKind::VirtualDesktop,OpenXRRuntimeKind::MetaOculus})
        for(const auto available:{std::array<bool,2>{{true,true}},std::array<bool,2>{{true,false}},std::array<bool,2>{{false,true}}})
            run([&]{argent::runStartup(Failure::None,nullptr,kind,available[0],available[1]);});
    for(const auto kind:{OpenXRRuntimeKind::SteamVR,OpenXRRuntimeKind::VDXR4Steam}){
        run([&]{argent::runStartup(Failure::None,nullptr,kind,true,true,true);});
        run([&]{argent::runStartup(Failure::None,nullptr,kind,false,true,true);});
        run([&]{argent::runStartup(Failure::None,nullptr,kind,true,true,false,false);});
    }
    for(const auto kind:{OpenXRRuntimeKind::Unknown,OpenXRRuntimeKind::SteamVR}){
        for(const auto value:{Failure::Path,Failure::Loader,Failure::Export,Failure::EnumerationCount,Failure::EnumerationFill,
            Failure::NoGraphics,Failure::ManifestWrapper,Failure::ManifestImpl,Failure::Create,Failure::System,
            Failure::Requirements,Failure::Properties,Failure::NativeThrow})run([&]{argent::runStartup(value,nullptr,kind);});
        for(const auto value:{Failure::Loader,Failure::ManifestWrapper,Failure::System,Failure::Properties})
            run([&]{argent::runStartup(value,"XR_DISABLED ",kind);});
        for(const auto prefix:{"XR_VULKAN_PATH ","XR_APPLICATION_IDENTITY ","XR_INITIALIZED "})
            run([&]{argent::runStartup(Failure::None,prefix,kind);});
    }
    run([&]{argent::runStartup(Failure::None,"[STEAM-XR-THREAD]",OpenXRRuntimeKind::SteamVR);});
    run([&]{argent::runStartup(Failure::None,"[STEAM-XR-THREAD]",OpenXRRuntimeKind::VDXR4Steam);});
    for(const bool device:{false,true}){
        run([&]{argent::runExtensions(Failure::None,nullptr,device);});
        run([&]{argent::runExtensions(Failure::None,"XR_REQUIRED_EXTENSION ",device);});
        for(const auto value:{Failure::ExtensionCount,Failure::ExtensionFill}){
            run([&]{argent::runExtensions(value,nullptr,device);});
            run([&]{argent::runExtensions(value,"XR Vulkan extensions",device);});
        }
        run([&]{argent::runExtensions(Failure::ExtensionThrow,"native extensions threw",device);});
        for(unsigned gate=0;gate<3;++gate)run([&]{argent::runExtensionGate(gate,device);});
    }
    std::cout<<scenarios<<" scenarios, "<<failures<<" failures\n";
    return failures?1:0;
}
