void events(){
    for(;;){
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};auto r=XR(xrPollEvent)(instance,&event);
        if(r==XR_EVENT_UNAVAILABLE)break;check(r,"xrPollEvent");
        if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)throw std::runtime_error("XR instance loss pending");
        if(event.type==XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED){
            const auto& changed=*reinterpret_cast<XrEventDataInteractionProfileChanged*>(&event);
            if(changed.session==session)controllerActions.syncCount=0;
        }
        if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING){
            const auto& change=*reinterpret_cast<XrEventDataReferenceSpaceChangePending*>(&event);
            if(change.session==session&&change.referenceSpaceType==XR_REFERENCE_SPACE_TYPE_LOCAL)referenceChangeTime=change.changeTime;
        }
        if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){
            auto& state=*reinterpret_cast<XrEventDataSessionStateChanged*>(&event);if(state.session!=session)continue;
            sessionState=state.state;try{log("XR_STATE="+std::to_string(state.state));}catch(...){}
            if(state.state!=XR_SESSION_STATE_FOCUSED)input::clear();
            if(state.state==XR_SESSION_STATE_READY&&!running){XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};bi.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;check(XR(xrBeginSession)(session,&bi),"xrBeginSession");running=true;}
            if(state.state==XR_SESSION_STATE_STOPPING&&running){
                check(XR(xrEndSession)(session),"xrEndSession");running=false;
                steamFramePrepared=steamFrameBegun=stereoPending.begun=false;
            }
            if(state.state==XR_SESSION_STATE_EXITING||state.state==XR_SESSION_STATE_LOSS_PENDING)throw std::runtime_error("XR session exiting/lost");
        }
    }
}
