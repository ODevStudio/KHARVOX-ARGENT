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
StereoPresentResult presentStereoImpl(Device& d,const sfs::StereoFrame& pair,uint32_t waitCount,const VkSemaphore* waits,const StereoMirror& mirror){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    // KHARVOX keeps borrowed game depth alive through submission and its fence.
    kharvox::GameImageLifetime::Use gameImages(kharvox::gameImageLifetime());
    bool consumed=false,submitted=false,complete=false;
    std::array<uint32_t,2> targets{};
    std::array<bool,2> acquired{},waited{};
    try{
        if(failed||!stereoPending.begun)return {};
        if(boundDevice!=d.device||swapchainLayers!=2||!stereoEyes[0].handle||!stereoEyes[1].handle||
           !validStereoPair(pair,stereoPending.pose,sourceExtent,format)){
            log("STEREO_PAIR_REJECTED stale/mixed frame, eye, extent, format or pose");cancelStereoImpl();return {};
        }
        const bool earlyRelease=!mirror&&kharvox::sfsEarlyXrRelease(earlyStereoReleaseRequested(pair.generation),runtimeKind,
            sfs::sourceRingActive(d.device),boundQueue==d.graphicsQueue&&boundDevice==d.device,
            fence!=VK_NULL_HANDLE,bool(d.queueMutex));
        const bool measure=extendedLogging();
        using ScheduleClock=std::chrono::steady_clock;
        ScheduleClock::time_point submitDone{},releaseAt{},endAt{},endDone{},completedAt{};
        double fenceWaitMs{};
        prepareHands(d,pair.pose.quadView);
        auto views=stereoProjectionViews(pair,XR_NULL_HANDLE,extent);
        for(unsigned e=0;e<2;++e){
            views[e].subImage.swapchain=stereoEyes[e].handle;
            views[e].subImage.imageArrayIndex=0;
        }
        if(!pair.pose.quadView){
            XrPosef rendered{};const bool matched=camera::renderedHead(pair.pose.serial,rendered);
            if(matched)rebaseProjectionViews(views,pair.pose.predictedHead,rendered);
            if(extendedLogging()&&pair.pose.serial%120==0)log("STEREO_RENDER_POSE serial="+std::to_string(pair.pose.serial)+" matched="+std::to_string(matched));
        }
        for(unsigned e=0;e<2;++e){
            XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            {std::lock_guard<std::recursive_mutex> queueGuard(*d.queueMutex);check(XR(xrAcquireSwapchainImage)(stereoEyes[e].handle,&acquireInfo,&targets[e]),"xrAcquireSwapchainImage stereo eye");}acquired[e]=true;
            XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};waitInfo.timeout=XR_INFINITE_DURATION;
            {static FrameTiming::Totals t;FrameTiming timing("xrWaitSwapchainImage",t);check(XR(xrWaitSwapchainImage)(stereoEyes[e].handle,&waitInfo),"xrWaitSwapchainImage stereo eye");}waited[e]=true;
        }
        checkVk(VK(vkResetCommandBuffer)(command,0),"reset stereo copy");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        checkVk(VK(vkBeginCommandBuffer)(command,&begin),"begin stereo copy");
        if(extendedLogging())sfs::markXrCheckpoint(d.device,command,"XR copy begin");
        copyTiming.begin(command,pair.pose.serial%copyTimingStride==0);
        diagnosticCopyTiming.begin(command,argent::perf::enabled()&&argent::perf::mode.load(std::memory_order_relaxed)==2&&argent::perf::sample(pair.pose.serial,2));
        std::array<VkImageMemoryBarrier,4> barriers{};
        for(auto& b:barriers){b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
        for(unsigned e=0;e<2;++e){auto& b=barriers[e];b.image=pair.eyes[e].image;b.subresourceRange.baseArrayLayer=pair.eyes[e].layer;
            b.oldLayout=pair.eyes[e].layout;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;}
        for(unsigned e=0;e<2;++e){auto& dst=barriers[2+e];dst.image=stereoEyes[e].images.at(targets[e]).image;
            const bool initialized=targets[e]<stereoEyes[e].initialized.size()&&stereoEyes[e].initialized[targets[e]];
            dst.oldLayout=initialized?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;dst.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            dst.srcAccessMask=initialized?(VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT):0;dst.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;}
        VK(vkCmdPipelineBarrier)(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,4,barriers.data());
        diagnosticCopyTiming.point(command,1);
        for(unsigned e=0;e<2;++e){
            const auto dstImage=stereoEyes[e].images.at(targets[e]).image;
            if(fsr1.active()&&!pair.pose.quadView){
                auto filtered=fsr1.record(command,pair.eyes[e].image,e,pair.generation,{0,0,sourceExtent.width,sourceExtent.height},extent,pair.eyes[e].layer);
                if(!filtered)throw std::runtime_error("FSR1 rejected eye image");
                VkImageBlit blit{};blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
                blit.srcOffsets[1]=blit.dstOffsets[1]={int32_t(extent.width),int32_t(extent.height),1};
                VK(vkCmdBlitImage)(command,filtered,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dstImage,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
            }else{
                VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,pair.eyes[e].layer,1};
                copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.extent={extent.width,extent.height,1};
                VK(vkCmdCopyImage)(command,pair.eyes[e].image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dstImage,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
            }
        }
        for(unsigned i=0;i<4;++i)finishStereoCopyBarrier(barriers[i],i>=2);
        VK(vkCmdPipelineBarrier)(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,4,barriers.data());
        copyTiming.end(command);
        diagnosticCopyTiming.point(command,2);
        if(extendedLogging())sfs::markXrCheckpoint(d.device,command,"XR copy end");
        recordHands(targets,pair,views,gameImages);
        diagnosticCopyTiming.point(command,3);
        checkVk(VK(vkEndCommandBuffer)(command),"end stereo copy");checkVk(VK(vkResetFences)(d.device,1,&fence),"reset stereo copy fence");
        std::vector<VkPipelineStageFlags> stages(waitCount,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=waitCount;submit.pWaitSemaphores=waits;submit.pWaitDstStageMask=stages.data();submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        {std::lock_guard<std::recursive_mutex> queueGuard(*d.queueMutex);checkVk(VK(vkQueueSubmit)(boundQueue,1,&submit,fence),"submit stereo copy");}
        consumed=submitted=true;
        if(measure)submitDone=ScheduleClock::now();
        auto finishCopy=[&]{
        const auto waitStart=measure?ScheduleClock::now():ScheduleClock::time_point{};
        {static FrameTiming::Totals before,after;FrameTiming timing(earlyRelease?"deferredRenderAndCopyFence":"renderAndCopyFence",earlyRelease?after:before);const auto result=VK(vkWaitForFences)(d.device,1,&fence,VK_TRUE,UINT64_MAX);
         if(result==VK_ERROR_DEVICE_LOST)sfs::reportGpuCheckpoints(d.device,boundQueue);
         checkVk(result,"wait stereo copy");}complete=true;
        if(measure){completedAt=ScheduleClock::now();fenceWaitMs=std::chrono::duration<double,std::milli>(completedAt-waitStart).count();}
        handRenderer.finishSceneIntegratedFrame();
        // Mirror the final eye while it is still acquired, including hands and laser.
        if(mirror)mirror(stereoEyes[1].images.at(targets[1]).image,extent,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        if(diagnosticCopyTiming.recorded){
            std::array<uint64_t,4> ticks{};
            if(diagnosticCopyTiming.read(ticks))log("PERF_GPU_XR serial="+std::to_string(pair.pose.serial)+" context="+(pair.pose.quadView?"quad":"world")+
                " queue="+std::to_string(reinterpret_cast<uint64_t>(boundQueue))+" startTick="+std::to_string(ticks[0])+" endTick="+std::to_string(ticks[3])+
                " barriersMs="+std::to_string(diagnosticCopyTiming.ms(ticks[0],ticks[1]))+" copyFsrMs="+std::to_string(diagnosticCopyTiming.ms(ticks[1],ticks[2]))+
                " handsMs="+std::to_string(diagnosticCopyTiming.ms(ticks[2],ticks[3]))+" totalMs="+std::to_string(diagnosticCopyTiming.ms(ticks[0],ticks[3]))+
                " fsr="+std::to_string(fsr1.active()&&!pair.pose.quadView)+" fenceWaitMs="+std::to_string(fenceWaitMs));
            else log("PERF_GPU_XR_UNAVAILABLE serial="+std::to_string(pair.pose.serial));
        }
        for(unsigned e=0;e<2;++e)if(targets[e]<stereoEyes[e].initialized.size())stereoEyes[e].initialized[targets[e]]=true;
        // Read only after the existing fence. Profiling adds no wait or queue
        // submission; this interval includes copy barriers and optional FSR.
        double gpuMs{};if(copyTiming.completed(gpuMs)){
            struct Totals{uint32_t samples{};double sum{},maximum{};};static Totals totals[2];
            auto& t=totals[pair.pose.quadView?0:1];t.sum+=gpuMs;t.maximum=(std::max)(t.maximum,gpuMs);
            if(++t.samples>=(copyTimingStride==1?120u:16u)){
                log("FRAME_GPU section=xrStereoCopy samples="+std::to_string(t.samples)+" meanMs="+std::to_string(t.sum/t.samples)+" maxMs="+std::to_string(t.maximum)+" context="+(pair.pose.quadView?"quad":"world"));t={};
            }
        }
        };
        auto publish=[&]{
        if(measure)releaseAt=ScheduleClock::now();
        for(unsigned e=0;e<2;++e){
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            {std::lock_guard<std::recursive_mutex> queueGuard(*d.queueMutex);check(XR(xrReleaseSwapchainImage)(stereoEyes[e].handle,&release),"release stereo eye image");}acquired[e]=waited[e]=false;
        }
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};projection.space=localSpace;projection.viewCount=2;projection.views=views.data();
        std::array<const XrCompositionLayerBaseHeader*,3> layers{reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection),nullptr,nullptr};
        uint32_t layerCount=1;
        std::array<XrCompositionLayerQuad,2> quads{};
        if(pair.pose.quadView){
            layerCount=pair.pose.stereoQuad?2:1;
            quads=stereoQuadLayers(pair.pose,{stereoEyes[0].handle,stereoEyes[1].handle},extent,space,localSpace);
            for(unsigned e=0;e<layerCount;++e)layers[e]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quads[e]);
        }
        XrCompositionLayerQuad bindingsLayer{};
        if(pair.pose.quadView&&!pair.pose.stereoQuad&&pauseBindingsReady&&presentation::pauseRootVisible()){
            bindingsLayer=pauseBindingsLayer(quads[0]);
            layers[layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&bindingsLayer);
        }
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=pair.pose.displayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=layerCount;end.layers=layers.data();
        const bool steamRuntime=kharvox::isSteamBackedOpenXRRuntime(runtimeKind);
        XrResult endResult{XR_SUCCESS};
        DWORD lifecycleThread=GetCurrentThreadId();
        if(measure)endAt=ScheduleClock::now();
        if(steamRuntime){
            lifecycleThread=xrWorker().invoke([&]{
                std::lock_guard<std::recursive_mutex> queueGuard(*d.queueMutex);
                endResult=XR(xrEndFrame)(session,&end);
                return GetCurrentThreadId();
            });
        }else{
            static FrameTiming::Totals t;FrameTiming timing("xrEndFrame",t);
            std::lock_guard<std::recursive_mutex> queueGuard(*d.queueMutex);
            endResult=XR(xrEndFrame)(session,&end);
        }
        if(measure)endDone=ScheduleClock::now();
        stereoPending.begun=false;
        if(steamRuntime){steamFrameBegun=false;++steamEndCalls;}
        check(endResult,"xrEndFrame composition");
        if(steamRuntime&&(steamEndCalls<=8||steamEndCalls%120==0))log("[STEAM-XR-STEREO] end lifecycleThread="+std::to_string(lifecycleThread)+" callerThread="+std::to_string(GetCurrentThreadId())+" wait/begin/end="+std::to_string(steamWaitCalls)+"/"+std::to_string(steamBeginCalls)+"/"+std::to_string(steamEndCalls));
        if(pair.generation==1||(extendedLogging()&&pair.generation%120==0))log(std::string(pair.pose.stereoQuad?"CINEMATIC_STEREO_QUAD_SUBMITTED serial=":pair.pose.quadView?"MENU_QUAD_SUBMITTED serial=":"STEREO_PROJECTION_SUBMITTED serial=")+std::to_string(pair.generation)+" perEyeSwapchains=1 initializedLayouts=1");
        };
        kharvox::finishSfsXrSubmission(earlyRelease,finishCopy,publish);
        if(measure&&(pair.generation<=8||pair.generation%120==0)){
            const auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
            log("XR_SUBMIT_ORDER serial="+std::to_string(pair.generation)+" context="+(pair.pose.quadView?"quad":"world")+
                " early="+std::to_string(earlyRelease)+" submitToReleaseMs="+std::to_string(ms(submitDone,releaseAt))+
                " submitToEndMs="+std::to_string(ms(submitDone,endAt))+" endFrameMs="+std::to_string(ms(endAt,endDone))+
                " submitToCompletionMs="+std::to_string(ms(submitDone,completedAt))+" fenceWaitMs="+std::to_string(fenceWaitMs)+" completionVerified=1");
        }
    }catch(const std::exception& error){
        if(submitted&&!complete){std::lock_guard<std::recursive_mutex> queueGuard(*d.queueMutex);complete=requireGpuRetirement(VK(vkQueueWaitIdle)(boundQueue),"stereo copy error")==VK_SUCCESS;}
        log(std::string("STEREO_PRESENT_FAILED ")+error.what());
        if(!submitted)fsr1.discardRecordedFrame();
        if(!submitted||complete)handRenderer.finishSceneIntegratedFrame();

        if(!submitted||complete)for(unsigned e=0;e<2;++e)if(acquired[e]&&waited[e]){
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};std::lock_guard<std::recursive_mutex> queueGuard(*d.queueMutex);XR(xrReleaseSwapchainImage)(stereoEyes[e].handle,&release);
        }
        try{cancelStereoImpl();}catch(...){}failed=true;
    }
    return {consumed,submitted&&complete};
}
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
