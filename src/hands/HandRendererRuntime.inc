HandRenderer::HandRenderer():impl_(new Impl){}
HandRenderer::~HandRenderer(){shutdown();delete impl_;}

bool HandRenderer::initialize(VkPhysicalDevice physicalDevice,VkDevice device,
    VkQueue queue,std::uint32_t queueFamily,const KharvoxVulkanDispatch&dispatch,
    VkFormat colorFormat,const std::array<VkExtent2D,2>&eyeExtents,
    const std::array<std::vector<VkImage>,2>&eyeImages,
    const std::wstring&runtimeDirectory,HandLog logger,bool arrayStereo,bool sceneDepthOnly){
    shutdown();impl_->physical=physicalDevice;impl_->device=device;impl_->queue=queue;impl_->queueFamily=queueFamily;impl_->vk=dispatch;impl_->format=colorFormat;impl_->extents=eyeExtents;impl_->root=runtimeDirectory;impl_->log=std::move(logger);
    std::unordered_map<std::string,std::string>config;std::ifstream input(impl_->root/L"hand_models.cfg");std::string line;while(std::getline(input,line)){line=trim(line);if(line.empty()||line[0]=='#')continue;const auto equals=line.find('=');if(equals!=std::string::npos)config[trim(line.substr(0,equals))]=trim(line.substr(equals+1));}
    if(!input&&!std::filesystem::exists(impl_->root/L"hand_models.cfg")){impl_->say("hand_models.cfg missing; static hands disabled");return false;}
    auto vector3=[&](const char*key,float value[3]){std::istringstream stream(config[key]);return bool(stream>>value[0]>>value[1]>>value[2]);};vector3("left_position",impl_->leftCalibration.position);vector3("left_rotation",impl_->leftCalibration.rotationDegrees);vector3("right_position",impl_->rightCalibration.position);vector3("right_rotation",impl_->rightCalibration.rotationDegrees);try{if(config.count("left_scale"))impl_->leftCalibration.scale=std::clamp(std::stof(config["left_scale"]),.05f,2.f);if(config.count("right_scale"))impl_->rightCalibration.scale=std::clamp(std::stof(config["right_scale"]),.05f,2.f);}catch(...){impl_->say("invalid hand scale; using safe defaults");}impl_->mirrorX[0]=config["left_fist_mirror_x"]=="1";impl_->mirrorX[1]=config["right_fist_mirror_x"]=="1";impl_->mirrorX[2]=config["left_gun_mirror_x"]=="1";impl_->mirrorX[3]=config["right_gun_mirror_x"]=="1";
    impl_->loadCalibrationFile(L"hand_models_calibration_default.cfg",true);
    impl_->defaultLeftCalibration=impl_->leftCalibration;impl_->defaultRightCalibration=impl_->rightCalibration;
    impl_->defaultWeaponCalibrations=impl_->weaponCalibrations;
    impl_->loadCalibrationFile(L"hand_models_calibration_saved.cfg",false);
    impl_->calibrationDraft={};
    for(const auto* filename:{L"hand_pose_calibration_default.cfg",L"hand_pose_calibration_saved.cfg"})
        impl_->loadPoseCalibrationFile(filename);

    char calibrationMode[24]{};
    if(GetEnvironmentVariableA("ARGENT_HAND_CALIBRATION",calibrationMode,sizeof(calibrationMode))>0){
        if(!_stricmp(calibrationMode,"rotation"))impl_->calibrationMode=CalibrationMode::Rotation;
        else if(!_stricmp(calibrationMode,"position"))impl_->calibrationMode=CalibrationMode::Position;
    }
    if(!impl_->createCommonResources()){impl_->say("Vulkan setup failed; native game rendering continues");shutdown();return false;}
    const std::array<const char*,4>keys{{"left_fist","right_fist","left_gun","right_gun"}};for(size_t i=0;i<keys.size();i++){const auto found=config.find(keys[i]);if(found==config.end()||found->second.empty()){impl_->say(std::string(keys[i])+" not configured; that hand pose is disabled");continue;}const auto path=impl_->root/std::filesystem::u8path(found->second);if(!std::filesystem::exists(path)){impl_->say(path.filename().u8string()+" missing; model disabled");continue;}impl_->loadAsset(path,impl_->assets[i]);}
    // One closed beam mesh: no crossed compositor ribbons or orientation singularity.
    std::vector<Vertex> beamVertices;
    std::vector<uint32_t> beamIndices;
    for(unsigned ring=0;ring<2;++ring)for(unsigned i=0;i<8;++i){
        const float angle=float(i)*6.28318530718f/8.f;
        Vertex v{};v.position[0]=.002f*std::cos(angle);v.position[1]=.002f*std::sin(angle);
        v.position[2]=ring?-20.f:0.f;v.normal[0]=std::cos(angle);v.normal[1]=std::sin(angle);
        v.baseColor[0]=1.f;v.baseColor[1]=.005f;v.baseColor[2]=.002f;v.material[0]=-1.f;
        beamVertices.push_back(v);
    }
    for(uint32_t i=0;i<8;++i){const auto n=(i+1)%8;
        for(auto v:{i,n,i+8,n,n+8,i+8})beamIndices.push_back(v);
    }
    impl_->laserAsset.ready=impl_->buffer(beamVertices.size()*sizeof(Vertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        beamVertices.data(),impl_->laserAsset.vertices,impl_->laserAsset.vertexMemory)
        &&impl_->buffer(beamIndices.size()*sizeof(uint32_t),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        beamIndices.data(),impl_->laserAsset.indices,impl_->laserAsset.indexMemory);
    impl_->laserAsset.indexCount=static_cast<uint32_t>(beamIndices.size());
    for (size_t eye = 0; eye < 2; eye++) {
      impl_->eyeViews[eye].resize(eyeImages[eye].size());
      impl_->depthTargets[eye].resize(eyeImages[eye].size());
      impl_->framebuffers[eye].resize(eyeImages[eye].size());
      for (size_t index = 0; index < eyeImages[eye].size(); index++) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = eyeImages[eye][index];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = colorFormat;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        view.subresourceRange.baseArrayLayer = arrayStereo ? uint32_t(eye) : 0;
        if (impl_->vk.createImageView(device, &view, nullptr,
                                      &impl_->eyeViews[eye][index]) !=
            VK_SUCCESS)
          continue;
        if(sceneDepthOnly)continue;
        if(!impl_->depthTarget(eyeExtents[eye],impl_->depthTargets[eye][index])){
          impl_->say("depth buffer creation failed; hand surface disabled for one eye image");
          continue;
        }
        const std::array<VkImageView,2>attachments{{impl_->eyeViews[eye][index],impl_->depthTargets[eye][index].view}};
        VkFramebufferCreateInfo framebuffer{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer.renderPass = impl_->renderPass;
        framebuffer.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebuffer.pAttachments = attachments.data();
        framebuffer.width = eyeExtents[eye].width;
        framebuffer.height = eyeExtents[eye].height;
        framebuffer.layers = 1;
        impl_->vk.createFramebuffer(device, &framebuffer, nullptr,
                                    &impl_->framebuffers[eye][index]);
      }
    }
    if(!sceneDepthOnly)for(const auto& eye:impl_->framebuffers){
      if(eye.empty()||std::any_of(eye.begin(),eye.end(),[](VkFramebuffer framebuffer){return !framebuffer;})){
        impl_->say("incomplete stereo targets; both hands disabled to avoid one-eye rendering");
        return false;
      }
    }
    for(const auto& eye:impl_->eyeViews)if(eye.empty()||std::any_of(eye.begin(),eye.end(),[](VkImageView v){return !v;}))return false;
    const auto available=availability();impl_->initialized=available.leftFist||available.rightFist||available.leftGun||available.rightGun;impl_->say(std::string("renderer ")+(impl_->initialized?"ready":"has no usable assets")+(sceneDepthOnly?"; scene-depth hands, constant environment lighting":"; self-depth overlay hands, constant environment lighting"));impl_->writeCalibrationStatus("Num 0 switches hand; Num 5 resets selected hand");return impl_->initialized;
}

void HandRenderer::shutdown() {
  if (!impl_ || !impl_->device)
    return;
  if (impl_->queue && impl_->vk.queueWaitIdle)
    impl_->retireQueue();
  impl_->cachedSceneFramebuffers.clearAfterCompletion(impl_->device,impl_->vk);
  for(auto framebuffer:impl_->sceneFramebuffers)
    if(framebuffer)impl_->vk.destroyFramebuffer(impl_->device,framebuffer,nullptr);
  for(auto& copy:impl_->sceneDepthCopies)impl_->destroyDepth(copy.target);
  for(auto&scene:impl_->scenePipelines){
    if(scene.hudMask)impl_->vk.destroyPipeline(impl_->device,scene.hudMask,nullptr);
    if(scene.hudPlaceholder)impl_->vk.destroyPipeline(impl_->device,scene.hudPlaceholder,nullptr);
    if(scene.pipeline)impl_->vk.destroyPipeline(impl_->device,scene.pipeline,nullptr);
    if(scene.renderPass)impl_->vk.destroyRenderPass(impl_->device,scene.renderPass,nullptr);
  }
  for (auto &eye : impl_->framebuffers)
    for (auto framebuffer : eye)
      if (framebuffer)
        impl_->vk.destroyFramebuffer(impl_->device, framebuffer, nullptr);
  for (auto &eye : impl_->eyeViews)
    for (auto view : eye)
      if (view)
        impl_->vk.destroyImageView(impl_->device, view, nullptr);
  for(auto&eye:impl_->depthTargets)for(auto&depth:eye){
    if(depth.view)impl_->vk.destroyImageView(impl_->device,depth.view,nullptr);
    if(depth.image)impl_->vk.destroyImage(impl_->device,depth.image,nullptr);
    if(depth.memory)impl_->vk.freeMemory(impl_->device,depth.memory,nullptr);
  }
  impl_->destroyAsset(impl_->laserAsset);
  for (auto &asset : impl_->assets)
    impl_->destroyAsset(asset);
  if (impl_->pipeline)
    impl_->vk.destroyPipeline(impl_->device, impl_->pipeline, nullptr);
  if(impl_->fragmentShader)
    impl_->vk.destroyShaderModule(impl_->device,impl_->fragmentShader,nullptr);
  if(impl_->hudMaskShader)impl_->vk.destroyShaderModule(impl_->device,impl_->hudMaskShader,nullptr);
  if(impl_->hudPlaceholderShader)impl_->vk.destroyShaderModule(impl_->device,impl_->hudPlaceholderShader,nullptr);
  if(impl_->vertexShader)
    impl_->vk.destroyShaderModule(impl_->device,impl_->vertexShader,nullptr);
  if (impl_->pipelineLayout)
    impl_->vk.destroyPipelineLayout(impl_->device, impl_->pipelineLayout,
                                    nullptr);
  if (impl_->renderPass)
    impl_->vk.destroyRenderPass(impl_->device, impl_->renderPass, nullptr);
  if (impl_->descriptorPool)
    impl_->vk.destroyDescriptorPool(impl_->device, impl_->descriptorPool,
                                    nullptr);
  if (impl_->descriptorLayout)
    impl_->vk.destroyDescriptorSetLayout(impl_->device, impl_->descriptorLayout,
                                         nullptr);
  if (impl_->sampler)
    impl_->vk.destroySampler(impl_->device, impl_->sampler, nullptr);
  if (impl_->uploadPool)
    impl_->vk.destroyCommandPool(impl_->device, impl_->uploadPool, nullptr);
  const auto logger = impl_->log;
  *impl_ = Impl{};
  impl_->log = logger;
  if (logger)
    logger("[HANDS] renderer resources released");
}

void HandRenderer::configureCalibration(HandWeaponKind profile,bool leftHanded,bool enabled,const argent::calibration::ApplyCommand& command){
    auto& p=*impl_;
    if(p.applyRevision.consume(command.revision)&&command.mode=="hands"){
        const auto target=command.profile+(command.left?"_left":"_right");
        argent::calibration::report(p.root,command.revision,p.calibrationDraft.scope==target&&p.saveCalibration(),"hands "+target);
    }
    if(p.calibrationWeapon!=profile||p.calibrationLeftHanded!=leftHanded)p.calibrationContextInitialized=false;
    p.calibrationWeapon=profile;p.calibrationLeftHanded=leftHanded;
    p.calibrationDraft.select(enabled?std::string(HandWeaponKindKey(profile))+(leftHanded?"_left":"_right"):std::string{});
}

void HandRenderer::setCalibrationEnabled(bool enabled){
    if(enabled==(impl_->calibrationMode!=CalibrationMode::None))return;
    impl_->calibrationMode=enabled?CalibrationMode::Rotation:CalibrationMode::None;
    impl_->calibrationPlusWasDown=impl_->calibrationHandWasDown=impl_->calibrationResetWasDown=false;
    impl_->calibrationContextInitialized=false;
}

HandAssetAvailability HandRenderer::availability()const{return {impl_->assets[0].ready,impl_->assets[1].ready,impl_->assets[2].ready,impl_->assets[3].ready};}
const HandCalibration&HandRenderer::leftCalibration()const{return impl_->leftCalibration;}
const HandCalibration&HandRenderer::rightCalibration()const{return impl_->rightCalibration;}

void HandRenderer::record(VkCommandBuffer commandBuffer,std::uint32_t eye,
    std::uint32_t imageIndex,const HandEyeView&view,const HandPose&leftGrip,
    const HandPose&rightGrip,const HandVisibilityOutput&visibility,
    const HandGameplayState&gameplay){
    if(!impl_->initialized||eye>=2||imageIndex>=impl_->framebuffers[eye].size()||!impl_->framebuffers[eye][imageIndex])return;impl_->pollCalibration(gameplay);const bool drawLeft=visibility.left!=HandModelKind::None&&leftGrip.valid;const bool drawRight=visibility.right!=HandModelKind::None&&rightGrip.valid;if(!drawLeft&&!drawRight)return;
    const auto&extent=impl_->extents[eye];
    const int32_t rectX=std::clamp(view.imageRectX,0,int32_t(extent.width));
    const int32_t rectY=std::clamp(view.imageRectY,0,int32_t(extent.height));
    const uint32_t requestedWidth=view.imageRectWidth?view.imageRectWidth:extent.width;
    const uint32_t requestedHeight=view.imageRectHeight?view.imageRectHeight:extent.height;
    const uint32_t rectWidth=std::min(requestedWidth,extent.width-uint32_t(rectX));
    const uint32_t rectHeight=std::min(requestedHeight,extent.height-uint32_t(rectY));
    if(!rectWidth||!rectHeight)return;
    VkRect2D scissor{{rectX,rectY},{rectWidth,rectHeight}};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = impl_->renderPass;
    begin.framebuffer = impl_->framebuffers[eye][imageIndex];
    begin.renderArea = scissor;
    std::array<VkClearValue,2>clearValues{};clearValues[1].depthStencil={1.f,0};
    begin.clearValueCount=static_cast<uint32_t>(clearValues.size());
    begin.pClearValues=clearValues.data();
    impl_->vk.cmdBeginRenderPass(commandBuffer, &begin,
                                 VK_SUBPASS_CONTENTS_INLINE);
    impl_->drawGeometry(commandBuffer,impl_->pipeline,extent,view,leftGrip,
        rightGrip,visibility,gameplay);
    impl_->vk.cmdEndRenderPass(commandBuffer);
}

bool HandRenderer::recordSceneIntegrated(VkCommandBuffer commandBuffer,
    const HandSceneTarget&target,const HandEyeView&view,
    const HandPose&leftGrip,const HandPose&rightGrip,
    const HandVisibilityOutput&visibility,const HandGameplayState&gameplay,const HandPose&laser){
    if(!impl_->initialized||!target.colorView||!target.depthView
        ||target.colorFormat==VK_FORMAT_UNDEFINED
        ||!handSceneDepthFormat(target.depthFormat)
        ||target.samples!=VK_SAMPLE_COUNT_1_BIT)return false;
    const bool drawLeft=visibility.left!=HandModelKind::None&&leftGrip.valid;
    const bool drawRight=visibility.right!=HandModelKind::None&&rightGrip.valid;
    if(!drawLeft&&!drawRight&&!laser.valid&&view.hudPlaceholder.empty())return false;
    impl_->pollCalibration(gameplay);
    auto*scene=impl_->scenePipeline(target);if(!scene)return false;
    Impl::SceneDepth* privateDepth=nullptr;
    if(target.copyDepthForHands){
        const auto slot=impl_->sceneDepthCopiesUsed++;
        if(slot==impl_->sceneDepthCopies.size())impl_->sceneDepthCopies.emplace_back();
        privateDepth=&impl_->sceneDepthCopies[slot];
        if(privateDepth->format!=target.depthFormat||privateDepth->extent.width!=target.extent.width
            ||privateDepth->extent.height!=target.extent.height){
            impl_->destroyDepth(privateDepth->target);*privateDepth={};
        }
        if(!privateDepth->target.view){
            if(!impl_->depthTarget(target.extent,privateDepth->target,target.depthFormat,true)){
                impl_->destroyDepth(privateDepth->target);return false;
            }
            privateDepth->format=target.depthFormat;privateDepth->extent=target.extent;
            impl_->say("Native private hand depth ready; scene depth/stencil preserved");
        }
    }
    const std::array<VkImageView,2>attachments{{target.colorView,privateDepth?privateDepth->target.view:target.depthView}};
    VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebufferInfo.renderPass=scene->renderPass;
    framebufferInfo.attachmentCount=static_cast<std::uint32_t>(attachments.size());
    framebufferInfo.pAttachments=attachments.data();framebufferInfo.width=target.extent.width;
    framebufferInfo.height=target.extent.height;framebufferInfo.layers=1;
    VkFramebuffer framebuffer{};
    const bool ownedColor=std::any_of(impl_->eyeViews.begin(),impl_->eyeViews.end(),[&](const auto& views){
        return std::find(views.begin(),views.end(),target.colorView)!=views.end();
    });
    if(privateDepth&&ownedColor){
        framebuffer=impl_->cachedSceneFramebuffers.get(impl_->device,impl_->vk,framebufferInfo);
        if(!framebuffer)return false;
    }else{
        if(impl_->vk.createFramebuffer(impl_->device,&framebufferInfo,nullptr,&framebuffer)!=VK_SUCCESS)return false;
        impl_->sceneFramebuffers.push_back(framebuffer);
    }
    if(privateDepth){
        copyHandSceneDepth(impl_->vk,commandBuffer,target.depthImage,privateDepth->target.image,
            target.extent,handSceneDepthAspect(target.depthFormat),privateDepth->initialized,target.depthArrayLayer,target.depthExtent,target.depthOffset);
        privateDepth->initialized=true;
    }
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass=scene->renderPass;begin.framebuffer=framebuffer;
    begin.renderArea={{0,0},target.extent};
    impl_->vk.cmdBeginRenderPass(commandBuffer,&begin,VK_SUBPASS_CONTENTS_INLINE);
    impl_->drawGeometry(commandBuffer,scene->pipeline,target.extent,view,leftGrip,
        rightGrip,visibility,gameplay,laser);
    impl_->vk.cmdEndRenderPass(commandBuffer);
    return true;
}

bool HandRenderer::recordSceneDepth(VkCommandBuffer cb,uint32_t eye,uint32_t index,
    HandSceneTarget target,const HandEyeView& view,const HandPose& left,const HandPose& right,
    const HandVisibilityOutput& visibility,const HandGameplayState& gameplay,const HandPose& laser){
    if(!impl_->initialized||eye>=2||index>=impl_->eyeViews[eye].size())return false;
    target.depthExtent=target.extent;target.extent=impl_->extents[eye];
    if(target.depthExtent.width!=target.extent.width||target.depthExtent.height!=target.extent.height){
        if(!impl_->vk.getPhysicalDeviceFormatProperties)return false;
        VkFormatProperties properties{};
        impl_->vk.getPhysicalDeviceFormatProperties(impl_->physical,target.depthFormat,&properties);
        const auto required=VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT;
        if((properties.optimalTilingFeatures&required)!=required)return false;
    }
    target.colorView=impl_->eyeViews[eye][index];target.colorFormat=impl_->format;
    target.copyDepthForHands=true;target.depthArrayLayer=eye;
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.image=target.depthImage;barrier.oldLayout=target.depthLayout;
    barrier.newLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange={handSceneDepthAspect(target.depthFormat),0,1,eye,1};
    barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    impl_->vk.cmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    const bool recorded=recordSceneIntegrated(cb,target,view,left,right,visibility,gameplay,laser);
    std::swap(barrier.oldLayout,barrier.newLayout);std::swap(barrier.srcAccessMask,barrier.dstAccessMask);
    impl_->vk.cmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    return recorded;
}

void HandRenderer::finishSceneIntegratedFrame(){
    if(!impl_->device)return;
    for(auto framebuffer:impl_->sceneFramebuffers)
        if(framebuffer)impl_->vk.destroyFramebuffer(impl_->device,framebuffer,nullptr);
    impl_->sceneFramebuffers.clear();
    impl_->sceneDepthCopiesUsed=0;
}
