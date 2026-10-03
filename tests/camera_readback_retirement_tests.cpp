#include "../src/CameraCapture.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
bool failAllocation{};
unsigned allocationFailures{};
}
void* operator new(std::size_t size){
    if(failAllocation){failAllocation=false;++allocationFailures;throw std::bad_alloc{};}
    if(auto* value=std::malloc(size?size:1))return value;
    throw std::bad_alloc{};
}
void operator delete(void* value)noexcept{std::free(value);}
void operator delete(void* value,std::size_t)noexcept{std::free(value);}

namespace {
std::string mode;
std::array<uintptr_t,4> resources{};
std::vector<unsigned char> bytes;
std::filesystem::path output;
bool pending{},mapped{},lateResolver{};
unsigned queueWaits{},fenceWaits{},submits{},unmaps{},copies{},logs{};
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T>T handle(uintptr_t value){return reinterpret_cast<T>(value);}
template<class T>VkResult create(unsigned index,const char* failure,T* out){
    if(mode==failure){*out=handle<T>(0xdead);return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
    check(!resources[index],"Fixture resource already live");resources[index]=10+index;*out=handle<T>(resources[index]);return VK_SUCCESS;
}
template<class T>void destroy(unsigned index,T value){
    if(pending||resources[index]!=reinterpret_cast<uintptr_t>(value)||!resources[index])ExitProcess(86);
    resources[index]=0;
}
VKAPI_ATTR VkResult VKAPI_CALL buffer(VkDevice,const VkBufferCreateInfo* info,const VkAllocationCallbacks*,VkBuffer* out){check(info->size==160&&info->usage==VK_BUFFER_USAGE_TRANSFER_DST_BIT,"Readback buffer changed");return create(0,"buffer-failed",out);}
VKAPI_ATTR void VKAPI_CALL requirements(VkDevice,VkBuffer value,VkMemoryRequirements* out){check(value==handle<VkBuffer>(10),"Invalid staging buffer");*out={160,1,1};}
VKAPI_ATTR void VKAPI_CALL properties(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties* out){out->memoryTypeCount=1;out->memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;}
VKAPI_ATTR VkResult VKAPI_CALL allocate(VkDevice,const VkMemoryAllocateInfo* info,const VkAllocationCallbacks*,VkDeviceMemory* out){check(info->allocationSize==160&&!info->memoryTypeIndex,"Readback allocation changed");bytes.resize(160);return create(1,"memory-failed",out);}
VKAPI_ATTR VkResult VKAPI_CALL bind(VkDevice,VkBuffer,VkDeviceMemory,VkDeviceSize offset){check(!offset,"Readback memory offset changed");return mode=="bind-failed"?VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL pool(VkDevice,const VkCommandPoolCreateInfo* info,const VkAllocationCallbacks*,VkCommandPool* out){check(info->queueFamilyIndex==3,"Readback queue family changed");return create(2,"pool-failed",out);}
VKAPI_ATTR VkResult VKAPI_CALL commands(VkDevice,const VkCommandBufferAllocateInfo* info,VkCommandBuffer* out){check(info->commandPool==handle<VkCommandPool>(12)&&info->commandBufferCount==1&&info->level==VK_COMMAND_BUFFER_LEVEL_PRIMARY,"Readback commands changed");*out=handle<VkCommandBuffer>(2);return mode=="commands-failed"?VK_ERROR_OUT_OF_HOST_MEMORY:VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL loader(VkDevice,void*){return mode=="loader-failed"?VK_ERROR_INITIALIZATION_FAILED:VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL begin(VkCommandBuffer,const VkCommandBufferBeginInfo*){return mode=="begin-failed"?VK_ERROR_OUT_OF_HOST_MEMORY:VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL end(VkCommandBuffer){return mode=="end-failed"?VK_ERROR_OUT_OF_HOST_MEMORY:VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*){}
VKAPI_ATTR void VKAPI_CALL copy(VkCommandBuffer,VkBuffer source,VkBuffer destination,uint32_t count,const VkBufferCopy* region){
    if(mode=="record-throws")throw 7;
    const bool first=source==handle<VkBuffer>(50);check(first||source==handle<VkBuffer>(51),"Unknown source buffer");
    check(destination==handle<VkBuffer>(10)&&count==1&&region->srcOffset==(first?16u:32u)&&region->dstOffset==(first?0u:64u)&&region->size==(first?64u:96u),"Readback copy changed");
    std::fill_n(bytes.begin()+size_t(region->dstOffset),size_t(region->size),first?0x21:0x42);++copies;
}
VKAPI_ATTR VkResult VKAPI_CALL fence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* out){return create(3,"fence-failed",out);}
VKAPI_ATTR VkResult VKAPI_CALL submit(VkQueue,uint32_t count,const VkSubmitInfo* info,VkFence value){check(count==1&&info->commandBufferCount==1&&value==handle<VkFence>(13),"Readback submit changed");++submits;pending=mode!="submit-failed";lateResolver=mode=="resolver-late";return pending?VK_SUCCESS:VK_ERROR_OUT_OF_DEVICE_MEMORY;}
VKAPI_ATTR VkResult VKAPI_CALL wait(VkDevice,uint32_t count,const VkFence*,VkBool32 all,uint64_t timeout){
    check(count==1&&all&&timeout==UINT64_MAX,"Readback fence wait changed");++fenceWaits;
    if(mode=="recovered"||mode=="recovered-log"||mode=="device-lost"||mode=="unverified"||mode=="unverified-log")return VK_ERROR_OUT_OF_HOST_MEMORY;
    pending=false;return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL idle(VkQueue){
    ++queueWaits;if(mode=="unverified"||mode=="unverified-log")return VK_ERROR_OUT_OF_HOST_MEMORY;
    pending=false;return mode=="device-lost"?VK_ERROR_DEVICE_LOST:VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL map(VkDevice,VkDeviceMemory,VkDeviceSize offset,VkDeviceSize size,VkMemoryMapFlags,void** out){
    if(pending)ExitProcess(87);check(!offset&&size==160,"Readback map changed");
    if(mode=="map-failed"){*out=handle<void*>(0xdead);return VK_ERROR_MEMORY_MAP_FAILED;}
    mapped=true;*out=bytes.data();failAllocation=mode=="host-oom";return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL unmap(VkDevice,VkDeviceMemory){if(pending||!mapped)ExitProcess(86);mapped=false;++unmaps;}
VKAPI_ATTR void VKAPI_CALL destroyBuffer(VkDevice,VkBuffer value,const VkAllocationCallbacks*){destroy(0,value);}
VKAPI_ATTR void VKAPI_CALL freeMemory(VkDevice,VkDeviceMemory value,const VkAllocationCallbacks*){destroy(1,value);mapped=false;}
VKAPI_ATTR void VKAPI_CALL destroyPool(VkDevice,VkCommandPool value,const VkAllocationCallbacks*){destroy(2,value);}
VKAPI_ATTR void VKAPI_CALL destroyFence(VkDevice,VkFence value,const VkAllocationCallbacks*){destroy(3,value);}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL instanceProc(VkInstance,const char* name){check(!std::strcmp(name,"vkGetPhysicalDeviceMemoryProperties"),"Unexpected instance procedure");return reinterpret_cast<PFN_vkVoidFunction>(properties);}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL deviceProc(VkDevice,const char* name){
    if(lateResolver)throw std::runtime_error("Late cleanup resolver failed");
#define ENTRY(api,fn) if(!std::strcmp(name,#api))return reinterpret_cast<PFN_vkVoidFunction>(fn)
    ENTRY(vkCreateBuffer,buffer);ENTRY(vkGetBufferMemoryRequirements,requirements);ENTRY(vkAllocateMemory,allocate);ENTRY(vkBindBufferMemory,bind);
    ENTRY(vkCreateCommandPool,pool);ENTRY(vkAllocateCommandBuffers,commands);ENTRY(vkBeginCommandBuffer,begin);ENTRY(vkEndCommandBuffer,end);
    ENTRY(vkCmdPipelineBarrier,barrier);ENTRY(vkCmdCopyBuffer,copy);ENTRY(vkCreateFence,fence);ENTRY(vkQueueSubmit,submit);ENTRY(vkWaitForFences,wait);ENTRY(vkQueueWaitIdle,idle);
    ENTRY(vkMapMemory,map);ENTRY(vkUnmapMemory,unmap);ENTRY(vkDestroyBuffer,destroyBuffer);ENTRY(vkFreeMemory,freeMemory);ENTRY(vkDestroyCommandPool,destroyPool);ENTRY(vkDestroyFence,destroyFence);
#undef ENTRY
    throw std::runtime_error("Unexpected device procedure");
}
}
namespace argent {
void log(const std::string&){++logs;if(mode=="success-log"||mode=="recovered-log"||mode=="unverified-log")throw std::runtime_error("Log failed");if(mode=="success-log-int")throw 7;}
namespace sfs {bool vrEnabled(){return true;}}
namespace trace {uint64_t currentFrame(){return 7;}}
namespace camera {void observeRenderedCamera(const void*,size_t){}}
struct CameraCaptureTestAccess {
    static void prepare(CameraCapture& camera,bool probe){
        auto& requests=probe?camera.pendingProbeReadback:camera.pendingReadback;
        requests.push_back({handle<VkBuffer>(50),16,{},0,0,64,output/"first.bin"});
        requests.push_back({handle<VkBuffer>(51),32,{},0,0,96,output/"second.bin"});
        if(probe)camera.probeGpuReady=true;
    }
};
}
namespace {
int child(bool probe){
    output=std::filesystem::temp_directory_path()/("argent-camera-readback-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    check(std::filesystem::create_directory(output),"Camera fixture directory already exists");if(mode=="file-failed")std::filesystem::create_directory(output/"first.bin");
    argent::CameraCapture camera;argent::Device device;device.device=handle<VkDevice>(1);device.gdpa=deviceProc;device.gipa=instanceProc;device.setLoaderData=loader;
    if(mode!="empty")argent::CameraCaptureTestAccess::prepare(camera,probe);
    bool escaped=false;try{camera.readback(device,handle<VkQueue>(3),3,probe);}catch(...){escaped=true;}
    failAllocation=false;check(!escaped,"Optional camera readback exception escaped");
    check(!pending&&!mapped,"Camera readback returned with pending GPU work or mapped memory");
    for(const auto resource:resources)check(!resource,"Camera readback leaked native resources");
    check(!camera.gpuProbePending(),"Camera readback retained drained requests");
    const bool recovery=mode=="recovered"||mode=="recovered-log"||mode=="device-lost"||mode=="resolver-late";
    check(queueWaits==unsigned(recovery),"Camera readback wait count changed");
    if(mode=="normal"||mode=="success-log"||mode=="success-log-int"){
        std::ifstream first(output/"first.bin",std::ios::binary),second(output/"second.bin",std::ios::binary);
        const std::vector<unsigned char> a((std::istreambuf_iterator<char>(first)),{}),b((std::istreambuf_iterator<char>(second)),{});
        check(a==std::vector<unsigned char>(64,0x21),"Camera first readback bytes changed");
        check(b==std::vector<unsigned char>(96,0x42),"Camera second readback bytes changed");
        check(fenceWaits==1&&submits==1&&unmaps==1&&copies==2,"Normal camera readback work changed");
    }
    if(recovery)check(!std::filesystem::exists(output/"first.bin"),"Failed readback exported pixels");
    if(mode=="host-oom")check(allocationFailures==1,"Camera host-OOM injection was not exercised");
    lateResolver=false;camera.readback(device,handle<VkQueue>(3),3,probe);check(queueWaits==unsigned(recovery),"Camera readback retried drained requests");
    return 0;
}
DWORD run(const wchar_t* executable,const char* scenario,bool probe){
    const auto name=std::wstring(scenario,scenario+std::strlen(scenario));std::wstring command=L"\""+std::wstring(executable)+L"\" "+name+(probe?L" probe":L" camera");
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Cannot start camera retirement check");
    const auto waited=WaitForSingleObject(process.hProcess,5000);
    if(waited!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,88);WaitForSingleObject(process.hProcess,5000);}
    DWORD status{};const bool exited=GetExitCodeProcess(process.hProcess,&status)!=FALSE;CloseHandle(process.hThread);CloseHandle(process.hProcess);
    check(waited==WAIT_OBJECT_0&&exited,"Camera readback check did not finish");return status;
}
}
int main(int argc,char** argv){try{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc==3){mode=argv[1];return child(!std::strcmp(argv[2],"probe"));}
    wchar_t executable[32768]{};const auto length=GetModuleFileNameW(nullptr,executable,std::size(executable));check(length&&length<std::size(executable),"Cannot resolve camera test executable");
    unsigned scenarios{},failures{};
    for(const auto* scenario:{"normal","empty","recovered","recovered-log","device-lost","unverified","unverified-log","success-log","success-log-int","buffer-failed","memory-failed","bind-failed","pool-failed","commands-failed","loader-failed","begin-failed","end-failed","fence-failed","record-throws","submit-failed","map-failed","host-oom","file-failed","resolver-late"})for(const bool probe:{false,true}){
        ++scenarios;const auto status=run(executable,scenario,probe);const bool failFast=!std::strcmp(scenario,"unverified")||!std::strcmp(scenario,"unverified-log");
        const bool passed=status==(failFast?0xc0000602u:0u);failures+=!passed;
        std::cout<<scenario<<" probe="<<probe<<" exit=0x"<<std::hex<<status<<std::dec<<(passed?" PASS":" FAIL")<<'\n';
    }
    std::cout<<scenarios<<" camera readback retirement scenarios, "<<failures<<" failures\n";return failures?1:0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
