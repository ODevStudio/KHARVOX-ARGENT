// OpenXR prediction/render/submission sequence adapted from KHARVOX.
// Included inside namespace argent; all Vulkan calls use downstream dispatch.
namespace {
kharvox::CinewindowAnchor menuAnchor;
kharvox::CinewindowCaptureReadiness menuReadiness;
bool previousMenuQuad{};
bool earlyStereoReleaseRequested(uint64_t serial){
    if constexpr(cleanRelease)return true;
    static const auto modeFile=[] {wchar_t path[32768]{};const auto n=GetEnvironmentVariableW(L"ARGENT_LOG",path,32768);
        return n&&n<32768?std::filesystem::path(path).parent_path()/L"test-xr-sync-mode":runtimePath()/L"logs/test-xr-sync-mode";}();
    static bool early=true,initialized=false;
    if(!initialized||serial%120==0){
        int mode=1;std::ifstream input(modeFile);if(input){int requested{};if(input>>requested&&(requested==0||requested==1))mode=requested;}
        if(!initialized||early!=(mode==1))log("XR_SYNC_MODE requestedEarly="+std::to_string(mode)+" source=test-xr-sync-mode");
        early=mode==1;initialized=true;
    }
    return early;
}
#include "StereoFrameBegin.inc"
#include "StereoFramePresent.inc"
}
bool beginStereoFrame(Device& d,const Source& source,sfs::FramePose& pose,XrPosef& head,bool quadView,
                      const sfs::Matrix* cinematicProjection,sfs::EyeUniforms* cinematicUniforms){
    auto begin=[&]{return beginStereoImpl(d,source,pose,head,quadView,cinematicProjection,cinematicUniforms);};return simulator?xrWorker().invoke(begin):begin();
}
StereoPresentResult presentStereoFrame(Device& d,const sfs::StereoFrame& pair,uint32_t n,const VkSemaphore* waits,const StereoMirror& mirror){
    auto present=[&]{return presentStereoImpl(d,pair,n,waits,mirror);};return simulator?xrWorker().invoke(present):present();
}
void cancelStereoFrame(){auto cancel=[] {std::lock_guard<std::recursive_mutex> guard(mutex);cancelStereoImpl();};if(simulator)xrWorker().invoke(cancel);else cancel();}
void retireStereoSources(VkDevice d){
    auto retire=[&]{
        std::lock_guard<std::recursive_mutex> guard(mutex);
        if(boundDevice!=d||!fsr1.active())return;
        std::lock_guard<std::recursive_mutex> queueGuard(*device.queueMutex);
        if(requireGpuRetirement(VK(vkQueueWaitIdle)(boundQueue),"stereo source")==VK_ERROR_DEVICE_LOST)failed=true;
        destroySwapchain();
    };
    if(simulator)xrWorker().invoke(retire);else retire();
}
