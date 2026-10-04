#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include "../src/openxr/OpenXRRuntimePolicy.h"
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <exception>
#include <iostream>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
thread_local bool failNextAllocation{};
unsigned allocationFailures{};
}
void* operator new(std::size_t size) {
    if(failNextAllocation){failNextAllocation=false;++allocationFailures;throw std::bad_alloc{};}
    if(auto* value=std::malloc(size?size:1))return value;
    throw std::bad_alloc{};
}
void operator delete(void* value) noexcept {std::free(value);}
void operator delete(void* value,std::size_t) noexcept {std::free(value);}

namespace {
enum class Mode {
    Success,Auxiliary,NestedAuxiliary,XrHandled,NativeFailure,NullSuccess,NativeThrow,
    ExtensionOom,ExtensionThrow,MissingCreate,MissingGet,MissingChain,MissingLink,
    NullInfo,NullOutput,RegistryOom,PostCreateOom,PostCreateThrow,MessengerFailure,
    MessengerLookupThrow,MissingMessengerDestroy,MissingDestroy,DestroyLookupThrow,
    ExtentRefused,MessengerRegistryOom,DestroyResolutionOom,DestroyResolutionNull
};
struct Scenario {
    const char* name;
    Mode mode{Mode::Success};
    bool game{true};
    bool debug{};
    const char* throwingLog{};
};
constexpr Scenario scenarios[]{
    {"non-game",Mode::Success,false},
    {"game"},
    {"debug",Mode::Success,true,true},
    {"auxiliary",Mode::Auxiliary},
    {"nested-auxiliary",Mode::NestedAuxiliary},
    {"xr-handled",Mode::XrHandled},
    {"start-log",Mode::Success,true,false,"vkCreateInstance app="},
    {"result-log",Mode::Success,true,false,"vkCreateInstance result="},
    {"messenger-log",Mode::Success,true,true,"GPU_ADDRESS_MESSENGER"},
    {"auxiliary-log",Mode::Auxiliary,true,false,"SteamVR auxiliary"},
    {"native-failure",Mode::NativeFailure},
    {"native-failure-log",Mode::NativeFailure,true,false,"vkCreateInstance result="},
    {"null-success",Mode::NullSuccess},
    {"native-throw",Mode::NativeThrow},
    {"extension-oom",Mode::ExtensionOom},
    {"extension-throw",Mode::ExtensionThrow},
    {"missing-create",Mode::MissingCreate,false},
    {"missing-get",Mode::MissingGet,false},
    {"missing-chain",Mode::MissingChain},
    {"missing-link",Mode::MissingLink},
    {"null-info",Mode::NullInfo},
    {"null-output",Mode::NullOutput},
    {"registry-oom",Mode::RegistryOom,false},
    {"messenger-registry-oom",Mode::MessengerRegistryOom,true,true},
    {"destroy-resolution-oom",Mode::DestroyResolutionOom,false},
    {"destroy-resolution-null",Mode::DestroyResolutionNull,false},
    {"post-create-oom",Mode::PostCreateOom,false},
    {"post-create-throw",Mode::PostCreateThrow,false},
    {"messenger-failure",Mode::MessengerFailure,true,true},
    {"messenger-lookup-throw",Mode::MessengerLookupThrow,true,true},
    {"missing-messenger-destroy",Mode::MissingMessengerDestroy,true,true},
    {"missing-destroy",Mode::MissingDestroy,false},
    {"destroy-lookup-throw",Mode::DestroyLookupThrow,true,true},
    {"extent-refused",Mode::ExtentRefused},
    {"extent-refused-log",Mode::ExtentRefused,true,false,"ETERNAL_RENDER_EXTENT"}
};
const Scenario* scenario{};
unsigned nativeCalls{},xrCalls{},liveInstances{},liveMessengers{},destroyCalls{},messengerCalls{},logsThrown{},destroyLookups{};
bool destroying{};
int dispatchTag{},poisonTag{};
struct DispatchHandle {void* dispatch;};
DispatchHandle instanceHandle{&dispatchTag},poisonHandle{&poisonTag};
const VkInstance instance=reinterpret_cast<VkInstance>(&instanceHandle);
const VkInstance poison=reinterpret_cast<VkInstance>(&poisonHandle);
const VkDebugUtilsMessengerEXT messenger=reinterpret_cast<VkDebugUtilsMessengerEXT>(std::uintptr_t{42});
const VkDebugUtilsMessengerEXT poisonMessenger=reinterpret_cast<VkDebugUtilsMessengerEXT>(std::uintptr_t{43});
const VkAllocationCallbacks* expectedAllocator{};
const VkInstanceCreateInfo* expectedInfo{};
std::recursive_mutex stateMutex;
template<class H> void* key(H h){return h?*reinterpret_cast<void**>(h):nullptr;}
struct Instance {VkInstance handle{};PFN_vkGetInstanceProcAddr gipa{};PFN_GetPhysicalDeviceProcAddr physicalProc{};PFN_vkCreateDevice createDevice{};bool game{},runtimeAuxiliary{};VkDebugUtilsMessengerEXT addressMessenger{};PFN_vkDestroyInstance destroy{};PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger{};};
std::unordered_map<void*,Instance> instances;
std::atomic<uint32_t> instanceCreateDepth{};
struct InstanceCreateScope {
    bool nested{};
    InstanceCreateScope():nested(instanceCreateDepth.fetch_add(1,std::memory_order_acq_rel)!=0){}
    ~InstanceCreateScope(){instanceCreateDepth.fetch_sub(1,std::memory_order_acq_rel);}
};
Instance instanceOf(void* k){std::lock_guard<std::recursive_mutex> lock(stateMutex);const auto found=instances.find(k);return found==instances.end()?Instance{}:found->second;}
bool gameProcess(){return scenario->game;}
VkExtent2D requestedEyeExtent(){return {1280,1280};}
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class T> T* chain(const void* p,VkStructureType type,VkLayerFunction function){
    while(p){auto n=static_cast<const VkBaseInStructure*>(p);if(n->sType==type){auto c=(T*)p;if(c->function==function)return c;}p=n->pNext;}return nullptr;
}
std::vector<const char*> extensions(uint32_t count,const char*const* names,const std::vector<std::string>& extra){
    std::vector<const char*> all;for(uint32_t i=0;i<count;++i)all.push_back(names[i]);
    for(const auto& e:extra)if(std::none_of(all.begin(),all.end(),[&](auto n){return e==n;}))all.push_back(e.c_str());return all;
}
VKAPI_ATTR VkResult VKAPI_CALL fakeCreateDevice(VkPhysicalDevice,const VkDeviceCreateInfo*,const VkAllocationCallbacks*,VkDevice*){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL fakeCreate(VkInstanceCreateInfo const* info,const VkAllocationCallbacks* allocator,VkInstance* out){
    ++nativeCalls;
    require(allocator==expectedAllocator,"Instance allocator changed");
    const bool auxiliary=scenario->mode==Mode::Auxiliary||scenario->mode==Mode::NestedAuxiliary;
    if(auxiliary)require(info==expectedInfo,"Auxiliary create was not passed through");
    else require(info->enabledExtensionCount==unsigned(scenario->game?scenario->debug?3:2:1),"Instance extensions changed");
    if(scenario->mode==Mode::NativeThrow)throw std::runtime_error("native create");
    if(scenario->mode==Mode::NativeFailure){*out=poison;return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
    if(scenario->mode==Mode::NullSuccess){*out=VK_NULL_HANDLE;return VK_SUCCESS;}
    ++liveInstances;*out=instance;
    failNextAllocation=scenario->mode==Mode::RegistryOom;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL fakeDestroy(VkInstance value,const VkAllocationCallbacks* allocator){
    require(value==instance&&allocator==expectedAllocator&&liveInstances==1&&liveMessengers==0,"Unsafe instance destruction");
    --liveInstances;++destroyCalls;
}
VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerate(const char*,uint32_t* count,VkExtensionProperties* props){
    if(props)std::strcpy(props[0].extensionName,VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    *count=1;return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL fakeMessenger(VkInstance value,const VkDebugUtilsMessengerCreateInfoEXT* info,const VkAllocationCallbacks* allocator,VkDebugUtilsMessengerEXT* out){
    ++messengerCalls;
    require(value==instance&&allocator==nullptr&&info->messageType==VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT,"Messenger creation contract changed");
    if(scenario->mode==Mode::MessengerFailure){*out=poisonMessenger;return VK_ERROR_OUT_OF_HOST_MEMORY;}
    ++liveMessengers;*out=messenger;return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL fakeDestroyMessenger(VkInstance value,VkDebugUtilsMessengerEXT handle,const VkAllocationCallbacks* allocator){
    require(value==instance&&handle==messenger&&allocator==nullptr&&liveMessengers==1,"Failed or unowned messenger was destroyed");
    --liveMessengers;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGet(VkInstance value,const char* name){
    if(destroying&&scenario->mode==Mode::DestroyLookupThrow)throw std::bad_alloc{};
    if(!std::strcmp(name,"vkCreateInstance"))return scenario->mode==Mode::MissingCreate?nullptr:reinterpret_cast<PFN_vkVoidFunction>(fakeCreate);
    if(!std::strcmp(name,"vkEnumerateInstanceExtensionProperties"))return reinterpret_cast<PFN_vkVoidFunction>(fakeEnumerate);
    require(value==instance,"Post-create dispatch used a failed instance");
    if(!std::strcmp(name,"vkDestroyInstance")){
        ++destroyLookups;
        if(destroyLookups==1&&scenario->mode==Mode::DestroyResolutionOom)throw std::bad_alloc{};
        if(destroyLookups==1&&scenario->mode==Mode::DestroyResolutionNull)return nullptr;
        return scenario->mode==Mode::MissingDestroy?nullptr:reinterpret_cast<PFN_vkVoidFunction>(fakeDestroy);
    }
    if(!std::strcmp(name,"vkCreateDevice")){
        if(scenario->mode==Mode::PostCreateOom)throw std::bad_alloc{};
        if(scenario->mode==Mode::PostCreateThrow)throw std::runtime_error("post-create dispatch");
        return reinterpret_cast<PFN_vkVoidFunction>(fakeCreateDevice);
    }
    if(!std::strcmp(name,"vkCreateDebugUtilsMessengerEXT")){
        if(scenario->mode==Mode::MessengerLookupThrow)throw std::runtime_error("messenger lookup");
        return reinterpret_cast<PFN_vkVoidFunction>(fakeMessenger);
    }
    if(!std::strcmp(name,"vkDestroyDebugUtilsMessengerEXT"))return scenario->mode==Mode::MissingMessengerDestroy?nullptr:reinterpret_cast<PFN_vkVoidFunction>(fakeDestroyMessenger);
    return nullptr;
}
}
namespace argent {
void installStartupCrashTrace(){}
void log(const std::string& value){
    if(scenario->throwingLog&&value.rfind(scenario->throwingLog,0)==0){++logsThrown;throw std::bad_alloc{};}
    if(scenario->mode==Mode::MessengerRegistryOom&&value.rfind("GPU_ADDRESS_MESSENGER",0)==0)failNextAllocation=true;
}
namespace sfs {
bool vrEnabled(){return scenario->mode==Mode::ExtentRefused;}
bool sourceRingRequested(){return vrEnabled();}
bool nativeProbeEnabled(){return false;}
}
namespace camera {bool installRenderExtent(uint32_t,uint32_t){return false;}}
bool initializeXR(){return true;}
std::vector<std::string> xrExtensions(bool device){
    require(!device,"Wrong extension type");
    if(scenario->mode==Mode::ExtensionOom)throw std::bad_alloc{};
    if(scenario->mode==Mode::ExtensionThrow)throw std::runtime_error("extensions");
    return {"VK_KHR_win32_surface"};
}
bool xrCreateGameInstance(PFN_vkGetInstanceProcAddr get,const VkInstanceCreateInfo* info,const VkAllocationCallbacks* allocator,VkInstance* out,VkResult& result){
    ++xrCalls;require(get==fakeGet,"Wrong XR instance dispatch");
    if(scenario->mode!=Mode::XrHandled)return false;
    result=fakeCreate(info,allocator,out);return true;
}
VKAPI_ATTR VkBool32 VKAPI_CALL gpuAddressCallback(VkDebugUtilsMessageSeverityFlagBitsEXT,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT*,void*){return VK_FALSE;}
}
extern "C" {
#include "../src/LayerInstanceCreation.inc"
}
namespace {
VkResult expectedResult(Mode value){
    switch(value){
    case Mode::Success:case Mode::Auxiliary:case Mode::NestedAuxiliary:case Mode::XrHandled:
    case Mode::MessengerFailure:case Mode::MissingMessengerDestroy:case Mode::DestroyLookupThrow:return VK_SUCCESS;
    case Mode::NativeFailure:return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    case Mode::ExtensionOom:case Mode::RegistryOom:case Mode::PostCreateOom:
    case Mode::MessengerRegistryOom:case Mode::DestroyResolutionOom:return VK_ERROR_OUT_OF_HOST_MEMORY;
    default:return VK_ERROR_INITIALIZATION_FAILED;
    }
}
void runScenario(std::size_t index){
    scenario=&scenarios[index];
    SetEnvironmentVariableW(L"XR_RUNTIME_JSON",L"C:/test/steamvr-runtime.json");
    SetEnvironmentVariableA("ARGENT_GPU_DIAGNOSTICS",scenario->debug?"1":"0");
    const uint32_t initialDepth=scenario->mode==Mode::NestedAuxiliary?1:0;instanceCreateDepth=initialDepth;
    VkLayerInstanceLink link{};link.pfnNextGetInstanceProcAddr=scenario->mode==Mode::MissingGet?nullptr:fakeGet;
    VkLayerInstanceCreateInfo loader{VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO};loader.function=VK_LAYER_LINK_INFO;
    loader.u.pLayerInfo=scenario->mode==Mode::MissingLink?nullptr:&link;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName=scenario->mode==Mode::Auxiliary?"steamvr_vrclient_interop":"Eternal";
    const char* enabled[]{"VK_KHR_surface"};
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pNext=scenario->mode==Mode::MissingChain?nullptr:&loader;
    info.pApplicationInfo=&app;info.enabledExtensionCount=1;info.ppEnabledExtensionNames=enabled;
    VkAllocationCallbacks allocator{};expectedAllocator=&allocator;expectedInfo=&info;
    VkInstance output=poison;
    const auto result=vkCreateInstance(scenario->mode==Mode::NullInfo?nullptr:&info,&allocator,scenario->mode==Mode::NullOutput?nullptr:&output);
    require(!failNextAllocation&&instanceCreateDepth==initialDepth,"Creation scope or allocation injection survived");
    require(result==expectedResult(scenario->mode),"Creation returned the wrong result");
    require(logsThrown==unsigned(scenario->throwingLog!=nullptr),"Throwing diagnostic was not reached");
    if(result==VK_SUCCESS){
        require(output==instance&&instances.size()==1&&liveInstances==1,"Successful instance was not registered");
        const auto state=instances.at(key(output));
        const bool auxiliary=scenario->mode==Mode::Auxiliary||scenario->mode==Mode::NestedAuxiliary;
        require(state.game==(scenario->game&&!auxiliary)&&state.runtimeAuxiliary==auxiliary,"Routing state was not preserved");
        const bool ownsMessenger=scenario->debug&&scenario->mode!=Mode::MessengerFailure&&scenario->mode!=Mode::MissingMessengerDestroy;
        require(liveMessengers==unsigned(ownsMessenger)&&state.addressMessenger==(ownsMessenger?messenger:VK_NULL_HANDLE),"Failed messenger output became owned");
        destroying=true;vkDestroyInstance(output,&allocator);
        require(destroyCalls==1&&liveInstances==0&&liveMessengers==0&&instances.empty(),"Destruction did not retire all ownership");
    }else{
        if(scenario->mode!=Mode::NullOutput)require(output==VK_NULL_HANDLE,"Failed create returned a stale output");
        require(instances.empty()&&liveInstances==0&&liveMessengers==0,"Failed registration leaked native resources");
    }
    if(scenario->mode==Mode::RegistryOom||scenario->mode==Mode::MessengerRegistryOom)
        require(allocationFailures==1&&destroyCalls==1,"Registry OOM did not roll back native creation");
    if(scenario->mode==Mode::MessengerRegistryOom)require(messengerCalls==1,"Messenger ownership was not exercised before OOM");
    if(scenario->mode==Mode::DestroyResolutionOom||scenario->mode==Mode::DestroyResolutionNull)
        require(destroyLookups==2&&destroyCalls==1,"Failed cleanup resolution did not recover ownership");
    if(scenario->mode==Mode::MissingMessengerDestroy)require(messengerCalls==0,"Messenger was created without a destruction procedure");
    if(scenario->mode==Mode::XrHandled)require(xrCalls==1&&nativeCalls==1,"XR-handled instance was recreated downstream");
}
bool runChild(const wchar_t* executable,std::size_t index){
    std::wstring command=L"\""+std::wstring(executable)+L"\" --scenario "+std::to_wstring(index);
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))throw std::runtime_error("CreateProcessW");
    const auto wait=WaitForSingleObject(process.hProcess,10000);
    if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,INFINITE);}
    DWORD code{};const bool read=GetExitCodeProcess(process.hProcess,&code)!=FALSE;
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    const DWORD expected=scenarios[index].mode==Mode::MissingDestroy?0xc0000602u:0u;
    const bool passed=wait==WAIT_OBJECT_0&&read&&code==expected;
    std::cout<<scenarios[index].name<<" exit=0x"<<std::hex<<code<<std::dec<<(passed?" passed":" FAILED")<<'\n';
    return passed;
}
}
int wmain(int argc,wchar_t** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    try{
        if(argc==3&&std::wcscmp(argv[1],L"--scenario")==0){
            const auto index=std::wcstoul(argv[2],nullptr,10);require(index<std::size(scenarios),"Invalid scenario");
            runScenario(index);return 0;
        }
        unsigned failures{};
        for(std::size_t i=0;i<std::size(scenarios);++i)if(!runChild(argv[0],i))++failures;
        std::cout<<std::size(scenarios)<<" instance scenarios; "<<failures<<" failures\n";
        return failures?1:0;
    }catch(const std::exception& error){
        std::cerr<<error.what()<<" native="<<nativeCalls<<" liveInstances="<<liveInstances<<" liveMessengers="<<liveMessengers<<'\n';
        return 1;
    }
}
