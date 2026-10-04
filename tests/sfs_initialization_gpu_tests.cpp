#include <windows.h>
#include "../src/sfs/NativeSfs.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
thread_local bool failNextAllocation{};
unsigned allocationFailures{};
}
void* operator new(std::size_t size){
    if(failNextAllocation){failNextAllocation=false;++allocationFailures;throw std::bad_alloc{};}
    if(auto* value=std::malloc(size?size:1))return value;
    throw std::bad_alloc{};
}
void operator delete(void* value) noexcept {std::free(value);}
void operator delete(void* value,std::size_t) noexcept {std::free(value);}

namespace {
enum class Mode {Normal,LogOnce,LogAlways,CaptureLog,AllocationFailure,BindFailure,MapFailure,MemoryTypeFailure,QueryFailure,RegistryOom,StateOom,ResolverFailure,Disabled};
constexpr const char* modes[]{"normal","log-once","log-always","capture-log","allocation-failure","bind-failure","map-failure","memory-type-failure","query-failure","registry-oom","state-oom","resolver-failure","disabled"};
Mode mode{};
unsigned logsThrown{},maps{},unmaps{},bufferCreates{},bufferDestroys{},memoryAllocations{},memoryFrees{};
unsigned descriptorLayouts{},pipelineLayouts{},pipelines{},shaders{};
VkBuffer parameterBuffer{};
VkDeviceMemory parameterMemory{};
void* parameterData{};
bool invalidCleanup{},failureLoggedBeforeCleanup{};
VkDevice device{};
PFN_vkGetDeviceProcAddr driver{};
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void ok(VkResult result){require(result==VK_SUCCESS,"Native Vulkan setup failed");}
template<class T>T procedure(const char* name){return reinterpret_cast<T>(driver(device,name));}
bool successful(Mode value){return value==Mode::Normal||value==Mode::LogOnce||value==Mode::LogAlways||value==Mode::CaptureLog||value==Mode::Disabled;}
bool empty(){return !parameterBuffer&&!parameterMemory&&!parameterData&&!descriptorLayouts&&!pipelineLayouts&&!pipelines&&!shaders;}
bool registered(){const auto next=driver(device,"vkCreateShaderModule");return argent::sfs::wrapProc(device,"vkCreateShaderModule",next)!=next;}
VKAPI_ATTR VkResult VKAPI_CALL createBuffer(VkDevice d,const VkBufferCreateInfo* info,const VkAllocationCallbacks* allocator,VkBuffer* out){
    require(!parameterBuffer,"A prior parameter buffer is still owned");
    const auto result=procedure<PFN_vkCreateBuffer>("vkCreateBuffer")(d,info,allocator,out);
    if(result==VK_SUCCESS){parameterBuffer=*out;++bufferCreates;}
    return result;
}
VKAPI_ATTR void VKAPI_CALL destroyBuffer(VkDevice d,VkBuffer value,const VkAllocationCallbacks* allocator){
    if(!value||value!=parameterBuffer){invalidCleanup=true;return;}
    procedure<PFN_vkDestroyBuffer>("vkDestroyBuffer")(d,value,allocator);parameterBuffer=VK_NULL_HANDLE;++bufferDestroys;
}
VKAPI_ATTR VkResult VKAPI_CALL allocateMemory(VkDevice d,const VkMemoryAllocateInfo* info,const VkAllocationCallbacks* allocator,VkDeviceMemory* out){
    if(mode==Mode::AllocationFailure){*out=VK_NULL_HANDLE;return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
    require(!parameterMemory,"A prior parameter allocation is still owned");
    const auto result=procedure<PFN_vkAllocateMemory>("vkAllocateMemory")(d,info,allocator,out);
    if(result==VK_SUCCESS){parameterMemory=*out;++memoryAllocations;}
    return result;
}
VKAPI_ATTR void VKAPI_CALL freeMemory(VkDevice d,VkDeviceMemory value,const VkAllocationCallbacks* allocator){
    if(!value||value!=parameterMemory||parameterData){invalidCleanup=true;return;}
    procedure<PFN_vkFreeMemory>("vkFreeMemory")(d,value,allocator);parameterMemory=VK_NULL_HANDLE;++memoryFrees;
}
VKAPI_ATTR VkResult VKAPI_CALL bindMemory(VkDevice d,VkBuffer buffer,VkDeviceMemory memory,VkDeviceSize offset){
    if(mode==Mode::BindFailure)return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    return procedure<PFN_vkBindBufferMemory>("vkBindBufferMemory")(d,buffer,memory,offset);
}
VKAPI_ATTR VkResult VKAPI_CALL mapMemory(VkDevice d,VkDeviceMemory memory,VkDeviceSize offset,VkDeviceSize size,VkMemoryMapFlags flags,void** out){
    if(mode==Mode::MapFailure){*out=nullptr;return VK_ERROR_MEMORY_MAP_FAILED;}
    const auto result=procedure<PFN_vkMapMemory>("vkMapMemory")(d,memory,offset,size,flags,out);
    if(result==VK_SUCCESS){parameterData=*out;++maps;failNextAllocation=mode==Mode::RegistryOom;}
    return result;
}
VKAPI_ATTR void VKAPI_CALL unmapMemory(VkDevice d,VkDeviceMemory memory){
    if(memory!=parameterMemory||!parameterData){invalidCleanup=true;return;}
    procedure<PFN_vkUnmapMemory>("vkUnmapMemory")(d,memory);parameterData=nullptr;++unmaps;
}
VKAPI_ATTR VkResult VKAPI_CALL createDescriptors(VkDevice d,const VkDescriptorSetLayoutCreateInfo* info,const VkAllocationCallbacks* allocator,VkDescriptorSetLayout* out){
    const auto result=procedure<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(d,info,allocator,out);
    if(result==VK_SUCCESS)++descriptorLayouts;return result;
}
VKAPI_ATTR void VKAPI_CALL destroyDescriptors(VkDevice d,VkDescriptorSetLayout value,const VkAllocationCallbacks* allocator){
    if(!descriptorLayouts){invalidCleanup=true;return;}--descriptorLayouts;
    procedure<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(d,value,allocator);
}
VKAPI_ATTR VkResult VKAPI_CALL createLayout(VkDevice d,const VkPipelineLayoutCreateInfo* info,const VkAllocationCallbacks* allocator,VkPipelineLayout* out){
    const auto result=procedure<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(d,info,allocator,out);
    if(result==VK_SUCCESS)++pipelineLayouts;return result;
}
VKAPI_ATTR void VKAPI_CALL destroyLayout(VkDevice d,VkPipelineLayout value,const VkAllocationCallbacks* allocator){
    if(!pipelineLayouts){invalidCleanup=true;return;}--pipelineLayouts;
    procedure<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(d,value,allocator);
}
VKAPI_ATTR VkResult VKAPI_CALL createShader(VkDevice d,const VkShaderModuleCreateInfo* info,const VkAllocationCallbacks* allocator,VkShaderModule* out){
    const auto result=procedure<PFN_vkCreateShaderModule>("vkCreateShaderModule")(d,info,allocator,out);
    if(result==VK_SUCCESS)++shaders;return result;
}
VKAPI_ATTR void VKAPI_CALL destroyShader(VkDevice d,VkShaderModule value,const VkAllocationCallbacks* allocator){
    if(!shaders){invalidCleanup=true;return;}--shaders;
    procedure<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(d,value,allocator);
}
VKAPI_ATTR VkResult VKAPI_CALL createPipelines(VkDevice d,VkPipelineCache cache,uint32_t count,const VkComputePipelineCreateInfo* infos,const VkAllocationCallbacks* allocator,VkPipeline* out){
    if(mode==Mode::QueryFailure){std::fill_n(out,count,VK_NULL_HANDLE);return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
    const auto result=procedure<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(d,cache,count,infos,allocator,out);
    for(uint32_t i=0;i<count;++i)if(out[i])++pipelines;return result;
}
VKAPI_ATTR void VKAPI_CALL destroyPipeline(VkDevice d,VkPipeline value,const VkAllocationCallbacks* allocator){
    if(!pipelines){invalidCleanup=true;return;}--pipelines;
    procedure<PFN_vkDestroyPipeline>("vkDestroyPipeline")(d,value,allocator);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolver(VkDevice d,const char* name){
    if(mode==Mode::ResolverFailure&&!std::strcmp(name,"vkCmdBeginQuery"))throw std::runtime_error("Injected dispatch resolution failure");
#define INTERCEPT(api,fn) if(!std::strcmp(name,#api))return reinterpret_cast<PFN_vkVoidFunction>(fn)
    INTERCEPT(vkCreateBuffer,createBuffer);INTERCEPT(vkDestroyBuffer,destroyBuffer);
    INTERCEPT(vkAllocateMemory,allocateMemory);INTERCEPT(vkFreeMemory,freeMemory);
    INTERCEPT(vkBindBufferMemory,bindMemory);INTERCEPT(vkMapMemory,mapMemory);INTERCEPT(vkUnmapMemory,unmapMemory);
    INTERCEPT(vkCreateDescriptorSetLayout,createDescriptors);INTERCEPT(vkDestroyDescriptorSetLayout,destroyDescriptors);
    INTERCEPT(vkCreatePipelineLayout,createLayout);INTERCEPT(vkDestroyPipelineLayout,destroyLayout);
    INTERCEPT(vkCreateShaderModule,createShader);INTERCEPT(vkDestroyShaderModule,destroyShader);
    INTERCEPT(vkCreateComputePipelines,createPipelines);INTERCEPT(vkDestroyPipeline,destroyPipeline);
#undef INTERCEPT
    return driver(d,name);
}
}
namespace argent {
void log(const std::string& message){
    const bool shouldThrow=mode==Mode::LogOnce?logsThrown==0&&message.rfind("[SFS] native ",0)==0:
        mode==Mode::CaptureLog?message.rfind("[SFS] WATER_GPU_CAPTURE",0)==0:
        mode!=Mode::Normal&&mode!=Mode::Disabled;
    if(shouldThrow){failureLoggedBeforeCleanup|=!successful(mode)&&!empty();++logsThrown;throw std::bad_alloc{};}
}
}
int main(int argc,char** argv){
    try{
        require(argc==2||(argc==3&&!std::strcmp(argv[2],"--mono")),"Expected a scenario and optional --mono");
        const auto chosen=std::find_if(std::begin(modes),std::end(modes),[&](const char* name){return std::strcmp(name,argv[1])==0;});
        require(chosen!=std::end(modes),"Unknown initialization scenario");mode=static_cast<Mode>(chosen-std::begin(modes));
        const auto initialMode=mode;
        SetEnvironmentVariableA("ARGENT_SFS_NATIVE_PROBE",mode==Mode::Disabled?"0":"1");
        SetEnvironmentVariableA("ARGENT_SFS_NATIVE_VR",argc==3?"0":"1");
        SetEnvironmentVariableA("ARGENT_PERFORMANCE_DIAGNOSTICS","0");SetEnvironmentVariableA("ARGENT_SFS_PROFILE_TIMING","0");
        SetEnvironmentVariableW(L"ARGENT_SFS_PROFILE",nullptr);
        SetEnvironmentVariableA("ARGENT_WATER_GPU_CAPTURE",mode==Mode::CaptureLog?"1":"0");
        wchar_t temporary[32768]{};require(GetTempPathW(std::size(temporary),temporary)>0,"No temporary directory");
        SetEnvironmentVariableW(L"ARGENT_CAPTURE_DIRECTORY",temporary);
        const auto loader=LoadLibraryW(L"vulkan-1.dll");require(loader,"No Vulkan loader");
        const auto get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));require(get,"No instance resolver");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};createInfo.pApplicationInfo=&app;
        VkInstance instance{};ok(reinterpret_cast<PFN_vkCreateInstance>(get(nullptr,"vkCreateInstance"))(&createInfo,nullptr,&instance));
#define INSTANCE(api) const auto api=reinterpret_cast<PFN_##api>(get(instance,#api));require(api,#api)
        INSTANCE(vkEnumeratePhysicalDevices);INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);
        INSTANCE(vkGetPhysicalDeviceMemoryProperties);INSTANCE(vkGetPhysicalDeviceProperties);INSTANCE(vkCreateDevice);INSTANCE(vkGetDeviceProcAddr);INSTANCE(vkDestroyInstance);
#undef INSTANCE
        uint32_t count{};ok(vkEnumeratePhysicalDevices(instance,&count,nullptr));require(count,"No physical device");
        std::vector<VkPhysicalDevice> physicals(count);ok(vkEnumeratePhysicalDevices(instance,&count,physicals.data()));const auto physical=physicals.front();
        vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());uint32_t family=UINT32_MAX;
        for(uint32_t i=0;i<count;++i)if(families[i].queueCount&&(families[i].queueFlags&VK_QUEUE_COMPUTE_BIT)){family=i;break;}
        require(family!=UINT32_MAX,"No compute queue");float priority=1;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue.queueFamilyIndex=family;queue.queueCount=1;queue.pQueuePriorities=&priority;
        VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queue;
        ok(vkCreateDevice(physical,&deviceInfo,nullptr,&device));driver=vkGetDeviceProcAddr;
        VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(physical,&memory);const auto validMemory=memory;
        if(mode==Mode::MemoryTypeFailure)for(uint32_t i=0;i<memory.memoryTypeCount;++i)memory.memoryTypes[i].propertyFlags&=~VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(physical,&properties);std::cout<<properties.deviceName<<'\n';
        require(!registered(),"Uninitialized native device was already wrapped");
        const argent::sfs::Configuration configuration{};
        failNextAllocation=mode==Mode::StateOom;
        bool initialized{},escaped{};
        try{initialized=argent::sfs::initialize(device,physical,resolver,memory,configuration);}catch(...){escaped=true;}
        std::cout<<argv[1]<<" initialized="<<initialized<<" escaped="<<escaped<<" registered="<<registered()
            <<" buffer="<<bool(parameterBuffer)<<" memory="<<bool(parameterMemory)<<" mapped="<<bool(parameterData)
            <<" query="<<descriptorLayouts<<'/'<<pipelineLayouts<<'/'<<pipelines<<" logsThrown="<<logsThrown<<'\n';
        require(!escaped&&!failNextAllocation&&initialized==successful(mode),"Initialization result escaped or was changed by diagnostics");
        require(!invalidCleanup&&!failureLoggedBeforeCleanup&&registered()==(initialized&&mode!=Mode::Disabled),"Initialization published failed state or logged before cleanup");
        if(mode==Mode::Disabled)require(empty()&&logsThrown==0,"Disabled probe allocated resources or logged");
        else if(initialized){
            require(parameterBuffer&&parameterMemory&&parameterData&&descriptorLayouts==1&&pipelineLayouts==1&&pipelines==1&&!shaders,"Successful initialization lost native resources");
            if(mode!=Mode::Normal)require(logsThrown==1,"Optional initialization diagnostic was not exercised");
            argent::sfs::shutdown(device);
        }else{
            require(empty()&&logsThrown==1,"Failed setup leaked resources or skipped failure diagnostics");
            if(mode==Mode::RegistryOom||mode==Mode::StateOom)require(allocationFailures==1,"Allocation boundary was not exercised");
            mode=Mode::Normal;
            require(argent::sfs::initialize(device,physical,resolver,validMemory,configuration)&&registered(),"Failed setup prevented a clean retry");
            argent::sfs::shutdown(device);
        }
        require(empty()&&!invalidCleanup&&!registered()&&bufferCreates==bufferDestroys&&memoryAllocations==memoryFrees&&maps==unmaps,"Shutdown or retry did not retire native ownership");
        procedure<PFN_vkDestroyDevice>("vkDestroyDevice")(device,nullptr);vkDestroyInstance(instance,nullptr);FreeLibrary(loader);
        std::cout<<modes[unsigned(initialMode)]<<" clean teardown; buffers="<<bufferCreates<<" maps="<<maps<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
