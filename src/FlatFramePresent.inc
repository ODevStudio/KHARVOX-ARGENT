bool presentQuadImpl(Device& d,VkQueue q,uint32_t family,uint32_t index,const Source& source,uint32_t imageIndex,const VkPresentInfoKHR& present){
    std::unique_lock<std::recursive_mutex> guard(mutex);if(!instance||failed||imageIndex>=source.images.size())return false;
    bool consumed=false,complete=false,begun=false,acquired=false,waited=false;XrTime time{};
    try{
        if(!session){
            if(kharvox::isSteamBackedOpenXRRuntime(runtimeKind)){
                static const bool allowSteamVrSession=[]{
                    char forceSteamEnable1[8]{};
                    if(GetEnvironmentVariableA("ARGENT_STEAMVR_FORCE_ENABLE1",forceSteamEnable1,sizeof(forceSteamEnable1))==1&&forceSteamEnable1[0]=='1')return true;
                    wchar_t marker[32768]{};
                    const auto n=GetEnvironmentVariableW(L"ARGENT_LOG",marker,32768);
                    if(!n||n>=32768)return false;
                    return GetFileAttributesW((std::filesystem::path(marker).parent_path()/L"enable_steamvr_xr_session").c_str())!=INVALID_FILE_ATTRIBUTES;
                }();
                if(!allowSteamVrSession){
                    static bool logged{};
                    if(!logged){logged=true;log("[STEAM-XR] xrCreateSession gated off; create logs/enable_steamvr_xr_session to test session startup");}
                    return false;
                }
                constexpr uint64_t requiredSteamVrNativePresents=300;
                if(steamVrNativePresentDeferrals<requiredSteamVrNativePresents){
                    ++steamVrNativePresentDeferrals;
                    if(steamVrNativePresentDeferrals==1||steamVrNativePresentDeferrals%60==0)
                        log("[STEAM-XR] deferring xrCreateSession until native presents stabilize count="+std::to_string(steamVrNativePresentDeferrals)+"/"+std::to_string(requiredSteamVrNativePresents));
                    return false;
                }
                log("[STEAM-XR] Releasing ARGENT runtime lock before xrCreateSession");
                guard.unlock();
                startSession(d,d.graphicsQueue,d.graphicsFamily,d.graphicsIndex);
                guard.lock();
            }else startSession(d,d.graphicsQueue,d.graphicsFamily,d.graphicsIndex);
        }
        if(boundDevice!=d.device){
            static uint64_t skipped{};
            if(++skipped==1||skipped%600==0)log("XR_SKIP binding_mismatch family="+std::to_string(family)+" index="+std::to_string(index)+" boundFamily="+std::to_string(boundFamily));
            return false;
        }
        events();if(!running)return false;
        createSwapchain(source);
        const bool steamRuntime=kharvox::isSteamBackedOpenXRRuntime(runtimeKind);
        XrFrameState frame{XR_TYPE_FRAME_STATE};
        if(steamRuntime&&steamFramePrepared){
            frame=steamPreparedFrame;
            time=frame.predictedDisplayTime;
            steamFramePrepared=false;
            begun=steamFrameBegun;
            if(!steamFrameBegun){++steamOrderViolations;log("[STEAM-XR-SPLIT] prepared frame lost begun state violation="+std::to_string(steamOrderViolations));}
            const auto consumedPrepared=++steamConsumedPreparedFrames;
            if(consumedPrepared<=8||consumedPrepared%120==0)log("[STEAM-XR-SPLIT] consumed="+std::to_string(consumedPrepared)+" predicted="+std::to_string(frame.predictedDisplayTime)+" period="+std::to_string(frame.predictedDisplayPeriod)+" thread="+std::to_string(GetCurrentThreadId())+" wait/begin/end="+std::to_string(steamWaitCalls)+"/"+std::to_string(steamBeginCalls)+"/"+std::to_string(steamEndCalls));
        }else{
            if(steamRuntime&&steamFrameBegun){++steamOrderViolations;log("[STEAM-XR-ORDER] local frame still open before xrWaitFrame violation="+std::to_string(steamOrderViolations));}
            XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
            XrResult waitResult{XR_SUCCESS};
            XrResult beginResult{XR_SUCCESS};
            DWORD lifecycleThread=GetCurrentThreadId();
            if(steamRuntime){
                lifecycleThread=xrWorker().invoke([&]{
                    waitResult=XR(xrWaitFrame)(session,&wi,&frame);
                    if(XR_SUCCEEDED(waitResult)){
                        XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
                        beginResult=XR(xrBeginFrame)(session,&bi);
                    }
                    return GetCurrentThreadId();
                });
            }else check(XR(xrWaitFrame)(session,&wi,&frame),"xrWaitFrame");
            if(steamRuntime)++steamWaitCalls;
            if(XR_FAILED(waitResult)){log("[STEAM-XR-ORDER] xrWaitFrame result="+std::to_string(waitResult)+" lifecycleThread="+std::to_string(lifecycleThread)+" callerThread="+std::to_string(GetCurrentThreadId()));check(waitResult,"xrWaitFrame");}
            if(!steamRuntime){XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};check(XR(xrBeginFrame)(session,&bi),"xrBeginFrame");}
            if(steamRuntime)++steamBeginCalls;
            if(XR_FAILED(beginResult)){log("[STEAM-XR-ORDER] xrBeginFrame result="+std::to_string(beginResult)+" lifecycleThread="+std::to_string(lifecycleThread)+" callerThread="+std::to_string(GetCurrentThreadId()));check(beginResult,"xrBeginFrame");}
            begun=true;
            if(steamRuntime)steamFrameBegun=true;
        }
        time=frame.predictedDisplayTime;
        static int previousRender=-1;
        if(previousRender!=int(frame.shouldRender)){previousRender=int(frame.shouldRender);log("XR_SHOULD_RENDER="+std::to_string(previousRender)+" state="+std::to_string(sessionState));}
        XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};const XrCompositionLayerBaseHeader* layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
        if(frame.shouldRender){
            const bool crossQueue=q!=boundQueue;
            if(crossQueue)bridge.prepare(d,family,boundFamily,source.extent,source.format);
            uint32_t target=0;XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};check(XR(xrAcquireSwapchainImage)(swapchain,&ai,&target),"xrAcquireSwapchainImage");acquired=true;
            XrSwapchainImageWaitInfo swi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};swi.timeout=XR_INFINITE_DURATION;check(XR(xrWaitSwapchainImage)(swapchain,&swi),"xrWaitSwapchainImage");waited=true;
            if(crossQueue)bridge.stage(d,q,source.images[imageIndex],source.extent,present,consumed);
            checkVk(VK(vkResetCommandBuffer)(command,0),"vkResetCommandBuffer");
            VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};cbi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;checkVk(VK(vkBeginCommandBuffer)(command,&cbi),"vkBeginCommandBuffer");
            copyTiming.begin(command,frames%copyTimingStride==0);
            VkImageMemoryBarrier barriers[2]{};
            for(auto& b:barriers){b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
            barriers[0].image=source.images[imageIndex];barriers[0].oldLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;barriers[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barriers[0].srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barriers[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            barriers[1].image=images.at(target).image;barriers[1].oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;barriers[1].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barriers[1].srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;barriers[1].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            VK(vkCmdPipelineBarrier)(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,crossQueue?1:2,barriers+(crossQueue?1:0));
            VkImageCopy copy{};copy.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.dstSubresource=copy.srcSubresource;copy.extent={extent.width,extent.height,1};
            if(crossQueue){VkBufferImageCopy region{};region.imageSubresource=copy.dstSubresource;region.imageExtent=copy.extent;VK(vkCmdCopyBufferToImage)(command,bridge.buffer,barriers[1].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);}
            else VK(vkCmdCopyImage)(command,barriers[0].image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,barriers[1].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
            for(auto& b:barriers)std::swap(b.oldLayout,b.newLayout);
            barriers[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barriers[0].dstAccessMask=0;
            barriers[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barriers[1].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            VK(vkCmdPipelineBarrier)(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,crossQueue?1:2,barriers+(crossQueue?1:0));
            copyTiming.end(command);
            checkVk(VK(vkEndCommandBuffer)(command),"vkEndCommandBuffer");checkVk(VK(vkResetFences)(d.device,1,&fence),"vkResetFences");
            std::vector<VkPipelineStageFlags> stages(present.waitSemaphoreCount,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=present.waitSemaphoreCount;submit.pWaitSemaphores=present.pWaitSemaphores;submit.pWaitDstStageMask=stages.data();submit.commandBufferCount=1;submit.pCommandBuffers=&command;
            VkPipelineStageFlags bridgeStage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            if(crossQueue){submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&bridge.ready;submit.pWaitDstStageMask=&bridgeStage;}
            {std::lock_guard<std::recursive_mutex> lock(*d.queueMutex);checkVk(VK(vkQueueSubmit)(boundQueue,1,&submit,fence),"vkQueueSubmit copy");}consumed=true;
            checkVk(VK(vkWaitForFences)(d.device,1,&fence,VK_TRUE,UINT64_MAX),"vkWaitForFences copy");
            complete=true;
            double copyMs{};if(copyTiming.completed(copyMs)&&(frames==0||frames%300==0))log("XR_COPY_GPU_MS="+std::to_string(copyMs));
            if(crossQueue){static uint64_t bridged{};if(++bridged==1||bridged%300==0)log("XR_QUEUE_BRIDGE count="+std::to_string(bridged)+" sourceFamily="+std::to_string(family)+" xrFamily="+std::to_string(boundFamily));}
            XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};check(XR(xrReleaseSwapchainImage)(swapchain,&ri),"xrReleaseSwapchainImage");acquired=false;
            quad.space=space;quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;quad.subImage.swapchain=swapchain;quad.subImage.imageRect=widescreenQuadRect(extent);
            quad.pose.orientation.w=1;quad.pose.position.z=-2.5f;quad.size=widescreenQuadSize(3.2f);
        }
        XrCompositionLayerQuad bindingsLayer{};
        std::array<const XrCompositionLayerBaseHeader*,2> menuLayers{layer,nullptr};uint32_t menuLayerCount=frame.shouldRender?1:0;
        if(frame.shouldRender&&pauseBindingsReady&&presentation::pauseRootVisible()){
            bindingsLayer=pauseBindingsLayer(quad);menuLayers[menuLayerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&bindingsLayer);
        }
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=time;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=menuLayerCount;end.layers=menuLayerCount?menuLayers.data():nullptr;
        auto r=steamRuntime?xrWorker().invoke([&]{return XR(xrEndFrame)(session,&end);}):XR(xrEndFrame)(session,&end);begun=false;if(steamRuntime){steamFrameBegun=false;++steamEndCalls;}check(r,"xrEndFrame");
        if(frame.shouldRender){++frames;if(frames==1||frames%300==0)log("QUAD_FRAME_SUBMITTED count="+std::to_string(frames)+" result="+std::to_string(r)+" state="+std::to_string(sessionState)+" gameImage="+std::to_string(imageIndex)+" predictedTime="+std::to_string(time));}
    }catch(const std::exception& e){
        failed=true;
        if(consumed&&!complete){std::lock_guard<std::recursive_mutex> queueGuard(*d.queueMutex);complete=requireGpuRetirement(VK(vkDeviceWaitIdle)(d.device),"quad copy error")==VK_SUCCESS;}
        try{if(acquired&&waited&&(!consumed||complete)){XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};XR(xrReleaseSwapchainImage)(swapchain,&ri);}}catch(...){}
        if(steamFramePrepared){time=steamPreparedFrame.predictedDisplayTime;begun=steamFrameBegun;steamFramePrepared=false;}
        try{if(begun){
            XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=time;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            const bool steamRuntime=kharvox::isSteamBackedOpenXRRuntime(runtimeKind);
            if(steamRuntime)xrWorker().invoke([&]{XR(xrEndFrame)(session,&end);});else XR(xrEndFrame)(session,&end);
            if(steamRuntime){steamFrameBegun=false;++steamEndCalls;}
        }}catch(...){}
        try{log(std::string("QUAD_DISABLED ")+e.what());}catch(...){}
    }
    return consumed;
}
