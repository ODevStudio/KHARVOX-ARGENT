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
enum class Kind {Sets,Commands,Pool};
struct Resource {uintptr_t handle{},pool{};Kind kind{};bool live{};};
struct Command {void* dispatch{};};
std::array<Resource,32> resources;
std::array<Command,32> commandHandles;
unsigned resourceCount{},invalidDestructions{},nativeCalls{},setFreeCalls{},poolResetCalls{},writes{},preNative{},boundary{};
bool fakeNative{},measurePre{},measurePost{},nativeFailure{},nullNative{},partialNull{},logThrows{},freeable{},secondary{},unknownLayout{};
VkDevice device{};
VkPhysicalDevice physical{};
VkPhysicalDeviceMemoryProperties memory{};
PFN_vkGetDeviceProcAddr driver{};
VkAllocationCallbacks allocator{};
VkDescriptorPool descriptorPool{};
VkCommandPool commandPool{};
std::array<VkDescriptorSetLayout,2> layouts{};
uint32_t family{};
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void ok(VkResult value){require(value==VK_SUCCESS,"Vulkan fixture setup failed");}
template<class T>T handle(uintptr_t value){return reinterpret_cast<T>(value);}
template<class T>T procedure(const char* name){return reinterpret_cast<T>(driver(device,name));}
template<class T>T hook(const char* name){return reinterpret_cast<T>(argent::sfs::wrapProc(device,name,driver(device,name)));}
unsigned live(Kind kind){unsigned count{};for(unsigned i=0;i<resourceCount;++i)count+=resources[i].live&&resources[i].kind==kind;return count;}
void stopPre(){++nativeCalls;if(measurePre){preNative=allocations;measuring=false;}}
template<class T>VkResult allocate(Kind kind,uintptr_t pool,uint32_t count,T* out){
    stopPre();
    if(nativeFailure){for(unsigned i=0;i<count;++i)out[i]=handle<T>(0xdead);return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
    if(nullNative){for(unsigned i=0;i<count;++i)out[i]=VK_NULL_HANDLE;return VK_SUCCESS;}
    for(unsigned i=0;i<count;++i){
        if(partialNull&&i==1){out[i]=VK_NULL_HANDLE;continue;}
        require(resourceCount<resources.size(),"Fixture resource capacity exceeded");
        auto value=uintptr_t(0x1000+16*resourceCount);
        if(kind==Kind::Commands){commandHandles[resourceCount].dispatch=*reinterpret_cast<void**>(device);value=reinterpret_cast<uintptr_t>(&commandHandles[resourceCount]);}
        resources[resourceCount++]={value,pool,kind,true};out[i]=handle<T>(value);
    }
    allocationCountdown=boundary;measuring=measurePost;return VK_SUCCESS;
}
bool dispose(Kind kind,uintptr_t value,uintptr_t pool=0){
    if(!value)return true;
    for(unsigned i=0;i<resourceCount;++i)if(resources[i].handle==value){
        if(resources[i].kind!=kind||!resources[i].live||(pool&&resources[i].pool!=pool))++invalidDestructions;
        else resources[i].live=false;
        return true;
    }
    if(value==0xdead||value==0xbeef){++invalidDestructions;return true;}
    return false;
}
void retire(Kind kind,uintptr_t pool){for(unsigned i=0;i<resourceCount;++i)if(resources[i].kind==kind&&resources[i].pool==pool)resources[i].live=false;}
VKAPI_ATTR VkResult VKAPI_CALL nativeSets(VkDevice d,const VkDescriptorSetAllocateInfo* i,VkDescriptorSet* out){
    if(!fakeNative)return procedure<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(d,i,out);
    require(i->descriptorPool==descriptorPool&&i->descriptorSetCount==3,"Descriptor pool/count changed");
    return allocate(Kind::Sets,reinterpret_cast<uintptr_t>(i->descriptorPool),i->descriptorSetCount,out);
}
VKAPI_ATTR VkResult VKAPI_CALL nativeFreeSets(VkDevice d,VkDescriptorPool pool,uint32_t count,const VkDescriptorSet* sets){
    if(!fakeNative)return procedure<PFN_vkFreeDescriptorSets>("vkFreeDescriptorSets")(d,pool,count,sets);
    ++setFreeCalls;require(freeable,"Illegal individual free on a non-freeable descriptor pool");
    for(unsigned i=0;i<count;++i)require(dispose(Kind::Sets,reinterpret_cast<uintptr_t>(sets[i]),reinterpret_cast<uintptr_t>(pool)),"Unknown descriptor freed");return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL nativeResetPool(VkDevice d,VkDescriptorPool pool,VkDescriptorPoolResetFlags flags){
    ++poolResetCalls;retire(Kind::Sets,reinterpret_cast<uintptr_t>(pool));return procedure<PFN_vkResetDescriptorPool>("vkResetDescriptorPool")(d,pool,flags);
}
VKAPI_ATTR void VKAPI_CALL nativeDestroyDescriptorPool(VkDevice d,VkDescriptorPool pool,const VkAllocationCallbacks* a){retire(Kind::Sets,reinterpret_cast<uintptr_t>(pool));procedure<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(d,pool,a);}
VKAPI_ATTR void VKAPI_CALL nativeWrites(VkDevice d,uint32_t count,const VkWriteDescriptorSet* infos,uint32_t copies,const VkCopyDescriptorSet* copied){
    if(!fakeNative){procedure<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(d,count,infos,copies,copied);return;}
    require(count==3&&!copies,"Projection write count changed");
    for(unsigned i=0;i<count;++i){const auto& w=infos[i];require(w.sType==VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET&&w.dstBinding==(i==1?1u:0u)&&w.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER&&w.descriptorCount==1&&w.pBufferInfo&&w.pBufferInfo->buffer&&w.pBufferInfo->range==sizeof(argent::sfs::EyeUniforms),"Projection write changed");}
    ++writes;
}
VKAPI_ATTR VkResult VKAPI_CALL nativeCommands(VkDevice d,const VkCommandBufferAllocateInfo* i,VkCommandBuffer* out){
    if(!fakeNative)return procedure<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(d,i,out);
    require(i->commandPool==commandPool&&i->commandBufferCount==3&&i->level==(secondary?VK_COMMAND_BUFFER_LEVEL_SECONDARY:VK_COMMAND_BUFFER_LEVEL_PRIMARY),"Command allocation info changed");
    return allocate(Kind::Commands,reinterpret_cast<uintptr_t>(i->commandPool),i->commandBufferCount,out);
}
VKAPI_ATTR void VKAPI_CALL nativeFreeCommands(VkDevice d,VkCommandPool pool,uint32_t count,const VkCommandBuffer* commands){
    if(!fakeNative){procedure<PFN_vkFreeCommandBuffers>("vkFreeCommandBuffers")(d,pool,count,commands);return;}
    for(unsigned i=0;i<count;++i){require(commands[i],"Null command buffer freed");require(dispose(Kind::Commands,reinterpret_cast<uintptr_t>(commands[i]),reinterpret_cast<uintptr_t>(pool)),"Unknown command freed");}
}
VKAPI_ATTR VkResult VKAPI_CALL nativePool(VkDevice d,const VkCommandPoolCreateInfo* i,const VkAllocationCallbacks* a,VkCommandPool* out){
    if(!fakeNative)return procedure<PFN_vkCreateCommandPool>("vkCreateCommandPool")(d,i,a,out);
    require(a==&allocator&&i->queueFamilyIndex==family&&i->flags==VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,"Command pool info/allocator changed");return allocate(Kind::Pool,0,1,out);
}
VKAPI_ATTR void VKAPI_CALL nativeDestroyCommandPool(VkDevice d,VkCommandPool pool,const VkAllocationCallbacks* a){
    retire(Kind::Commands,reinterpret_cast<uintptr_t>(pool));
    if(dispose(Kind::Pool,reinterpret_cast<uintptr_t>(pool))){require(a==&allocator,"Command pool destruction lost its allocator");return;}
    procedure<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(d,pool,a);
}
VKAPI_ATTR VkResult VKAPI_CALL nativeBegin(VkCommandBuffer,const VkCommandBufferBeginInfo*){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL nativeEnd(VkCommandBuffer){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL nativeResetCommand(VkCommandBuffer,VkCommandBufferResetFlags){return VK_SUCCESS;}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolver(VkDevice d,const char* name){
#define ENTRY(api,fn) if(!std::strcmp(name,#api))return reinterpret_cast<PFN_vkVoidFunction>(fn)
    ENTRY(vkAllocateDescriptorSets,nativeSets);ENTRY(vkFreeDescriptorSets,nativeFreeSets);ENTRY(vkResetDescriptorPool,nativeResetPool);ENTRY(vkDestroyDescriptorPool,nativeDestroyDescriptorPool);ENTRY(vkUpdateDescriptorSets,nativeWrites);
    ENTRY(vkAllocateCommandBuffers,nativeCommands);ENTRY(vkFreeCommandBuffers,nativeFreeCommands);ENTRY(vkCreateCommandPool,nativePool);ENTRY(vkDestroyCommandPool,nativeDestroyCommandPool);
    ENTRY(vkBeginCommandBuffer,nativeBegin);ENTRY(vkEndCommandBuffer,nativeEnd);ENTRY(vkResetCommandBuffer,nativeResetCommand);
#undef ENTRY
    return driver(d,name);
}
struct Outcome {VkResult result{};std::array<uintptr_t,3> outputs{0xbeef,0xbeef,0xbeef};bool escaped{};};
Outcome invoke(Kind kind,bool missingInfo=false,bool missingOutput=false){
    Outcome result;
    try{
        if(kind==Kind::Sets){std::array<VkDescriptorSetLayout,3> seeds{layouts[0],unknownLayout?handle<VkDescriptorSetLayout>(0xface):layouts[1],layouts[0]};VkDescriptorSetAllocateInfo i{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};i.descriptorPool=descriptorPool;i.descriptorSetCount=3;i.pSetLayouts=seeds.data();std::array<VkDescriptorSet,3> out{handle<VkDescriptorSet>(0xbeef),handle<VkDescriptorSet>(0xbeef),handle<VkDescriptorSet>(0xbeef)};result.result=hook<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(device,missingInfo?nullptr:&i,missingOutput?nullptr:out.data());for(unsigned k=0;k<3;++k)result.outputs[k]=reinterpret_cast<uintptr_t>(out[k]);}
        if(kind==Kind::Commands){VkCommandBufferAllocateInfo i{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};i.commandPool=commandPool;i.level=secondary?VK_COMMAND_BUFFER_LEVEL_SECONDARY:VK_COMMAND_BUFFER_LEVEL_PRIMARY;i.commandBufferCount=3;std::array<VkCommandBuffer,3> out{handle<VkCommandBuffer>(0xbeef),handle<VkCommandBuffer>(0xbeef),handle<VkCommandBuffer>(0xbeef)};result.result=hook<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(device,missingInfo?nullptr:&i,missingOutput?nullptr:out.data());for(unsigned k=0;k<3;++k)result.outputs[k]=reinterpret_cast<uintptr_t>(out[k]);}
        if(kind==Kind::Pool){VkCommandPoolCreateInfo i{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};i.queueFamilyIndex=family;i.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;VkCommandPool out=handle<VkCommandPool>(0xbeef);result.result=hook<PFN_vkCreateCommandPool>("vkCreateCommandPool")(device,missingInfo?nullptr:&i,&allocator,missingOutput?nullptr:&out);result.outputs[0]=reinterpret_cast<uintptr_t>(out);}
    }catch(...){result.escaped=true;}
    allocationCountdown=0;measuring=false;return result;
}
void resetInjection(){allocationCountdown=allocations=allocationFailures=boundary=preNative=0;measuring=measurePre=measurePost=nativeFailure=nullNative=partialNull=logThrows=unknownLayout=false;}
void start(Kind kind,bool capture,bool freeSets=false,bool second=false){
    resetInjection();resourceCount=invalidDestructions=nativeCalls=setFreeCalls=poolResetCalls=writes=0;resources={};commandHandles={};fakeNative=false;freeable=freeSets;secondary=second;
    SetEnvironmentVariableA("ARGENT_WATER_GPU_CAPTURE",capture?"1":"0");require(argent::sfs::initialize(device,physical,resolver,memory,{}),"SFS initialization failed");
    if(kind==Kind::Sets){
        for(unsigned k=0;k<2;++k){const VkDescriptorSetLayoutBinding b{k?0u:3u,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,2,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};VkDescriptorSetLayoutCreateInfo i{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};i.bindingCount=1;i.pBindings=&b;ok(hook<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(device,&i,nullptr,&layouts[k]));}
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,32};VkDescriptorPoolCreateInfo i{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};i.maxSets=16;i.poolSizeCount=1;i.pPoolSizes=&size;i.flags=freeable?VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT:0;ok(hook<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(device,&i,nullptr,&descriptorPool));
    }
    if(kind==Kind::Commands){VkCommandPoolCreateInfo i{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};i.queueFamilyIndex=family;i.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;ok(hook<PFN_vkCreateCommandPool>("vkCreateCommandPool")(device,&i,nullptr,&commandPool));}
    fakeNative=true;
}
void release(Kind kind,const Outcome& result){
    if(kind==Kind::Sets){if(freeable){std::array<VkDescriptorSet,3> sets{};for(unsigned i=0;i<3;++i)sets[i]=handle<VkDescriptorSet>(result.outputs[i]);ok(hook<PFN_vkFreeDescriptorSets>("vkFreeDescriptorSets")(device,descriptorPool,3,sets.data()));}else ok(hook<PFN_vkResetDescriptorPool>("vkResetDescriptorPool")(device,descriptorPool,0));}
    if(kind==Kind::Commands){std::array<VkCommandBuffer,3> commands{};for(unsigned i=0;i<3;++i)commands[i]=handle<VkCommandBuffer>(result.outputs[i]);hook<PFN_vkFreeCommandBuffers>("vkFreeCommandBuffers")(device,commandPool,3,commands.data());}
    if(kind==Kind::Pool)hook<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(device,handle<VkCommandPool>(result.outputs[0]),&allocator);
}
void published(Kind kind,const Outcome& result,unsigned prior=0,bool keep=false){
    require(!result.escaped&&result.result==VK_SUCCESS,"Complete allocation failed");
    const auto count=kind==Kind::Pool?1u:3u;for(unsigned i=0;i<count;++i)require(result.outputs[i]&&result.outputs[i]!=0xbeef&&result.outputs[i]!=0xdead,"Complete allocation output missing");
    require(live(kind)==prior+count&&!invalidDestructions,"Complete allocation lost native ownership");
    if(kind==Kind::Commands){for(unsigned i=0;i<3;++i){auto cb=handle<VkCommandBuffer>(result.outputs[i]);VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};ok(hook<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(cb,&begin));ok(hook<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(cb));}}
    if(!keep){release(kind,result);const auto remaining=kind==Kind::Sets&&!freeable?0u:prior;require(live(kind)==remaining&&!invalidDestructions,"Application release leaked native ownership");}
}
void failed(Kind kind,const Outcome& result,VkResult expected,unsigned prior=0){
    const auto count=kind==Kind::Pool?1u:3u;bool outputs=true;for(unsigned i=0;i<count;++i)outputs&=!result.outputs[i];
    require(!result.escaped&&result.result==expected&&outputs&&live(kind)==prior&&!invalidDestructions,"Failed allocation escaped, leaked ownership or retained poisoned outputs");
    require(!poolResetCalls&&!setFreeCalls,"Failed allocation reset the caller pool or individually freed sets");
    resetInjection();published(kind,invoke(kind),prior);
}
void finish(){
    resetInjection();fakeNative=false;
    if(descriptorPool){hook<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(device,descriptorPool,nullptr);descriptorPool=VK_NULL_HANDLE;}
    for(auto& layout:layouts){if(layout)hook<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(device,layout,nullptr);layout=VK_NULL_HANDLE;}
    if(commandPool){try{hook<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(device,commandPool,nullptr);}catch(...){nativeDestroyCommandPool(device,commandPool,nullptr);}commandPool=VK_NULL_HANDLE;}
    argent::sfs::shutdown(device);
}
unsigned postNative(Kind kind,bool capture,bool mode,unsigned point=0){
    start(kind,capture,mode,mode);measurePost=!point;boundary=point;logThrows=point!=0;const auto result=invoke(kind);const auto count=allocations;
    if(!point&&kind==Kind::Sets)require(!count,"Descriptor bookkeeping allocated after native success");
    if(point)require(allocationFailures==1,"Post-native host allocation boundary was not exercised");
    if(result.result==VK_SUCCESS&&!result.escaped)published(kind,result);else failed(kind,result,VK_ERROR_OUT_OF_HOST_MEMORY);
    finish();return count;
}
void existingPreparationFailure(Kind kind,bool capture,bool mode,unsigned point){
    start(kind,capture,mode,mode);const auto seed=invoke(kind);published(kind,seed,0,true);
    allocationCountdown=point;logThrows=true;const auto calls=nativeCalls;
    const auto result=invoke(kind);require(allocationFailures==1&&nativeCalls==calls,"Later preparation OOM reached native allocation");
    failed(kind,result,VK_ERROR_OUT_OF_HOST_MEMORY,3);
    if(kind==Kind::Commands||freeable)published(kind,seed);
    finish();
}
unsigned existingRegistrationFailure(Kind kind,bool capture,bool mode,unsigned point=0){
    start(kind,capture,mode,mode);const auto seed=invoke(kind);published(kind,seed,0,true);
    measurePost=!point;boundary=point;logThrows=point!=0;const auto result=invoke(kind);const auto count=allocations;
    if(point){require(allocationFailures==1,"Later registration OOM boundary was not exercised");failed(kind,result,VK_ERROR_OUT_OF_HOST_MEMORY,3);}
    else published(kind,result,3);
    published(kind,seed);finish();return count;
}
void malformedBatch(Kind kind,bool capture,bool mode){
    start(kind,capture,mode,mode);const auto seed=invoke(kind);published(kind,seed,0,true);
    partialNull=true;const auto result=invoke(kind);
    require(!result.escaped&&result.result==VK_ERROR_INITIALIZATION_FAILED,"Partial-null native success was accepted");
    for(const auto output:result.outputs)require(!output,"Partial-null native success published a sibling");
    require(live(kind)==(kind==Kind::Sets?5u:3u)&&!invalidDestructions,"Malformed batch corrupted native ownership");
    require(!setFreeCalls&&!poolResetCalls,"Malformed batch retired the caller's existing sets");
    resetInjection();
    if(kind==Kind::Sets){require(writes==1,"Malformed descriptor batch wrote invalid outputs");ok(hook<PFN_vkResetDescriptorPool>("vkResetDescriptorPool")(device,descriptorPool,0));require(!live(kind),"Caller reset did not retire malformed descriptor siblings");}
    else published(kind,seed);
    finish();
}
unsigned preparation(Kind kind,bool capture,bool mode,unsigned point=0){
    start(kind,capture,mode,mode);measurePre=!point;measuring=measurePre;allocationCountdown=point;logThrows=point!=0;const auto result=invoke(kind);const auto count=preNative;
    if(point){require(allocationFailures==1&&!nativeCalls,"Preparation OOM reached native allocation");failed(kind,result,VK_ERROR_OUT_OF_HOST_MEMORY);}else published(kind,result);
    finish();return count;
}
unsigned scenarios{},failures{};
template<class F>void run(const char* name,F&& action){++scenarios;try{action();}catch(const std::exception& e){allocationCountdown=0;measuring=false;++failures;std::cerr<<name<<": "<<e.what()<<'\n';finish();}catch(...){allocationCountdown=0;measuring=false;++failures;std::cerr<<name<<": unknown exception\n";finish();}}
}
namespace argent {void log(const std::string&){if(logThrows)throw 7;}}
int main(int argc,char** argv){try{
    const bool extended=argc==2&&!std::strcmp(argv[1],"--extended");
    SetEnvironmentVariableA("ARGENT_SFS_NATIVE_PROBE","1");SetEnvironmentVariableA("ARGENT_SFS_NATIVE_VR","1");SetEnvironmentVariableA("ARGENT_PERFORMANCE_DIAGNOSTICS","0");SetEnvironmentVariableA("ARGENT_SFS_PROFILE_TIMING","0");SetEnvironmentVariableW(L"ARGENT_SFS_PROFILE",nullptr);
    wchar_t temporary[32768]{};require(GetTempPathW(std::size(temporary),temporary)>0,"No temporary directory");SetEnvironmentVariableW(L"ARGENT_CAPTURE_DIRECTORY",temporary);
    const auto loader=LoadLibraryW(L"vulkan-1.dll");require(loader,"No Vulkan loader");const auto get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));require(get,"No instance resolver");
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;VkInstance instance{};ok(reinterpret_cast<PFN_vkCreateInstance>(get(nullptr,"vkCreateInstance"))(&info,nullptr,&instance));
#define INSTANCE(api) const auto api=reinterpret_cast<PFN_##api>(get(instance,#api));require(api,#api)
    INSTANCE(vkEnumeratePhysicalDevices);INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);INSTANCE(vkGetPhysicalDeviceMemoryProperties);INSTANCE(vkGetPhysicalDeviceProperties);INSTANCE(vkCreateDevice);INSTANCE(vkGetDeviceProcAddr);INSTANCE(vkDestroyInstance);
