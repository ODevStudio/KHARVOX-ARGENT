#include "../src/StereoReadback.h"
#include "../src/openxr/WeaponHandling.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>

namespace argent {void log(const std::string& message){std::cout<<message<<'\n';}}
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static std::set<uintptr_t> live;
static uintptr_t nextHandle=10;
static bool pending{};
static unsigned queueWaits{};
static VkResult fenceResult=VK_SUCCESS,idleResult=VK_SUCCESS,submitResult=VK_SUCCESS,mapResult=VK_SUCCESS;
static VkDeviceSize bufferBytes{};
static std::vector<unsigned char> pixels;
static unsigned imageReleases{},imageDestructions{};
static std::filesystem::path fixture;
template<class T> static T create(){const auto value=nextHandle++;live.insert(value);return reinterpret_cast<T>(value);}
template<class T> static void destroy(T value){if(pending||live.erase(reinterpret_cast<uintptr_t>(value))!=1)ExitProcess(86);}
static VKAPI_ATTR VkResult VKAPI_CALL createBuffer(VkDevice,const VkBufferCreateInfo* info,const VkAllocationCallbacks*,VkBuffer* out){bufferBytes=info->size;*out=create<VkBuffer>();return VK_SUCCESS;}
static VKAPI_ATTR void VKAPI_CALL requirements(VkDevice,VkBuffer,VkMemoryRequirements* out){*out={bufferBytes,1,1};}
static VKAPI_ATTR void VKAPI_CALL properties(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties* out){out->memoryTypeCount=1;out->memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;}
static VKAPI_ATTR VkResult VKAPI_CALL allocateMemory(VkDevice,const VkMemoryAllocateInfo* info,const VkAllocationCallbacks*,VkDeviceMemory* out){pixels.resize(size_t(info->allocationSize));*out=create<VkDeviceMemory>();return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL bind(VkDevice,VkBuffer,VkDeviceMemory,VkDeviceSize){return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL createPool(VkDevice,const VkCommandPoolCreateInfo*,const VkAllocationCallbacks*,VkCommandPool* out){*out=create<VkCommandPool>();return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL allocateCommand(VkDevice,const VkCommandBufferAllocateInfo*,VkCommandBuffer* out){*out=reinterpret_cast<VkCommandBuffer>(2);return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL begin(VkCommandBuffer,const VkCommandBufferBeginInfo*){return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL end(VkCommandBuffer){return VK_SUCCESS;}
static VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*){}
static VKAPI_ATTR void VKAPI_CALL copy(VkCommandBuffer,VkImage,VkImageLayout,VkBuffer,uint32_t,const VkBufferImageCopy*){}
static VKAPI_ATTR void VKAPI_CALL upload(VkCommandBuffer,VkBuffer,VkImage,VkImageLayout,uint32_t,const VkBufferImageCopy*){}
static VKAPI_ATTR VkResult VKAPI_CALL createFence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* out){*out=create<VkFence>();return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL submit(VkQueue,uint32_t,const VkSubmitInfo*,VkFence){pending=submitResult==VK_SUCCESS;return submitResult;}
static VKAPI_ATTR VkResult VKAPI_CALL wait(VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t){if(fenceResult==VK_SUCCESS)pending=false;return fenceResult;}
static VKAPI_ATTR VkResult VKAPI_CALL idle(VkQueue){++queueWaits;if(idleResult==VK_SUCCESS||idleResult==VK_ERROR_DEVICE_LOST)pending=false;return idleResult;}
static VKAPI_ATTR VkResult VKAPI_CALL map(VkDevice,VkDeviceMemory,VkDeviceSize,VkDeviceSize,VkMemoryMapFlags,void** out){if(pending)ExitProcess(87);if(mapResult==VK_SUCCESS)*out=pixels.data();return mapResult;}
static VKAPI_ATTR void VKAPI_CALL unmap(VkDevice,VkDeviceMemory){if(pending)ExitProcess(86);}
static VKAPI_ATTR void VKAPI_CALL destroyFence(VkDevice,VkFence value,const VkAllocationCallbacks*){destroy(value);}
static VKAPI_ATTR void VKAPI_CALL destroyPool(VkDevice,VkCommandPool value,const VkAllocationCallbacks*){destroy(value);}
static VKAPI_ATTR void VKAPI_CALL destroyBuffer(VkDevice,VkBuffer value,const VkAllocationCallbacks*){destroy(value);}
static VKAPI_ATTR void VKAPI_CALL freeMemory(VkDevice,VkDeviceMemory value,const VkAllocationCallbacks*){destroy(value);}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL instanceProc(VkInstance,const char* name){check(!std::strcmp(name,"vkGetPhysicalDeviceMemoryProperties"),"Unexpected instance function");return reinterpret_cast<PFN_vkVoidFunction>(properties);}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL deviceProc(VkDevice,const char* name){
#define MAP(api,fn) if(!std::strcmp(name,#api))return reinterpret_cast<PFN_vkVoidFunction>(fn)
    MAP(vkCreateBuffer,createBuffer);MAP(vkGetBufferMemoryRequirements,requirements);
    MAP(vkAllocateMemory,allocateMemory);MAP(vkBindBufferMemory,bind);
    MAP(vkCreateCommandPool,createPool);MAP(vkAllocateCommandBuffers,allocateCommand);
    MAP(vkBeginCommandBuffer,begin);MAP(vkEndCommandBuffer,end);
    MAP(vkCmdPipelineBarrier,barrier);MAP(vkCmdCopyImageToBuffer,copy);
    MAP(vkCmdCopyBufferToImage,upload);
    MAP(vkCreateFence,createFence);MAP(vkQueueSubmit,submit);MAP(vkWaitForFences,wait);MAP(vkQueueWaitIdle,idle);
    MAP(vkMapMemory,map);MAP(vkUnmapMemory,unmap);MAP(vkDestroyFence,destroyFence);
    MAP(vkDestroyCommandPool,destroyPool);MAP(vkDestroyBuffer,destroyBuffer);MAP(vkFreeMemory,freeMemory);
#undef MAP
    throw std::runtime_error(std::string("Unexpected device function: ")+name);
}
static XRAPI_ATTR XrResult XRAPI_CALL createSwapchain(XrSession,const XrSwapchainCreateInfo*,XrSwapchain* out){*out=create<XrSwapchain>();return XR_SUCCESS;}
static XRAPI_ATTR XrResult XRAPI_CALL enumerateImages(XrSwapchain,uint32_t capacity,uint32_t* count,XrSwapchainImageBaseHeader* images){*count=1;if(capacity)reinterpret_cast<XrSwapchainImageVulkanKHR*>(images)->image=reinterpret_cast<VkImage>(3);return XR_SUCCESS;}
static XRAPI_ATTR XrResult XRAPI_CALL acquireImage(XrSwapchain,const XrSwapchainImageAcquireInfo*,uint32_t* index){*index=0;return XR_SUCCESS;}
static XRAPI_ATTR XrResult XRAPI_CALL waitImage(XrSwapchain,const XrSwapchainImageWaitInfo*){return XR_SUCCESS;}
static XRAPI_ATTR XrResult XRAPI_CALL releaseImage(XrSwapchain,const XrSwapchainImageReleaseInfo*){if(pending||idleResult==VK_ERROR_DEVICE_LOST)ExitProcess(86);++imageReleases;return XR_SUCCESS;}
static XRAPI_ATTR XrResult XRAPI_CALL destroySwapchain(XrSwapchain value){destroy(value);++imageDestructions;return XR_SUCCESS;}
namespace argent {
static Device device;
static VkDevice boundDevice{};
static VkQueue boundQueue{};
static uint32_t boundFamily{};
static XrSession session{};
struct PauseBindingsImage {XrSwapchain handle{};uint32_t width{},height{};std::vector<XrSwapchainImageVulkanKHR> images;};
static PauseBindingsImage pauseBindings;
static bool pauseBindingsReady{};
std::filesystem::path runtimePath(){return fixture;}
template<class T> static T xrProc(const char* name){
#define MAP(api,fn) if(!std::strcmp(name,#api))return reinterpret_cast<T>(fn)
    MAP(xrCreateSwapchain,createSwapchain);MAP(xrEnumerateSwapchainImages,enumerateImages);
    MAP(xrAcquireSwapchainImage,acquireImage);MAP(xrWaitSwapchainImage,waitImage);
    MAP(xrReleaseSwapchainImage,releaseImage);MAP(xrDestroySwapchain,destroySwapchain);
#undef MAP
    throw std::runtime_error(std::string("Unexpected XR function: ")+name);
}
#define VK(api) device.proc<PFN_##api>(#api)
#define XR(api) xrProc<PFN_##api>(#api)
#include "../src/PauseBindings.inc"
#undef XR
#undef VK
}
static int pauseChild(const std::string& mode){
    if(mode=="pause-unverified")idleResult=VK_ERROR_OUT_OF_HOST_MEMORY;
    if(mode=="pause-device-lost")idleResult=VK_ERROR_DEVICE_LOST;
    if(mode=="pause-submit-failed")submitResult=VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if(mode=="pause-map-failed")mapResult=VK_ERROR_MEMORY_MAP_FAILED;
    argent::device.device=reinterpret_cast<VkDevice>(1);argent::device.graphicsQueue=reinterpret_cast<VkQueue>(2);
    argent::device.gipa=instanceProc;argent::device.gdpa=deviceProc;
    argent::boundDevice=argent::device.device;argent::boundQueue=argent::device.graphicsQueue;
    const bool ready=argent::initializePauseBindings({VK_FORMAT_R8G8B8A8_SRGB});
    check(ready==(mode=="pause-normal")&&argent::pauseBindingsReady==ready,"Pause upload incorrectly reported availability");
    check(!pending,"Pause upload returned with live work");
    check(queueWaits==unsigned(submitResult==VK_SUCCESS&&mapResult==VK_SUCCESS),"Pause upload wait count changed");
    check(imageReleases==unsigned(mode=="pause-normal"||mode=="pause-submit-failed"),"Pause image released without completion");
    if(ready){
        check(live.size()==1,"Successful pause upload leaked staging resources");
        check(argent::initializePauseBindings({})&&queueWaits==1,"Pause reopened with another upload");
        argent::releasePauseBindings();
    }
    check(live.empty(),"Pause upload leaked resources");
    check(imageDestructions==unsigned(mode!="pause-map-failed"),"Pause swapchain not destroyed exactly once");
    return 0;
}
static int child(const std::string& mode){
    const bool failedFence=mode!="normal"&&mode!="submit-failed"&&mode!="map-failed";
    if(failedFence)fenceResult=VK_ERROR_OUT_OF_HOST_MEMORY;
    if(mode=="unverified")idleResult=VK_ERROR_OUT_OF_HOST_MEMORY;
    if(mode=="device-lost")idleResult=VK_ERROR_DEVICE_LOST;
    if(mode=="submit-failed")submitResult=VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if(mode=="map-failed")mapResult=VK_ERROR_MEMORY_MAP_FAILED;
    argent::Device device;device.device=reinterpret_cast<VkDevice>(1);device.graphicsQueue=reinterpret_cast<VkQueue>(2);
    device.gipa=instanceProc;device.gdpa=deviceProc;
    const auto output=std::filesystem::temp_directory_path()/("argent-transfer-"+std::to_string(GetCurrentProcessId())+".ppm");
    bool rejected=false;
    try{argent::readbackStereo(device,reinterpret_cast<VkImage>(3),{2,2},VK_FORMAT_R8G8B8A8_UNORM,output);}
    catch(const std::runtime_error&){rejected=true;}
    check(rejected==(mode!="normal"),"Readback failure was not propagated");
    check(!pending&&live.empty(),"Readback leaked resources or freed pending work");
    check(queueWaits==unsigned(failedFence),"Readback changed the normal wait count or skipped error retirement");
    if(mode=="normal"){
        std::ifstream captured(output,std::ios::binary);std::string magic;unsigned width{},height{},maximum{};
        captured>>magic>>width>>height>>maximum;
        check(magic=="P6"&&width==4&&height==2&&maximum==255,"Invalid readback output");
        captured.close();std::filesystem::remove(output);
    }
    return 0;
}
static DWORD run(const wchar_t* executable,const wchar_t* mode){
    std::wstring command=L"\""+std::wstring(executable)+L"\" "+mode+L" \""+fixture.wstring()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Cannot start transfer retirement check");
    const auto waited=WaitForSingleObject(process.hProcess,10000);
    if(waited!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,88);WaitForSingleObject(process.hProcess,5000);}
    DWORD status{};const bool exited=GetExitCodeProcess(process.hProcess,&status)!=FALSE;
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    check(waited==WAIT_OBJECT_0&&exited,"Transfer retirement check did not finish");return status;
}
int main(int argc,char** argv){try{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc==3){fixture=std::filesystem::absolute(argv[2]);return std::string(argv[1]).find("pause-")==0?pauseChild(argv[1]):child(argv[1]);}
    check(argc==2,"Missing runtime fixture path");fixture=std::filesystem::absolute(argv[1]);
    wchar_t executable[32768]{};const auto size=GetModuleFileNameW(nullptr,executable,32768);
    check(size&&size<32768,"Cannot resolve test executable");
    for(const auto* mode:{L"normal",L"recovered",L"device-lost",L"unverified",L"submit-failed",L"map-failed",L"pause-normal",L"pause-unverified",L"pause-device-lost",L"pause-submit-failed",L"pause-map-failed"}){
        const auto status=run(executable,mode);
        std::wcout<<mode<<L" exit=0x"<<std::hex<<status<<std::dec<<L'\n';
        const bool failFast=!std::wcscmp(mode,L"unverified")||!std::wcscmp(mode,L"pause-unverified");
        check(status==(failFast?0xc0000602u:0u),"Transfer released unretired GPU resources or misreported availability");
    }
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
