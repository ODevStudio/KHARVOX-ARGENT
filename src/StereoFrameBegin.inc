void cancelStereoImpl(){
    if(!stereoPending.begun)return;
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=stereoPending.pose.displayTime;
    end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    const bool steamRuntime=kharvox::isSteamBackedOpenXRRuntime(runtimeKind);
    const auto endCall=[&]{std::lock_guard<std::recursive_mutex> queueGuard(*device.queueMutex);return XR(xrEndFrame)(session,&end);};
    const auto result=steamRuntime?xrWorker().invoke(endCall):endCall();
    stereoPending.begun=false;
    if(steamRuntime){steamFrameBegun=false;++steamEndCalls;}
    check(result,"xrEndFrame empty stereo");
}
bool beginStereoImpl(Device& d,const Source& source,sfs::FramePose& pose,XrPosef& head,bool quadView,
                     const sfs::Matrix* cinematicProjection,sfs::EyeUniforms* cinematicUniforms){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!instance||failed||!d.graphicsQueue)return false;
    try{
        if(stereoPending.begun)throw std::runtime_error("Previous stereo frame was not retired");
        if(!session){menuAnchor={};menuReadiness={};previousMenuQuad=false;startSession(d,d.graphicsQueue,d.graphicsFamily,d.graphicsIndex);}
        if(boundDevice!=d.device)throw std::runtime_error("Stereo Vulkan device changed");
        events();if(!running)return false;createSwapchain(source,2,!quadView);
        const bool steamRuntime=kharvox::isSteamBackedOpenXRRuntime(runtimeKind);
        const DWORD callerThread=GetCurrentThreadId();
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};XrFrameState state{XR_TYPE_FRAME_STATE};
        XrResult waitResult{XR_SUCCESS};
        XrResult beginResult{XR_SUCCESS};
        DWORD lifecycleThread=callerThread;
        if(steamRuntime){
            if(steamFrameBegun){++steamOrderViolations;log("[STEAM-XR-STEREO] frame still open before stereo xrWaitFrame violation="+std::to_string(steamOrderViolations));}
            lifecycleThread=xrWorker().invoke([&]{
                waitResult=XR(xrWaitFrame)(session,&wait,&state);
                if(XR_SUCCEEDED(waitResult)){
                    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
                    std::lock_guard<std::recursive_mutex> queueGuard(*d.queueMutex);beginResult=XR(xrBeginFrame)(session,&begin);
                }
                return GetCurrentThreadId();
            });
            ++steamWaitCalls;
        }else{
            static FrameTiming::Totals t;FrameTiming timing("xrWaitFrame",t);
            waitResult=XR(xrWaitFrame)(session,&wait,&state);
        }
        check(waitResult,"xrWaitFrame stereo");
        if(steamRuntime)++steamBeginCalls;
        else {XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};std::lock_guard<std::recursive_mutex> queueGuard(*d.queueMutex);beginResult=XR(xrBeginFrame)(session,&begin);}
        check(beginResult,"xrBeginFrame stereo");
        stereoPending.begun=true;stereoPending.pose={};stereoPending.pose.displayTime=state.predictedDisplayTime;
        if(steamRuntime){
            steamFrameBegun=true;
            if(stereoSerial<8||stereoSerial%120==0)log("[STEAM-XR-STEREO] begin lifecycleThread="+std::to_string(lifecycleThread)+" callerThread="+std::to_string(callerThread)+" shouldRender="+std::to_string(state.shouldRender)+" predicted="+std::to_string(state.predictedDisplayTime)+" period="+std::to_string(state.predictedDisplayPeriod)+" wait/begin/end="+std::to_string(steamWaitCalls)+"/"+std::to_string(steamBeginCalls)+"/"+std::to_string(steamEndCalls));
        }
        controllerActions.update(state.predictedDisplayTime,sessionState==XR_SESSION_STATE_FOCUSED,!quadView);
        stereoPending.hands=input::snapshot();
        if(!state.shouldRender){menuReadiness.trackedSince=0;cancelStereoImpl();return false;}
        auto& next=stereoPending.pose;next.serial=++stereoSerial;
        for(auto& view:next.views)view.type=XR_TYPE_VIEW;
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime=state.predictedDisplayTime;locate.space=localSpace;
        XrViewState views{XR_TYPE_VIEW_STATE};uint32_t count{};
        check(XR(xrLocateViews)(session,&locate,&views,2,&count,next.views.data()),"xrLocateViews stereo");
        constexpr auto valid=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
        if(count!=2||(views.viewStateFlags&valid)!=valid){menuReadiness.trackedSince=0;cancelStereoImpl();return false;}
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        check(XR(xrLocateSpace)(space,localSpace,state.predictedDisplayTime,&location),"xrLocateSpace head");
        constexpr auto headValid=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
        if((location.locationFlags&headValid)!=headValid){menuReadiness.trackedSince=0;cancelStereoImpl();return false;}
        head=location.pose;next.predictedHead=head;next.quadView=quadView;
        camera::updateHeadsetFov(head,next.views.data(),2);
        if(referenceChangeTime)menuReadiness.referenceChanged(referenceChangeTime);
        if(referenceChangeTime&&state.predictedDisplayTime>=referenceChangeTime){next.recenterRequested=true;referenceChangeTime=0;menuAnchor={};}
        next.headPositionTracked=(location.locationFlags&XR_SPACE_LOCATION_POSITION_TRACKED_BIT)!=0;
        if(quadView){
            if(!previousMenuQuad){menuAnchor={};menuReadiness.trackedSince=0;}
            constexpr auto trackedViews=XR_VIEW_STATE_ORIENTATION_TRACKED_BIT|XR_VIEW_STATE_POSITION_TRACKED_BIT;
            constexpr auto trackedHead=XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
            const bool ready=menuReadiness.ready(state.predictedDisplayTime,menuAnchorSessionVisible(sessionState),
                (views.viewStateFlags&trackedViews)==trackedViews&&(location.locationFlags&trackedHead)==trackedHead);
            if(!menuAnchor.valid&&ready){auto q=head.orientation;auto p=head.position;
                if(kharvox::captureCinewindowAnchor(menuAnchor,q.x,q.y,q.z,q.w,p.x,p.y,p.z))
                    log("MENU_ANCHOR captured after tracked settling height="+std::to_string(p.y));}
            next.quadHeadLocked=!menuAnchor.valid;
            if(next.quadHeadLocked)next.quadPose={{0,0,0,1},{0,0,-2.5f}};
            else {auto p=kharvox::fixedCinewindowLayerPose(menuAnchor,2.5f);
                next.quadPose={{p.orientationX,p.orientationY,p.orientationZ,p.orientationW},{p.positionX,p.positionY,p.positionZ}};}
        }else {menuAnchor={};menuReadiness.trackedSince=0;}
        next.stereoQuad=false;
        constexpr auto tracked=XR_VIEW_STATE_ORIENTATION_TRACKED_BIT|XR_VIEW_STATE_POSITION_TRACKED_BIT;
        if(cinematicProjection&&cinematicUniforms&&(views.viewStateFlags&tracked)==tracked)
            next.stereoQuad=cinematicQuadProjection(*cinematicProjection,next,source.extent,camera::unitsPerMeter(),*cinematicUniforms);
        previousMenuQuad=quadView;pose=next;return true;
    }catch(const std::exception& error){
        failed=true;
        try{cancelStereoImpl();}catch(...){}
        try{log(std::string("STEREO_BEGIN_FAILED ")+error.what());}catch(...){}
        return false;
    }
}
