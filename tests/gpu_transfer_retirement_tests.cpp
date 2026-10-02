#include "../src/StereoReadback.h"
#include <array>
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
static std::array<unsigned char,32> pixels{};
template<class T> static T create(){const auto value=nextHandle++;live.insert(value);return reinterpret_cast<T>(value);}
template<class T> static void destroy(T value){if(pending||live.erase(reinterpret_cast<uintptr_t>(value))!=1)ExitProcess(86);}
static VKAPI_ATTR VkResult VKAPI_CALL createBuffer(VkDevice,const VkBufferCreateInfo*,const VkAllocationCallbacks*,VkBuffer* out){*out=create<VkBuffer>();return VK_SUCCESS;}
static VKAPI_ATTR void VKAPI_CALL requirements(VkDevice,VkBuffer,VkMemoryRequirements* out){*out={32,1,1};}
static VKAPI_ATTR void VKAPI_CALL properties(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties* out){out->memoryTypeCount=1;out->memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;}
static VKAPI_ATTR VkResult VKAPI_CALL allocateMemory(VkDevice,const VkMemoryAllocateInfo*,const VkAllocationCallbacks*,VkDeviceMemory* out){*out=create<VkDeviceMemory>();return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL bind(VkDevice,VkBuffer,VkDeviceMemory,VkDeviceSize){return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL createPool(VkDevice,const VkCommandPoolCreateInfo*,const VkAllocationCallbacks*,VkCommandPool* out){*out=create<VkCommandPool>();return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL allocateCommand(VkDevice,const VkCommandBufferAllocateInfo*,VkCommandBuffer* out){*out=reinterpret_cast<VkCommandBuffer>(2);return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL begin(VkCommandBuffer,const VkCommandBufferBeginInfo*){return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL end(VkCommandBuffer){return VK_SUCCESS;}
static VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*){}
static VKAPI_ATTR void VKAPI_CALL copy(VkCommandBuffer,VkImage,VkImageLayout,VkBuffer,uint32_t,const VkBufferImageCopy*){}
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
    MAP(vkCreateFence,createFence);MAP(vkQueueSubmit,submit);MAP(vkWaitForFences,wait);MAP(vkQueueWaitIdle,idle);
    MAP(vkMapMemory,map);MAP(vkUnmapMemory,unmap);MAP(vkDestroyFence,destroyFence);
    MAP(vkDestroyCommandPool,destroyPool);MAP(vkDestroyBuffer,destroyBuffer);MAP(vkFreeMemory,freeMemory);
#undef MAP
    throw std::runtime_error(std::string("Unexpected device function: ")+name);
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
    std::wstring command=L"\""+std::wstring(executable)+L"\" "+mode;
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
    if(argc==2)return child(argv[1]);
    wchar_t executable[32768]{};const auto size=GetModuleFileNameW(nullptr,executable,32768);
    check(size&&size<32768,"Cannot resolve test executable");
    for(const auto* mode:{L"normal",L"recovered",L"device-lost",L"unverified",L"submit-failed",L"map-failed"}){
        const auto status=run(executable,mode);
        std::wcout<<mode<<L" exit=0x"<<std::hex<<status<<std::dec<<L'\n';
        check(status==(std::wcscmp(mode,L"unverified")?0u:0xc0000602u),"Readback released unretired GPU resources");
    }
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
