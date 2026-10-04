// OpenXR action lifecycle/bindings adapted from KHARVOX OpenXRBootstrap.
// Included inside QuadRuntime's anonymous namespace after XR()/check().
struct ControllerActions {
 XrActionSet set{};
 XrAction aim{},grip{},stick{},trigger{},squeeze{},lower{},upper{},click{},menu{},indexMenu{},haptic{};
 std::array<kharvox::XInputHapticOutputState,2> hapticState{};kharvox::XInputRumbleFrameAccumulator rumbleFrame;
 kharvox::WeaponRumbleContext weaponRumbleContext;
 std::array<XrPath,2> hands{};
 std::array<XrSpace,2> aimSpace{},gripSpace{};
 std::array<bool,2> heldGrip{};
 std::array<kharvox::GripControllerProfile,2> gripProfiles{};
 uint64_t syncCount{};
 bool ready{},punchDown{},indexMenuHeld{};
 input::GameplayMapping mapping;input::EternalMotionWheel motionWheel;
 std::array<kharvox::ControllerClickState,2> wheelClicks{};
 input::WeaponConfig configuration;input::TwoHandSupport support;
 std::array<input::HandSmoothing,2> handFilters{};
 input::WeaponPoseCalibration weaponEditor;
 input::ShoulderChainsaw shoulder;input::CrucibleGesture physicalKill;kharvox::HandsJumpState handsJump;
 std::map<std::string,XrVector3f> supportProfiles;
 ULONGLONG psvr2FireStartedTick{};KharvoxWeaponKind psvr2FireWeapon{KharvoxWeaponKind::Unknown};
 ULONGLONG configRead{};std::string configText;bool captureDown{};
#include "XrConfiguration.inc"

 KharvoxWeaponKind hapticWeapon() const {
  return player::hapticWeapon();
 }
 void updatePsvr2Triggers(bool focused,bool gameplay,bool fireDown){
  const auto weapon=hapticWeapon();const auto now=GetTickCount64();
  const bool active=configuration.psvr2AdaptiveTriggers&&focused&&gameplay;
  if(active&&fireDown&&weapon!=KharvoxWeaponKind::Unknown&&weapon!=KharvoxWeaponKind::Fists){
   if(!psvr2FireStartedTick||psvr2FireWeapon!=weapon){psvr2FireStartedTick=now;psvr2FireWeapon=weapon;}
  }else{psvr2FireStartedTick=0;psvr2FireWeapon=KharvoxWeaponKind::Unknown;}
  kharvox::psvr2::TriggerPolicyInput input{
   configuration.psvr2AdaptiveTriggers,
   kharvox::isSteamBackedOpenXRRuntime(runtimeKind),
   focused,
   gameplay,
   weapon,
   fireDown,
   psvr2FireStartedTick?now-psvr2FireStartedTick:0,
   KharvoxWeaponAmmoState::Unknown,
   configuration.leftHanded,
   focused?kharvox::psvr2::TriggerOffReason::Menu:kharvox::psvr2::TriggerOffReason::Unfocused};
  const auto command=kharvox::psvr2::selectTriggerCommand(input);
  if(configuration.psvr2AdaptiveTriggers&&extendedLogging()){
   static kharvox::psvr2::TriggerCommand previous;static auto previousWeapon=KharvoxWeaponKind::Unknown;
   if(command!=previous||weapon!=previousWeapon){
    log("PSVR2_TRIGGER weapon="+std::to_string(int(weapon))+" effect="+std::to_string(int(command.effect))+" offReason="+std::to_string(int(command.offReason)));
    previous=command;previousWeapon=weapon;
   }
  }
  KharvoxPsvr2SubmitTrigger(command);
 }
 void updateHaptics(bool focused,bool gameplay=false,bool fireDown=false,bool twoHand=false){
  const auto now=GetTickCount64();
  auto current=focused&&now-input::rumbleTick.load()<500?input::rumble.load():0;
  auto peak=input::rumblePeak.exchange(0);if(!focused){peak=0;rumbleFrame={};wheelClicks={};motionWheel={};}
  auto packed=kharvox::selectXInputRumbleForFrame(rumbleFrame,current,peak,now);
  auto nativeSignal=kharvox::mapXInputRumble(kharvox::xinputRumbleLowMotor(packed),kharvox::xinputRumbleHighMotor(packed));
  const auto fanout=kharvox::selectAdditiveHapticFanout(packed,nativeSignal,false);
  if(configuration.bhapticsEnabled)KharvoxBhapticsSubmitRumble(kharvox::xinputRumbleLowMotor(fanout.bhapticsRumble),kharvox::xinputRumbleHighMotor(fanout.bhapticsRumble));
  else KharvoxBhapticsSubmitRumble(0,0);
  const bool weaponRumble=weaponRumbleContext.update(focused&&gameplay,fireDown,fanout.controllerSignal.active,now);
  const auto desired=kharvox::routeXInputHapticSignal(fanout.controllerSignal,weaponRumble,configuration.leftHanded,twoHand);
  for(int hand=0;hand<2;++hand){
   const auto signal=kharvox::mixControllerClick(desired[hand],wheelClicks[hand],now);
   const auto duration=kharvox::controllerHapticDurationMilliseconds(desired[hand],wheelClicks[hand],now);
   auto command=kharvox::selectXInputHapticCommand(hapticState[hand],signal,now);
   XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};info.action=haptic;info.subactionPath=hands[hand];
   if(command==kharvox::XInputHapticCommand::Apply){XrHapticVibration pulse{XR_TYPE_HAPTIC_VIBRATION};pulse.amplitude=signal.amplitude;pulse.frequency=signal.frequencyHz;pulse.duration=XrDuration(duration)*1000000;
    if(XR_SUCCEEDED(XR(xrApplyHapticFeedback)(session,&info,reinterpret_cast<XrHapticBaseHeader*>(&pulse))))kharvox::noteXInputHapticApplySucceeded(hapticState[hand],signal,now,duration);else kharvox::noteXInputHapticApplyFailed(hapticState[hand],now);
   }else if(command==kharvox::XInputHapticCommand::Stop){XR(xrStopHapticFeedback)(session,&info);kharvox::noteXInputHapticStopAttempted(hapticState[hand]);}
  }
 }