#undef INSTANCE
    uint32_t count{};ok(vkEnumeratePhysicalDevices(instance,&count,nullptr));require(count,"No Vulkan GPU");std::vector<VkPhysicalDevice> physicals(count);ok(vkEnumeratePhysicalDevices(instance,&count,physicals.data()));physical=physicals.front();
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());family=UINT32_MAX;for(unsigned i=0;i<count;++i)if(families[i].queueCount&&(families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)){family=i;break;}require(family!=UINT32_MAX,"No graphics queue");
    float priority=1;VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue.queueFamilyIndex=family;queue.queueCount=1;queue.pQueuePriorities=&priority;VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queue;ok(vkCreateDevice(physical,&deviceInfo,nullptr,&device));driver=vkGetDeviceProcAddr;
    vkGetPhysicalDeviceMemoryProperties(physical,&memory);VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(physical,&properties);std::cout<<properties.deviceName<<'\n';
    for(auto kind:{Kind::Sets,Kind::Commands,Kind::Pool})for(bool capture:{false,true})for(bool mode:{false,true}){
        if(kind==Kind::Pool&&mode)continue;
        unsigned boundaries{};run("post-native allocation measurement",[&]{boundaries=postNative(kind,capture,mode);});
        for(unsigned i=1;i<=boundaries;++i)run("post-native registration OOM",[&]{postNative(kind,capture,mode,i);});
        run("poisoned native allocation failure",[&]{start(kind,capture,mode,mode);nativeFailure=true;failed(kind,invoke(kind),VK_ERROR_OUT_OF_DEVICE_MEMORY);finish();});
        if(extended){
            unsigned prepared{};run("preparation allocation measurement",[&]{prepared=preparation(kind,capture,mode);});
            for(unsigned i=1;i<=prepared;++i)run("preparation OOM with throwing diagnostics",[&]{preparation(kind,capture,mode,i);});
            std::cout<<"batch="<<(kind==Kind::Sets?"descriptors":kind==Kind::Commands?"commands":"pool")<<" capture="<<capture<<" mode="<<mode<<" preparation="<<prepared<<" postNative="<<boundaries<<'\n';
            run("null native successful outputs",[&]{start(kind,capture,mode,mode);nullNative=true;failed(kind,invoke(kind),VK_ERROR_INITIALIZATION_FAILED);finish();});
            run("null top-level arguments",[&]{start(kind,capture,mode,mode);const auto input=invoke(kind,true);const auto output=invoke(kind,false,true);require(!input.escaped&&input.result==VK_ERROR_INITIALIZATION_FAILED&&!output.escaped&&output.result==input.result,"Null top-level arguments escaped");require(!nativeCalls&&!live(kind),"Null arguments reached native allocation");finish();});
            if(kind==Kind::Sets)run("missing layout metadata before native allocation",[&]{start(kind,capture,mode,mode);unknownLayout=true;const auto result=invoke(kind);require(!nativeCalls,"Unknown layout reached native allocation");failed(kind,result,VK_ERROR_INITIALIZATION_FAILED);finish();});
            if(kind!=Kind::Pool)run("existing batch survives later allocation failure",[&]{start(kind,capture,mode,mode);const auto seed=invoke(kind);published(kind,seed,0,true);nativeFailure=true;failed(kind,invoke(kind),VK_ERROR_OUT_OF_DEVICE_MEMORY,3);if(kind==Kind::Commands||freeable)published(kind,seed);finish();});
            if(kind!=Kind::Pool){
                for(unsigned i=1;i<=prepared;++i)run("existing batch survives preparation OOM",[&]{existingPreparationFailure(kind,capture,mode,i);});
                if(kind==Kind::Commands){unsigned existing{};run("existing batch registration allocation measurement",[&]{existing=existingRegistrationFailure(kind,capture,mode);});for(unsigned i=1;i<=existing;++i)run("existing batch survives registration OOM",[&]{existingRegistrationFailure(kind,capture,mode,i);});}
                run("partial-null native success preserves prior batch",[&]{malformedBatch(kind,capture,mode);});
            }
        }
    }
    procedure<PFN_vkDestroyDevice>("vkDestroyDevice")(device,nullptr);vkDestroyInstance(instance,nullptr);FreeLibrary(loader);std::cout<<scenarios<<" linked SFS batch allocation scenarios, "<<failures<<" failures\n";return failures?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
