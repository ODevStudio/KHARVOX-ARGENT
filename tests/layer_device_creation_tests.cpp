#include <windows.h>
#include "../src/QuadRuntime.h"
#include "../src/openxr/RuntimeVulkanDispatch.h"
#include "../src/sfs/DeviceCapabilities.h"
#include "../src/sfs/EternalProfile.h"
#include "../src/GpuDiagnosticsConfig.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <exception>
#include <iostream>
#include <new>
#include <stdexcept>
#include <unordered_map>

namespace {
thread_local unsigned allocationCountdown{};
unsigned allocationFailures{};
}
void* operator new(std::size_t size){
    if(allocationCountdown&&!--allocationCountdown){++allocationFailures;throw std::bad_alloc{};}
    if(auto* value=std::malloc(size?size:1))return value;
    throw std::bad_alloc{};
}
void operator delete(void* value) noexcept {std::free(value);}
void operator delete(void* value,std::size_t) noexcept {std::free(value);}

namespace {
enum class Mode {
    Success,Auxiliary,AuxiliaryFallback,XrHandled,Deferred,NativeFailure,NullSuccess,
    NativeThrow,MissingChain,MissingLink,MissingGet,MissingGdpa,MissingInstance,
    MissingGipa,MissingCreate,MissingFamilies,MissingMemory,MissingProperties,
    MissingFeatures,UnsupportedViews,NullInfo,NullOutput,ExtensionOom,StateOom,
    MutexOom,FamilyOom,FamilyThrow,ProfileOom,SfsFailed,SfsThrow,ConfigurationOom,
    QueueOom,InstrumentationThrow,RegistryOom,AuxiliaryRegistryOom,HookRegistryOom,
    PartialHookRegistryOom,BindThrow,DestroyResolutionOom,DestroyResolutionNull,
    MissingDestroy,DestroyLookupThrow,DestroySfsThrow
};
struct Scenario {
    const char* name;
    Mode mode{Mode::Success};
    bool game{true},probe{true},vr{true},debug{};
    const char* throwingLog{};
};
constexpr Scenario scenarios[]{
    {"non-game",Mode::Success,false,false,false},
    {"game-no-sfs",Mode::Success,true,false,false},
    {"sfs-mono",Mode::Success,true,true,false},
    {"sfs-vr"},
    {"sfs-diagnostics",Mode::Success,true,true,true,true},
    {"auxiliary",Mode::Auxiliary},
    {"auxiliary-fallback",Mode::AuxiliaryFallback},
    {"xr-handled",Mode::XrHandled},
    {"deferred",Mode::Deferred},
    {"capability-log",Mode::Success,true,true,true,false,"SFS_GPU device="},
    {"diagnostics-log",Mode::Success,true,true,true,false,"GPU_DIAGNOSTICS_REQUESTED"},
    {"checkpoint-log",Mode::Success,true,true,true,true,"GPU_CHECKPOINT_EXTENSION"},
    {"fault-log",Mode::Success,true,true,true,true,"GPU_FAULT_EXTENSION"},
    {"nv-log",Mode::Success,true,true,true,true,"GPU_NV_DIAGNOSTICS"},
    {"water-log",Mode::Success,true,true,true,false,"WATER_ROBUSTNESS_FEATURE"},
    {"address-log",Mode::Success,true,true,true,false,"GPU_ADDRESS_REPORT"},
    {"material-log",Mode::Success,true,true,true,false,"GPU_MATERIAL_FEATURES"},
    {"active-log",Mode::Success,true,true,true,false,"SFS_VR active"},
    {"result-log",Mode::Success,true,true,true,false,"vkCreateDevice result="},
    {"auxiliary-log",Mode::Auxiliary,true,true,true,false,"SteamVR auxiliary"},
    {"deferred-log",Mode::Deferred,true,true,true,false,"XR_VULKAN_DEVICE deferred"},
    {"native-failure",Mode::NativeFailure},
    {"native-failure-log",Mode::NativeFailure,true,true,true,false,"vkCreateDevice result="},
    {"null-success",Mode::NullSuccess},
    {"native-throw",Mode::NativeThrow},
    {"missing-chain",Mode::MissingChain},
    {"missing-link",Mode::MissingLink},
    {"missing-get",Mode::MissingGet},
    {"missing-gdpa",Mode::MissingGdpa},
    {"missing-instance",Mode::MissingInstance},
    {"missing-gipa",Mode::MissingGipa},
    {"missing-create",Mode::MissingCreate},
    {"missing-families",Mode::MissingFamilies},
    {"missing-memory",Mode::MissingMemory},
    {"missing-properties",Mode::MissingProperties},
    {"missing-features",Mode::MissingFeatures},
    {"missing-features-log",Mode::MissingFeatures,true,true,true,false,"SFS_INIT_REFUSED"},
    {"unsupported-views",Mode::UnsupportedViews},
    {"unsupported-views-log",Mode::UnsupportedViews,true,true,true,false,"SFS_INIT_REFUSED"},
    {"null-info",Mode::NullInfo},
    {"null-output",Mode::NullOutput},
    {"extension-oom",Mode::ExtensionOom},
    {"state-oom",Mode::StateOom},
    {"mutex-oom",Mode::MutexOom},
    {"family-oom",Mode::FamilyOom},
    {"family-throw",Mode::FamilyThrow},
    {"profile-oom",Mode::ProfileOom},
    {"sfs-failed",Mode::SfsFailed},
    {"sfs-throw",Mode::SfsThrow},
    {"configuration-oom",Mode::ConfigurationOom},
    {"queue-oom",Mode::QueueOom},
    {"instrumentation-throw",Mode::InstrumentationThrow,true,true,true,true},
    {"registry-oom",Mode::RegistryOom,false,false,false},
    {"auxiliary-registry-oom",Mode::AuxiliaryRegistryOom},
    {"hook-registry-oom",Mode::HookRegistryOom},
    {"partial-hook-registry-oom",Mode::PartialHookRegistryOom},
    {"bind-throw",Mode::BindThrow},
    {"destroy-resolution-oom",Mode::DestroyResolutionOom},
    {"destroy-resolution-null",Mode::DestroyResolutionNull},
    {"missing-destroy",Mode::MissingDestroy},
    {"destroy-lookup-throw",Mode::DestroyLookupThrow},
    {"destroy-sfs-throw",Mode::DestroySfsThrow}
};
const Scenario* scenario{};
unsigned nativeCalls{},xrCalls{},liveDevices{},liveSfs{},destroyCalls{},shutdownCalls{},
    bindCalls{},xrShutdownCalls{},hookAttempts{},hookStops{},logsThrown{},destroyLookups{};
bool destroying{},hookActive{},bound{},checkingRollback{};
int physicalTag{},deviceTag{},poisonTag{};
struct DispatchHandle {void* dispatch;};
DispatchHandle instanceHandle{&physicalTag},physicalHandle{&physicalTag},deviceHandle{&deviceTag},poisonHandle{&poisonTag};
const VkInstance instance=reinterpret_cast<VkInstance>(&instanceHandle);
const VkPhysicalDevice physical=reinterpret_cast<VkPhysicalDevice>(&physicalHandle);
const VkDevice device=reinterpret_cast<VkDevice>(&deviceHandle),poison=reinterpret_cast<VkDevice>(&poisonHandle);
const VkAllocationCallbacks* expectedAllocator{};
const VkDeviceCreateInfo* expectedInfo{};
std::recursive_mutex stateMutex;
template<class H> void* key(H h){return h?*reinterpret_cast<void**>(h):nullptr;}
struct Instance {VkInstance handle{};PFN_vkGetInstanceProcAddr gipa{};PFN_vkCreateDevice createDevice{};bool game{},runtimeAuxiliary{};VkDebugUtilsMessengerEXT addressMessenger{};};
struct Mirror {void destroy(argent::Device&){throw std::runtime_error("unexpected mirror");}};
struct State : argent::Device {
    bool game{},runtimeAuxiliary{},sfs{};
    PFN_vkDestroyDevice destroy{};
    std::vector<uint32_t> queueFamilies;
    std::vector<VkQueueFamilyProperties> families;
    std::unordered_map<VkSwapchainKHR,std::unique_ptr<Mirror>> mirrors;
};
Instance instanceState;
std::unordered_map<void*,std::shared_ptr<State>> devices;
std::atomic<uint64_t> deviceGeneration{1};
Instance instanceOf(void* k){return k==key(physical)?instanceState:Instance{};}
std::shared_ptr<State> deviceOf(void* k){const auto found=devices.find(k);return found==devices.end()?nullptr:found->second;}
bool gameProcess(){return scenario->game;}
VkExtent2D requestedEyeExtent(){return {1280,1280};}
bool auxiliary(){return scenario->mode==Mode::Auxiliary||scenario->mode==Mode::AuxiliaryFallback||scenario->mode==Mode::AuxiliaryRegistryOom;}
void require(bool condition,const char* message){
    if(condition)return;
    if(scenario&&(scenario->mode==Mode::MissingDestroy||scenario->mode==Mode::DestroySfsThrow))ExitProcess(0x56);
    throw std::runtime_error(message);
}
template<class T> T* chain(const void* p,VkStructureType type,VkLayerFunction function){
    while(p){const auto n=static_cast<const VkBaseInStructure*>(p);if(n->sType==type){auto c=(T*)p;if(c->function==function)return c;}p=n->pNext;}return nullptr;
}
std::vector<const char*> extensions(uint32_t count,const char*const* names,const std::vector<std::string>& extra){
    std::vector<const char*> all;for(uint32_t i=0;i<count;++i)all.push_back(names[i]);
    for(const auto& e:extra)if(std::none_of(all.begin(),all.end(),[&](auto n){return e==n;}))all.push_back(e.c_str());return all;
}
bool hasExtension(const VkDeviceCreateInfo& info,const char* name){
    return std::any_of(info.ppEnabledExtensionNames,info.ppEnabledExtensionNames+info.enabledExtensionCount,[&](const auto value){return !std::strcmp(value,name);});
}
VKAPI_ATTR VkResult VKAPI_CALL fakeLoaderData(VkDevice,void*){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL fakeCreate(VkPhysicalDevice value,const VkDeviceCreateInfo* info,const VkAllocationCallbacks* allocator,VkDevice* out){
    ++nativeCalls;
    require(value==physical&&allocator==expectedAllocator,"Native creation contract changed");
    require(info->queueCreateInfoCount==1&&info->pQueueCreateInfos==expectedInfo->pQueueCreateInfos,"Queue configuration changed");
    if(auxiliary())require(info==expectedInfo,"Auxiliary creation was not passed through");
    else{
        require(hasExtension(*info,VK_KHR_SWAPCHAIN_EXTENSION_NAME),"Application extension lost");
        const bool needsXr=scenario->game&&(!scenario->probe||scenario->vr);
        require(hasExtension(*info,VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME)==needsXr,"XR extensions changed");
        const auto views=argent::sfs::multiviewRequest(info->pNext);
        require(views.present==bool(scenario->game&&scenario->probe)&&!views.duplicate&&views.enabled,"Multiview negotiation changed");
    }
    const auto loader=chain<VkLayerDeviceCreateInfo>(info->pNext,VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,VK_LAYER_LINK_INFO);
    const auto callback=chain<VkLayerDeviceCreateInfo>(info->pNext,VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,VK_LOADER_DATA_CALLBACK);
    require(loader&&!loader->u.pLayerInfo&&callback&&callback->u.pfnSetDeviceLoaderData==fakeLoaderData,"Loader chain contract changed");
    if(scenario->mode==Mode::NativeThrow)throw std::runtime_error("native create");
    if(scenario->mode==Mode::NativeFailure){*out=poison;return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
    if(scenario->mode==Mode::NullSuccess){*out=VK_NULL_HANDLE;return VK_SUCCESS;}
    ++liveDevices;*out=device;
    if(scenario->mode==Mode::StateOom)allocationCountdown=1;
    if(scenario->mode==Mode::MutexOom)allocationCountdown=2;
    if(scenario->mode==Mode::AuxiliaryRegistryOom)allocationCountdown=3;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL fakeDestroy(VkDevice value,const VkAllocationCallbacks* allocator){
    if(value!=device||allocator!=expectedAllocator||liveDevices!=1||liveSfs||hookActive||!devices.empty())ExitProcess(0x56);
    --liveDevices;++destroyCalls;
}
VKAPI_ATTR void VKAPI_CALL fakeFamilies(VkPhysicalDevice value,uint32_t* count,VkQueueFamilyProperties* properties){
    require(value==physical,"Queue query handle changed");
    if(!properties){
        if(scenario->mode==Mode::FamilyThrow)throw std::runtime_error("families");
        if(scenario->mode==Mode::FamilyOom)allocationCountdown=1;
    }else{
        properties[0].queueFlags=VK_QUEUE_GRAPHICS_BIT;properties[0].timestampValidBits=64;
        if(scenario->mode==Mode::RegistryOom)allocationCountdown=1;
    }
    *count=1;
}
VKAPI_ATTR void VKAPI_CALL fakeMemory(VkPhysicalDevice value,VkPhysicalDeviceMemoryProperties* properties){
    require(value==physical,"Memory query handle changed");
    properties->memoryTypeCount=1;
    if(scenario->mode==Mode::ProfileOom)allocationCountdown=1;
}
void fillProperties(VkPhysicalDeviceProperties& properties){
    properties.apiVersion=VK_API_VERSION_1_2;properties.limits.maxImageArrayLayers=2048;
    properties.limits.maxImageDimension2D=16384;properties.limits.timestampPeriod=1;
    std::strcpy(properties.deviceName,"fixture GPU");
}
VKAPI_ATTR void VKAPI_CALL fakeProperties(VkPhysicalDevice value,VkPhysicalDeviceProperties* properties){require(value==physical,"Properties handle changed");fillProperties(*properties);}
VKAPI_ATTR void VKAPI_CALL fakeProperties2(VkPhysicalDevice value,VkPhysicalDeviceProperties2* properties){
    require(value==physical,"Properties2 handle changed");fillProperties(properties->properties);
    for(auto node=static_cast<VkBaseOutStructure*>(properties->pNext);node;node=node->pNext)
        if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES)reinterpret_cast<VkPhysicalDeviceMultiviewProperties*>(node)->maxMultiviewViewCount=2;
}
VKAPI_ATTR void VKAPI_CALL fakeFeatures(VkPhysicalDevice value,VkPhysicalDeviceFeatures2* features){
    require(value==physical,"Features handle changed");
    for(auto node=static_cast<VkBaseOutStructure*>(features->pNext);node;node=node->pNext){
        if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES)reinterpret_cast<VkPhysicalDeviceMultiviewFeatures*>(node)->multiview=scenario->mode==Mode::UnsupportedViews?VK_FALSE:VK_TRUE;
        if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT)reinterpret_cast<VkPhysicalDeviceFaultFeaturesEXT*>(node)->deviceFault=VK_TRUE;
        if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV)reinterpret_cast<VkPhysicalDeviceDiagnosticsConfigFeaturesNV*>(node)->diagnosticsConfig=VK_TRUE;
    }
}
VKAPI_ATTR VkResult VKAPI_CALL fakeEnumerate(VkPhysicalDevice,const char*,uint32_t* count,VkExtensionProperties* properties){
    constexpr const char* names[]{VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME,VK_EXT_DEVICE_FAULT_EXTENSION_NAME,VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME};
    *count=unsigned(std::size(names));if(properties)for(std::size_t i=0;i<std::size(names);++i)std::strcpy(properties[i].extensionName,names[i]);
    return VK_SUCCESS;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGet(VkInstance value,const char* name){
    require(value==instance,"Instance dispatch handle changed");
    if(!std::strcmp(name,"vkCreateDevice"))return scenario->mode==Mode::MissingCreate?nullptr:reinterpret_cast<PFN_vkVoidFunction>(fakeCreate);
    if(!std::strcmp(name,"vkGetPhysicalDeviceQueueFamilyProperties"))return scenario->mode==Mode::MissingFamilies?nullptr:reinterpret_cast<PFN_vkVoidFunction>(fakeFamilies);
    if(!std::strcmp(name,"vkGetPhysicalDeviceMemoryProperties"))return scenario->mode==Mode::MissingMemory?nullptr:reinterpret_cast<PFN_vkVoidFunction>(fakeMemory);
    if(!std::strcmp(name,"vkGetPhysicalDeviceProperties"))return scenario->mode==Mode::MissingProperties?nullptr:reinterpret_cast<PFN_vkVoidFunction>(fakeProperties);
    if(!std::strcmp(name,"vkGetPhysicalDeviceProperties2")||!std::strcmp(name,"vkGetPhysicalDeviceProperties2KHR"))return reinterpret_cast<PFN_vkVoidFunction>(fakeProperties2);
    if(!std::strcmp(name,"vkGetPhysicalDeviceFeatures2")||!std::strcmp(name,"vkGetPhysicalDeviceFeatures2KHR"))return scenario->mode==Mode::MissingFeatures?nullptr:reinterpret_cast<PFN_vkVoidFunction>(fakeFeatures);
    if(!std::strcmp(name,"vkEnumerateDeviceExtensionProperties"))return reinterpret_cast<PFN_vkVoidFunction>(fakeEnumerate);
    return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeGdpa(VkDevice value,const char* name){
    require(value==device&&liveDevices==1,"Device dispatch used an unowned handle");
    if(destroying&&scenario->mode==Mode::DestroyLookupThrow)throw std::bad_alloc{};
    if(!std::strcmp(name,"vkDestroyDevice")){
        ++destroyLookups;
        if(destroyLookups==1&&scenario->mode==Mode::DestroyResolutionOom)throw std::bad_alloc{};
        if(destroyLookups==1&&scenario->mode==Mode::DestroyResolutionNull)return nullptr;
        return scenario->mode==Mode::MissingDestroy?nullptr:reinterpret_cast<PFN_vkVoidFunction>(fakeDestroy);
    }
    return nullptr;
}
}
namespace argent {
void log(const std::string& value){if(scenario->throwingLog&&value.rfind(scenario->throwingLog,0)==0){++logsThrown;throw std::bad_alloc{};}}
bool extendedLogging(){return true;}
bool gpuCrashDiagnostics(){return scenario->debug;}
std::vector<std::string> xrExtensions(bool forDevice){
    require(forDevice,"Wrong extension type");
    if(scenario->mode==Mode::ExtensionOom)throw std::bad_alloc{};
    return {VK_KHR_SWAPCHAIN_EXTENSION_NAME,VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME};
}
bool xrCreateGameDevice(PFN_vkGetInstanceProcAddr get,PFN_vkGetDeviceProcAddr gdpa,VkInstance i,VkPhysicalDevice p,const VkDeviceCreateInfo* runtimeInfo,const VkDeviceCreateInfo* downstreamInfo,const VkAllocationCallbacks* a,VkDevice* out,VkResult& result){
    ++xrCalls;require(get==fakeGet&&gdpa==fakeGdpa&&i==instance&&p==physical,"XR dispatch changed");
    require(runtimeInfo&&downstreamInfo&&runtimeInfo->enabledExtensionCount==downstreamInfo->enabledExtensionCount,"XR creation infos changed");
    if(scenario->mode!=Mode::XrHandled)return false;
    result=fakeCreate(p,downstreamInfo,a,out);return true;
}
void xrBindGameDevice(VkDevice value,PFN_vkGetDeviceProcAddr gdpa){
    ++bindCalls;require(value==device&&gdpa==fakeGdpa&&devices.size()==1,"Binding before registration");
    if(scenario->mode==Mode::BindThrow)throw std::runtime_error("bind lock");
    bound=true;
}
void shutdownXR(VkDevice value){++xrShutdownCalls;require(value==device&&liveDevices==1,"XR shutdown lost device");bound=false;}
namespace camera {
bool install() noexcept {
    ++hookAttempts;hookActive=true;
    if(scenario->mode==Mode::HookRegistryOom||scenario->mode==Mode::PartialHookRegistryOom)allocationCountdown=1;
    return scenario->mode!=Mode::PartialHookRegistryOom;
}
void stop() noexcept {++hookStops;hookActive=false;}
}
namespace dlss {bool install() noexcept {return true;}}
namespace capture {void forgetDevice(VkDevice) noexcept {}}
namespace sfs {
bool nativeProbeEnabled(){return scenario->probe;}
bool vrEnabled(){return scenario->vr;}
bool initialize(VkDevice value,VkPhysicalDevice p,PFN_vkGetDeviceProcAddr gdpa,const VkPhysicalDeviceMemoryProperties& memory,const Configuration& config){
    require(value==device&&p==physical&&gdpa==fakeGdpa&&memory.memoryTypeCount==1,"SFS initialization changed");
    require(config.imageComputeStereo==scenario->vr,"SFS profile changed");
    if(scenario->mode==Mode::SfsThrow)throw std::runtime_error("sfs setup");
    if(scenario->mode==Mode::SfsFailed)return false;
    ++liveSfs;return true;
}
void configurePerformanceGpu(VkDevice value,float period,uint32_t count,const VkQueueFamilyProperties* families){
    require(value==device&&liveSfs==1&&period==1&&count==1&&families->timestampValidBits==64,"SFS performance configuration changed");
    if(scenario->mode==Mode::ConfigurationOom)throw std::bad_alloc{};
    if(scenario->mode==Mode::QueueOom)allocationCountdown=1;
}
void enableGpuCheckpoints(VkDevice,PFN_vkGetDeviceProcAddr){if(scenario->mode==Mode::InstrumentationThrow)throw std::runtime_error("instrumentation");}
void enableGpuFaultReport(VkDevice,PFN_vkGetDeviceProcAddr){}
void enableWaterRobustness(VkDevice){}
void shutdown(VkDevice value){
    require(value==device&&liveSfs==1&&!hookActive,"SFS retired before hooks stopped");
    if(checkingRollback)require(devices.empty(),"Rollback did not unpublish the device");
    if(scenario->mode==Mode::DestroySfsThrow)throw std::runtime_error("unsafe sfs retirement");
    --liveSfs;++shutdownCalls;
}
}
}
extern "C" {
#include "../src/LayerDeviceCreation.inc"
}
namespace {
VkResult expectedResult(Mode mode){
    switch(mode){
    case Mode::Success:case Mode::Auxiliary:case Mode::AuxiliaryFallback:case Mode::XrHandled:case Mode::Deferred:
    case Mode::DestroyLookupThrow:case Mode::DestroySfsThrow:return VK_SUCCESS;
    case Mode::NativeFailure:return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    case Mode::MissingFeatures:case Mode::UnsupportedViews:return VK_ERROR_FEATURE_NOT_PRESENT;
    case Mode::ExtensionOom:case Mode::StateOom:case Mode::MutexOom:case Mode::FamilyOom:case Mode::ProfileOom:
    case Mode::ConfigurationOom:case Mode::QueueOom:case Mode::RegistryOom:case Mode::AuxiliaryRegistryOom:
    case Mode::HookRegistryOom:case Mode::PartialHookRegistryOom:case Mode::DestroyResolutionOom:return VK_ERROR_OUT_OF_HOST_MEMORY;
    default:return VK_ERROR_INITIALIZATION_FAILED;
    }
}
void runScenario(std::size_t index){
    scenario=&scenarios[index];
    SetEnvironmentVariableA("ARGENT_GPU_DIAGNOSTICS",scenario->debug?"1":"0");
    SetEnvironmentVariableA("ARGENT_STEAMVR_DEFER_XR_DEVICE",scenario->mode==Mode::Deferred?"1":"0");
    instanceState.handle=scenario->mode==Mode::MissingInstance?VK_NULL_HANDLE:instance;
    instanceState.gipa=scenario->mode==Mode::MissingGipa?nullptr:fakeGet;
    instanceState.createDevice=fakeCreate;instanceState.game=scenario->game;instanceState.runtimeAuxiliary=auxiliary();
    VkLayerDeviceLink link{};link.pfnNextGetInstanceProcAddr=scenario->mode==Mode::MissingGet||scenario->mode==Mode::AuxiliaryFallback?nullptr:fakeGet;
    link.pfnNextGetDeviceProcAddr=scenario->mode==Mode::MissingGdpa?nullptr:fakeGdpa;
    VkLayerDeviceCreateInfo callback{VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO};callback.function=VK_LOADER_DATA_CALLBACK;callback.u.pfnSetDeviceLoaderData=fakeLoaderData;
    VkLayerDeviceCreateInfo loader{VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO};loader.function=VK_LAYER_LINK_INFO;loader.pNext=&callback;loader.u.pLayerInfo=scenario->mode==Mode::MissingLink?nullptr:&link;
    const char* extensions[]{VK_KHR_SWAPCHAIN_EXTENSION_NAME};const float priority=1;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue.queueCount=1;queue.pQueuePriorities=&priority;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};info.pNext=scenario->mode==Mode::MissingChain?nullptr:&loader;
    info.enabledExtensionCount=1;info.ppEnabledExtensionNames=extensions;info.queueCreateInfoCount=1;info.pQueueCreateInfos=&queue;
    VkAllocationCallbacks allocator{};expectedAllocator=&allocator;expectedInfo=&info;
    VkDevice output=poison;checkingRollback=expectedResult(scenario->mode)!=VK_SUCCESS;
    const auto result=vkCreateDevice(physical,scenario->mode==Mode::NullInfo?nullptr:&info,&allocator,scenario->mode==Mode::NullOutput?nullptr:&output);
    require(!allocationCountdown,"Allocation injection was not exercised");
    require(result==expectedResult(scenario->mode),"Creation returned the wrong result");
    require(logsThrown==unsigned(scenario->throwingLog!=nullptr),"Throwing diagnostic was not reached");
    if(result==VK_SUCCESS){
        require(output==device&&devices.size()==1&&liveDevices==1,"Successful device was not registered");
        const auto s=devices.at(key(device));
        const bool ownsSfs=scenario->game&&scenario->probe&&!auxiliary();
        require(s->device==device&&s->physical==physical&&s->instance==instance&&s->gipa==fakeGet&&s->gdpa==fakeGdpa&&s->setLoaderData==fakeLoaderData,"Device state changed");
        require(s->game==(scenario->game&&!auxiliary())&&s->runtimeAuxiliary==auxiliary()&&s->sfs==ownsSfs,"Device routing changed");
        require(liveSfs==unsigned(ownsSfs)&&bound==!auxiliary(),"Successful subsystem ownership lost");
        require(s->queueFamilies.size()==unsigned(ownsSfs)&&s->families.size()==unsigned(!auxiliary()),"Queue state lost");
        destroying=true;vkDestroyDevice(output,&allocator);
        require(!liveDevices&&!liveSfs&&!hookActive&&devices.empty()&&destroyCalls==1,"Device destruction leaked resources");
        require(shutdownCalls==unsigned(ownsSfs)&&xrShutdownCalls==unsigned(scenario->game&&!auxiliary()),"Teardown routing changed");
        if(scenario->mode==Mode::DestroyLookupThrow)require(destroyLookups==1,"Destruction resolved dispatch again");
    }else{
        if(scenario->mode!=Mode::NullOutput)require(output==VK_NULL_HANDLE,"Failed create returned a stale output");
        require(devices.empty()&&!liveDevices&&!liveSfs&&!hookActive&&!bound,"Failed setup leaked ownership");
        if(scenario->mode==Mode::BindThrow)require(deviceGeneration==3&&bindCalls==1,"Published rollback did not invalidate caches");
    }
    if(scenario->mode==Mode::HookRegistryOom||scenario->mode==Mode::PartialHookRegistryOom)
        require(allocationFailures==1&&hookAttempts==1&&hookStops==1&&shutdownCalls==1,"Partial hook/SFS rollback was not exercised");
    if(scenario->mode==Mode::ConfigurationOom||scenario->mode==Mode::QueueOom||scenario->mode==Mode::InstrumentationThrow)
        require(shutdownCalls==1&&destroyCalls==1,"Published SFS state was not retired");
    if(scenario->mode==Mode::DestroyResolutionOom||scenario->mode==Mode::DestroyResolutionNull)
        require(destroyLookups==2&&destroyCalls==1,"Cleanup dispatch resolution did not recover");
    if(scenario->mode==Mode::XrHandled)require(xrCalls==1&&nativeCalls==1,"XR-handled device was recreated downstream");
    if(auxiliary())require(xrCalls==0&&hookAttempts==0&&bindCalls==0,"Auxiliary device used game setup");
}
bool runChild(const wchar_t* executable,std::size_t index){
    std::wstring command=L"\""+std::wstring(executable)+L"\" --scenario "+std::to_wstring(index);
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))throw std::runtime_error("CreateProcessW");
    const auto wait=WaitForSingleObject(process.hProcess,10000);
    if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,INFINITE);}
    DWORD code{};const bool read=GetExitCodeProcess(process.hProcess,&code)!=FALSE;
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    const auto mode=scenarios[index].mode;const DWORD expected=mode==Mode::MissingDestroy||mode==Mode::DestroySfsThrow?0xc0000602u:0u;
    const bool passed=wait==WAIT_OBJECT_0&&read&&code==expected;
    std::cout<<scenarios[index].name<<" exit=0x"<<std::hex<<code<<std::dec<<(passed?" passed":" FAILED")<<'\n';return passed;
}
}
int wmain(int argc,wchar_t** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    try{
        if(argc==3&&std::wcscmp(argv[1],L"--scenario")==0){const auto index=std::wcstoul(argv[2],nullptr,10);require(index<std::size(scenarios),"Invalid scenario");runScenario(index);return 0;}
        unsigned failures{};for(std::size_t i=0;i<std::size(scenarios);++i)if(!runChild(argv[0],i))++failures;
        std::cout<<std::size(scenarios)<<" device scenarios; "<<failures<<" failures\n";return failures?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<" native="<<nativeCalls<<" liveDevices="<<liveDevices<<" liveSfs="<<liveSfs<<'\n';return 1;}
}
