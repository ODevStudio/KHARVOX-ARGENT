void prepareSteamFrame(Device&,VkSwapchainKHR swapchain){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!kharvox::isSteamBackedOpenXRRuntime(runtimeKind)||!session||!running||!swapchain||failed)return;
    try{
        if(steamFramePrepared||steamFrameBegun){
            const auto repeated=++steamRepeatedAcquires;
            if(repeated<=8||repeated%120==0)log("[STEAM-XR-SPLIT] acquire arrived before prepared frame was presented; keeping one pending frame count="+std::to_string(repeated)+" thread="+std::to_string(GetCurrentThreadId()));
            return;
        }
        events();if(!running)return;
        const DWORD acquireCallerThread=GetCurrentThreadId();
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState frame{XR_TYPE_FRAME_STATE};
        XrResult waitResult{XR_ERROR_RUNTIME_FAILURE};
        XrResult beginResult{XR_ERROR_RUNTIME_FAILURE};
        const DWORD lifecycleThread=xrWorker().invoke([&]{
            waitResult=XR(xrWaitFrame)(session,&wait,&frame);
            if(XR_SUCCEEDED(waitResult)){
                XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
                beginResult=XR(xrBeginFrame)(session,&begin);
            }
            return GetCurrentThreadId();
        });
        ++steamWaitCalls;
        if(XR_FAILED(waitResult)){log("[STEAM-XR-SPLIT] xrWaitFrame result="+std::to_string(waitResult)+" lifecycleThread="+std::to_string(lifecycleThread)+" acquireCallerThread="+std::to_string(acquireCallerThread));return;}
        ++steamBeginCalls;
        if(XR_FAILED(beginResult)){log("[STEAM-XR-SPLIT] xrBeginFrame result="+std::to_string(beginResult)+" lifecycleThread="+std::to_string(lifecycleThread)+" acquireCallerThread="+std::to_string(acquireCallerThread));return;}
        steamPreparedFrame=frame;
        steamFramePrepared=true;
        steamFrameBegun=true;
        const auto prepared=++steamPreparedFrames;
        if(prepared<=8||prepared%120==0)log("[STEAM-XR-SPLIT] prepared="+std::to_string(prepared)+" shouldRender="+std::to_string(frame.shouldRender)+" predicted="+std::to_string(frame.predictedDisplayTime)+" period="+std::to_string(frame.predictedDisplayPeriod)+" lifecycleThread="+std::to_string(lifecycleThread)+" acquireCallerThread="+std::to_string(acquireCallerThread)+" wait/begin/end="+std::to_string(steamWaitCalls)+"/"+std::to_string(steamBeginCalls)+"/"+std::to_string(steamEndCalls));
    }catch(const std::exception& e){try{log(std::string("[STEAM-XR-SPLIT] prepare failed: ")+e.what());}catch(...){}}
}
