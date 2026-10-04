VKAPI_ATTR VkResult VKAPI_CALL createImage(VkDevice d,const VkImageCreateInfo* i,const VkAllocationCallbacks* a,VkImage* out){
    if(!out)return VK_ERROR_INITIALIZATION_FAILED;
    *out=VK_NULL_HANDLE;
    if(!i)return VK_ERROR_INITIALIZATION_FAILED;
RESULT_BEGIN
    const auto destroy=FN(vkDestroyImage);
    auto info=*i;
    if(s->waterCapture&&(info.usage&(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))&&!(info.usage&VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT))info.usage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if(stereoImage(info)&&(info.usage&VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))info.usage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImage image{};
    const auto result=s->images.create(d,info,a,&image,FN(vkCreateImage),destroy);
    if(result!=VK_SUCCESS)return result;
    const auto cleanup=[&](VkImage h){
        if(s->waterCapture)s->waterCapture->forgetImage(h);
        handDepth::handSceneImageDestroyed(h);
        s->images.destroy(d,h,a,destroy);
    };
    std::unique_ptr<std::remove_pointer_t<VkImage>,decltype(cleanup)> owned(image,cleanup);
    handDepth::handSceneImageCreated(image,info);
    if(s->waterCapture)s->waterCapture->image(image,imageInfo(info));
    waterCaptureImage(d,image,imageInfo(info));
    *out=owned.release();
    return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyImage(VkDevice d,VkImage image,const VkAllocationCallbacks* a){
 kharvox::gameImageLifetime().retire(kharvox::GameImageLifetime::key(image),[&]{
  auto s=state(d);if(s->waterCapture)s->waterCapture->forgetImage(image);handDepth::handSceneImageDestroyed(image);s->images.destroy(d,image,a,FN(vkDestroyImage));
 });
}
VKAPI_ATTR VkResult VKAPI_CALL createView(VkDevice d,const VkImageViewCreateInfo* i,const VkAllocationCallbacks* a,VkImageView* out){
    if(!out)return VK_ERROR_INITIALIZATION_FAILED;
    *out=VK_NULL_HANDLE;
    if(!i)return VK_ERROR_INITIALIZATION_FAILED;
RESULT_BEGIN
    const auto destroy=FN(vkDestroyImageView);
    const auto info=s->images.shaderViewInfo(*i);
    VkImageView view{};
    const auto result=FN(vkCreateImageView)(d,&info,a,&view);
    if(result!=VK_SUCCESS)return result;
    if(!view)return VK_ERROR_INITIALIZATION_FAILED;
    const auto cleanup=[&](VkImageView h){
        handDepth::handSceneImageViewDestroyed(h);
        if(s->waterCapture)s->waterCapture->forgetView(h);
        s->viewInfos.erase(h);s->viewLayers.erase(h);
        destroy(d,h,a);
    };
    std::unique_ptr<std::remove_pointer_t<VkImageView>,decltype(cleanup)> owned(view,cleanup);
    handDepth::handSceneImageViewCreated(view,info);
    if(s->waterCapture)s->waterCapture->view(view,info);
    s->viewLayers[view]=(s->images.layers(i->image)==2&&info.subresourceRange.baseArrayLayer==0&&info.subresourceRange.layerCount>=2)?2:1;
    if(!info.pNext)s->viewInfos[view]=info;
    waterCaptureView(d,view,info);
    *out=owned.release();
    return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyView(VkDevice d,VkImageView view,const VkAllocationCallbacks* a){
 kharvox::gameImageLifetime().retire(kharvox::GameImageLifetime::key(view),[&]{
  auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);auto eyes=s->eyeViews.find(view);if(eyes!=s->eyeViews.end()){for(auto eye:eyes->second)if(eye)FN(vkDestroyImageView)(d,eye,nullptr);s->eyeViews.erase(eyes);}handDepth::handSceneImageViewDestroyed(view);if(s->waterCapture)s->waterCapture->forgetView(view);s->viewInfos.erase(view);s->viewLayers.erase(view);FN(vkDestroyImageView)(d,view,a);
 });
}
VKAPI_ATTR VkResult VKAPI_CALL createPass(VkDevice d,const VkRenderPassCreateInfo* i,const VkAllocationCallbacks* a,VkRenderPass* out){
    if(!out)return VK_ERROR_INITIALIZATION_FAILED;
    *out=VK_NULL_HANDLE;
    if(!i)return VK_ERROR_INITIALIZATION_FAILED;
RESULT_BEGIN
    const auto create=FN(vkCreateRenderPass);
    const auto destroy=FN(vkDestroyRenderPass);
    auto input=*i;std::vector<VkAttachmentDescription> attachments;
    if(s->sources&&i->attachmentCount){attachments.assign(i->pAttachments,i->pAttachments+i->attachmentCount);for(auto& attachment:attachments){
        if(attachment.initialLayout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)attachment.initialLayout=VK_IMAGE_LAYOUT_GENERAL;
        if(attachment.finalLayout==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)attachment.finalLayout=VK_IMAGE_LAYOUT_GENERAL;
    }input.pAttachments=attachments.data();}
    RenderPassPlan plan(input,true);if(!plan.valid())return VK_ERROR_FEATURE_NOT_PRESENT;
    VkRenderPass mono{};
    auto result=create(d,&input,a,&mono);
    if(result!=VK_SUCCESS)return result;
    if(!mono)return VK_ERROR_INITIALIZATION_FAILED;
    const auto cleanupMono=[&](VkRenderPass h){
        handDepth::handSceneRenderPassDestroyed(h);
        if(s->waterCapture)s->waterCapture->forgetPass(h);
        s->passes.erase(h);destroy(d,h,a);
    };
    std::unique_ptr<std::remove_pointer_t<VkRenderPass>,decltype(cleanupMono)> monoOwned(mono,cleanupMono);
    VkRenderPass stereo{};
    result=create(d,&plan.info(),a,&stereo);
    if(result!=VK_SUCCESS)return result;
    if(!stereo)return VK_ERROR_INITIALIZATION_FAILED;
    const auto cleanupStereo=[&](VkRenderPass h){destroy(d,h,a);};
    std::unique_ptr<std::remove_pointer_t<VkRenderPass>,decltype(cleanupStereo)> stereoOwned(stereo,cleanupStereo);
    s->passes[mono]=stereo;
    handDepth::handSceneRenderPassCreated(mono,input);
    if(s->waterCapture)s->waterCapture->renderPass(mono,input);
    stereoOwned.release();
    *out=monoOwned.release();
    return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyPass(VkDevice d,VkRenderPass pass,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->passRetirement.fetch_add(1,std::memory_order_release);handDepth::handSceneRenderPassDestroyed(pass);if(s->waterCapture)s->waterCapture->forgetPass(pass);auto it=s->passes.find(pass);if(it!=s->passes.end()){FN(vkDestroyRenderPass)(d,it->second,a);s->passes.erase(it);}FN(vkDestroyRenderPass)(d,pass,a);}
VKAPI_ATTR VkResult VKAPI_CALL createFramebuffer(VkDevice d,const VkFramebufferCreateInfo* i,const VkAllocationCallbacks* a,VkFramebuffer* out){
    if(!out)return VK_ERROR_INITIALIZATION_FAILED;
    *out=VK_NULL_HANDLE;
    if(!i)return VK_ERROR_INITIALIZATION_FAILED;
RESULT_BEGIN
    const auto destroy=FN(vkDestroyFramebuffer);
    auto info=*i;bool stereo=i->attachmentCount!=0;
    if(i->flags&VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT)return VK_ERROR_FEATURE_NOT_PRESENT;
    for(uint32_t j=0;j<i->attachmentCount;++j){auto v=s->viewLayers.find(i->pAttachments[j]);if(v==s->viewLayers.end()||v->second<2)stereo=false;}
    uint32_t stereoAttachments{};for(uint32_t j=0;j<i->attachmentCount;++j){auto v=s->viewLayers.find(i->pAttachments[j]);if(v!=s->viewLayers.end()&&v->second>=2)++stereoAttachments;}
    const bool mixed=stereoAttachments&&stereoAttachments<i->attachmentCount;
    auto pass=s->passes.find(i->renderPass);if(pass==s->passes.end())return VK_ERROR_INITIALIZATION_FAILED;
    if(stereo)info.renderPass=pass->second;
    VkFramebuffer framebuffer{};
    const auto result=FN(vkCreateFramebuffer)(d,&info,a,&framebuffer);
    if(result!=VK_SUCCESS)return result;
    if(!framebuffer)return VK_ERROR_INITIALIZATION_FAILED;
    const auto cleanup=[&](VkFramebuffer h){
        handDepth::handSceneFramebufferDestroyed(h);
        if(s->waterCapture)s->waterCapture->forgetFramebuffer(h);
        s->framebufferStereo.erase(h);s->mixedFramebuffers.erase(h);
        destroy(d,h,a);
    };
    std::unique_ptr<std::remove_pointer_t<VkFramebuffer>,decltype(cleanup)> owned(framebuffer,cleanup);
    handDepth::handSceneFramebufferCreated(framebuffer,*i);
    if(s->waterCapture)s->waterCapture->framebuffer(framebuffer,*i);
    s->framebufferStereo[framebuffer]=stereo;s->mixedFramebuffers[framebuffer]=mixed;
    if(mixed&&s->mixedDiagnostics++<24){try{
            note("[SFS-MIXED] framebuffer="+std::to_string(reinterpret_cast<uintptr_t>(framebuffer))+" size="+std::to_string(i->width)+"x"+std::to_string(i->height)+" stereoAttachments="+std::to_string(stereoAttachments)+" total="+std::to_string(i->attachmentCount)+" mode=mono");
            for(uint32_t j=0;j<i->attachmentCount;++j){auto v=s->viewInfos.find(i->pAttachments[j]);if(v!=s->viewInfos.end())note("[SFS-MIXED] attachment="+std::to_string(j)+" image="+std::to_string(reinterpret_cast<uintptr_t>(v->second.image))+" format="+std::to_string(v->second.format)+" baseLayer="+std::to_string(v->second.subresourceRange.baseArrayLayer)+" viewLayers="+std::to_string(v->second.subresourceRange.layerCount));}
    }catch(...) {}}
    *out=owned.release();
    return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyFramebuffer(VkDevice d,VkFramebuffer fb,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->framebufferRetirement.fetch_add(1,std::memory_order_release);handDepth::handSceneFramebufferDestroyed(fb);if(s->waterCapture)s->waterCapture->forgetFramebuffer(fb);s->framebufferStereo.erase(fb);s->mixedFramebuffers.erase(fb);FN(vkDestroyFramebuffer)(d,fb,a);}
