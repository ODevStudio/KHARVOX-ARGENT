#include "../src/QuadRuntime.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>
#include <unordered_map>

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
enum class Mode {Normal,NativeFailed,ConfigureFailed,ConfigureThrow,CountFailed,FillFailed,Incomplete,Shrink,Empty,TrackThrow,
    MirrorFailed,MirrorThrow,MirrorOtherThrow,CancelThrow,MissingQueue,MissingCreate,MissingDestroy,MissingImages,MissingMemory,
    MissingGipa,MissingGdpa,NullSuccess,AlwaysIncomplete,ExcessCount,MirrorDestroyThrow,RetireThrow};
struct Scenario {bool source{true},game{true};Mode mode{Mode::Normal};const char* throwingLog{};};
Scenario scenario;
bool active{},liveChain{},tracked{},mirrorLive{};
bool oldOwned{},oldRetired{};
unsigned created{},destroyed{},enumerations{},trackedCount{},mirrorCreates{},mirrorDestroys{},cancels{},retirements{},logsThrown{};
VkAllocationCallbacks allocator;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class T> T handle(uintptr_t value){return reinterpret_cast<T>(value);}
VkSwapchainKHR chain(){return handle<VkSwapchainKHR>(0x100);}
VKAPI_ATTR VkResult VKAPI_CALL createNative(VkDevice,const VkSwapchainCreateInfoKHR* info,const VkAllocationCallbacks* callbacks,VkSwapchainKHR* out){
    require(info->imageArrayLayers==(scenario.source?2u:1u),"Swapchain layer count changed");
    require(callbacks==(scenario.source?nullptr:&allocator),"Native allocation callbacks changed");
    if(scenario.mode==Mode::NativeFailed){*out=handle<VkSwapchainKHR>(0xdead);return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
    if(scenario.mode==Mode::NullSuccess){*out=VK_NULL_HANDLE;return VK_SUCCESS;}
    ++created;liveChain=true;*out=chain();return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroyNative(VkDevice,VkSwapchainKHR value,const VkAllocationCallbacks* callbacks){
    require(liveChain&&value==chain(),"Unowned swapchain was destroyed");
    require(callbacks==(scenario.source?nullptr:&allocator),"Destruction allocation callbacks changed");
    liveChain=false;++destroyed;
}
VKAPI_ATTR VkResult VKAPI_CALL images(VkDevice,VkSwapchainKHR value,uint32_t* count,VkImage* out){
    require(value==chain()&&liveChain,"Enumeration used an unowned chain");
    if(!out){if(scenario.mode==Mode::CountFailed)return VK_ERROR_DEVICE_LOST;*count=scenario.mode==Mode::Empty?0:3;return VK_SUCCESS;}
    ++enumerations;
    if(scenario.mode==Mode::FillFailed){*count=0;return VK_ERROR_OUT_OF_HOST_MEMORY;}
    const auto size=scenario.mode==Mode::Shrink?2u:3u;
    const auto written=(std::min)(*count,size);
    for(unsigned i=0;i<written;++i)out[i]=handle<VkImage>(0x200+i);
    *count=written;
    if(scenario.mode==Mode::ExcessCount)*count=4;
    if(scenario.mode==Mode::AlwaysIncomplete)return VK_INCOMPLETE;
    if(scenario.mode==Mode::Incomplete&&enumerations==1)return VK_INCOMPLETE;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL memoryProperties(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties*){}
VKAPI_ATTR VkResult VKAPI_CALL surfaceProperties(VkPhysicalDevice,VkSurfaceKHR,VkSurfaceCapabilitiesKHR* out){out->supportedUsageFlags=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;return VK_SUCCESS;}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL instanceProc(VkInstance,const char* name){
    if(!std::strcmp(name,"vkGetPhysicalDeviceMemoryProperties"))return scenario.mode==Mode::MissingMemory?nullptr:reinterpret_cast<PFN_vkVoidFunction>(memoryProperties);
    if(!std::strcmp(name,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))return reinterpret_cast<PFN_vkVoidFunction>(surfaceProperties);
    return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL deviceProc(VkDevice,const char* name){
    if(!std::strcmp(name,"vkCreateSwapchainKHR"))return scenario.mode==Mode::MissingCreate?nullptr:reinterpret_cast<PFN_vkVoidFunction>(createNative);
    if(!std::strcmp(name,"vkDestroySwapchainKHR"))return scenario.mode==Mode::MissingDestroy?nullptr:reinterpret_cast<PFN_vkVoidFunction>(destroyNative);
    if(!std::strcmp(name,"vkGetSwapchainImagesKHR"))return scenario.mode==Mode::MissingImages?nullptr:reinterpret_cast<PFN_vkVoidFunction>(images);
    return nullptr;
}
}
namespace argent {
class DesktopMirror {
public:
    bool create(Device&,const VkSwapchainCreateInfoKHR&){
        ++mirrorCreates;mirrorLive=true;
        if(scenario.mode==Mode::MirrorFailed)return false;
        if(scenario.mode==Mode::MirrorThrow)throw std::runtime_error("mirror failure");
        if(scenario.mode==Mode::MirrorOtherThrow)throw 7;
        return true;
    }
    void destroy(Device&){
        if(scenario.mode==Mode::MirrorDestroyThrow)throw std::runtime_error("mirror retirement failure");
        if(mirrorLive){mirrorLive=false;++mirrorDestroys;}
    }
};
void log(const std::string& value){if(scenario.throwingLog&&value.rfind(scenario.throwingLog,0)==0){++logsThrown;throw std::bad_alloc{};}}
void cancelStereoFrame(){++cancels;if(scenario.mode==Mode::CancelThrow)throw std::runtime_error("cancel failure");}
void retireStereoSources(VkDevice){++retirements;if(scenario.mode==Mode::RetireThrow)throw std::runtime_error("source retirement failure");}
namespace sfs {
bool sourceRingRequested(){return scenario.source;}
bool sourceRingActive(VkDevice){return active;}
bool configureSourceRing(VkDevice,PFN_vkGetDeviceProcAddr,const VkPhysicalDeviceMemoryProperties&,VkQueue,std::recursive_mutex*){
    if(scenario.mode==Mode::ConfigureThrow)throw std::bad_alloc{};
    if(scenario.mode==Mode::ConfigureFailed)return false;
    active=true;return true;
}
bool sourceSwapchain(VkDevice,VkSwapchainKHR value){return scenario.source&&((liveChain&&value==chain())||(oldOwned&&value==handle<VkSwapchainKHR>(0x300)));}
VkResult createSourceSwapchain(VkDevice device,const VkSwapchainCreateInfoKHR& info,VkSwapchainKHR* out){
    if(info.oldSwapchain&&oldOwned)oldRetired=true;
    return createNative(device,&info,nullptr,out);
}
VkResult sourceImages(VkDevice device,VkSwapchainKHR value,uint32_t* count,VkImage* out){return images(device,value,count,out);}
void swapchainImages(VkDevice,VkSwapchainKHR,uint32_t count,const VkImage*){tracked=true;trackedCount=count;if(scenario.mode==Mode::TrackThrow)throw std::bad_alloc{};}
void swapchainDestroyed(VkDevice,VkSwapchainKHR){tracked=false;}
void destroySourceSwapchain(VkDevice device,VkSwapchainKHR value){require(!tracked,"Native source destroyed before image registration withdrawal");destroyNative(device,value,nullptr);}
}
}
namespace {
struct State:argent::Device {
    bool game{true},sfs{true};
    std::vector<uint32_t> queueFamilies{0,1};
    std::unordered_map<VkSwapchainKHR,argent::Source> sources;
    std::unordered_map<VkSwapchainKHR,std::unique_ptr<argent::DesktopMirror>> mirrors;
};
std::shared_ptr<State> current;
std::recursive_mutex stateMutex;
template<class T> void* key(T value){return reinterpret_cast<void*>(value);}
std::shared_ptr<State> deviceOf(void*){return current;}
}
extern "C" {
#include "../src/LayerSwapchainLifecycle.inc"
}
namespace {
void reset(const Scenario& value){
    scenario=value;active=liveChain=tracked=mirrorLive=oldOwned=oldRetired=false;
    created=destroyed=enumerations=trackedCount=mirrorCreates=mirrorDestroys=cancels=retirements=logsThrown=0;
    allocationCountdown=allocations=allocationFailures=0;measuring=false;
    current=std::make_shared<State>();current->device=handle<VkDevice>(1);current->graphicsQueue=handle<VkQueue>(2);
    current->gipa=instanceProc;current->gdpa=deviceProc;current->game=value.game;
    if(value.mode==Mode::MissingQueue)current->graphicsQueue=VK_NULL_HANDLE;
    if(value.mode==Mode::MissingGipa)current->gipa=nullptr;
    if(value.mode==Mode::MissingGdpa)current->gdpa=nullptr;
}
bool expectedFailure(){
    return scenario.mode==Mode::NativeFailed||scenario.mode==Mode::ConfigureFailed||scenario.mode==Mode::ConfigureThrow
        ||scenario.mode==Mode::CountFailed||scenario.mode==Mode::FillFailed||scenario.mode==Mode::Empty
        ||scenario.mode==Mode::TrackThrow||scenario.mode==Mode::MissingQueue||scenario.mode==Mode::MissingCreate
        ||scenario.mode==Mode::MissingDestroy||scenario.mode==Mode::MissingImages||scenario.mode==Mode::MissingMemory
        ||scenario.mode==Mode::MissingGipa||scenario.mode==Mode::MissingGdpa||scenario.mode==Mode::NullSuccess
        ||scenario.mode==Mode::AlwaysIncomplete||scenario.mode==Mode::ExcessCount;
}
void checkCreation(const Scenario& value,unsigned allocation=0,bool measure=false){
    reset(value);
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};info.imageArrayLayers=1;
    info.imageFormat=VK_FORMAT_R8G8B8A8_UNORM;info.imageExtent={64,64};info.minImageCount=3;
    const auto original=info;VkSwapchainKHR output=handle<VkSwapchainKHR>(0xbeef);
    VkResult result=VK_ERROR_UNKNOWN;bool escaped{};
    allocationCountdown=allocation;measuring=measure;
    try{result=vkCreateSwapchainKHR(current->device,&info,&allocator,&output);}catch(...){escaped=true;}
    allocationCountdown=0;measuring=false;
    require(!escaped,"Exception escaped swapchain creation");
    require(!std::memcmp(&info,&original,sizeof(info)),"Caller create-info changed");
    if(allocation)require(allocationFailures==1,"Allocation injection was not exercised");
    if(value.throwingLog)require(logsThrown>0,"Diagnostic injection was not exercised");
    if(expectedFailure()||result!=VK_SUCCESS){
        require(result!=VK_SUCCESS&&output==VK_NULL_HANDLE,"Creation failure retained a handle or returned success");
        if(allocation)require(result==VK_ERROR_OUT_OF_HOST_MEMORY,"Allocation failure returned the wrong error");
        if(value.mode==Mode::NativeFailed)require(result==VK_ERROR_OUT_OF_DEVICE_MEMORY,"Native failure changed");
        if(value.mode==Mode::CountFailed)require(result==VK_ERROR_DEVICE_LOST,"Enumeration failure changed");
        if(value.mode==Mode::FillFailed)require(result==VK_ERROR_OUT_OF_HOST_MEMORY,"Image-fill failure changed");
        require(!liveChain&&!tracked&&!mirrorLive&&current->sources.empty()&&current->mirrors.empty(),"Failed creation leaked ownership");
        require(created==destroyed,"Failed creation did not destroy exactly the owned handle");
        return;
    }
    require(output==chain()&&liveChain&&created==1&&!destroyed,"Successful creation lost native ownership");
    if(value.game||value.source){
        if(value.mode==Mode::Incomplete)require(enumerations==2,"Incomplete image enumeration was not retried");
        require(current->sources.size()==1,"Source metadata was not registered");
        const auto& source=current->sources.at(output);
        require(source.images.size()==(value.mode==Mode::Shrink?2u:3u)&&source.transferable,"Source enumeration did not commit the returned image count");
        require(source.extent.width==64&&source.format==info.imageFormat&&source.displaySrgb,"Source display metadata changed");
        if(value.source)require(tracked&&trackedCount==source.images.size(),"Stereo image tracking changed");
    }else require(current->sources.empty()&&!enumerations,"Non-game creation added image tracking");
    if(value.source&&value.mode!=Mode::MirrorFailed&&value.mode!=Mode::MirrorThrow&&value.mode!=Mode::MirrorOtherThrow&&!allocation)
        require(mirrorLive&&current->mirrors.size()==1,"Successful optional mirror was lost");
    if(value.mode==Mode::MirrorFailed||value.mode==Mode::MirrorThrow||value.mode==Mode::MirrorOtherThrow)
        require(!mirrorLive&&current->mirrors.empty()&&mirrorDestroys==1,"Failed mirror retained partial ownership");
    bool destroyEscaped{};try{vkDestroySwapchainKHR(current->device,output,&allocator);}catch(...){destroyEscaped=true;}
    require(!destroyEscaped,"Cancellation exception escaped swapchain destruction");
    require(!liveChain&&!tracked&&!mirrorLive&&destroyed==1&&current->sources.empty()&&current->mirrors.empty(),"Destruction skipped native or registry ownership");
    require(cancels==unsigned(value.source)&&retirements==unsigned(value.source),"Source retirement changed");
}
void replacementFailure(bool foreign){
    reset({true,true,Mode::NativeFailed});oldOwned=!foreign;
    const auto old=handle<VkSwapchainKHR>(0x300);
    auto mirror=std::make_unique<argent::DesktopMirror>();VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    mirror->create(*current,info);current->mirrors.emplace(old,std::move(mirror));
    info.oldSwapchain=old;info.imageArrayLayers=1;info.imageExtent={64,64};
    VkSwapchainKHR out=handle<VkSwapchainKHR>(0xbeef);
    const auto result=vkCreateSwapchainKHR(current->device,&info,&allocator,&out);
    require(result==VK_ERROR_OUT_OF_DEVICE_MEMORY&&out==VK_NULL_HANDLE&&!liveChain,"Failed replacement published ownership");
    require(oldRetired==!foreign&&mirrorDestroys==unsigned(!foreign)&&mirrorLive==foreign,"Replacement changed unrelated mirror ownership or retained a retired mirror");
    for(auto& entry:current->mirrors)entry.second->destroy(*current);
}
void invalidInput(){
    reset({});const auto device=current->device;
    VkSwapchainKHR out=handle<VkSwapchainKHR>(0xbeef);
    require(vkCreateSwapchainKHR(device,nullptr,&allocator,&out)==VK_ERROR_INITIALIZATION_FAILED&&out==VK_NULL_HANDLE,"Null creation input was not rejected");
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    require(vkCreateSwapchainKHR(device,&info,&allocator,nullptr)==VK_ERROR_INITIALIZATION_FAILED,"Null output was not rejected");
    vkDestroySwapchainKHR(device,VK_NULL_HANDLE,&allocator);
    current.reset();out=handle<VkSwapchainKHR>(0xbeef);
    require(vkCreateSwapchainKHR(device,&info,&allocator,&out)==VK_ERROR_DEVICE_LOST&&out==VK_NULL_HANDLE,"Missing device reached native creation");
    require(!created&&!destroyed,"Invalid input reached native dispatch");
}
void retirementFailure(const wchar_t* argument){
    std::array<wchar_t,32768> path{};require(GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()))>0,"Cannot locate retirement fixture");
    auto command=L"\""+std::wstring(path.data())+L"\" "+argument;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    require(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Cannot start retirement fixture");
    const auto waited=WaitForSingleObject(process.hProcess,5000);
    if(waited!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,88);WaitForSingleObject(process.hProcess,INFINITE);}
    DWORD code{};const bool read=GetExitCodeProcess(process.hProcess,&code)!=FALSE;
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    require(waited==WAIT_OBJECT_0&&read&&code==0xc0000602,"Unverified retirement did not fail fast before resource destruction");
}
}
int main(int argc,char** argv){
    std::cout<<std::unitbuf;std::cerr<<std::unitbuf;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    SetEnvironmentVariableA("ARGENT_FSR1","0");
    if(argc>1){checkCreation({true,true,!std::strcmp(argv[1],"mirror-retirement")?Mode::MirrorDestroyThrow:Mode::RetireThrow});return 88;}
    unsigned checks{},failures{};
    const auto run=[&](const Scenario& value,unsigned allocation=0){
        ++checks;try{checkCreation(value,allocation);}catch(const std::exception& error){++failures;std::cerr<<"source="<<value.source<<" mode="<<unsigned(value.mode)<<" allocation="<<allocation<<": "<<error.what()<<'\n';}
    };
    for(const bool source:{false,true}){
        for(const auto mode:{Mode::Normal,Mode::NativeFailed,Mode::CountFailed,Mode::FillFailed,Mode::Incomplete,Mode::Shrink,Mode::Empty,Mode::NullSuccess,Mode::AlwaysIncomplete,Mode::ExcessCount})run({source,true,mode});
        run({source,true,Mode::Normal,source?"SFS_SOURCE_SWAPCHAIN":"SWAPCHAIN"});
        const Scenario value{source,true};checkCreation(value,0,true);const auto boundaries=allocations;
        std::cout<<"source="<<source<<" allocationBoundaries="<<boundaries<<'\n';
        for(unsigned boundary=1;boundary<=boundaries;++boundary)run(value,boundary);
    }
    for(const auto mode:{Mode::ConfigureFailed,Mode::ConfigureThrow,Mode::TrackThrow,Mode::MirrorFailed,Mode::MirrorThrow,Mode::MirrorOtherThrow,Mode::CancelThrow,Mode::MissingQueue,Mode::MissingGipa,Mode::MissingMemory})run({true,true,mode});
    for(const auto mode:{Mode::MissingCreate,Mode::MissingDestroy,Mode::MissingImages,Mode::MissingGdpa})run({false,true,mode});
    run({true,true,Mode::NativeFailed,"SFS_SOURCE_CREATE_FAILED"});
    run({true,true,Mode::MirrorFailed,"SFS_DESKTOP unavailable"});
    run({true,true,Mode::MirrorThrow,"SFS_DESKTOP unavailable"});
    run({false,false});
    for(const bool foreign:{false,true}){++checks;try{replacementFailure(foreign);}catch(const std::exception& error){++failures;std::cerr<<"replacement foreign="<<foreign<<": "<<error.what()<<'\n';}}
    ++checks;try{invalidInput();}catch(const std::exception& error){++failures;std::cerr<<"invalid input: "<<error.what()<<'\n';}
    for(const auto* argument:{L"mirror-retirement",L"source-retirement"}){++checks;try{retirementFailure(argument);}catch(const std::exception& error){++failures;std::cerr<<"retirement: "<<error.what()<<'\n';}}
    std::cout<<checks<<" outer swapchain checks; "<<failures<<" failures\n";return failures?1:0;
}
