#include "../src/sfs/SourceRing.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
thread_local unsigned allocationCountdown{};
bool measuring{};
unsigned allocations{},allocationFailures{};
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
using kharvox::sfs::SourceRing;
enum class Operation {None,Image,Memory,Bind,Fence};
struct Resource {uintptr_t handle{};bool live{};};
std::array<Resource,64> images,memories,fences;
unsigned imageCount{},memoryCount{},fenceCount{},imageCalls{},currentSlot{},failedSlot{},submits{},waits{},invalidDestructions{};
Operation failure{};
VkResult nativeFailure{};
bool poisonOutput{};
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class T> T handle(uintptr_t value){return reinterpret_cast<T>(value);}
template<class T> T allocate(std::array<Resource,64>& resources,unsigned& count,uintptr_t base){
    const auto value=base+count*16;resources[count++]={value,true};return handle<T>(value);
}
template<class T> void release(std::array<Resource,64>& resources,unsigned count,T value){
    for(unsigned i=0;i<count;++i)if(resources[i].handle==reinterpret_cast<uintptr_t>(value)&&resources[i].live){resources[i].live=false;return;}
    ++invalidDestructions;
}
unsigned live(const std::array<Resource,64>& resources,unsigned count){
    unsigned result{};for(unsigned i=0;i<count;++i)result+=resources[i].live;return result;
}
bool fails(Operation operation){return failure==operation&&currentSlot==failedSlot;}
VKAPI_ATTR VkResult VKAPI_CALL createImage(VkDevice,const VkImageCreateInfo* info,const VkAllocationCallbacks*,VkImage* out){
    currentSlot=imageCalls++;
    require(info->arrayLayers==2&&info->extent.width==64&&info->extent.height==64,"Stereo source dimensions changed");
    if(fails(Operation::Image)){if(poisonOutput)*out=handle<VkImage>(0xdead);return nativeFailure;}
    *out=allocate<VkImage>(images,imageCount,0x1000);return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroyImage(VkDevice,VkImage image,const VkAllocationCallbacks*){release(images,imageCount,image);}
VKAPI_ATTR void VKAPI_CALL requirements(VkDevice,VkImage,VkMemoryRequirements* out){out->size=4096;out->alignment=16;out->memoryTypeBits=1;}
VKAPI_ATTR VkResult VKAPI_CALL allocateMemory(VkDevice,const VkMemoryAllocateInfo* info,const VkAllocationCallbacks*,VkDeviceMemory* out){
    require(info->pNext&&info->memoryTypeIndex==0&&info->allocationSize==4096,"Dedicated source allocation changed");
    if(fails(Operation::Memory)){if(poisonOutput)*out=handle<VkDeviceMemory>(0xdead);return nativeFailure;}
    *out=allocate<VkDeviceMemory>(memories,memoryCount,0x2000);return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL freeMemory(VkDevice,VkDeviceMemory memory,const VkAllocationCallbacks*){release(memories,memoryCount,memory);}
VKAPI_ATTR VkResult VKAPI_CALL bindMemory(VkDevice,VkImage,VkDeviceMemory,VkDeviceSize){return fails(Operation::Bind)?nativeFailure:VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL createFence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* out){
    if(fails(Operation::Fence)){if(poisonOutput)*out=handle<VkFence>(0xdead);return nativeFailure;}
    *out=allocate<VkFence>(fences,fenceCount,0x3000);return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroyFence(VkDevice,VkFence fence,const VkAllocationCallbacks*){release(fences,fenceCount,fence);}
VKAPI_ATTR VkResult VKAPI_CALL resetFences(VkDevice,uint32_t,const VkFence*){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL waitFences(VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t){++waits;return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL submit(VkQueue,uint32_t,const VkSubmitInfo*,VkFence){++submits;return VK_SUCCESS;}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolver(VkDevice,const char* name){
#define ENTRY(api,fn) if(!std::strcmp(name,#api))return reinterpret_cast<PFN_vkVoidFunction>(fn)
    ENTRY(vkCreateImage,createImage);ENTRY(vkDestroyImage,destroyImage);ENTRY(vkGetImageMemoryRequirements,requirements);
    ENTRY(vkAllocateMemory,allocateMemory);ENTRY(vkFreeMemory,freeMemory);ENTRY(vkBindImageMemory,bindMemory);
    ENTRY(vkCreateFence,createFence);ENTRY(vkDestroyFence,destroyFence);ENTRY(vkResetFences,resetFences);
    ENTRY(vkWaitForFences,waitFences);ENTRY(vkQueueSubmit,submit);
#undef ENTRY
    return nullptr;
}
void reset(){
    images={};memories={};fences={};imageCount=memoryCount=fenceCount=imageCalls=currentSlot=failedSlot=submits=waits=invalidDestructions=0;
    allocations=allocationFailures=allocationCountdown=0;measuring=poisonOutput=false;failure=Operation::None;
}
void initialize(SourceRing& ring,bool hasMemory=true){
    VkPhysicalDeviceMemoryProperties memory{};memory.memoryTypeCount=1;
    memory.memoryTypes[0].propertyFlags=hasMemory?VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT:0;
    require(ring.initialize(handle<VkDevice>(1),handle<VkQueue>(2),resolver,memory,nullptr),"Cannot initialize source fixture");
}
VkSwapchainCreateInfoKHR info(unsigned count,VkSwapchainKHR old=VK_NULL_HANDLE){
    VkSwapchainCreateInfoKHR result{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};result.minImageCount=count;
    result.imageArrayLayers=2;result.imageExtent={64,64};result.imageFormat=VK_FORMAT_R8G8B8A8_UNORM;
    result.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;result.oldSwapchain=old;return result;
}
VkSwapchainKHR existing(SourceRing& ring,bool replacement){
    if(!replacement)return VK_NULL_HANDLE;
    VkSwapchainKHR chain{};require(ring.create(info(5),&chain)==VK_SUCCESS,"Cannot create old source");
    uint32_t image{};require(ring.acquire(chain,0,handle<VkSemaphore>(3),VK_NULL_HANDLE,&image)==VK_SUCCESS&&image==0,"Cannot acquire old image");
    imageCalls=0;return chain;
}
bool oldRetiredAndPresentable(SourceRing& ring,VkSwapchainKHR old){
    if(!old)return true;
    uint32_t index=99;const auto acquired=ring.acquire(old,0,handle<VkSemaphore>(4),VK_NULL_HANDLE,&index);
    const uint32_t held=0;
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.swapchainCount=1;present.pSwapchains=&old;present.pImageIndices=&held;
    const auto presented=ring.present(handle<VkQueue>(2),present,true,true);
    return acquired==VK_ERROR_OUT_OF_DATE_KHR&&index==99&&presented==VK_SUCCESS;
}
unsigned allocationCount(unsigned count,bool replacement){
    reset();SourceRing ring;initialize(ring);const auto old=existing(ring,replacement);
    VkSwapchainKHR chain{};measuring=true;const auto result=ring.create(info(count,old),&chain);measuring=false;
    const auto measured=allocations;
    require(result==VK_SUCCESS&&measured&&ring.owns(chain),"Cannot measure creation allocations");
    ring.clearAfterDeviceIdle();require(!live(images,imageCount)&&!invalidDestructions,"Successful creation leaked resources");return measured;
}
void creationFailure(unsigned count,bool replacement,unsigned allocation,Operation operation,unsigned slot,bool poison,bool hasMemory=true){
    reset();SourceRing ring;initialize(ring);const auto old=existing(ring,replacement);
    if(!hasMemory){ring.clearAfterDeviceIdle();initialize(ring,false);}
    failure=operation;failedSlot=slot;poisonOutput=poison;nativeFailure=VK_ERROR_OUT_OF_DEVICE_MEMORY;
    VkSwapchainKHR output=handle<VkSwapchainKHR>(0xbeef);VkResult result=VK_ERROR_UNKNOWN;bool escaped{};
    const auto beforeSubmit=submits;
    allocationCountdown=allocation;
    try{result=ring.create(info(count,hasMemory?old:VK_NULL_HANDLE),&output);}catch(...){escaped=true;}
    allocationCountdown=0;
    const auto expected=allocation?VK_ERROR_OUT_OF_HOST_MEMORY:hasMemory?nativeFailure:VK_ERROR_FEATURE_NOT_PRESENT;
    const unsigned survivors=replacement&&hasMemory?5:0;
    const bool clean=live(images,imageCount)==survivors&&live(memories,memoryCount)==survivors&&live(fences,fenceCount)==survivors&&!invalidDestructions;
    bool registry=true;
    for(unsigned i=0;i<imageCount;++i)registry=registry&&(ring.ownsImage(handle<VkImage>(images[i].handle))==(i<survivors));
    const bool noGpuWork=submits==beforeSubmit&&!waits;
    const bool oldUsable=oldRetiredAndPresentable(ring,hasMemory?old:VK_NULL_HANDLE);
    ring.clearAfterDeviceIdle();
    require(!escaped,"Creation exception escaped instead of returning a Vulkan error");
    require(result==expected&&output==VK_NULL_HANDLE,"Creation failed with the wrong result or stale output");
    require(!allocation||allocationFailures==1,"Host allocation injection was not exercised");
    require(clean&&registry,"Failed creation leaked resources or image ownership");
    require(noGpuWork,"Creation rollback submitted or waited for unsubmitted resources");
    require(oldUsable,"Replacement failure did not retire the old chain while preserving its acquired image");
    require(!live(images,imageCount)&&!live(memories,memoryCount)&&!live(fences,fenceCount),"Teardown leaked resources after failed creation");
}
void rejected(unsigned field,bool replacement){
    reset();SourceRing ring;initialize(ring);const auto old=existing(ring,replacement);
    auto request=info(2,old);VkBaseInStructure extension{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    switch(field){case 0:request.flags=VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR;break;case 1:request.pNext=&extension;break;
        case 2:request.imageArrayLayers=1;break;case 3:request.imageExtent.width=0;break;case 4:request.imageExtent.height=0;break;case 5:request.minImageCount=6;break;}
    VkSwapchainKHR output=handle<VkSwapchainKHR>(0xbeef);
    const auto result=ring.create(request,&output);
    const bool oldUsable=oldRetiredAndPresentable(ring,old);
    ring.clearAfterDeviceIdle();
    require(result==VK_ERROR_FEATURE_NOT_PRESENT&&output==VK_NULL_HANDLE&&!imageCalls,"Unsupported creation produced resources or a stale output");
    require(oldUsable,"Unsupported replacement did not retire the old chain");
    require(!live(images,imageCount)&&!invalidDestructions,"Rejected creation leaked resources");
}
void invalidOld(){
    reset();SourceRing ring;initialize(ring);const auto old=existing(ring,true);
    VkSwapchainKHR output=handle<VkSwapchainKHR>(0xbeef);
    const auto foreignResult=ring.create(info(2,handle<VkSwapchainKHR>(0xf00)),&output);
    const bool foreignRejected=foreignResult==VK_ERROR_INITIALIZATION_FAILED&&output==VK_NULL_HANDLE&&!imageCalls;
    uint32_t index{};const bool unrelatedUsable=ring.acquire(old,0,handle<VkSemaphore>(4),VK_NULL_HANDLE,&index)==VK_SUCCESS;
    VkSwapchainKHR newer{};const bool recreated=ring.create(info(2,old),&newer)==VK_SUCCESS;
    output=handle<VkSwapchainKHR>(0xbeef);
    const bool retiredRejected=ring.create(info(2,old),&output)==VK_ERROR_INITIALIZATION_FAILED&&output==VK_NULL_HANDLE;
    const bool nullRejected=ring.create(info(2),nullptr)==VK_ERROR_FEATURE_NOT_PRESENT;
    ring.clearAfterDeviceIdle();require(!live(images,imageCount)&&!invalidDestructions,"Invalid-old checks leaked resources");
    require(foreignRejected&&unrelatedUsable,"Foreign replacement changed an unrelated old chain");
    require(recreated&&retiredRejected&&nullRejected,"Creation accepted a retired old chain or null output");
}
}
int main(){
    unsigned checks{},failures{};
    const auto run=[&](auto&& test,const char* group,unsigned index){
        ++checks;try{test();}catch(const std::exception& error){++failures;std::cerr<<group<<" case="<<index<<": "<<error.what()<<" live="<<live(images,imageCount)<<" invalidDestructions="<<invalidDestructions<<'\n';}
    };
    for(const bool replacement:{false,true}){
        for(const unsigned count:{2u,5u}){
            const auto boundaries=allocationCount(count,replacement);
            std::cout<<"images="<<count<<" replacement="<<replacement<<" allocationBoundaries="<<boundaries<<'\n';
            for(unsigned boundary=1;boundary<=boundaries;++boundary)run([&]{creationFailure(count,replacement,boundary,Operation::None,0,false);},"host-allocation",boundary);
        }
        for(const auto operation:{Operation::Image,Operation::Memory,Operation::Bind,Operation::Fence})
            for(unsigned slot=0;slot<5;++slot)for(const bool poison:{false,true})
                run([&]{creationFailure(5,replacement,0,operation,slot,poison);},"native-allocation",unsigned(operation)*10+slot*2+poison);
        for(unsigned field=0;field<6;++field)run([&]{rejected(field,replacement);},"unsupported",field);
    }
    run([]{creationFailure(5,false,0,Operation::None,0,false,false);},"memory-type",0);
    run(invalidOld,"old-chain",0);
    std::cout<<checks<<" source creation checks; "<<failures<<" failures\n";return failures?1:0;
}
