 void destroy(){
  if(ready){
   try{updateHaptics(false);}catch(...){}
   try{updatePsvr2Triggers(false,false,false);}catch(...){}
  }
  KharvoxPsvr2SubmitTrigger(kharvox::psvr2::offCommand(configuration.leftHanded,kharvox::psvr2::TriggerOffReason::SessionEnd));
  KharvoxPsvr2IpcRequestStop();KharvoxBhapticsIpcRequestStop();
  input::clear();ready=false;heldGrip={};handFilters={};indexMenuHeld=false;syncCount=0;gripProfiles={};
  for(auto& h:aimSpace)if(h){try{XR(xrDestroySpace)(h);}catch(...){}h=XR_NULL_HANDLE;}
  for(auto& h:gripSpace)if(h){try{XR(xrDestroySpace)(h);}catch(...){}h=XR_NULL_HANDLE;}
  if(set){try{XR(xrDestroyActionSet)(set);}catch(...){}set=XR_NULL_HANDLE;}
 }
 XrPath path(const char* text){XrPath p{};check(XR(xrStringToPath)(instance,text,&p),text);return p;}
 void create(){
  try{
   hands={path("/user/hand/left"),path("/user/hand/right")};
   XrActionSetCreateInfo si{XR_TYPE_ACTION_SET_CREATE_INFO};strcpy_s(si.actionSetName,"argent_gameplay");strcpy_s(si.localizedActionSetName,"ARGENT Gameplay");
   check(XR(xrCreateActionSet)(instance,&si,&set),"create controller actions");
   auto action=[&](const char* name,XrActionType type,XrAction& out){
    XrActionCreateInfo ai{XR_TYPE_ACTION_CREATE_INFO};strcpy_s(ai.actionName,name);strcpy_s(ai.localizedActionName,name);ai.actionType=type;ai.countSubactionPaths=2;ai.subactionPaths=hands.data();
    check(XR(xrCreateAction)(set,&ai,&out),name);
   };
   action("aim",XR_ACTION_TYPE_POSE_INPUT,aim);action("grip",XR_ACTION_TYPE_POSE_INPUT,grip);
   action("stick",XR_ACTION_TYPE_VECTOR2F_INPUT,stick);action("trigger",XR_ACTION_TYPE_FLOAT_INPUT,trigger);
   action("squeeze",XR_ACTION_TYPE_FLOAT_INPUT,squeeze);action("lower_button",XR_ACTION_TYPE_BOOLEAN_INPUT,lower);
   action("upper_button",XR_ACTION_TYPE_BOOLEAN_INPUT,upper);action("stick_click",XR_ACTION_TYPE_BOOLEAN_INPUT,click);
   action("menu",XR_ACTION_TYPE_BOOLEAN_INPUT,menu);action("haptic",XR_ACTION_TYPE_VIBRATION_OUTPUT,haptic);
   action("index_pause_force",XR_ACTION_TYPE_FLOAT_INPUT,indexMenu);
   auto suggest=[&](const char* profile,bool index){
    std::vector<XrActionSuggestedBinding> bindings;
    for(int h=0;h<2;++h){
     const std::string hand=h?"/user/hand/right/input/":"/user/hand/left/input/";
     auto bind=[&](XrAction a,const char* control){bindings.push_back({a,path((hand+control).c_str())});};
     bindings.push_back({haptic,path((std::string(h?"/user/hand/right/output/haptic":"/user/hand/left/output/haptic")).c_str())});
     bind(aim,"aim/pose");bind(grip,"grip/pose");bind(stick,"thumbstick");bind(click,"thumbstick/click");
     bind(trigger,"trigger/value");bind(squeeze,kharvox::gripInputComponent(index?kharvox::GripControllerProfile::ValveIndex:kharvox::GripControllerProfile::Default));
     bind(lower,input::faceButtonComponent(index,h,false));bind(upper,input::faceButtonComponent(index,h,true));
     if(h==0)bind(index?indexMenu:menu,input::pauseComponent(index));
    }
    XrInteractionProfileSuggestedBinding info{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};info.interactionProfile=path(profile);info.countSuggestedBindings=uint32_t(bindings.size());info.suggestedBindings=bindings.data();
    auto result=XR(xrSuggestInteractionProfileBindings)(instance,&info);log(std::string("XR_INPUT_BINDINGS ")+profile+" result="+std::to_string(result));
   };
   suggest("/interaction_profiles/oculus/touch_controller",false);
   suggest("/interaction_profiles/valve/index_controller",true);
   std::vector<XrActionSuggestedBinding> simple;
   for(int h=0;h<2;++h){std::string prefix=h?"/user/hand/right/input/":"/user/hand/left/input/";
    simple.push_back({haptic,path(h?"/user/hand/right/output/haptic":"/user/hand/left/output/haptic")});
    simple.push_back({aim,path((prefix+"aim/pose").c_str())});simple.push_back({grip,path((prefix+"grip/pose").c_str())});
    simple.push_back({lower,path((prefix+"select/click").c_str())});simple.push_back({menu,path((prefix+"menu/click").c_str())});}
   XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};sb.interactionProfile=path("/interaction_profiles/khr/simple_controller");sb.countSuggestedBindings=uint32_t(simple.size());sb.suggestedBindings=simple.data();XR(xrSuggestInteractionProfileBindings)(instance,&sb);
   for(int h=0;h<2;++h){XrActionSpaceCreateInfo info{XR_TYPE_ACTION_SPACE_CREATE_INFO};info.subactionPath=hands[h];info.poseInActionSpace.orientation.w=1;
    info.action=aim;check(XR(xrCreateActionSpace)(session,&info,&aimSpace[h]),"aim space");info.action=grip;check(XR(xrCreateActionSpace)(session,&info,&gripSpace[h]),"grip space");}
   XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&set;
   check(XR(xrAttachSessionActionSets)(session,&attach),"attach controller actions");ready=true;
   KharvoxBhapticsIpcStart();
   KharvoxPsvr2IpcStart();
   log(std::string("XR_INPUT_READY XInputGetState=")+(input::install()?"hooked":"unavailable"));
   log(std::string("XR_HAPTICS XInputSetState=")+(input::installHaptics()?"hooked":"unavailable"));
  }catch(const std::exception& e){destroy();try{log(std::string("XR_INPUT_FAILED ")+e.what());}catch(...){}}
 }
