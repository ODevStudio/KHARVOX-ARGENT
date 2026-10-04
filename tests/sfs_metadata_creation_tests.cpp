#include <windows.h>
#include "../src/sfs/NativeSfs.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
thread_local unsigned allocationCountdown{};
unsigned allocations{},allocationFailures{};
bool measuring{};
}
void* operator new(std::size_t size){
    if(measuring)++allocations;
    if(allocationCountdown&&!--allocationCountdown){++allocationFailures;throw std::bad_alloc{};}
    if(auto* value=std::malloc(size?size:1))return value;
    throw std::bad_alloc{};
}
void operator delete(void* value)noexcept{std::free(value);}
void operator delete(void* value,std::size_t)noexcept{std::free(value);}

namespace {
enum class Kind {Shader,Buffer,Layout,PipelineLayout,Pool,Query};
struct Resource {uintptr_t handle{};Kind kind{};bool live{};};
std::array<Resource,16> resources;
unsigned resourceCount{},invalidDestructions{},postNativeBoundary{};
bool fakeNative{},armMeasurement{},nativeFailure{},nullNative{},logThrows{},captureEnabled{},unknownLayout{},emptyPipeline{};
VkDevice device{};
VkPhysicalDevice physical{};
VkPhysicalDeviceMemoryProperties memory{};
PFN_vkGetDeviceProcAddr driver{};
VkDescriptorSetLayout seedLayout{};
VkAllocationCallbacks allocator{};
VkQueryType queryKind=VK_QUERY_TYPE_TIMESTAMP;
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void ok(VkResult value){require(value==VK_SUCCESS,"Vulkan fixture setup failed");}
template<class T>T handle(uintptr_t value){return reinterpret_cast<T>(value);}
template<class T>T procedure(const char* name){return reinterpret_cast<T>(driver(device,name));}
template<class T>T hook(const char* name){return reinterpret_cast<T>(argent::sfs::wrapProc(device,name,driver(device,name)));}
unsigned live(){unsigned count{};for(unsigned i=0;i<resourceCount;++i)count+=resources[i].live;return count;}
template<class T>VkResult allocate(Kind kind,const VkAllocationCallbacks* a,T* out){
    require(a==&allocator,"Native creation lost the caller allocator");
    if(nativeFailure){*out=handle<T>(0xdead);return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
    if(nullNative){*out=VK_NULL_HANDLE;return VK_SUCCESS;}
    const auto value=0x1000+16*resourceCount;resources[resourceCount++]={value,kind,true};*out=handle<T>(value);
    allocationCountdown=postNativeBoundary;measuring=armMeasurement;return VK_SUCCESS;
}
template<class T>bool dispose(Kind kind,T h,const VkAllocationCallbacks* a){
    const auto value=reinterpret_cast<uintptr_t>(h);
    for(unsigned i=0;i<resourceCount;++i)if(resources[i].handle==value){
        if(resources[i].kind!=kind||!resources[i].live||a!=&allocator)++invalidDestructions;
        else resources[i].live=false;
        return true;
    }
    if(value==0xdead){++invalidDestructions;return true;}
    return false;
}
VKAPI_ATTR VkResult VKAPI_CALL nativeShader(VkDevice d,const VkShaderModuleCreateInfo* i,const VkAllocationCallbacks* a,VkShaderModule* out){
    if(!fakeNative)return procedure<PFN_vkCreateShaderModule>("vkCreateShaderModule")(d,i,a,out);
    require(i->codeSize==20&&i->pCode[0]==0x07230203,"Shader code changed during metadata registration");return allocate(Kind::Shader,a,out);
}
VKAPI_ATTR void VKAPI_CALL destroyShader(VkDevice d,VkShaderModule h,const VkAllocationCallbacks* a){if(!dispose(Kind::Shader,h,a))procedure<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(d,h,a);}
VKAPI_ATTR VkResult VKAPI_CALL nativeBuffer(VkDevice d,const VkBufferCreateInfo* i,const VkAllocationCallbacks* a,VkBuffer* out){
    if(!fakeNative)return procedure<PFN_vkCreateBuffer>("vkCreateBuffer")(d,i,a,out);
    require(i->size==128&&i->usage==(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|(captureEnabled?VK_BUFFER_USAGE_TRANSFER_SRC_BIT:0)),"Capture buffer usage changed");return allocate(Kind::Buffer,a,out);
}
VKAPI_ATTR void VKAPI_CALL destroyBuffer(VkDevice d,VkBuffer h,const VkAllocationCallbacks* a){if(!dispose(Kind::Buffer,h,a))procedure<PFN_vkDestroyBuffer>("vkDestroyBuffer")(d,h,a);}
VKAPI_ATTR VkResult VKAPI_CALL nativeLayout(VkDevice d,const VkDescriptorSetLayoutCreateInfo* i,const VkAllocationCallbacks* a,VkDescriptorSetLayout* out){
    if(!fakeNative)return procedure<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(d,i,a,out);
    require(i->bindingCount==3&&i->pBindings[2].binding==1&&i->pBindings[2].descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,"Projection binding changed");return allocate(Kind::Layout,a,out);
}
VKAPI_ATTR void VKAPI_CALL destroyLayout(VkDevice d,VkDescriptorSetLayout h,const VkAllocationCallbacks* a){if(!dispose(Kind::Layout,h,a))procedure<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(d,h,a);}
VKAPI_ATTR VkResult VKAPI_CALL nativePipelineLayout(VkDevice d,const VkPipelineLayoutCreateInfo* i,const VkAllocationCallbacks* a,VkPipelineLayout* out){
    if(!fakeNative)return procedure<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(d,i,a,out);
    require(i->setLayoutCount==(emptyPipeline?0u:2u),"Pipeline layout set count changed");return allocate(Kind::PipelineLayout,a,out);
}
VKAPI_ATTR void VKAPI_CALL destroyPipelineLayout(VkDevice d,VkPipelineLayout h,const VkAllocationCallbacks* a){if(!dispose(Kind::PipelineLayout,h,a))procedure<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(d,h,a);}
VKAPI_ATTR VkResult VKAPI_CALL nativePool(VkDevice d,const VkDescriptorPoolCreateInfo* i,const VkAllocationCallbacks* a,VkDescriptorPool* out){
    if(!fakeNative)return procedure<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(d,i,a,out);
    require(i->maxSets==4&&i->poolSizeCount==2&&i->pPoolSizes[1].type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER&&i->pPoolSizes[1].descriptorCount==4,"Projection pool reservation changed");return allocate(Kind::Pool,a,out);
}
VKAPI_ATTR void VKAPI_CALL destroyPool(VkDevice d,VkDescriptorPool h,const VkAllocationCallbacks* a){if(!dispose(Kind::Pool,h,a))procedure<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(d,h,a);}
VKAPI_ATTR VkResult VKAPI_CALL nativeQuery(VkDevice d,const VkQueryPoolCreateInfo* i,const VkAllocationCallbacks* a,VkQueryPool* out){
    if(!fakeNative)return procedure<PFN_vkCreateQueryPool>("vkCreateQueryPool")(d,i,a,out);
    require(i->queryType==queryKind&&i->queryCount==(queryKind==VK_QUERY_TYPE_PIPELINE_STATISTICS?3u:6u),"Logical query expansion changed");return allocate(Kind::Query,a,out);
}
VKAPI_ATTR void VKAPI_CALL destroyQuery(VkDevice d,VkQueryPool h,const VkAllocationCallbacks* a){if(!dispose(Kind::Query,h,a))procedure<PFN_vkDestroyQueryPool>("vkDestroyQueryPool")(d,h,a);}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolver(VkDevice d,const char* name){
#define ENTRY(api,fn) if(!std::strcmp(name,#api))return reinterpret_cast<PFN_vkVoidFunction>(fn)
    ENTRY(vkCreateShaderModule,nativeShader);ENTRY(vkDestroyShaderModule,destroyShader);
    ENTRY(vkCreateBuffer,nativeBuffer);ENTRY(vkDestroyBuffer,destroyBuffer);
    ENTRY(vkCreateDescriptorSetLayout,nativeLayout);ENTRY(vkDestroyDescriptorSetLayout,destroyLayout);
    ENTRY(vkCreatePipelineLayout,nativePipelineLayout);ENTRY(vkDestroyPipelineLayout,destroyPipelineLayout);
    ENTRY(vkCreateDescriptorPool,nativePool);ENTRY(vkDestroyDescriptorPool,destroyPool);
    ENTRY(vkCreateQueryPool,nativeQuery);ENTRY(vkDestroyQueryPool,destroyQuery);
#undef ENTRY
    return driver(d,name);
}
struct Outcome {VkResult result{};uintptr_t output=0xbeef;bool escaped{};};
Outcome invoke(Kind kind,bool missingInfo=false,bool missingOutput=false){
    Outcome result;
    try{switch(kind){
    case Kind::Shader:{std::array<uint32_t,5> code{0x07230203,0x10000,0,2,0};VkShaderModuleCreateInfo i{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};i.codeSize=sizeof(code);i.pCode=code.data();VkShaderModule h=handle<VkShaderModule>(0xbeef);result.result=hook<PFN_vkCreateShaderModule>("vkCreateShaderModule")(device,missingInfo?nullptr:&i,&allocator,missingOutput?nullptr:&h);result.output=reinterpret_cast<uintptr_t>(h);break;}
    case Kind::Buffer:{VkBufferCreateInfo i{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};i.size=128;i.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;VkBuffer h=handle<VkBuffer>(0xbeef);result.result=hook<PFN_vkCreateBuffer>("vkCreateBuffer")(device,missingInfo?nullptr:&i,&allocator,missingOutput?nullptr:&h);result.output=reinterpret_cast<uintptr_t>(h);break;}
    case Kind::Layout:{std::array<VkDescriptorSetLayoutBinding,2> bindings{{{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,2,VK_SHADER_STAGE_VERTEX_BIT,nullptr},{3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};VkDescriptorSetLayoutCreateInfo i{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};i.bindingCount=uint32_t(bindings.size());i.pBindings=bindings.data();VkDescriptorSetLayout h=handle<VkDescriptorSetLayout>(0xbeef);result.result=hook<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(device,missingInfo?nullptr:&i,&allocator,missingOutput?nullptr:&h);result.output=reinterpret_cast<uintptr_t>(h);break;}
    case Kind::PipelineLayout:{std::array<VkDescriptorSetLayout,2> layouts{seedLayout,unknownLayout?handle<VkDescriptorSetLayout>(0xface):seedLayout};VkPipelineLayoutCreateInfo i{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};i.setLayoutCount=emptyPipeline?0:uint32_t(layouts.size());i.pSetLayouts=layouts.data();VkPipelineLayout h=handle<VkPipelineLayout>(0xbeef);result.result=hook<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(device,missingInfo?nullptr:&i,&allocator,missingOutput?nullptr:&h);result.output=reinterpret_cast<uintptr_t>(h);break;}
    case Kind::Pool:{VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,8};VkDescriptorPoolCreateInfo i{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};i.maxSets=4;i.poolSizeCount=1;i.pPoolSizes=&size;VkDescriptorPool h=handle<VkDescriptorPool>(0xbeef);result.result=hook<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(device,missingInfo?nullptr:&i,&allocator,missingOutput?nullptr:&h);result.output=reinterpret_cast<uintptr_t>(h);break;}
    case Kind::Query:{VkQueryPoolCreateInfo i{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};i.queryType=queryKind;i.queryCount=3;VkQueryPool h=handle<VkQueryPool>(0xbeef);result.result=hook<PFN_vkCreateQueryPool>("vkCreateQueryPool")(device,missingInfo?nullptr:&i,&allocator,missingOutput?nullptr:&h);result.output=reinterpret_cast<uintptr_t>(h);break;}
    }}catch(...){result.escaped=true;}
    allocationCountdown=0;measuring=false;return result;
}
void destroy(Kind kind,uintptr_t output){
    switch(kind){
    case Kind::Shader:hook<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(device,handle<VkShaderModule>(output),&allocator);break;
    case Kind::Buffer:hook<PFN_vkDestroyBuffer>("vkDestroyBuffer")(device,handle<VkBuffer>(output),&allocator);break;
    case Kind::Layout:hook<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(device,handle<VkDescriptorSetLayout>(output),&allocator);break;
    case Kind::PipelineLayout:hook<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(device,handle<VkPipelineLayout>(output),&allocator);break;
    case Kind::Pool:hook<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(device,handle<VkDescriptorPool>(output),&allocator);break;
    case Kind::Query:hook<PFN_vkDestroyQueryPool>("vkDestroyQueryPool")(device,handle<VkQueryPool>(output),&allocator);break;
    }
}
void start(Kind kind,bool capture){
    allocationCountdown=allocations=allocationFailures=postNativeBoundary=resourceCount=invalidDestructions=0;resources={};
    measuring=fakeNative=armMeasurement=nativeFailure=nullNative=logThrows=unknownLayout=emptyPipeline=false;seedLayout=VK_NULL_HANDLE;
    captureEnabled=capture;SetEnvironmentVariableA("ARGENT_WATER_GPU_CAPTURE",capture?"1":"0");
    require(argent::sfs::initialize(device,physical,resolver,memory,{}),"SFS setup failed before creation injection");fakeNative=true;
    if(kind==Kind::PipelineLayout){const auto seed=invoke(Kind::Layout);require(!seed.escaped&&seed.result==VK_SUCCESS,"Seed layout creation failed");seedLayout=handle<VkDescriptorSetLayout>(seed.output);}
}
void finish(){
    allocationCountdown=postNativeBoundary=0;measuring=armMeasurement=logThrows=false;
    if(seedLayout)destroy(Kind::Layout,reinterpret_cast<uintptr_t>(seedLayout));
    fakeNative=false;argent::sfs::shutdown(device);
}
void clean(){require(live()==unsigned(seedLayout!=VK_NULL_HANDLE)&&!invalidDestructions,"Creation leaked native ownership or destroyed an undefined output");}
void published(Kind kind,const Outcome& result){
    require(!result.escaped&&result.result==VK_SUCCESS&&result.output&&result.output!=0xbeef,"Complete creation failed to publish ownership");
    require(live()==1+unsigned(seedLayout!=VK_NULL_HANDLE),"Complete creation lost native ownership");
    destroy(kind,result.output);clean();
}
void failed(Kind kind,const Outcome& result,VkResult expected){
    clean();require(!result.escaped&&result.result==expected&&!result.output,"Failed creation escaped, changed its result or published ownership");
    postNativeBoundary=0;armMeasurement=nativeFailure=nullNative=logThrows=unknownLayout=false;
    published(kind,invoke(kind));
}
unsigned hostAllocation(Kind kind,bool capture,unsigned boundary=0){
    start(kind,capture);armMeasurement=!boundary;postNativeBoundary=boundary;
    const auto result=invoke(kind);const auto count=allocations;
    if(boundary)require(allocationFailures==1,"Post-native allocation boundary was not exercised");
    if(result.result==VK_SUCCESS&&!result.escaped)published(kind,result);else failed(kind,result,VK_ERROR_OUT_OF_HOST_MEMORY);
    finish();return count;
}
unsigned scenarios{},failures{};
template<class F>void run(const char* name,F&& action){++scenarios;try{action();}catch(const std::exception& e){allocationCountdown=0;measuring=false;++failures;std::cerr<<name<<": "<<e.what()<<'\n';finish();}catch(...){allocationCountdown=0;measuring=false;++failures;std::cerr<<name<<": unknown exception\n";finish();}}
}
namespace argent {void log(const std::string&){if(logThrows)throw 7;}}
int main(int argc,char** argv){try{
    const bool extended=argc==2&&!std::strcmp(argv[1],"--extended");
    SetEnvironmentVariableA("ARGENT_SFS_NATIVE_PROBE","1");SetEnvironmentVariableA("ARGENT_SFS_NATIVE_VR","1");
    SetEnvironmentVariableA("ARGENT_PERFORMANCE_DIAGNOSTICS","0");SetEnvironmentVariableA("ARGENT_SFS_PROFILE_TIMING","0");SetEnvironmentVariableW(L"ARGENT_SFS_PROFILE",nullptr);
    wchar_t temporary[32768]{};require(GetTempPathW(std::size(temporary),temporary)>0,"No temporary directory");SetEnvironmentVariableW(L"ARGENT_CAPTURE_DIRECTORY",temporary);
    const auto loader=LoadLibraryW(L"vulkan-1.dll");require(loader,"No Vulkan loader");
    const auto get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));require(get,"No instance resolver");
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;
    VkInstance instance{};ok(reinterpret_cast<PFN_vkCreateInstance>(get(nullptr,"vkCreateInstance"))(&info,nullptr,&instance));
#define INSTANCE(api) const auto api=reinterpret_cast<PFN_##api>(get(instance,#api));require(api,#api)
    INSTANCE(vkEnumeratePhysicalDevices);INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);INSTANCE(vkGetPhysicalDeviceMemoryProperties);
    INSTANCE(vkGetPhysicalDeviceProperties);INSTANCE(vkCreateDevice);INSTANCE(vkGetDeviceProcAddr);INSTANCE(vkDestroyInstance);
#undef INSTANCE
    uint32_t count{};ok(vkEnumeratePhysicalDevices(instance,&count,nullptr));require(count,"No Vulkan GPU");
    std::vector<VkPhysicalDevice> physicals(count);ok(vkEnumeratePhysicalDevices(instance,&count,physicals.data()));physical=physicals.front();
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
    uint32_t family=UINT32_MAX;for(uint32_t i=0;i<count;++i)if(families[i].queueCount&&(families[i].queueFlags&VK_QUEUE_COMPUTE_BIT)){family=i;break;}require(family!=UINT32_MAX,"No compute queue");
    float priority=1;VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue.queueFamilyIndex=family;queue.queueCount=1;queue.pQueuePriorities=&priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queue;ok(vkCreateDevice(physical,&deviceInfo,nullptr,&device));driver=vkGetDeviceProcAddr;
    vkGetPhysicalDeviceMemoryProperties(physical,&memory);VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(physical,&properties);std::cout<<properties.deviceName<<'\n';
    for(auto kind:{Kind::Shader,Kind::Buffer,Kind::Layout,Kind::PipelineLayout,Kind::Pool,Kind::Query}){
        for(bool capture:{false,true}){
            unsigned boundaries{};run("host allocation measurement",[&]{boundaries=hostAllocation(kind,capture);});
            for(unsigned i=1;i<=boundaries;++i)run("post-native host allocation rollback",[&]{hostAllocation(kind,capture,i);});
            run("poisoned native failure",[&]{start(kind,capture);nativeFailure=true;failed(kind,invoke(kind),VK_ERROR_OUT_OF_DEVICE_MEMORY);finish();});
            if(extended){
                run("null native success",[&]{start(kind,capture);nullNative=true;failed(kind,invoke(kind),VK_ERROR_INITIALIZATION_FAILED);finish();});
                run("invalid top-level arguments",[&]{start(kind,capture);const auto input=invoke(kind,true);const auto output=invoke(kind,false,true);require(!input.escaped&&input.result==VK_ERROR_INITIALIZATION_FAILED&&!input.output&&!output.escaped&&output.result==input.result,"Invalid creation arguments reached native setup");clean();finish();});
                if(kind!=Kind::Buffer&&kind!=Kind::Query)run("pre-native host OOM with broken diagnostics",[&]{start(kind,capture);logThrows=true;allocationCountdown=1;const auto result=invoke(kind);require(allocationFailures==1&&resourceCount==unsigned(seedLayout!=VK_NULL_HANDLE),"Preparation failure reached native allocation");failed(kind,result,VK_ERROR_OUT_OF_HOST_MEMORY);finish();});
            }
        }
    }
    run("missing pipeline layout metadata",[]{start(Kind::PipelineLayout,true);unknownLayout=true;failed(Kind::PipelineLayout,invoke(Kind::PipelineLayout),VK_ERROR_INITIALIZATION_FAILED);finish();});
    for(bool capture:{false,true})run("optional query diagnostics",[&]{start(Kind::Query,capture);logThrows=true;published(Kind::Query,invoke(Kind::Query));finish();});
    if(extended)for(auto type:{VK_QUERY_TYPE_OCCLUSION,VK_QUERY_TYPE_PIPELINE_STATISTICS})run("query expansion policy",[&]{queryKind=type;start(Kind::Query,true);published(Kind::Query,invoke(Kind::Query));finish();queryKind=VK_QUERY_TYPE_TIMESTAMP;});
    if(extended){
        run("empty pipeline layout",[]{start(Kind::PipelineLayout,true);emptyPipeline=true;published(Kind::PipelineLayout,invoke(Kind::PipelineLayout));finish();});
        run("query count overflow",[]{start(Kind::Query,true);VkQueryPoolCreateInfo i{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};i.queryType=VK_QUERY_TYPE_TIMESTAMP;i.queryCount=UINT32_MAX;VkQueryPool h=handle<VkQueryPool>(0xbeef);const auto r=hook<PFN_vkCreateQueryPool>("vkCreateQueryPool")(device,&i,&allocator,&h);failed(Kind::Query,{r,reinterpret_cast<uintptr_t>(h)},VK_ERROR_OUT_OF_HOST_MEMORY);finish();});
        run("pool count overflow",[]{start(Kind::Pool,true);VkDescriptorPoolCreateInfo i{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};i.maxSets=UINT32_MAX;VkDescriptorPool h=handle<VkDescriptorPool>(0xbeef);const auto r=hook<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(device,&i,&allocator,&h);failed(Kind::Pool,{r,reinterpret_cast<uintptr_t>(h)},VK_ERROR_OUT_OF_HOST_MEMORY);finish();});
    }
    procedure<PFN_vkDestroyDevice>("vkDestroyDevice")(device,nullptr);vkDestroyInstance(instance,nullptr);FreeLibrary(loader);
    std::cout<<scenarios<<" linked SFS metadata creation scenarios, "<<failures<<" failures\n";return failures?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
