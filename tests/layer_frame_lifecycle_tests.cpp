#include "../src/QuadRuntime.h"
#include "../src/EternalPresentation.h"
#include "../src/EternalCameraHook.h"
#include "../src/DesktopMirrorPacing.h"
#include "../src/FrameTiming.h"
#include "../src/PresentAnalysis.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#include <unordered_map>

namespace {
thread_local unsigned allocationCountdown{};
unsigned allocationFailures{};
}
void* operator new(std::size_t size){
    if(allocationCountdown&&!--allocationCountdown){++allocationFailures;throw std::bad_alloc{};}
    if(auto* value=std::malloc(size?size:1))return value;
    throw std::bad_alloc{};
}
void operator delete(void* value) noexcept {std::free(value);}
void operator delete(void* value,std::size_t) noexcept {std::free(value);}

namespace {
enum class Phase {Prepare,Acquire,Acquire2,Present};
enum class Mode {Normal,Grid,Decal,Screen,Projection,InvalidProjection,InvalidScreen,BeginRejected,UploadFailed,DeviceLost,CheckpointOom,
    CancelThrow,PairMissing,PairCancelThrow,NotConsumed,Incomplete,ReadbackThrow,MirrorThrow,TraceThrow,
    Native,QuadConsumed,QuadUnused,Unknown,NonGame,MissingDevice,NotReady,Suboptimal,DeviceMask};
struct Scenario {const char* name;Phase phase;Mode mode{Mode::Normal};const char* throwingLog{};bool otherThrow{},world{};};
constexpr Scenario scenarios[]{
    {"grid-log",Phase::Prepare,Mode::Grid,"ETERNAL_GRID_TEST"},
    {"decal-log",Phase::Prepare,Mode::Decal,"ETERNAL_DECAL_COARSE"},
    {"screen-log",Phase::Prepare,Mode::Screen,"ETERNAL_SCREEN_PROBE"},
    {"prepare",Phase::Prepare},
    {"mode-log",Phase::Prepare,Mode::Normal,"SFS_MODE"},
    {"cinematic-log",Phase::Prepare,Mode::Normal,"CINEMATIC_STEREO"},
    {"recenter-log",Phase::Prepare,Mode::Normal,"SFS_RECENTER"},
    {"hook-log",Phase::Prepare,Mode::Normal,"ETERNAL_HOOK"},
    {"shader-policy-log",Phase::Prepare,Mode::Normal,"CINEMATIC_SHADER_POLICY"},
    {"world-prepare",Phase::Prepare,Mode::Normal,nullptr,false,true},
    {"camera-log",Phase::Prepare,Mode::Projection,"ETERNAL_CAMERA_STEREO",false,true},
    {"ui-log",Phase::Prepare,Mode::InvalidScreen,"SFS_UI_PROJECTION_UNAVAILABLE",false,true},
    {"projection-cancel-throws",Phase::Prepare,Mode::InvalidProjection,nullptr,false,true},
    {"nonstandard-log",Phase::Prepare,Mode::Normal,"SFS_RECENTER",true},
    {"begin-rejected",Phase::Prepare,Mode::BeginRejected},
    {"upload-failed",Phase::Prepare,Mode::UploadFailed},
    {"device-lost",Phase::Prepare,Mode::DeviceLost},
    {"checkpoint-oom",Phase::Prepare,Mode::CheckpointOom},
    {"cancel-throws",Phase::Prepare,Mode::CancelThrow},
    {"acquire",Phase::Acquire},
    {"acquire-log",Phase::Acquire,Mode::Normal,"SFS_RECENTER"},
    {"acquire-suboptimal",Phase::Acquire,Mode::Suboptimal},
    {"acquire-not-ready",Phase::Acquire,Mode::NotReady},
    {"acquire2",Phase::Acquire2},
    {"acquire2-log",Phase::Acquire2,Mode::Normal,"CINEMATIC_STEREO"},
    {"acquire2-device-mask",Phase::Acquire2,Mode::DeviceMask},
    {"native-acquire",Phase::Acquire,Mode::Native},
    {"native-acquire2",Phase::Acquire2,Mode::Native},
    {"mono-acquire",Phase::Acquire,Mode::QuadUnused},
    {"present",Phase::Present},
    {"pair-missing",Phase::Present,Mode::PairMissing},
    {"pair-missing-log",Phase::Present,Mode::PairMissing,"SFS_PRESENT_NO_PAIR"},
    {"pair-cancel-throws",Phase::Present,Mode::PairCancelThrow},
    {"not-consumed",Phase::Present,Mode::NotConsumed},
    {"not-consumed-log",Phase::Present,Mode::NotConsumed,"SFS_PRESENT_NOT_CONSUMED"},
    {"incomplete-source",Phase::Present,Mode::Incomplete},
    {"readback-throws",Phase::Present,Mode::ReadbackThrow},
    {"readback-nonstandard",Phase::Present,Mode::ReadbackThrow,nullptr,true},
    {"mirror-throws",Phase::Present,Mode::MirrorThrow},
    {"mirror-handler-log",Phase::Present,Mode::MirrorThrow,"mirror failure"},
    {"mirror-nonstandard",Phase::Present,Mode::MirrorThrow,nullptr,true},
    {"trace-throws",Phase::Present,Mode::TraceThrow},
    {"trace-nonstandard",Phase::Present,Mode::TraceThrow,nullptr,true},
    {"source-result-log",Phase::Present,Mode::Normal,"SFS_GAME_PRESENT"},
    {"native-present",Phase::Present,Mode::Native},
    {"native-result-log",Phase::Present,Mode::Native,"GAME_PRESENT"},
    {"quad-consumed",Phase::Present,Mode::QuadConsumed},
    {"quad-unused",Phase::Present,Mode::QuadUnused},
    {"unknown-source",Phase::Present,Mode::Unknown},
    {"non-game",Phase::Present,Mode::NonGame},
    {"missing-device",Phase::Present,Mode::MissingDevice}
};
const Scenario* scenario{};
unsigned begins{},uploads{},prepares{},cancels{},stops{},poseUpdates{},checkpoints{},logsThrown{};
unsigned acquisitions{},handoffs{},retirements{},completions{},readbacks{},mirrorCalls{},traces{},nativePresents{},quads{},steamPrepares{};
bool pending{},retiredConsumed{},retiredComplete{},checkpointAfterCancel{};
argent::sfs::FramePose publishedPose;
argent::sfs::EyeUniforms publishedUniforms;
std::filesystem::path directory;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class T> T handle(uintptr_t value){return reinterpret_cast<T>(value);}
bool virtualSource(){return scenario->mode!=Mode::Native&&scenario->mode!=Mode::QuadConsumed&&scenario->mode!=Mode::QuadUnused&&scenario->mode!=Mode::Unknown&&scenario->mode!=Mode::NonGame;}
bool pairMissing(){return scenario->mode==Mode::PairMissing||scenario->mode==Mode::PairCancelThrow;}
VkResult uploadResult(){
    if(scenario->mode==Mode::UploadFailed)return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if(scenario->mode==Mode::DeviceLost||scenario->mode==Mode::CheckpointOom)return VK_ERROR_DEVICE_LOST;
    if(scenario->mode==Mode::CancelThrow)return VK_ERROR_INITIALIZATION_FAILED;
    return VK_SUCCESS;
}
void optionalFailure(){if(scenario->otherThrow)throw 7;throw std::runtime_error("optional failure");}
SHORT frameTestKeyState(int){return 0;}
}
namespace argent {
class DesktopMirror {
public:
    bool needsFrame()const{return true;}
    void present(Device&,VkImage,VkQueue,DesktopMirrorPacing::Clock::time_point,VkExtent2D,VkImageLayout,bool verified){
        ++mirrorCalls;require(verified,"Mirror lost source completion");
        if(scenario->mode==Mode::MirrorThrow){if(scenario->otherThrow)throw 7;throw std::runtime_error("mirror failure");}
    }
};
void log(const std::string& value){
    if(scenario->throwingLog&&value.rfind(scenario->throwingLog,0)==0){++logsThrown;if(scenario->otherThrow)throw 7;throw std::bad_alloc{};}
}
namespace hud {bool flatMenuVisible(bool=false){return false;}}
namespace camera {
void stop() noexcept {++stops;}
void beginRender(uint64_t) noexcept {}
void updatePose(XrPosef,bool,bool,bool) noexcept {++poseUpdates;}
float unitsPerMeter() noexcept {return 1;}
Stats stats() noexcept {return {};}
}
bool beginStereoFrame(Device&,const Source&,sfs::FramePose& pose,XrPosef& head,bool quad,const sfs::Matrix*,sfs::EyeUniforms*){
    ++begins;if(scenario->mode==Mode::BeginRejected)return false;
    pending=true;pose.serial=120;pose.quadView=quad;pose.stereoQuad=quad;head.orientation.w=scenario->mode==Mode::InvalidScreen?0.f:1.f;
    for(auto& view:pose.views){view.pose.orientation.w=1;view.fov={-0.5f,0.5f,0.5f,-0.5f};}
    return true;
}
void cancelStereoFrame(){++cancels;pending=false;if(scenario->mode==Mode::CancelThrow||scenario->mode==Mode::InvalidProjection||scenario->mode==Mode::PairCancelThrow)throw std::runtime_error("cancel failure");}
void prepareSteamFrame(Device&,VkSwapchainKHR){++steamPrepares;}
StereoPresentResult presentStereoFrame(Device&,const sfs::StereoFrame&,uint32_t,const VkSemaphore*,const StereoMirror& mirror){
    ++handoffs;if(mirror)mirror(handle<VkImage>(6),{1280,1280},VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    pending=false;const bool consumed=scenario->mode!=Mode::NotConsumed;
    return {consumed,consumed&&scenario->mode!=Mode::Incomplete};
}
bool presentQuad(Device&,VkQueue,uint32_t,uint32_t,const Source&,uint32_t,const VkPresentInfoKHR&){++quads;return scenario->mode==Mode::QuadConsumed;}
void readbackStereoIfRequested(Device&,VkImage,VkExtent2D,VkFormat){++readbacks;if(scenario->mode==Mode::ReadbackThrow)optionalFailure();}
namespace trace {void present(){++traces;if(scenario->mode==Mode::TraceThrow)optionalFailure();}}
namespace sfs {
bool vrEnabled(){return scenario->mode!=Mode::QuadConsumed&&scenario->mode!=Mode::QuadUnused;}
bool sourceSwapchain(VkDevice,VkSwapchainKHR){return virtualSource();}
VkResult acquireSource(VkDevice,VkSwapchainKHR,uint64_t,VkSemaphore,VkFence,uint32_t* image){
    ++acquisitions;if(scenario->mode==Mode::NotReady)return VK_NOT_READY;*image=0;
    return scenario->mode==Mode::Suboptimal?VK_SUBOPTIMAL_KHR:VK_SUCCESS;
}
void prepare(VkDevice,const FramePose& pose,const EyeUniforms& uniforms){++prepares;publishedPose=pose;publishedUniforms=uniforms;}
VkResult beginFrame(VkDevice,VkSwapchainKHR,uint32_t){++uploads;if(scenario->mode==Mode::CheckpointOom)allocationCountdown=1;return uploadResult();}
void reportGpuCheckpoints(VkDevice,VkQueue) noexcept {++checkpoints;checkpointAfterCancel=cancels==1&&stops>0;}
bool pair(VkDevice,VkImage,VkExtent2D,VkFormat,StereoFrame&){return !pairMissing();}
VkResult presentSource(VkDevice,VkQueue,const VkPresentInfoKHR& info,bool consumed,bool complete){
    ++retirements;retiredConsumed=consumed;retiredComplete=complete;
    if(info.pResults)info.pResults[0]=VK_SUCCESS;
    return VK_SUCCESS;
}
void copyCompleted(VkDevice){++completions;}
}
}
namespace {
struct State : argent::Device {
    struct Camera {bool projection(argent::sfs::Matrix& matrix,uint64_t=0){
        if(scenario->mode!=Mode::Projection&&scenario->mode!=Mode::InvalidProjection)return false;
        matrix[0]=scenario->mode==Mode::Projection?1.f:0.f;matrix[5]=-1;return true;
    }} camera;
    bool game{true},sfs{true},menuQuad{true},calibrated{},cinematicStereo{};
    XrPosef calibratedHead{};
    argent::presentation::Policy presentation;
    argent::presentation::Mode presentationMode{argent::presentation::Mode::Unknown};
    argent::sfs::FramePose framePose;
    struct Queue {uint32_t family{},index{};bool graphics{true};};
    std::unordered_map<VkQueue,Queue> queues;
    std::unordered_map<VkSwapchainKHR,argent::Source> sources;
    std::unordered_map<VkSwapchainKHR,std::unique_ptr<argent::DesktopMirror>> mirrors;
};
std::recursive_mutex stateMutex;
std::shared_ptr<State> current;
std::atomic<uint64_t> presents{};
template<class T> void* key(T value){return reinterpret_cast<void*>(value);}
std::shared_ptr<State> deviceOf(void*){return scenario->mode==Mode::MissingDevice?nullptr:current;}
VKAPI_ATTR VkResult VKAPI_CALL fakePresent(VkQueue,const VkPresentInfoKHR* info){
    ++nativePresents;
    const bool consumed=scenario->mode==Mode::Native||scenario->mode==Mode::QuadConsumed;
    require(info->waitSemaphoreCount==(consumed?0u:1u),"Native present waited on consumed semaphores");
    require((info->pWaitSemaphores==nullptr)==consumed,"Native wait pointers changed");
    if(info->pResults)info->pResults[0]=VK_SUBOPTIMAL_KHR;
    return VK_SUBOPTIMAL_KHR;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeProc(VkDevice,const char* name){
    if(!std::strcmp(name,"vkQueuePresentKHR"))return reinterpret_cast<PFN_vkVoidFunction>(fakePresent);
    if(!std::strcmp(name,"vkAcquireNextImageKHR"))return reinterpret_cast<PFN_vkVoidFunction>(argent::sfs::acquireSource);
    if(!std::strcmp(name,"vkAcquireNextImage2KHR"))return reinterpret_cast<PFN_vkVoidFunction>(+[](VkDevice device,const VkAcquireNextImageInfoKHR* info,uint32_t* image)->VkResult{
        return argent::sfs::acquireSource(device,info->swapchain,info->timeout,info->semaphore,info->fence,image);
    });
    return nullptr;
}
}
extern "C" {
#define GetAsyncKeyState frameTestKeyState
#include "../src/LayerFramePreparation.inc"
#undef GetAsyncKeyState
#include "../src/LayerFramePresentation.inc"
}
namespace {
void run(const Scenario& value){
    scenario=&value;begins=uploads=prepares=cancels=stops=poseUpdates=checkpoints=logsThrown=0;
    acquisitions=handoffs=retirements=completions=readbacks=mirrorCalls=traces=nativePresents=quads=steamPrepares=0;
    allocationFailures=allocationCountdown=0;pending=value.phase==Phase::Present;retiredConsumed=retiredComplete=checkpointAfterCancel=false;presents=0;
    current=std::make_shared<State>();current->device=handle<VkDevice>(1);current->graphicsQueue=handle<VkQueue>(3);current->gdpa=fakeProc;
    current->game=value.mode!=Mode::NonGame;
    current->menuQuad=!value.world;current->presentation.quad=!value.world;
    current->queues.emplace(current->graphicsQueue,State::Queue{});
    const auto chain=handle<VkSwapchainKHR>(2);
    argent::Source source;source.extent={1280,1280};source.format=VK_FORMAT_R8G8B8A8_UNORM;source.transferable=true;source.images={handle<VkImage>(4)};
    if(value.mode!=Mode::Unknown)current->sources.emplace(chain,std::move(source));
    current->mirrors.emplace(chain,std::make_unique<argent::DesktopMirror>());
    argent::presentation::latest={};argent::presentation::latest.tick=GetTickCount64();argent::presentation::latest.valid=argent::presentation::latest.inGame=true;
    for(const auto* marker:{"test-light-grid","test-decal-coarse","test-screen-mode"})std::filesystem::remove(directory/marker);
    if(value.mode==Mode::Grid)std::ofstream(directory/"test-light-grid").put('1');
    if(value.mode==Mode::Decal)std::ofstream(directory/"test-decal-coarse").put('1');
    if(value.mode==Mode::Screen)std::ofstream(directory/"test-screen-mode")<<3;
    VkResult result=VK_ERROR_UNKNOWN,perChain=VK_ERROR_UNKNOWN;uint32_t image=99;
    const auto semaphore=handle<VkSemaphore>(5);
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&semaphore;
    present.swapchainCount=1;present.pSwapchains=&chain;present.pImageIndices=&image;present.pResults=&perChain;
    bool escaped{};
    try{
        switch(value.phase){
        case Phase::Prepare:result=prepareStereo(current,chain,0);break;
        case Phase::Acquire:result=vkAcquireNextImageKHR(current->device,chain,100,semaphore,VK_NULL_HANDLE,&image);break;
        case Phase::Acquire2:{VkAcquireNextImageInfoKHR info{VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR};info.swapchain=chain;info.timeout=100;info.semaphore=semaphore;info.deviceMask=value.mode==Mode::DeviceMask?2:1;
            result=vkAcquireNextImage2KHR(current->device,&info,&image);break;}
        case Phase::Present:image=0;result=vkQueuePresentKHR(current->graphicsQueue,&present);break;
        }
    }catch(...){escaped=true;}
    allocationCountdown=0;
    require(!escaped,"Exception escaped the production frame caller");
    if(value.throwingLog)require(logsThrown>0,"Diagnostic injection was not exercised");
    if(value.phase==Phase::Present){
        if(value.mode==Mode::MissingDevice){require(result==VK_ERROR_DEVICE_LOST&&!retirements&&!nativePresents,"Missing device reached presentation");return;}
        if(virtualSource()){
            const bool consumed=!pairMissing()&&value.mode!=Mode::NotConsumed;
            require(result==VK_SUCCESS&&perChain==VK_SUCCESS&&retirements==1&&completions==1&&!nativePresents,"Virtual source retirement or completion was skipped");
            require(retiredConsumed==consumed&&retiredComplete==(consumed&&value.mode!=Mode::Incomplete),"XR handoff completion flags changed");
            require(cancels==unsigned(pairMissing())&&!pending,"Pending XR frame was not retired");
            require(readbacks==unsigned(consumed),"Optional readback changed wait consumption");
        }else{
            require(result==VK_SUBOPTIMAL_KHR&&perChain==result&&nativePresents==1&&!retirements,"Native present result changed");
            require(quads==unsigned(value.mode==Mode::QuadConsumed||value.mode==Mode::QuadUnused),"Quad routing changed");
        }
        require(present.waitSemaphoreCount==1&&present.pWaitSemaphores==&semaphore,"Caller present info was mutated");
        require(presents==unsigned(current->game),"Presentation count changed after optional failures");
        return;
    }
    if(value.mode==Mode::NotReady||value.mode==Mode::DeviceMask){
        require(!begins&&!uploads&&result==(value.mode==Mode::NotReady?VK_NOT_READY:VK_ERROR_FEATURE_NOT_PRESENT),"Unsuccessful acquisition prepared a frame");return;
    }
    if(value.mode==Mode::QuadUnused){require(result==VK_SUCCESS&&!begins&&!uploads&&steamPrepares==1&&acquisitions==1,"Mono acquisition changed Steam preparation");return;}
    require(result==(value.mode==Mode::Suboptimal?VK_SUBOPTIMAL_KHR:uploadResult()),"Frame preparation changed the Vulkan result");
    require(begins==1&&!steamPrepares,"SFS preparation started an unrelated Steam frame");
    if(value.mode==Mode::BeginRejected)require(!prepares&&!uploads&&!pending&&stops==1,"Rejected XR begin continued rendering");
    else if(value.mode==Mode::InvalidProjection)require(!prepares&&!uploads&&!pending&&cancels==1,"Rejected projection continued rendering");
    else{
        require(prepares==1&&uploads==1&&poseUpdates==1,"Frame pose or uniform publication was skipped");
        require(publishedPose.serial==120&&current->framePose.serial==120&&publishedUniforms.diagnostics[0]==1&&publishedUniforms.diagnostics[1]==float(value.world),"Published pose or shader policy changed after diagnostic failure");
        require(cancels==unsigned(uploadResult()!=VK_SUCCESS)&&pending==(uploadResult()==VK_SUCCESS),"Failed upload kept a pending XR frame");
        if(value.mode==Mode::DeviceLost)require(checkpoints==1&&checkpointAfterCancel,"Diagnostics preceded failure cleanup");
        if(value.mode==Mode::CheckpointOom)require(allocationFailures==1&&!checkpoints,"Checkpoint allocation failure was not exercised");
    }
}
}
int main(){
    std::cout<<std::unitbuf;std::cerr<<std::unitbuf;
    SetEnvironmentVariableA("ARGENT_EXTENDED_LOGGING","1");SetEnvironmentVariableA("ARGENT_PERFORMANCE_DIAGNOSTICS","0");
    directory=std::filesystem::temp_directory_path()/("argent-frame-tests-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);SetEnvironmentVariableW(L"ARGENT_LOG",(directory/L"test.log").c_str());
    unsigned failures{};
    for(const auto& value:scenarios){
        try{run(value);std::cout<<value.name<<" passed\n";}
        catch(const std::exception& error){++failures;std::cerr<<value.name<<": "<<error.what()<<" begins="<<begins<<" uploads="<<uploads<<" cancels="<<cancels<<" retirements="<<retirements<<" logs="<<logsThrown<<'\n';}
    }
    std::filesystem::remove(directory/"test-light-grid");std::filesystem::remove(directory/"test-decal-coarse");std::filesystem::remove(directory/"test-screen-mode");std::filesystem::remove(directory);
    std::cout<<std::size(scenarios)<<" outer frame scenarios; "<<failures<<" failures\n";
    return failures?1:0;
}