#include "XrActionLifecycle.inc"
 void update(XrTime time,bool focused,bool world){
  if(!ready||!focused){if(ready){updateHaptics(false);updatePsvr2Triggers(false,false,false);}input::clear();heldGrip={};indexMenuHeld=false;syncCount=0;mapping={};motionWheel={};wheelClicks={};support={};shoulder={};physicalKill={};handsJump={};handFilters={};captureDown=false;return;}
  try{
   XrActiveActionSet active{set,XR_NULL_PATH};XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&active;
   if(XR(xrSyncActions)(session,&sync)!=XR_SUCCESS){updatePsvr2Triggers(false,false,false);input::clear();heldGrip={};indexMenuHeld=false;syncCount=0;motionWheel={};wheelClicks={};physicalKill={};handsJump={};handFilters={};return;}
   input::Snapshot sample;sample.tick=GetTickCount64();input::HandsInput controls;
   std::array<XrVector3f,2> velocity{};std::array<bool,2> velocityValid{},gripVelocityValid{};std::array<float,2> gripUpSpeed{};
   if(syncCount++%120==0)for(int h=0;h<2;++h){
    XrInteractionProfileState profile{XR_TYPE_INTERACTION_PROFILE_STATE};
    if(XR_SUCCEEDED(XR(xrGetCurrentInteractionProfile)(session,hands[h],&profile))){
     const auto selected=profile.interactionProfile==path("/interaction_profiles/valve/index_controller")?kharvox::GripControllerProfile::ValveIndex:kharvox::GripControllerProfile::Default;
     if(selected!=gripProfiles[h]){heldGrip[h]=false;if(h==0)indexMenuHeld=false;}
     if(syncCount==1||selected!=gripProfiles[h]){
      const auto thresholds=kharvox::gripThresholds(selected);
      log("XR_GRIP_PROFILE hand="+std::to_string(h)+" component="+kharvox::gripInputComponent(selected)+" press="+std::to_string(thresholds.press)+" release="+std::to_string(thresholds.release));
     }
     gripProfiles[h]=selected;
    }
   }
   for(int h=0;h<2;++h){
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};info.subactionPath=hands[h];
    auto boolean=[&](XrAction a){info.action=a;XrActionStateBoolean value{XR_TYPE_ACTION_STATE_BOOLEAN};check(XR(xrGetActionStateBoolean)(session,&info,&value),"boolean action");sample.active|=value.isActive!=0;return value.isActive&&value.currentState;};
    auto scalar=[&](XrAction a){info.action=a;XrActionStateFloat value{XR_TYPE_ACTION_STATE_FLOAT};check(XR(xrGetActionStateFloat)(session,&info,&value),"float action");sample.active|=value.isActive!=0;return value.isActive?value.currentState:0.f;};
    info.action=stick;XrActionStateVector2f value{XR_TYPE_ACTION_STATE_VECTOR2F};check(XR(xrGetActionStateVector2f)(session,&info,&value),"stick action");sample.active|=value.isActive!=0;
    controls.stick[h]=value.isActive?value.currentState:XrVector2f{};
    controls.trigger[h]=scalar(trigger);
    heldGrip[h]=kharvox::updateGripPressed(scalar(squeeze),heldGrip[h],gripProfiles[h]);
    controls.grip[h]=heldGrip[h];controls.lower[h]=boolean(lower);controls.upper[h]=boolean(upper);
    controls.click[h]=boolean(click);controls.menu[h]=boolean(menu);
    if(h==0){indexMenuHeld=input::updateIndexPause(scalar(indexMenu),indexMenuHeld);controls.menu[h]=controls.menu[h]||indexMenuHeld;}
    auto locate=[&](XrAction a,XrSpace actionSpace,XrPosef& pose,bool& valid){
     info.action=a;XrActionStatePose status{XR_TYPE_ACTION_STATE_POSE};check(XR(xrGetActionStatePose)(session,&info,&status),"pose action");
     if(!status.isActive)return;XrSpaceVelocity motion{XR_TYPE_SPACE_VELOCITY};XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};location.next=&motion;check(XR(xrLocateSpace)(actionSpace,localSpace,time,&location),"controller pose");
     constexpr auto required=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
     valid=(location.locationFlags&required)==required;if(valid)pose=location.pose;
     if(a==grip){gripVelocityValid[h]=valid&&(motion.velocityFlags&XR_SPACE_VELOCITY_LINEAR_VALID_BIT);gripUpSpeed[h]=motion.linearVelocity.y;}
     if(a==aim){velocityValid[h]=valid&&(motion.velocityFlags&XR_SPACE_VELOCITY_LINEAR_VALID_BIT);velocity[h]=motion.linearVelocity;}
    };
    locate(aim,aimSpace[h],sample.aim[h],sample.aimValid[h]);locate(grip,gripSpace[h],sample.grip[h],sample.gripValid[h]);
   }
   const auto rawAim=sample.aim,rawGrip=sample.grip;
   refreshConfiguration();
   hud::setTutorialBindingLayout({configuration.leftHanded,configuration.leftHandSwapSticks,
    gripProfiles[0]==kharvox::GripControllerProfile::ValveIndex,gripProfiles[1]==kharvox::GripControllerProfile::ValveIndex});
   const int primary=configuration.leftHanded?0:1,off=1-primary;
   const int turnHand=configuration.leftHanded&&configuration.leftHandSwapSticks?0:1;
   const auto presentationState=presentation::snapshot();
   const bool tutorialInput=hud::flatMenuVisible(false,true)&&!presentationState.dossierMenu;
   const bool gameplay=world&&presentation::gameplayInput.load()&&!tutorialInput;
   const std::string activeProfile=player::weaponProfile();
   const bool crucible=player::crucibleEquipped()||activeProfile=="sentinel_hammer";
   const auto demon=revenant::actor.load();sample.revenantActor=gameplay?demon:0;
   const bool calibrationActive=configuration.calibrationMode!="off";
   const bool captureSupport=configuration.captureSupport||configuration.calibrationMode=="support";
   const bool smoothHands=configuration.handSmoothing&&gameplay&&!demon&&!calibrationActive&&!captureSupport&&
    !presentation::nativeAnimation.load()&&!presentation::syncAttack.load()&&!presentation::scriptedMovement.load()&&!presentation::attachedWallClimb();
   for(int h=0;h<2;++h)handFilters[h].update(sample.grip[h],sample.aim[h],sample.gripValid[h],sample.aimValid[h],time,smoothHands);
   hud::pollHandCalibration(gameplay&&configuration.calibrationMode=="hud"&&!presentation::syncAttack.load(),configuration.leftHanded);
   weaponEditor.load(runtimePath());
   weaponEditor.configure(configuration.profile,configuration.leftHanded,configuration.calibrationMode=="weapon",configuration.calibrationApply);
   weaponEditor.poll(configuration.profile,configuration.leftHanded,gameplay&&configuration.calibrationMode=="weapon"&&
    input::WeaponPoseCalibration::canEdit(configuration.profile,activeProfile)&&!presentation::syncAttack.load());
   XrSpaceLocation headLocation{XR_TYPE_SPACE_LOCATION};
   const auto headResult=XR(xrLocateSpace)(space,localSpace,time,&headLocation);
   constexpr XrSpaceLocationFlags tracked=XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
   const bool headTracked=XR_SUCCEEDED(headResult)&&(headLocation.locationFlags&tracked)==tracked;
    const bool saw=shoulder.update(headLocation.pose,rawGrip[primary],headTracked&&sample.gripValid[primary],gameplay&&!demon&&configuration.shoulderChainsaw&&!calibrationActive&&!captureSupport&&!controls.grip[off]&&!mapping.wheelHeld&&controls.stick[turnHand].y>=-.75f,controls.grip[primary],configuration.leftHanded,sample.tick);
   if(shoulder.consumed)controls.grip[primary]=false;
   sample.managedTurn=gameplay;sample.smoothSpeed=configuration.smoothSpeed;sample.snapTurn=gameplay&&configuration.snapTurn;sample.snapDegrees=configuration.snapDegrees;sample.dominant=primary;sample.offhandMovement=configuration.offhandMovement;
   sample.weaponPivot=configuration.weaponPivot;
   sample.laserSight=configuration.laserSight;
   auto calibration=configuration.selectedFor(activeProfile);
   const auto baseCalibration=calibration;
   weaponEditor.apply(calibration,activeProfile,configuration.leftHanded);
   std::copy_n(activeProfile.c_str(),std::min(activeProfile.size(),sample.weaponProfile.size()-1),sample.weaponProfile.data());
   sample.weaponCorrection=std::memcmp(&baseCalibration.pose,&calibration.pose,sizeof(calibration.pose))!=0;
   if(extendedLogging()){
    static std::string previous;
    const auto status=activeProfile+" mode="+(configuration.leftHanded?"left":"right")+" correction="+std::to_string(sample.weaponCorrection);
    if(status!=previous){previous=status;log("XR_WEAPON_CALIBRATION "+status+" sharedRoot=baseline");}
   }
   // Separate profile support captures from hand/weapon alignment settings.
   if(auto it=supportProfiles.find(activeProfile);it!=supportProfiles.end())calibration.support=it->second;
   sample.weaponValid=gameplay&&!demon&&sample.aimValid[primary]&&sample.gripValid[primary];
   if(sample.weaponValid){
    sample.weapon=input::calibrated(sample.aim[primary],sample.grip[primary],configuration.hand[primary],calibration);
    sample.weaponBase=input::calibrated(sample.aim[primary],sample.grip[primary],configuration.hand[primary],baseCalibration);
    const bool capture=captureSupport&&controls.trigger[primary]>.7f;
    if(capture&&!captureDown&&controls.grip[off]&&sample.gripValid[off]){
     auto v=input::rotate(input::inverse(sample.weapon.orientation),input::sub(sample.grip[off].position,sample.weapon.position));
     if(input::length(v)>=.08f&&input::length(v)<=1.2f){
      if(input::saveSupportCalibration(configPath(),activeProfile,v,supportProfiles)){calibration.support=v;log("XR_TWO_HAND support captured profile="+activeProfile);}else log("XR_TWO_HAND capture save failed");
     }
    }
    captureDown=capture;
    const auto beforeSupport=sample.weapon.orientation;
    sample.weapon.orientation=support.update(sample.weapon,sample.grip[off],sample.gripValid[off],controls.grip[off],calibration);
    sample.weaponBase.orientation=input::normalized(input::product(input::product(sample.weapon.orientation,input::inverse(beforeSupport)),sample.weaponBase.orientation));
    sample.twoHand=support.latched;
   }else {support={};captureDown=false;}
   if(gameplay){
    if(!demon&&!sample.weaponValid){controls.trigger[primary]=0;controls.grip[primary]=false;}
    if(sample.twoHand)controls.grip[off]=false; // Support grip cannot also cycle equipment.
    if(configuration.leftHanded){std::swap(controls.trigger[0],controls.trigger[1]);std::swap(controls.grip[0],controls.grip[1]);}
   }
   input::routeLeftHandButtons(controls,configuration.leftHanded,configuration.leftHandSwapSticks);
   // Filter logical action channels after side routing; wheel/Crucible stay
   // available for calibration in both layouts.
   if(gameplay&&(calibrationActive||captureSupport))mapping.filterCalibration(controls);
   // Keep held locomotion through traversal/kill animations without enabling
   // manual weapon/camera control while those animations own the player.
   const bool mappingWorld=input::gameplayMappingContext(world,gameplay,presentation::scriptedMovement.load());
   sample.pad=mapping.map(controls,mappingWorld,sample.tick,configuration.legacyLayout,presentationState.dossierMenu,tutorialInput,demon!=0);
   // Selection follows the dominant physical grip; stick axes were already
   // routed by the left-hand layout and filtered by the menu context guard.
   const auto wheel=motionWheel.update(sample.pad,gameplay&&!demon&&mapping.wheelHeld,
    configuration.leftHanded,configuration.leftHandSwapSticks,rawGrip[primary],
    headTracked&&sample.gripValid[primary],headLocation.pose.orientation);
   wheelClicks[1-wheel.hand]={};
   if(!wheel.active||(!wheel.stickOwned&&!wheel.tracked))wheelClicks={};
   else if(wheel.click)kharvox::queueControllerClick(wheelClicks[wheel.hand],sample.tick);
   if(saw)sample.pad.wButtons|=XINPUT_GAMEPAD_X;
   // Crucible limits physical punch gestures to the off-hand below. Explicit
   // stick-click Use/melee input must remain available for native interactions.
   static uint64_t crucibleSwingTick{};static std::string gestureProfile;
   if(gestureProfile!=activeProfile){physicalKill={};handsJump={};handFilters={};crucibleSwingTick=0;gestureProfile=activeProfile;}
   const auto gesture=physicalKill.update(time,gameplay&&!demon&&configuration.physicalGloryKill&&!calibrationActive&&!captureSupport&&!presentation::syncAttack.load()&&!(sample.pad.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER)&&!shoulder.consumed,crucible,primary,configuration.gloryHands,configuration.glorySpeed,rawAim,velocity,velocityValid);
   sample.manualWeaponTrigger=sample.pad.bRightTrigger>20;
   if(gesture.fire&&physicalKill.sword.pulse==time+200000000)crucibleSwingTick=sample.tick;
   sample.crucibleSwing=crucibleSwingTick;
   if(gesture.fire)sample.pad.bRightTrigger=255;
   if(extendedLogging()&&gesture.fire&&physicalKill.sword.pulse==time+200000000)log("XR_CRUCIBLE_SWING trigger=right threshold="+std::to_string(configuration.glorySpeed));
   const bool punch=gesture.punch;
   if(punch&&!(sample.pad.wButtons&XINPUT_GAMEPAD_LEFT_THUMB)){sample.pad.wButtons|=XINPUT_GAMEPAD_RIGHT_THUMB;if(!punchDown)log("XR_PHYSICAL_GLORY_KILL pulse=200ms threshold="+std::to_string(configuration.glorySpeed));}punchDown=punch;
   const auto previousJumpPulse=handsJump.pulseUntil;
   const bool handsJumpDown=handsJump.update(time,
    configuration.handsJump&&gameplay&&!demon&&!calibrationActive&&!captureSupport&&!mapping.wheelHeld&&
    !presentation::syncAttack.load()&&!presentation::scriptedMovement.load()&&!shoulder.consumed,
    gripVelocityValid[0]&&gripVelocityValid[1],gripUpSpeed[0],gripUpSpeed[1]);
   if(handsJumpDown)sample.pad.wButtons|=XINPUT_GAMEPAD_A;
   if(extendedLogging()&&handsJump.pulseUntil>previousJumpPulse)log("XR_HANDS_JUMP threshold=1.9 leftUp="+std::to_string(gripUpSpeed[0])+" rightUp="+std::to_string(gripUpSpeed[1]));
   const bool fireDown=sample.pad.bRightTrigger>20;
   input::publish(sample);updatePsvr2Triggers(true,gameplay,fireDown);updateHaptics(true,gameplay,fireDown,sample.twoHand);
   if(extendedLogging()&&stereoSerial%300==0)log("XR_INPUT active="+std::to_string(sample.active)+" leftPose="+std::to_string(sample.aimValid[0])+" rightPose="+std::to_string(sample.aimValid[1])+" gamePolls="+std::to_string(input::getStateCalls.load())+" injected="+std::to_string(input::injectedCalls.load()));
  }catch(const std::exception& e){input::clear();heldGrip={};indexMenuHeld=false;syncCount=0;motionWheel={};wheelClicks={};physicalKill={};handsJump={};handFilters={};log(std::string("XR_INPUT_SYNC_FAILED ")+e.what());}
 }
} controllerActions;
