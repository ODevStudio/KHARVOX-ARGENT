#include <windows.h>
#include "../src/sfs/NativeSfs.h"
#include "../src/sfs/ShaderIdentity.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
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
enum class Kind {Graphics,Compute,StereoCompute};
struct Resource {uintptr_t handle{};bool shader{},live{};};
std::array<Resource,128> resources;
unsigned resourceCount{},invalidDestructions{},shaderCalls{},pipelineCalls{},requestPipelineCalls{};
unsigned failShader{},failPipeline{},nullShader{},nullPipeline{},injectionShader{},injectionPipeline{},boundary{};
bool fakeNative{},armMeasurement{},logThrows{},logAfterPipeline{},captureEnabled{},checkpoints{};
VkResult nativeError=VK_ERROR_OUT_OF_DEVICE_MEMORY;
VkDevice device{};
VkPhysicalDevice physical{};
VkPhysicalDeviceMemoryProperties memory{};
PFN_vkGetDeviceProcAddr driver{};
VkAllocationCallbacks allocator{};
VkPipelineLayout layout{};
VkDescriptorSetLayout descriptor{};
VkRenderPass pass{};
std::array<VkShaderModule,3> originals{};
std::array<std::vector<uint32_t>,3> words;
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void ok(VkResult value){require(value==VK_SUCCESS,"Vulkan fixture setup failed");}
template<class T>T handle(uintptr_t value){return reinterpret_cast<T>(value);}
template<class T>T procedure(const char* name){return reinterpret_cast<T>(driver(device,name));}
template<class T>T hook(const char* name){return reinterpret_cast<T>(argent::sfs::wrapProc(device,name,driver(device,name)));}
unsigned live(bool shader){unsigned count{};for(unsigned i=0;i<resourceCount;++i)count+=resources[i].live&&resources[i].shader==shader;return count;}
template<class T>VkResult allocate(bool shader,const VkAllocationCallbacks* a,T* out){
    require(a==(shader?nullptr:&allocator),"Native creation lost its allocator");
    const auto call=shader?++shaderCalls:++pipelineCalls;
    if(!shader)Sleep(3);
    if(call==(shader?failShader:failPipeline)){*out=handle<T>(0xdead);return nativeError;}
    if(call==(shader?nullShader:nullPipeline)){*out=VK_NULL_HANDLE;return VK_SUCCESS;}
    require(resourceCount<resources.size(),"Fixture resource capacity exceeded");
    const auto value=0x1000+16*resourceCount;resources[resourceCount++]={value,shader,true};*out=handle<T>(value);
    if(call==(shader?injectionShader:injectionPipeline)){allocationCountdown=boundary;measuring=armMeasurement;}
    return VK_SUCCESS;
}
template<class T>bool dispose(bool shader,T h,const VkAllocationCallbacks* a){
    const auto value=reinterpret_cast<uintptr_t>(h);
    for(unsigned i=0;i<resourceCount;++i)if(resources[i].handle==value){
        if(resources[i].shader!=shader||!resources[i].live||a!=(shader?nullptr:&allocator))++invalidDestructions;
        else resources[i].live=false;
        return true;
    }
    if(value==0xdead||!value){++invalidDestructions;return true;}
    return false;
}
VKAPI_ATTR VkResult VKAPI_CALL nativeShader(VkDevice d,const VkShaderModuleCreateInfo* i,const VkAllocationCallbacks* a,VkShaderModule* out){
    if(!fakeNative)return procedure<PFN_vkCreateShaderModule>("vkCreateShaderModule")(d,i,a,out);
    require(i->codeSize>20&&i->pCode[0]==0x07230203,"Compiled shader words missing");return allocate(true,a,out);
}
VKAPI_ATTR void VKAPI_CALL destroyShader(VkDevice d,VkShaderModule h,const VkAllocationCallbacks* a){if(!dispose(true,h,a))procedure<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(d,h,a);}
VKAPI_ATTR VkResult VKAPI_CALL graphics(VkDevice d,VkPipelineCache cache,uint32_t count,const VkGraphicsPipelineCreateInfo* i,const VkAllocationCallbacks* a,VkPipeline* out){
    if(!fakeNative)return procedure<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines")(d,cache,count,i,a,out);
    require(count==1&&cache==handle<VkPipelineCache>(0xcafe)&&i->layout==layout,"Graphics creation batch/cache/layout changed");
    require(i->stageCount==2&&i->pStages[0].module!=originals[0]&&i->pStages[1].module!=originals[1],"Graphics variants use untransformed shaders");
    require(i->renderPass&&(((pipelineCalls-requestPipelineCalls)%2==0)==(i->renderPass==pass)),"Graphics mono/stereo render-pass selection changed");
    return allocate(false,a,out);
}
VKAPI_ATTR VkResult VKAPI_CALL compute(VkDevice d,VkPipelineCache cache,uint32_t count,const VkComputePipelineCreateInfo* i,const VkAllocationCallbacks* a,VkPipeline* out){
    if(!fakeNative)return procedure<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(d,cache,count,i,a,out);
    require(count==1&&cache==handle<VkPipelineCache>(0xcafe)&&i->layout==layout&&i->stage.module!=originals[2],"Compute creation batch/cache/layout changed");
    return allocate(false,a,out);
}
VKAPI_ATTR void VKAPI_CALL destroyPipeline(VkDevice d,VkPipeline h,const VkAllocationCallbacks* a){if(!dispose(false,h,a))procedure<PFN_vkDestroyPipeline>("vkDestroyPipeline")(d,h,a);}
VKAPI_ATTR void VKAPI_CALL checkpoint(VkCommandBuffer,const void*){}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolver(VkDevice d,const char* name){
#define ENTRY(api,fn) if(!std::strcmp(name,#api))return reinterpret_cast<PFN_vkVoidFunction>(fn)
    ENTRY(vkCreateShaderModule,nativeShader);ENTRY(vkDestroyShaderModule,destroyShader);
    ENTRY(vkCreateGraphicsPipelines,graphics);ENTRY(vkCreateComputePipelines,compute);ENTRY(vkDestroyPipeline,destroyPipeline);
    ENTRY(vkCmdSetCheckpointNV,checkpoint);
#undef ENTRY
    return driver(d,name);
}
struct Outcome {VkResult result{};std::array<VkPipeline,3> outputs{};bool escaped{};};
Outcome invoke(Kind kind,uint32_t count=1,bool missingInfo=false,bool missingOutput=false,VkPipelineCreateFlags flags=0){
    Outcome result;result.outputs.fill(handle<VkPipeline>(0xbeef));requestPipelineCalls=pipelineCalls;
    try{
        if(kind==Kind::Graphics){
            std::array<VkPipelineShaderStageCreateInfo,2> stages{};
            for(unsigned i=0;i<2;++i){stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;stages[i].module=originals[i];stages[i].pName="main";}
            std::array<VkGraphicsPipelineCreateInfo,3> infos{};
            for(auto& i:infos){i.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;i.flags=flags;i.stageCount=2;i.pStages=stages.data();i.layout=layout;i.renderPass=pass;}
            result.result=hook<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines")(device,handle<VkPipelineCache>(0xcafe),count,missingInfo?nullptr:infos.data(),&allocator,missingOutput?nullptr:result.outputs.data());
        }else{
            std::array<VkComputePipelineCreateInfo,3> infos{};
            for(auto& i:infos){i.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;i.flags=flags;i.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};i.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;i.stage.module=originals[2];i.stage.pName="main";i.layout=layout;}
            result.result=hook<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(device,handle<VkPipelineCache>(0xcafe),count,missingInfo?nullptr:infos.data(),&allocator,missingOutput?nullptr:result.outputs.data());
        }
    }catch(...){result.escaped=true;}
    allocationCountdown=0;measuring=false;return result;
}
unsigned variants(Kind kind){return kind==Kind::Graphics?2:kind==Kind::StereoCompute?3:1;}
unsigned modules(Kind kind){return kind==Kind::Graphics?4:kind==Kind::StereoCompute?3:1;}
void resetInjection(){allocationCountdown=allocations=allocationFailures=0;failShader=failPipeline=nullShader=nullPipeline=injectionShader=injectionPipeline=boundary=0;armMeasurement=measuring=logThrows=logAfterPipeline=false;nativeError=VK_ERROR_OUT_OF_DEVICE_MEMORY;}
void start(Kind kind,bool capture,bool markers=true){
    resetInjection();resourceCount=invalidDestructions=shaderCalls=pipelineCalls=0;resources={};fakeNative=false;captureEnabled=capture;checkpoints=markers;
    SetEnvironmentVariableA("ARGENT_WATER_GPU_CAPTURE",capture?"1":"0");
    argent::sfs::Configuration configuration;
    if(kind==Kind::StereoCompute)configuration.stereoComputeShaders.insert(kharvox::sfs::profileHash(words[2].data(),uint32_t(words[2].size()*4)));
    require(argent::sfs::initialize(device,physical,resolver,memory,configuration),"SFS initialization failed");
    if(checkpoints)argent::sfs::enableGpuCheckpoints(device,resolver);
    for(unsigned k=0;k<3;++k){VkShaderModuleCreateInfo i{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};i.codeSize=words[k].size()*4;i.pCode=words[k].data();ok(hook<PFN_vkCreateShaderModule>("vkCreateShaderModule")(device,&i,nullptr,&originals[k]));}
    VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo descriptorInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};descriptorInfo.bindingCount=1;descriptorInfo.pBindings=&binding;
    ok(hook<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(device,&descriptorInfo,nullptr,&descriptor));
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layoutInfo.setLayoutCount=1;layoutInfo.pSetLayouts=&descriptor;
    ok(hook<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(device,&layoutInfo,nullptr,&layout));
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    VkRenderPassCreateInfo passInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};passInfo.subpassCount=1;passInfo.pSubpasses=&subpass;
    ok(hook<PFN_vkCreateRenderPass>("vkCreateRenderPass")(device,&passInfo,nullptr,&pass));fakeNative=true;
}
void disposeOutputs(const Outcome& result,uint32_t count){
    for(uint32_t i=0;i<count;++i)if(result.outputs[i]&&result.outputs[i]!=handle<VkPipeline>(0xbeef)&&result.outputs[i]!=handle<VkPipeline>(0xdead))hook<PFN_vkDestroyPipeline>("vkDestroyPipeline")(device,result.outputs[i],&allocator);
}
void finish(){
    resetInjection();fakeNative=false;
    if(pass)hook<PFN_vkDestroyRenderPass>("vkDestroyRenderPass")(device,pass,nullptr);pass=VK_NULL_HANDLE;
    if(layout)hook<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(device,layout,nullptr);layout=VK_NULL_HANDLE;
    if(descriptor)hook<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(device,descriptor,nullptr);descriptor=VK_NULL_HANDLE;
    for(auto& shader:originals){if(shader)hook<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(device,shader,nullptr);shader=VK_NULL_HANDLE;}
    argent::sfs::shutdown(device);
}
void published(Kind kind,const Outcome& result,uint32_t count=1){
    require(!result.escaped&&result.result==VK_SUCCESS,"Complete pipeline creation failed");
    for(unsigned i=0;i<count;++i)require(result.outputs[i]&&result.outputs[i]!=handle<VkPipeline>(0xbeef),"Complete pipeline output missing");
    require(live(false)==variants(kind)*count,"Complete pipeline lost native ownership");
    disposeOutputs(result,count);require(!live(false)&&!invalidDestructions,"Pipeline destruction leaked variants or destroyed invalid ownership");
}
void failed(Kind kind,const Outcome& result,VkResult expected,uint32_t count=1,unsigned successes=0){
    const bool valid=!result.escaped&&result.result==expected;
    bool outputs=true;for(unsigned i=0;i<count;++i)outputs&=i<successes?result.outputs[i]&&result.outputs[i]!=handle<VkPipeline>(0xbeef):!result.outputs[i];
    const bool ownership=live(false)==successes*variants(kind)&&!invalidDestructions;
    disposeOutputs(result,count);
    require(valid&&outputs&&ownership,"Failed creation escaped, published partial ownership, leaked variants or destroyed poisoned outputs");
    resetInjection();const auto before=shaderCalls;
    published(kind,invoke(kind));require(shaderCalls-before<=modules(kind),"Retry compiled duplicate cache entries");
}
unsigned postNative(Kind kind,bool capture,unsigned point=0){
    start(kind,capture);injectionPipeline=variants(kind);armMeasurement=!point;boundary=point;
    const auto result=invoke(kind);const auto count=allocations;
    if(point)require(allocationFailures==1,"Post-pipeline host allocation boundary was not exercised");
    if(result.result==VK_SUCCESS&&!result.escaped)published(kind,result);else failed(kind,result,VK_ERROR_OUT_OF_HOST_MEMORY);
    finish();require(!live(true)&&!live(false)&&!invalidDestructions,"Shutdown leaked compiled shader or pipeline ownership");return count;
}
unsigned scenarios{},failures{};
template<class F>void run(const char* name,F&& action){
    ++scenarios;try{action();}catch(const std::exception& e){allocationCountdown=0;measuring=false;++failures;std::cerr<<name<<": "<<e.what()<<'\n';finish();}catch(...){allocationCountdown=0;measuring=false;++failures;std::cerr<<name<<": unknown exception\n";finish();}
}
}
namespace argent {void log(const std::string&){if(logThrows||(logAfterPipeline&&pipelineCalls))throw 7;}}
int main(int argc,char** argv){try{
    require(argc==4||argc==5,"Need vertex, fragment and compute fixtures");const bool extended=argc==5;
    for(unsigned i=0;i<3;++i){std::ifstream file(argv[i+1],std::ios::binary|std::ios::ate);require(bool(file),"Missing SPIR-V fixture");const auto size=file.tellg();require(size>20&&size%4==0,"Invalid SPIR-V fixture");words[i].resize(size_t(size)/4);file.seekg(0);file.read(reinterpret_cast<char*>(words[i].data()),size);require(bool(file),"Incomplete SPIR-V fixture");}
    SetEnvironmentVariableA("ARGENT_SFS_NATIVE_PROBE","1");SetEnvironmentVariableA("ARGENT_SFS_NATIVE_VR","1");SetEnvironmentVariableA("ARGENT_PERFORMANCE_DIAGNOSTICS","0");SetEnvironmentVariableA("ARGENT_SFS_PROFILE_TIMING","1");SetEnvironmentVariableW(L"ARGENT_SFS_PROFILE",nullptr);
    wchar_t temporary[32768]{};require(GetTempPathW(std::size(temporary),temporary)>0,"No temporary directory");SetEnvironmentVariableW(L"ARGENT_CAPTURE_DIRECTORY",temporary);
    const auto loader=LoadLibraryW(L"vulkan-1.dll");require(loader,"No Vulkan loader");
    const auto get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));require(get,"No instance resolver");
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;
    VkInstance instance{};ok(reinterpret_cast<PFN_vkCreateInstance>(get(nullptr,"vkCreateInstance"))(&info,nullptr,&instance));
