#include "../src/QuadRuntime.h"
#include "../src/XrWorker.h"
#include "../src/openxr/OpenXRRuntimePolicy.h"
#include "../src/openxr/RuntimeDeviceCreateChain.h"
#include "../src/openxr/RuntimeVulkanDispatch.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
thread_local bool failNextAllocation{};
unsigned allocationFailures{};
}
void* operator new(std::size_t size) {
    if(failNextAllocation){failNextAllocation=false;++allocationFailures;throw std::bad_alloc{};}
    if(auto* memory=std::malloc(size?size:1))return memory;
    throw std::bad_alloc{};
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory,std::size_t) noexcept { std::free(memory); }

namespace argent {
namespace {
enum class Mode { Success, XrFailure, VkFailure, NullSuccess, ThrowNative, SelectionFailure,
    UuidMismatch, MissingProperties, MissingLoader, Simulator, KhrProperties, ChainAllocationFailure };
Mode mode{};
const char* throwingPrefix="";
bool throwLog{},failed{},enable2{},simulator{};
unsigned instanceCalls{},deviceCalls{},nativeCalls{},createdInstances{},createdDevices{},propertiesCalls{},logsThrown{};
DWORD callbackThread{};
XrInstance instance{};
XrSystemId system=1;
VkPhysicalDevice runtimeSelectedPhysical{};
kharvox::OpenXRRuntimeKind runtimeKind{};
std::recursive_mutex mutex;
std::atomic<PFN_vkGetInstanceProcAddr> runtimeNext{};
std::atomic<PFN_vkGetDeviceProcAddr> runtimeGdpa{};
std::atomic<VkDevice> runtimeDevice{};
std::atomic<PFN_vkCreateDevice> runtimeCreate{};
std::atomic<VkPhysicalDevice> runtimePhysical{};
std::atomic<const VkDeviceCreateInfo*> runtimeDownstream{};
std::atomic<bool> runtimeSessionRoute{};
const VkDeviceCreateInfo* expectedDownstream{};
const VkAllocationCallbacks* expectedAllocator{};
const VkInstanceCreateInfo* expectedInstanceInfo{};
const VkDeviceCreateInfo* expectedRuntimeInfo{};
bool addedRuntimeFeatures{};
unsigned preservedChains{};
void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
template<class T> T handle(std::uintptr_t value) { return reinterpret_cast<T>(value); }
void check(XrResult result,const char* operation) { if(XR_FAILED(result))throw std::runtime_error(operation); }
bool success(Mode value) { return value==Mode::Success||value==Mode::Simulator||value==Mode::KhrProperties; }
VKAPI_ATTR void VKAPI_CALL fakeProperties(VkPhysicalDevice gpu,VkPhysicalDeviceProperties2* properties) {
    ++propertiesCalls;
    require(gpu==handle<VkPhysicalDevice>(2)||gpu==handle<VkPhysicalDevice>(4),"Wrong GPU dispatch level");
    auto* id=reinterpret_cast<VkPhysicalDeviceIDProperties*>(properties->pNext);
    id->deviceUUID[0]=gpu==handle<VkPhysicalDevice>(2)&&mode==Mode::UuidMismatch?8:7;
}
VKAPI_ATTR VkResult VKAPI_CALL fakeCreateDevice(VkPhysicalDevice physical,const VkDeviceCreateInfo* info,const VkAllocationCallbacks* allocator,VkDevice* out) {
    ++nativeCalls;callbackThread=GetCurrentThreadId();
    require(physical==handle<VkPhysicalDevice>(4)&&allocator==expectedAllocator,"Adapter lost the downstream GPU or allocator");
    if(addedRuntimeFeatures){
        const auto* original=static_cast<const VkLayerDeviceCreateInfo*>(expectedDownstream->pNext);
        const auto* originalLink=static_cast<const VkLayerDeviceCreateInfo*>(original->pNext);
        const auto* callback=static_cast<const VkLayerDeviceCreateInfo*>(info->pNext);
        require(callback&&callback!=original&&callback->sType==VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO
            &&callback->function==VK_LOADER_DATA_CALLBACK&&callback->u.pfnSetDeviceLoaderData==original->u.pfnSetDeviceLoaderData,
            "Adapter lost copied loader callback ownership");
        auto* link=static_cast<const VkLayerDeviceCreateInfo*>(callback->pNext);
        require(link&&link!=originalLink&&link->sType==VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO
            &&link->function==VK_LAYER_LINK_INFO&&link->u.pLayerInfo==originalLink->u.pLayerInfo
            &&link->pNext==expectedRuntimeInfo->pNext,"Adapter discarded runtime-added features or loader links");
        const auto* timeline=static_cast<const VkPhysicalDeviceTimelineSemaphoreFeatures*>(link->pNext);
        require(timeline->timelineSemaphore==VK_TRUE&&timeline->pNext==originalLink->pNext
            &&info->enabledExtensionCount==expectedRuntimeInfo->enabledExtensionCount
            &&info->ppEnabledExtensionNames==expectedRuntimeInfo->ppEnabledExtensionNames
            &&info->queueCreateInfoCount==expectedRuntimeInfo->queueCreateInfoCount
            &&info->pQueueCreateInfos==expectedRuntimeInfo->pQueueCreateInfos,"Runtime features, extensions or queues changed");
        const auto* appFeatures=static_cast<const VkPhysicalDeviceFeatures2*>(timeline->pNext);
        const auto* views=static_cast<const VkPhysicalDeviceMultiviewFeatures*>(appFeatures->pNext);
        require(appFeatures->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2&&views
            &&views->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES&&views->multiview==VK_TRUE,
            "Runtime merge lost application multiview features");
        ++preservedChains;
        const_cast<VkLayerDeviceCreateInfo*>(link)->u.pLayerInfo=nullptr;
        const_cast<VkLayerDeviceCreateInfo*>(callback)->pNext=nullptr;
        auto* features=reinterpret_cast<VkBaseOutStructure*>(const_cast<void*>(originalLink->pNext));
        features->pNext=nullptr;
    }else require(info->pNext==expectedDownstream->pNext,"Adapter lost the feature-only chain");
    if(mode==Mode::VkFailure){*out=VK_NULL_HANDLE;return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
    if(mode==Mode::NullSuccess){*out=VK_NULL_HANDLE;return VK_SUCCESS;}
    ++createdDevices;*out=handle<VkDevice>(50);return VK_SUCCESS;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeNext(VkInstance value,const char* name) {
    require(value==handle<VkInstance>(3),"Wrong instance dispatch level");
    if(!std::strcmp(name,"vkCreateDevice"))return reinterpret_cast<PFN_vkVoidFunction>(fakeCreateDevice);
    if(!std::strcmp(name,"vkGetPhysicalDeviceProperties2")||!std::strcmp(name,"vkGetPhysicalDeviceProperties2KHR")){
        if(mode==Mode::MissingProperties||(mode==Mode::KhrProperties&&!std::strcmp(name,"vkGetPhysicalDeviceProperties2")))return nullptr;
        return reinterpret_cast<PFN_vkVoidFunction>(fakeProperties);
    }
    return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeDeviceProc(VkDevice,const char*) { return nullptr; }
PFN_vkGetInstanceProcAddr publicGipa() { return mode==Mode::MissingLoader||mode==Mode::Simulator?nullptr:fakeNext; }
}
void log(const std::string& message) {
    if(throwLog&&message.rfind(throwingPrefix,0)==0){++logsThrown;throw std::bad_alloc{};}
}
namespace {
#include "../src/RuntimeVulkanCallbacks.inc"
XrResult fake_xrCreateVulkanInstanceKHR(XrInstance value,const XrVulkanInstanceCreateInfoKHR* info,VkInstance* out,VkResult* result) {
    ++instanceCalls;
    require(value==instance&&info->systemId==system&&info->pfnGetInstanceProcAddr==fakeNext
        &&info->vulkanCreateInfo==expectedInstanceInfo&&info->vulkanAllocator==expectedAllocator,"Instance creation contract changed");
    if(mode==Mode::ThrowNative)throw std::runtime_error("native creation threw");
    if(mode==Mode::XrFailure){*out=VK_NULL_HANDLE;*result=VK_SUCCESS;return XR_ERROR_RUNTIME_FAILURE;}
    if(mode==Mode::VkFailure){*out=VK_NULL_HANDLE;*result=VK_ERROR_OUT_OF_DEVICE_MEMORY;return XR_SUCCESS;}
    if(mode==Mode::NullSuccess){*out=VK_NULL_HANDLE;*result=VK_SUCCESS;return XR_SUCCESS;}
    ++createdInstances;*out=handle<VkInstance>(40);*result=VK_SUCCESS;return XR_SUCCESS;
}
XrResult fake_xrGetVulkanGraphicsDevice2KHR(XrInstance,const XrVulkanGraphicsDeviceGetInfoKHR* info,VkPhysicalDevice* out) {
    require(info->vulkanInstance==handle<VkInstance>(3)&&info->systemId==system,"Physical-device selection contract changed");
    if(mode==Mode::SelectionFailure)return XR_ERROR_RUNTIME_FAILURE;
    *out=handle<VkPhysicalDevice>(2);return XR_SUCCESS;
}
XrResult fake_xrCreateVulkanDeviceKHR(XrInstance,const XrVulkanDeviceCreateInfoKHR* info,VkDevice* out,VkResult* result) {
    ++deviceCalls;
    require(propertiesCalls==2&&info->vulkanPhysicalDevice==handle<VkPhysicalDevice>(2)
        &&info->vulkanAllocator==expectedAllocator&&runtimeDownstream.load()==expectedDownstream
        &&runtimePhysical.load()==handle<VkPhysicalDevice>(4)&&!runtimeSessionRoute.load(),"UUID or callback ownership was bypassed");
    auto* head=reinterpret_cast<VkBaseOutStructure*>(const_cast<void*>(expectedDownstream->pNext));
    if(!addedRuntimeFeatures)head->pNext=nullptr;
    if(mode==Mode::ThrowNative)throw std::runtime_error("native creation threw");
    if(mode==Mode::XrFailure){*out=VK_NULL_HANDLE;*result=VK_SUCCESS;return XR_ERROR_RUNTIME_FAILURE;}
    auto runtime=*info->vulkanCreateInfo;
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    timeline.timelineSemaphore=VK_TRUE;timeline.pNext=const_cast<void*>(runtime.pNext);
    const char* extensions[]{"VK_KHR_swapchain","VK_KHR_timeline_semaphore"};
    float priority=1;VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue.queueCount=1;queue.pQueuePriorities=&priority;
    if(addedRuntimeFeatures){runtime.pNext=&timeline;runtime.enabledExtensionCount=2;runtime.ppEnabledExtensionNames=extensions;
        runtime.queueCreateInfoCount=1;runtime.pQueueCreateInfos=&queue;expectedRuntimeInfo=&runtime;}
    *result=xrWorker().invoke([&]{
        const auto create=reinterpret_cast<PFN_vkCreateDevice>(info->pfnGetInstanceProcAddr(handle<VkInstance>(3),"vkCreateDevice"));
        require(create==runtimeCreateAdapter,"Runtime did not receive the armed adapter");
        failNextAllocation=mode==Mode::ChainAllocationFailure;
        return create(info->vulkanPhysicalDevice,&runtime,info->vulkanAllocator,out);
    });
    return XR_SUCCESS;
}
}
#define XR(name) fake_##name
#include "../src/XrVulkanCreation.inc"
#undef XR
namespace {
void reset(Mode value,const char* prefix,bool steam=false) {
    mode=value;throwingPrefix=prefix?prefix:"";throwLog=prefix!=nullptr;
    failed=false;enable2=true;simulator=value==Mode::Simulator;
    instance=handle<XrInstance>(1);runtimeSelectedPhysical=VK_NULL_HANDLE;
    runtimeKind=steam?kharvox::OpenXRRuntimeKind::SteamVR:kharvox::OpenXRRuntimeKind::Unknown;
    runtimeNext=nullptr;runtimeGdpa=nullptr;runtimeDevice=VK_NULL_HANDLE;runtimeCreate=nullptr;
    runtimePhysical=VK_NULL_HANDLE;runtimeDownstream=nullptr;runtimeSessionRoute=false;
    instanceCalls=deviceCalls=nativeCalls=createdInstances=createdDevices=propertiesCalls=logsThrown=0;callbackThread=0;
    addedRuntimeFeatures=false;preservedChains=allocationFailures=0;expectedRuntimeInfo=nullptr;
}
VkResult expectedResult(Mode value) { return success(value)?VK_SUCCESS:value==Mode::VkFailure?VK_ERROR_OUT_OF_DEVICE_MEMORY:
    value==Mode::ChainAllocationFailure?VK_ERROR_OUT_OF_HOST_MEMORY:VK_ERROR_INITIALIZATION_FAILED; }
void runInstance(Mode value,const char* prefix) {
    reset(value,prefix);
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};VkAllocationCallbacks allocator{};
    expectedInstanceInfo=&info;expectedAllocator=&allocator;
    VkInstance output=handle<VkInstance>(90);VkResult result=VK_SUCCESS;
    bool handled{},escaped{};
    try { handled=xrCreateGameInstance(fakeNext,&info,&allocator,&output,result); }
    catch(...) { escaped=true; }
    require(!escaped&&handled&&result==expectedResult(value),"Instance result or diagnostic escaped its boundary");
    require(output==(success(value)?handle<VkInstance>(40):VK_NULL_HANDLE)&&createdInstances==unsigned(success(value))
        &&instanceCalls==1,"Successful instance ownership was lost or failed output survived");
    std::cout<<"instance mode="<<int(value)<<" log="<<(prefix?prefix:"none")<<" result="<<result<<" owned="<<createdInstances<<'\n';
}
void runDevice(Mode value,const char* prefix,bool steam,bool runtimeFeatures=false) {
    reset(value,prefix,steam);
    addedRuntimeFeatures=runtimeFeatures;
    VkPhysicalDeviceFeatures2 head{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceMultiviewFeatures tail{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};tail.multiview=VK_TRUE;head.pNext=&tail;
    VkDeviceCreateInfo downstream{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};downstream.pNext=&head;
    VkLayerDeviceLink loaderLink{};
    VkLayerDeviceCreateInfo link{VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO};link.function=VK_LAYER_LINK_INFO;link.u.pLayerInfo=&loaderLink;link.pNext=&head;
    VkLayerDeviceCreateInfo callback{VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO};callback.function=VK_LOADER_DATA_CALLBACK;callback.pNext=&link;
    callback.u.pfnSetDeviceLoaderData=+[](VkDevice,void*)->VkResult{return VK_SUCCESS;};
    if(runtimeFeatures)downstream.pNext=&callback;
    const auto runtime=downstream;VkAllocationCallbacks allocator{};
    auto runtimeWithoutLoader=runtime;if(runtimeFeatures)runtimeWithoutLoader.pNext=&head;
    expectedDownstream=&downstream;expectedAllocator=&allocator;
    VkDevice output=handle<VkDevice>(90);VkResult result=VK_SUCCESS;
    bool handled{},escaped{};
    try { handled=xrCreateGameDevice(fakeNext,fakeDeviceProc,handle<VkInstance>(3),handle<VkPhysicalDevice>(4),&runtimeWithoutLoader,&downstream,&allocator,&output,result); }
    catch(...) { escaped=true; }
    require(!escaped&&handled&&result==expectedResult(value),"Device result or diagnostic escaped its boundary");
    require(output==(success(value)?handle<VkDevice>(50):VK_NULL_HANDLE)&&createdDevices==unsigned(success(value)),
        "Successful device ownership was lost or failed output survived");
    require(!runtimeCreate.load()&&!runtimePhysical.load()&&!runtimeDownstream.load()&&head.pNext==&tail,
        "Temporary callback pointers or mutated loader links survived creation");
    require(runtimeSelectedPhysical==(success(value)?handle<VkPhysicalDevice>(2):VK_NULL_HANDLE)
        &&runtimeSessionRoute.load()==(success(value)&&steam),"Failed device published session routing");
    if(success(value))require(nativeCalls==1&&callbackThread!=GetCurrentThreadId(),"Creation did not cross the production worker callback");
    if(runtimeFeatures){
        require(callback.pNext==&link&&link.pNext==&head&&link.u.pLayerInfo==&loaderLink,
            "Native creation mutated the original loader records");
        require(preservedChains==unsigned(value!=Mode::ChainAllocationFailure)
            &&allocationFailures==unsigned(value==Mode::ChainAllocationFailure),"Runtime chain preservation or allocation rejection was not exercised");
        if(value==Mode::ChainAllocationFailure)require(!nativeCalls,"Allocation failure reached native device creation");
    }
    if(value==Mode::SelectionFailure||value==Mode::UuidMismatch||value==Mode::MissingProperties||value==Mode::MissingLoader)
        require(!deviceCalls&&!nativeCalls&&!createdDevices,"Unavailable UUID verification created a device");
    std::cout<<"device mode="<<int(value)<<" steam="<<steam<<" log="<<(prefix?prefix:"none")
        <<" result="<<result<<" owned="<<createdDevices<<" logsThrown="<<logsThrown<<" runtimeFeatures="<<runtimeFeatures
        <<" preserved="<<preservedChains<<" allocationFailures="<<allocationFailures<<'\n';
}
void runBinding(bool diagnostic) {
    reset(Mode::Success,diagnostic?"[RUNTIME-ENABLE2] retained":nullptr);
    bool escaped{};
    try { xrBindGameDevice(handle<VkDevice>(50),fakeDeviceProc); }catch(...) { escaped=true; }
    require(!escaped&&runtimeDevice.load()==handle<VkDevice>(50)&&runtimeGdpa.load()==fakeDeviceProc,
        "Binding diagnostics escaped successful creation");
    std::cout<<"binding diagnostic="<<diagnostic<<" retained=1\n";
}
void runLookup(bool nullName) {
    reset(Mode::Success,"[RUNTIME-ENABLE2] GIPA");runtimeNext=fakeNext;
    PFN_vkVoidFunction resolved{};bool escaped{};
    try { resolved=runtimeGipa(handle<VkInstance>(3),nullName?nullptr:"vkCreateDevice"); }catch(...) { escaped=true; }
    require(!escaped&&resolved==(nullName?nullptr:reinterpret_cast<PFN_vkVoidFunction>(fakeCreateDevice))&&logsThrown==1,
        "Callback diagnostic changed its resolved procedure");
    std::cout<<"lookup nullName="<<nullName<<" logsThrown="<<logsThrown<<'\n';
}
void runGates(unsigned gate) {
    reset(Mode::Success,nullptr);
    if(gate==0)instance=XR_NULL_HANDLE;
    else if(gate==1)failed=true;
    else enable2=false;
    VkInstance instanceOutput=handle<VkInstance>(90);VkDevice deviceOutput=handle<VkDevice>(90);
    VkResult result=VK_ERROR_FEATURE_NOT_PRESENT;
    require(!xrCreateGameInstance(fakeNext,nullptr,nullptr,&instanceOutput,result)
        &&!xrCreateGameDevice(fakeNext,fakeDeviceProc,VK_NULL_HANDLE,VK_NULL_HANDLE,nullptr,nullptr,nullptr,&deviceOutput,result),
        "Disabled mediation attempted runtime creation");
    require(instanceOutput==handle<VkInstance>(90)&&deviceOutput==handle<VkDevice>(90)
        &&result==VK_ERROR_FEATURE_NOT_PRESENT&&!instanceCalls&&!deviceCalls,"Passthrough mediation changed native output");
    std::cout<<"gate="<<gate<<" untouched=1\n";
}
}
}

int main() {
    unsigned scenarios{},failures{};
    const auto run=[&](const auto& operation){
        ++scenarios;
        try { operation(); }catch(const std::exception& error){++failures;std::cerr<<"case="<<scenarios<<": "<<error.what()<<'\n';}
    };
    for(const auto mode:{argent::Mode::Success,argent::Mode::XrFailure,argent::Mode::VkFailure,argent::Mode::NullSuccess,argent::Mode::ThrowNative})
        for(const auto prefix:{static_cast<const char*>(nullptr),"XR_VULKAN_INSTANCE "})run([&]{argent::runInstance(mode,prefix);});
    run([&]{argent::runInstance(argent::Mode::ThrowNative,"native creation threw");});
    for(const bool steam:{false,true}){
        for(const auto mode:{argent::Mode::Success,argent::Mode::XrFailure,argent::Mode::VkFailure,argent::Mode::NullSuccess,
            argent::Mode::ThrowNative,argent::Mode::SelectionFailure,argent::Mode::UuidMismatch,
            argent::Mode::MissingProperties,argent::Mode::MissingLoader,argent::Mode::Simulator,argent::Mode::KhrProperties})
            run([&]{argent::runDevice(mode,nullptr,steam);});
        for(const auto prefix:{"XR_VULKAN_DEVICE ","xrCreateVulkanDeviceKHR cross-thread", "[RUNTIME-ENABLE2] GIPA", "[RUNTIME-ENABLE2] vkCreateDevice adapter"})
            run([&]{argent::runDevice(argent::Mode::Success,prefix,steam);});
        for(const auto mode:{argent::Mode::XrFailure,argent::Mode::VkFailure,argent::Mode::NullSuccess})
            run([&]{argent::runDevice(mode,"XR_VULKAN_DEVICE ",steam);});
        run([&]{argent::runDevice(argent::Mode::ThrowNative,"native creation threw",steam);});
        run([&]{argent::runDevice(argent::Mode::UuidMismatch,"XR GPU UUID",steam);});
        for(const auto mode:{argent::Mode::Success,argent::Mode::VkFailure,argent::Mode::NullSuccess,argent::Mode::ChainAllocationFailure})
            run([&]{argent::runDevice(mode,nullptr,steam,true);});
        run([&]{argent::runDevice(argent::Mode::Success,"[RUNTIME-ENABLE2] vkCreateDevice adapter",steam,true);});
    }
    for(const bool diagnostic:{false,true})run([&]{argent::runBinding(diagnostic);});
    for(const bool nullName:{false,true})run([&]{argent::runLookup(nullName);});
    for(unsigned gate=0;gate<3;++gate)run([&]{argent::runGates(gate);});
    std::cout<<scenarios<<" scenarios, "<<failures<<" failures\n";
    return failures?1:0;
}
