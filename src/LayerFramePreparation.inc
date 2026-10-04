bool quadSteamPrepareAllowed(const std::shared_ptr<State>& s){
 return s&&(!s->sfs||!argent::sfs::vrEnabled());
}
VkResult prepareStereo(const std::shared_ptr<State>& s,VkSwapchainKHR sc,uint32_t image){
 if(!s->sfs||!argent::sfs::vrEnabled())return VK_SUCCESS;
 argent::Source source;{std::lock_guard<std::recursive_mutex> lock(stateMutex);auto it=s->sources.find(sc);if(it==s->sources.end())return VK_SUCCESS;source=it->second;}
 argent::sfs::FramePose pose;XrPosef head;
 const auto options=argent::presentation::options();
 auto sample=argent::presentation::snapshot();sample.flatMenu=argent::hud::flatMenuVisible(true)||(options.cinematics3d&&sample.cutscene&&argent::hud::flatMenuVisible());
 const auto mode=argent::presentation::classify(sample,GetTickCount64(),options);
 bool nextQuad=s->menuQuad;
 const bool manualToggle=(GetAsyncKeyState(VK_F11)&1)!=0;
 if(options.automatic)nextQuad=s->presentation.update(mode,options,GetTickCount64());
 else if(manualToggle)nextQuad=!nextQuad;
 argent::presentation::scriptedMovement=mode==argent::presentation::Mode::Sync||mode==argent::presentation::Mode::Traversal||mode==argent::presentation::Mode::Interaction;
 argent::presentation::gameplayInput=options.automatic?mode==argent::presentation::Mode::Gameplay:!nextQuad;
 argent::presentation::worldPresentation=!nextQuad;
 argent::presentation::hideGameplayHud=argent::presentation::hideHudDuringAnimation(mode,nextQuad);
 const bool changedQuad=nextQuad!=s->menuQuad;
 if(changedQuad){s->menuQuad=nextQuad;if(!nextQuad)s->calibrated=false;argent::camera::stop();}
 if(mode!=s->presentationMode||changedQuad){s->presentationMode=mode;try{argent::log(std::string("SFS_MODE automatic=")+std::to_string(options.automatic)+" context="+argent::presentation::name(mode)+" quad="+std::to_string(s->menuQuad));}catch(...){}}
 argent::sfs::EyeUniforms uniforms{{argent::sfs::identity(),argent::sfs::identity()},{}};
 argent::sfs::Matrix cinematicProjection{};
 const bool wantCinema=argent::presentation::stereoCinematic(mode,options,s->menuQuad)&&s->camera.projection(cinematicProjection,100);
 if(!argent::beginStereoFrame(*s,source,pose,head,s->menuQuad,wantCinema?&cinematicProjection:nullptr,&uniforms)){argent::camera::stop();return VK_SUCCESS;}
 if(s->cinematicStereo!=pose.stereoQuad){s->cinematicStereo=pose.stereoQuad;
  try{argent::log(std::string("CINEMATIC_STEREO active=")+std::to_string(pose.stereoQuad)+" context="+argent::presentation::name(mode)+" cameraFresh="+std::to_string(wantCinema)+" headLocked="+std::to_string(pose.quadHeadLocked)+" tracked="+std::to_string(pose.headPositionTracked));}catch(...){}}
 argent::camera::beginRender(pose.serial);
 const bool recenter=!s->calibrated||pose.recenterRequested||(GetAsyncKeyState(VK_F12)&1);
 if(recenter){s->calibratedHead=head;s->calibrated=true;try{argent::log("SFS_RECENTER calibrated current head");}catch(...){}}
 argent::camera::updatePose(head,!s->menuQuad,recenter,pose.headPositionTracked);
 if(argent::extendedLogging()&&pose.serial%120==0)try{auto h=argent::camera::stats();argent::log("ETERNAL_HOOK calls="+std::to_string(h.calls)+" applied="+std::to_string(h.applied)+" rejected="+std::to_string(h.rejected));}catch(...){}
  argent::sfs::Matrix projection{};projection[0]=1;projection[5]=-float(source.extent.width)/float(source.extent.height);
  if(!s->menuQuad&&s->camera.projection(projection)){
  if(!argent::sfs::parallelEyeProjection(projection,pose.views,argent::camera::unitsPerMeter(),uniforms)){try{argent::cancelStereoFrame();}catch(...){}return VK_SUCCESS;}
  if(argent::extendedLogging()&&(pose.serial==1||pose.serial%120==0))try{argent::log("ETERNAL_CAMERA_STEREO serial="+std::to_string(pose.serial)+" fx="+std::to_string(projection[0])+" fy="+std::to_string(projection[5])+" unitsPerMeter="+std::to_string(argent::camera::unitsPerMeter())+" positionTracked="+std::to_string(pose.headPositionTracked));}catch(...){}
 }
  if(!s->menuQuad&&!argent::sfs::screenProjection(projection,head,pose.views,uniforms))try{argent::log("SFS_UI_PROJECTION_UNAVAILABLE");}catch(...){}
  static const auto gridMarker=[] {try{wchar_t p[32768]{};const auto n=GetEnvironmentVariableW(L"ARGENT_LOG",p,32768);return n&&n<32768?std::filesystem::path(p).parent_path()/L"test-light-grid":std::filesystem::path{};}catch(...){return std::filesystem::path{};}}();
  static bool gridTest=false;
  if(!argent::cleanRelease&&pose.serial%120==0&&!gridMarker.empty()){
   const auto attributes=GetFileAttributesW(gridMarker.c_str());const bool enabled=attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY);
   if(enabled!=gridTest){gridTest=enabled;try{argent::log("ETERNAL_GRID_TEST enabled="+std::to_string(gridTest));}catch(...){}}
  }
  const auto shaderPolicy=argent::presentation::shaderPolicy(s->menuQuad,pose.stereoQuad);
  uniforms.diagnostics[0]=shaderPolicy.centerGrid?1.f:0.f;
  uniforms.diagnostics[1]=shaderPolicy.worldWorkarounds?1.f:0.f;
  if(argent::extendedLogging()&&pose.stereoQuad&&pose.serial%120==0){
   try{argent::log("CINEMATIC_SHADER_POLICY worldWorkarounds="+std::to_string(shaderPolicy.worldWorkarounds)+
    " centerGrid="+std::to_string(shaderPolicy.centerGrid)+" centerGridDepthSafe="+std::to_string(shaderPolicy.centerGrid)+" fx="+std::to_string(cinematicProjection[0])+
    " gainLeft="+std::to_string(uniforms.clipFromCenter[0][0])+" gainRight="+std::to_string(uniforms.clipFromCenter[1][0])+
    " shiftWLeft="+std::to_string(uniforms.eyeTranslation[0][3])+" shiftWRight="+std::to_string(uniforms.eyeTranslation[1][3]));}catch(...){}
  }
  static bool coarseDecals=false;
  if(!argent::cleanRelease&&pose.serial%120==0&&!gridMarker.empty())try{
   const auto marker=gridMarker.parent_path()/L"test-decal-coarse";
   const auto attributes=GetFileAttributesW(marker.c_str());
   const bool enabled=attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY);
   if(enabled!=coarseDecals){coarseDecals=enabled;argent::log("ETERNAL_DECAL_COARSE enabled="+std::to_string(enabled));}
  }catch(...){}
  uniforms.diagnostics[2]=coarseDecals?1.f:0.f;
  static int screenProbe=0;
  if(!argent::cleanRelease&&pose.serial%120==0&&!gridMarker.empty())try{
   int requested=0;std::ifstream file(gridMarker.parent_path()/L"test-screen-mode");
   if(!(file>>requested)||requested<0||requested>5)requested=0;
   if(requested!=screenProbe){screenProbe=requested;argent::log("ETERNAL_SCREEN_PROBE mode="+std::to_string(screenProbe));}
  }catch(...){}
  uniforms.diagnostics[3]=!s->menuQuad?float(screenProbe):0.f;
  if(screenProbe==2)uniforms.diagnostics[0]=0.f;
  s->framePose=pose;argent::sfs::prepare(s->device,pose,uniforms);
 VkResult result;
 {static argent::FrameTiming::Totals t;argent::FrameTiming timing("uniformRetirementAndUpload",t);
  std::unique_lock<std::recursive_mutex> queueLock(*s->queueMutex,std::defer_lock);
  {static argent::FrameTiming::Totals queueWait;argent::FrameTiming timing("uniformQueueLockWait",queueWait);queueLock.lock();}
  result=argent::sfs::beginFrame(s->device,sc,image);}
 if(result!=VK_SUCCESS){
  argent::camera::stop();try{argent::cancelStereoFrame();}catch(...){}
  if(result==VK_ERROR_DEVICE_LOST)try{
   std::vector<VkQueue> queues;{std::lock_guard<std::recursive_mutex> lock(stateMutex);for(const auto& q:s->queues)queues.push_back(q.first);}
   for(auto queue:queues)argent::sfs::reportGpuCheckpoints(s->device,queue);
  }catch(...){}
 }
 return result;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice d,VkSwapchainKHR sc,uint64_t timeout,VkSemaphore sem,VkFence fence,uint32_t* image){
 auto s=deviceOf(key(d));VkResult r;
 if(argent::sfs::sourceSwapchain(d,sc)){static argent::FrameTiming::Totals t;argent::FrameTiming timing("sourceAcquire",t);r=argent::sfs::acquireSource(d,sc,timeout,sem,fence,image);}
 else r=s->proc<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR")(d,sc,timeout,sem,fence,image);
 if(r==VK_SUCCESS||r==VK_SUBOPTIMAL_KHR){if(quadSteamPrepareAllowed(s))argent::prepareSteamFrame(*s,sc);const auto prepared=prepareStereo(s,sc,*image);if(prepared!=VK_SUCCESS)return prepared;}return r;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImage2KHR(VkDevice d,const VkAcquireNextImageInfoKHR* info,uint32_t* image){
 auto s=deviceOf(key(d));if(argent::sfs::sourceSwapchain(d,info->swapchain))return info->deviceMask==1?vkAcquireNextImageKHR(d,info->swapchain,info->timeout,info->semaphore,info->fence,image):VK_ERROR_FEATURE_NOT_PRESENT;
 auto r=s->proc<PFN_vkAcquireNextImage2KHR>("vkAcquireNextImage2KHR")(d,info,image);
 if((r==VK_SUCCESS||r==VK_SUBOPTIMAL_KHR)&&image){if(quadSteamPrepareAllowed(s))argent::prepareSteamFrame(*s,info->swapchain);const auto prepared=prepareStereo(s,info->swapchain,*image);if(prepared!=VK_SUCCESS)return prepared;}
 return r;
}