#define INSTANCE(api) const auto api=reinterpret_cast<PFN_##api>(get(instance,#api));require(api,#api)
    INSTANCE(vkEnumeratePhysicalDevices);INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);INSTANCE(vkGetPhysicalDeviceMemoryProperties);INSTANCE(vkGetPhysicalDeviceFeatures2);
    INSTANCE(vkGetPhysicalDeviceProperties);INSTANCE(vkCreateDevice);INSTANCE(vkGetDeviceProcAddr);INSTANCE(vkDestroyInstance);
#undef INSTANCE
    uint32_t count{};ok(vkEnumeratePhysicalDevices(instance,&count,nullptr));require(count,"No Vulkan GPU");std::vector<VkPhysicalDevice> physicals(count);ok(vkEnumeratePhysicalDevices(instance,&count,physicals.data()));physical=physicals.front();
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
    uint32_t family=UINT32_MAX;for(uint32_t i=0;i<count;++i)if(families[i].queueCount&&(families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)){family=i;break;}require(family!=UINT32_MAX,"No graphics queue");
    VkPhysicalDeviceMultiviewFeatures multiview{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&multiview;vkGetPhysicalDeviceFeatures2(physical,&features);require(multiview.multiview,"No multiview");multiview.multiviewGeometryShader=multiview.multiviewTessellationShader=VK_FALSE;
    float priority=1;VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue.queueFamilyIndex=family;queue.queueCount=1;queue.pQueuePriorities=&priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};deviceInfo.pNext=&multiview;deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queue;ok(vkCreateDevice(physical,&deviceInfo,nullptr,&device));driver=vkGetDeviceProcAddr;
    vkGetPhysicalDeviceMemoryProperties(physical,&memory);VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(physical,&properties);std::cout<<properties.deviceName<<'\n';
    run("global hand tracker warmup",[]{start(Kind::Graphics,false);published(Kind::Graphics,invoke(Kind::Graphics));finish();require(!live(true)&&!live(false)&&!invalidDestructions,"Warmup leaked ownership");});
    for(auto kind:{Kind::Graphics,Kind::Compute,Kind::StereoCompute})for(bool capture:{false,true}){
        unsigned boundaries{};run("post-pipeline allocation measurement",[&]{boundaries=postNative(kind,capture);});
        for(unsigned i=1;i<=boundaries;++i)run("post-pipeline host allocation rollback",[&]{postNative(kind,capture,i);});
        for(unsigned i=1;i<=modules(kind);++i)run("compiled module registration OOM",[&]{start(kind,capture);injectionShader=i;boundary=1;failed(kind,invoke(kind),VK_ERROR_OUT_OF_HOST_MEMORY);finish();require(!live(true)&&!live(false)&&!invalidDestructions,"Shader cache insertion leaked its native module");});
        for(unsigned i=1;i<=variants(kind);++i)run("native pipeline failure",[&]{start(kind,capture);failPipeline=i;failed(kind,invoke(kind),nativeError);finish();require(!live(true)&&!live(false)&&!invalidDestructions,"Native pipeline failure leaked ownership");});
        run("partial-success batch",[&]{start(kind,capture);failPipeline=variants(kind)+1;failed(kind,invoke(kind,3),nativeError,3,1);finish();require(!live(true)&&!live(false)&&!invalidDestructions,"Batch failure leaked ownership");});
        run("optional diagnostics after native creation",[&]{start(kind,capture);logAfterPipeline=true;published(kind,invoke(kind));finish();require(!live(true)&&!live(false)&&!invalidDestructions,"Diagnostics leaked ownership");});
        if(extended){
            for(unsigned i=1;i<=modules(kind);++i)for(unsigned point:{2u,3u})run("later compiled module registration boundaries",[&]{start(kind,capture);injectionShader=i;boundary=point;const auto result=invoke(kind);require(allocationFailures==1,"Shader registration boundary was not exercised");if(result.result==VK_SUCCESS&&!result.escaped)published(kind,result);else failed(kind,result,VK_ERROR_OUT_OF_HOST_MEMORY);finish();require(!live(true)&&!live(false)&&!invalidDestructions,"Later shader registration boundary leaked ownership");});
            run("compiled variant cache reuse",[&]{start(kind,capture);published(kind,invoke(kind,3),3);const auto created=shaderCalls;require(created==modules(kind),"Batch duplicated compiled variants");published(kind,invoke(kind));require(shaderCalls==created,"Cached variants were recompiled");finish();require(!live(true)&&!live(false)&&!invalidDestructions,"Cache lifetime leaked ownership");});
            for(unsigned i=1;i<=variants(kind);++i)run("later batch variant failure with broken diagnostics",[&]{start(kind,capture);failPipeline=variants(kind)+i;logAfterPipeline=true;failed(kind,invoke(kind,3),nativeError,3,1);finish();require(!live(true)&&!live(false)&&!invalidDestructions,"Later batch variant failure lost ownership");});
            run("later batch registration OOM",[&]{start(kind,capture);injectionPipeline=variants(kind)*2;boundary=1;const auto result=invoke(kind,3);require(allocationFailures==1,"Later batch registration OOM was not exercised");if(result.result==VK_SUCCESS&&!result.escaped)published(kind,result,3);else failed(kind,result,VK_ERROR_OUT_OF_HOST_MEMORY,3,1);finish();require(!live(true)&&!live(false)&&!invalidDestructions,"Later batch registration OOM lost ownership");});
            run("optional shader diagnostics",[&]{start(kind,capture);logThrows=true;published(kind,invoke(kind));finish();require(!live(true)&&!invalidDestructions,"Shader diagnostics leaked ownership");});
            for(unsigned i=1;i<=variants(kind);++i)run("null native pipeline success",[&]{start(kind,capture);nullPipeline=i;failed(kind,invoke(kind),VK_ERROR_INITIALIZATION_FAILED);finish();require(!live(true)&&!live(false)&&!invalidDestructions,"Null pipeline success leaked ownership");});
            run("null native shader success",[&]{start(kind,capture);nullShader=1;failed(kind,invoke(kind),VK_ERROR_INITIALIZATION_FAILED);finish();require(!live(true)&&!invalidDestructions,"Null shader success leaked ownership");});
            run("poisoned native shader failure",[&]{start(kind,capture);failShader=1;failed(kind,invoke(kind),VK_ERROR_INITIALIZATION_FAILED);finish();require(!live(true)&&!invalidDestructions,"Native shader failure destroyed poisoned output");});
            run("pipeline compilation required",[&]{start(kind,capture);failPipeline=variants(kind);nativeError=VK_PIPELINE_COMPILE_REQUIRED;failed(kind,invoke(kind),nativeError);finish();require(!live(true)&&!live(false)&&!invalidDestructions,"Compile-required result leaked ownership");});
            run("invalid top-level arguments",[&]{start(kind,capture);const auto input=invoke(kind,1,true);const auto output=invoke(kind,1,false,true);require(!input.escaped&&input.result==VK_ERROR_INITIALIZATION_FAILED&&!input.outputs[0]&&!output.escaped&&output.result==input.result,"Invalid arguments reached pipeline creation");require(!shaderCalls&&!pipelineCalls,"Invalid arguments reached native creation");finish();});
        }
    }
    procedure<PFN_vkDestroyDevice>("vkDestroyDevice")(device,nullptr);vkDestroyInstance(instance,nullptr);FreeLibrary(loader);
    std::cout<<scenarios<<" linked SFS pipeline creation scenarios, "<<failures<<" failures\n";return failures?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
