#include "../src/DesktopMirror.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>

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
void operator delete(void* value) noexcept {std::free(value);}
void operator delete(void* value,std::size_t) noexcept {std::free(value);}

namespace {
enum class Operation {None,Chain,Pool,Command,Loader,Semaphore,Fence};
enum class Mode {Normal,Shrink,Incomplete,AlwaysIncomplete,Empty,Excess,FormatShrink,ModeShrink,FormatIncomplete,FormatAlwaysIncomplete,
    ModeIncomplete,ModeAlwaysIncomplete,ImageCountFailed,ImageFillFailed};
struct Resource {uintptr_t handle{};Operation kind{};bool live{};};
std::array<Resource,32> resources;
std::array<unsigned,7> calls{};
unsigned resourceCount{},invalidDestructions{},idleWaits{},imageFills{},formatFills{},modeFills{},selectedWrongMode{};
Operation failure{};
Mode mode{};
unsigned failedCall{};
bool poison{},logThrow{};
const char* missing{};
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class T> T handle(uintptr_t value){return reinterpret_cast<T>(value);}
unsigned live(){unsigned result{};for(unsigned i=0;i<resourceCount;++i)result+=resources[i].live;return result;}
template<class T> VkResult create(Operation operation,T* out){
    const auto call=++calls[unsigned(operation)];
    if(failure==operation&&call==failedCall){if(poison)*out=handle<T>(0xdead);return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
    const auto value=0x1000+resourceCount*16;resources[resourceCount++]={value,operation,true};*out=handle<T>(value);return VK_SUCCESS;
}
template<class T> void destroy(Operation operation,T value){
    for(unsigned i=0;i<resourceCount;++i)if(resources[i].handle==reinterpret_cast<uintptr_t>(value)&&resources[i].kind==operation&&resources[i].live){resources[i].live=false;return;}
    ++invalidDestructions;
}
VKAPI_ATTR VkResult VKAPI_CALL createChain(VkDevice,const VkSwapchainCreateInfoKHR* info,const VkAllocationCallbacks*,VkSwapchainKHR* out){
    require(info->imageArrayLayers==1&&!info->flags&&!info->pNext&&info->imageUsage==VK_IMAGE_USAGE_TRANSFER_DST_BIT,"Mirror WSI contract changed");
    require(info->imageSharingMode==VK_SHARING_MODE_EXCLUSIVE&&!info->queueFamilyIndexCount&&!info->oldSwapchain,"Mirror queue or old-chain contract changed");
    if((mode==Mode::ModeShrink||mode==Mode::ModeAlwaysIncomplete)&&info->presentMode!=VK_PRESENT_MODE_FIFO_KHR)++selectedWrongMode;
    return create(Operation::Chain,out);
}
VKAPI_ATTR void VKAPI_CALL destroyChain(VkDevice,VkSwapchainKHR value,const VkAllocationCallbacks*){destroy(Operation::Chain,value);}
VKAPI_ATTR VkResult VKAPI_CALL images(VkDevice,VkSwapchainKHR,uint32_t* count,VkImage* out){
    if(!out){*count=mode==Mode::Empty?0:3;return mode==Mode::ImageCountFailed?VK_ERROR_DEVICE_LOST:VK_SUCCESS;}
    if(mode==Mode::ImageFillFailed){*count=0;return VK_ERROR_OUT_OF_HOST_MEMORY;}
    ++imageFills;const auto size=mode==Mode::Shrink?2u:3u;const auto written=(std::min)(*count,size);
    for(unsigned i=0;i<written;++i)out[i]=handle<VkImage>(0x2000+i);
    *count=mode==Mode::Excess?4:written;
    if(mode==Mode::AlwaysIncomplete||(mode==Mode::Incomplete&&imageFills==1))return VK_INCOMPLETE;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL createPool(VkDevice,const VkCommandPoolCreateInfo*,const VkAllocationCallbacks*,VkCommandPool* out){return create(Operation::Pool,out);}
VKAPI_ATTR void VKAPI_CALL destroyPool(VkDevice,VkCommandPool value,const VkAllocationCallbacks*){destroy(Operation::Pool,value);}
VKAPI_ATTR VkResult VKAPI_CALL allocateCommands(VkDevice,const VkCommandBufferAllocateInfo*,VkCommandBuffer* out){
    ++calls[unsigned(Operation::Command)];
    if(failure==Operation::Command){if(poison)*out=handle<VkCommandBuffer>(0xdead);return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
    *out=handle<VkCommandBuffer>(0x4000);return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL loader(VkDevice,void*){++calls[unsigned(Operation::Loader)];return failure==Operation::Loader?VK_ERROR_INITIALIZATION_FAILED:VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL createSemaphore(VkDevice,const VkSemaphoreCreateInfo*,const VkAllocationCallbacks*,VkSemaphore* out){return create(Operation::Semaphore,out);}
VKAPI_ATTR void VKAPI_CALL destroySemaphore(VkDevice,VkSemaphore value,const VkAllocationCallbacks*){destroy(Operation::Semaphore,value);}
VKAPI_ATTR VkResult VKAPI_CALL createFence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* out){return create(Operation::Fence,out);}
VKAPI_ATTR void VKAPI_CALL destroyFence(VkDevice,VkFence value,const VkAllocationCallbacks*){destroy(Operation::Fence,value);}
VKAPI_ATTR VkResult VKAPI_CALL deviceIdle(VkDevice){++idleWaits;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL formats(VkPhysicalDevice,VkSurfaceKHR,uint32_t* count,VkSurfaceFormatKHR* out){
    if(!out){*count=mode==Mode::FormatShrink?2:1;return VK_SUCCESS;}
    ++formatFills;*count=1;*out={mode==Mode::FormatShrink?VK_FORMAT_B8G8R8A8_UNORM:VK_FORMAT_R8G8B8A8_SRGB,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    return mode==Mode::FormatAlwaysIncomplete||(mode==Mode::FormatIncomplete&&formatFills==1)?VK_INCOMPLETE:VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL modes(VkPhysicalDevice,VkSurfaceKHR,uint32_t* count,VkPresentModeKHR* out){
    if(!out){*count=mode==Mode::ModeShrink?3:1;return VK_SUCCESS;}
    ++modeFills;*count=1;*out=mode==Mode::ModeShrink?VK_PRESENT_MODE_FIFO_KHR:VK_PRESENT_MODE_IMMEDIATE_KHR;
    return mode==Mode::ModeAlwaysIncomplete||(mode==Mode::ModeIncomplete&&modeFills==1)?VK_INCOMPLETE:VK_SUCCESS;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL instanceProc(VkInstance,const char* name){
    if(!std::strcmp(name,"vkGetPhysicalDeviceSurfaceFormatsKHR"))return reinterpret_cast<PFN_vkVoidFunction>(formats);
    if(!std::strcmp(name,"vkGetPhysicalDeviceSurfacePresentModesKHR"))return reinterpret_cast<PFN_vkVoidFunction>(modes);
    return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL deviceProc(VkDevice,const char* name){
    if(missing&&!std::strcmp(name,missing))return nullptr;
#define ENTRY(api,fn) if(!std::strcmp(name,#api))return reinterpret_cast<PFN_vkVoidFunction>(fn)
    ENTRY(vkCreateSwapchainKHR,createChain);ENTRY(vkDestroySwapchainKHR,destroyChain);ENTRY(vkGetSwapchainImagesKHR,images);
    ENTRY(vkCreateCommandPool,createPool);ENTRY(vkDestroyCommandPool,destroyPool);ENTRY(vkAllocateCommandBuffers,allocateCommands);
    ENTRY(vkCreateSemaphore,createSemaphore);ENTRY(vkDestroySemaphore,destroySemaphore);ENTRY(vkCreateFence,createFence);
    ENTRY(vkDestroyFence,destroyFence);ENTRY(vkDeviceWaitIdle,deviceIdle);
#undef ENTRY
    return nullptr;
}
void reset(){
    resources={};calls={};resourceCount=invalidDestructions=idleWaits=imageFills=formatFills=modeFills=selectedWrongMode=0;
    failure=Operation::None;mode=Mode::Normal;failedCall=1;poison=logThrow=measuring=false;missing=nullptr;
    allocations=allocationFailures=allocationCountdown=0;
}
argent::Device device(){argent::Device result;result.device=handle<VkDevice>(1);result.graphicsQueue=handle<VkQueue>(2);
    result.gipa=instanceProc;result.gdpa=deviceProc;result.setLoaderData=loader;return result;}
VkSwapchainCreateInfoKHR info(){
    VkSwapchainCreateInfoKHR result{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};result.imageExtent={64,64};
    result.imageArrayLayers=2;result.imageFormat=VK_FORMAT_R8G8B8A8_UNORM;result.presentMode=VK_PRESENT_MODE_FIFO_KHR;
    result.imageSharingMode=VK_SHARING_MODE_CONCURRENT;return result;
}
void failedCreation(Operation operation,unsigned index,bool poisoned){
    reset();auto d=device();argent::DesktopMirror mirror;failure=operation;failedCall=index;poison=poisoned;
    bool success{};try{success=mirror.create(d,info(),60,true);}catch(...){}
    const bool clean=!mirror.handle()&&!mirror.needsFrame()&&!live()&&!invalidDestructions&&!idleWaits;
    mirror.destroy(d);mirror.destroy(d);
    require(!success,"Injected native setup failure returned success");
    require(clean,"Failed setup retained native ownership, enabled presentation or waited for unsubmitted work");
    require(!live()&&!invalidDestructions,"Failed setup destroyed an undefined output or leaked resources");
}
unsigned hostAllocation(bool showEye,unsigned boundary=0){
    reset();auto d=device();argent::DesktopMirror mirror;allocationCountdown=boundary;measuring=!boundary;
    bool success{};try{success=mirror.create(d,info(),60,showEye);}catch(...){}
    allocationCountdown=0;measuring=false;const auto count=allocations;
    if(boundary)require(allocationFailures==1,"Host allocation injection was not exercised");
    if(success)require(mirror.handle()&&mirror.needsFrame()&&live()==7,"Successful setup did not retain all mirror resources");
    else require(!mirror.handle()&&!mirror.needsFrame()&&!live()&&!idleWaits,"Host setup failure retained native ownership");
    mirror.destroy(d);mirror.destroy(d);require(!live()&&!invalidDestructions,"Host allocation cleanup lost resource ownership");
    return count;
}
void enumeration(Mode value){
    reset();mode=value;auto d=device();argent::DesktopMirror mirror;bool success{};
    const bool expected=value!=Mode::AlwaysIncomplete&&value!=Mode::Empty&&value!=Mode::Excess&&value!=Mode::FormatShrink
        &&value!=Mode::FormatAlwaysIncomplete&&value!=Mode::ImageCountFailed&&value!=Mode::ImageFillFailed;
    try{success=mirror.create(d,info(),60,true);}catch(...){}
    const bool clean=success||(!mirror.handle()&&!live()&&!mirror.needsFrame()&&!idleWaits);
    if(success&&value==Mode::Shrink)require(calls[unsigned(Operation::Semaphore)]==3,"Shrinking enumeration left stale per-image semaphores");
    if(success&&value==Mode::Incomplete)require(imageFills==2,"Incomplete enumeration was not retried");
    if(success&&value==Mode::FormatIncomplete)require(formatFills==2,"Incomplete format enumeration was not retried");
    if(success&&value==Mode::ModeIncomplete)require(modeFills==2,"Incomplete mode enumeration was not retried");
    if(value==Mode::FormatAlwaysIncomplete)require(formatFills==3&&!calls[unsigned(Operation::Chain)],"Format retries were not bounded before native creation");
    if(value==Mode::ModeAlwaysIncomplete)require(modeFills==3,"Present-mode retries were not bounded");
    if(value==Mode::FormatShrink)require(!calls[unsigned(Operation::Chain)],"Unwritten surface formats enabled an unsupported format");
    mirror.destroy(d);
    require(success==expected&&clean&&!live()&&!invalidDestructions&&!selectedWrongMode,"Enumeration failure lost ownership or selected unwritten entries");
}
void diagnostics(){
    reset();auto d=device();argent::DesktopMirror mirror;logThrow=true;bool success{};
    try{success=mirror.create(d,info(),60,true);}catch(...){}
    logThrow=false;mirror.destroy(d);require(success&&!live()&&!invalidDestructions,"Optional setup diagnostics changed ownership or outcome");
}
void repeatedCreation(){
    reset();auto d=device();argent::DesktopMirror mirror;require(mirror.create(d,info(),60,true),"Cannot create first mirror");
    const auto original=mirror.handle();const auto before=live();bool accepted{};
    try{accepted=mirror.create(d,info(),60,true);}catch(...){}
    const bool unchanged=!accepted&&mirror.handle()==original&&mirror.needsFrame()&&live()==before;
    mirror.destroy(d);require(unchanged&&!live()&&!invalidDestructions,"Repeated creation overwrote live mirror ownership");
}
void missingDispatch(const char* name){
    reset();missing=name;auto d=device();argent::DesktopMirror mirror;bool success{};
    try{success=mirror.create(d,info(),60,true);}catch(...){}
    require(!success&&!mirror.handle()&&!mirror.needsFrame()&&!live()&&!calls[unsigned(Operation::Chain)],"Missing required dispatch reached native creation");
}
void cachedCleanup(){
    reset();auto d=device();argent::DesktopMirror mirror;require(mirror.create(d,info(),60,true),"Cannot create dispatch-cache fixture");
    d.gdpa=nullptr;mirror.destroy(d);mirror.destroy(d);
    require(!live()&&!invalidDestructions&&idleWaits==1,"Cleanup re-resolved native ownership procedures");
}
void retryCreation(){
    reset();auto d=device();argent::DesktopMirror mirror;failure=Operation::Semaphore;failedCall=3;poison=true;
    try{mirror.create(d,info(),60,true);}catch(...){}
    require(!live()&&!mirror.handle()&&!mirror.needsFrame()&&!idleWaits,"Retry started from partial setup ownership");
    failure=Operation::None;require(mirror.create(d,info(),60,true),"Clean mirror could not retry setup");
    mirror.destroy(d);require(!live()&&!invalidDestructions,"Retry leaked or destroyed an undefined resource");
}
void missingContext(unsigned field){
    reset();auto d=device();argent::DesktopMirror mirror;
    switch(field){case 0:d.gipa=nullptr;break;case 1:d.gdpa=nullptr;break;case 2:d.graphicsQueue=VK_NULL_HANDLE;break;
        case 3:d.queueMutex.reset();break;case 4:d.device=VK_NULL_HANDLE;break;}
    require(!mirror.create(d,info(),60,true)&&!mirror.handle()&&!mirror.needsFrame()&&!live()&&!calls[unsigned(Operation::Chain)],"Missing context reached native setup");
}
}
namespace argent {void log(const std::string&){if(logThrow)throw std::bad_alloc{};}}
int main(int argc,char**){
    std::cout<<std::unitbuf;std::cerr<<std::unitbuf;
    unsigned checks{},failures{};
    const auto run=[&](auto&& test,const char* name,unsigned index){++checks;try{test();}catch(const std::exception& error){++failures;std::cerr<<name<<" case="<<index<<": "<<error.what()<<'\n';}};
    for(const auto operation:{Operation::Chain,Operation::Pool,Operation::Command,Operation::Loader,Operation::Semaphore,Operation::Fence})
        for(unsigned index=1;index<=(operation==Operation::Semaphore?4u:1u);++index)
            for(const bool poisoned:{false,true})run([&]{failedCreation(operation,index,poisoned);},"native",unsigned(operation)*10+index*2+poisoned);
    for(const bool showEye:{false,true}){
        const auto count=hostAllocation(showEye);std::cout<<"eye="<<showEye<<" allocationBoundaries="<<count<<'\n';
        for(unsigned boundary=1;boundary<=count;++boundary)run([&]{hostAllocation(showEye,boundary);},"host",boundary);
    }
    for(const auto value:{Mode::Shrink,Mode::Incomplete,Mode::AlwaysIncomplete,Mode::Empty,Mode::Excess,Mode::FormatShrink,Mode::ModeShrink,
        Mode::FormatIncomplete,Mode::FormatAlwaysIncomplete,Mode::ModeIncomplete,Mode::ModeAlwaysIncomplete,Mode::ImageCountFailed,Mode::ImageFillFailed})run([&]{enumeration(value);},"enumeration",unsigned(value));
    run(diagnostics,"diagnostics",0);run(repeatedCreation,"repeated",0);
    run(cachedCleanup,"cached-cleanup",0);run(retryCreation,"retry",0);
    if(argc>1){
        for(const auto* name:{"vkCreateSwapchainKHR","vkDestroySwapchainKHR","vkGetSwapchainImagesKHR","vkCreateCommandPool","vkDestroyCommandPool","vkAllocateCommandBuffers","vkCreateSemaphore","vkDestroySemaphore","vkCreateFence","vkDestroyFence","vkDeviceWaitIdle"})
            run([&]{missingDispatch(name);},name,0);
        for(unsigned field=0;field<5;++field)run([&]{missingContext(field);},"context",field);
    }
    std::cout<<checks<<" mirror creation checks; "<<failures<<" failures\n";return failures?1:0;
}
