static void disposeLayerMirror(State& state,argent::DesktopMirror& mirror)noexcept{
    try{mirror.destroy(state);}catch(...){RaiseFailFastException(nullptr,nullptr,0);std::terminate();}
}
static void removeLayerMirror(State& state,VkSwapchainKHR chain){
    std::unique_ptr<argent::DesktopMirror> mirror;
    {std::lock_guard<std::recursive_mutex> lock(stateMutex);const auto found=state.mirrors.find(chain);
     if(found!=state.mirrors.end()){mirror=std::move(found->second);state.mirrors.erase(found);}}
    if(mirror)disposeLayerMirror(state,*mirror);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice d,const VkSwapchainCreateInfoKHR* ci,const VkAllocationCallbacks* a,VkSwapchainKHR* out){
    if(!out)return VK_ERROR_INITIALIZATION_FAILED;
    *out=VK_NULL_HANDLE;
    if(!ci)return VK_ERROR_INITIALIZATION_FAILED;
    std::shared_ptr<State> s;VkSwapchainKHR created{};bool owned{},virtualSource{},tracked{};
    PFN_vkDestroySwapchainKHR destroy{};
    const auto rollback=[&]()noexcept{
        if(!owned)return;
        try{
            {std::lock_guard<std::recursive_mutex> lock(stateMutex);s->sources.erase(created);}
            if(virtualSource){if(tracked)argent::sfs::swapchainDestroyed(d,created);argent::sfs::destroySourceSwapchain(d,created);}
            else destroy(d,created,a);
        }catch(...){RaiseFailFastException(nullptr,nullptr,0);std::terminate();}
    };
    try{
    s=deviceOf(key(d));if(!s)return VK_ERROR_DEVICE_LOST;
    auto modified=*ci;bool copy=false;
    if(s->sfs&&argent::sfs::sourceRingRequested()){
      virtualSource=true;
      if(!s->graphicsQueue)return VK_ERROR_INITIALIZATION_FAILED;
      if(!argent::sfs::sourceRingActive(d)){VkPhysicalDeviceMemoryProperties mem{};
       const auto query=s->gipa?reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(s->gipa(s->instance,"vkGetPhysicalDeviceMemoryProperties")):nullptr;
       if(!query)return VK_ERROR_INITIALIZATION_FAILED;
       query(s->physical,&mem);
       if(!argent::sfs::configureSourceRing(d,s->gdpa,mem,s->graphicsQueue,s->queueMutex.get()))return VK_ERROR_INITIALIZATION_FAILED;}
      modified.imageArrayLayers=2;
      char fsr[8]{};
      if(GetEnvironmentVariableA("ARGENT_FSR1",fsr,sizeof(fsr))==1&&fsr[0]=='1'
          &&s->gipa
          &&(ci->imageFormat==VK_FORMAT_R8G8B8A8_UNORM||ci->imageFormat==VK_FORMAT_B8G8R8A8_UNORM)){
        const auto query=reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties>(s->gipa(s->instance,"vkGetPhysicalDeviceImageFormatProperties"));
        const auto formatQuery=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(s->gipa(s->instance,"vkGetPhysicalDeviceFormatProperties"));
        VkImageFormatProperties properties{};VkFormatProperties features{};
        const auto usage=modified.imageUsage|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        if(formatQuery)formatQuery(s->physical,ci->imageFormat,&features);
        if(query&&(features.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
            &&query(s->physical,ci->imageFormat,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,usage,0,&properties)==VK_SUCCESS
            &&properties.maxExtent.width>=ci->imageExtent.width&&properties.maxExtent.height>=ci->imageExtent.height
            &&properties.maxArrayLayers>=2&&(properties.sampleCounts&VK_SAMPLE_COUNT_1_BIT))
          modified.imageUsage|=VK_IMAGE_USAGE_SAMPLED_BIT;
      }
      if(s->queueFamilies.size()>1){modified.imageSharingMode=VK_SHARING_MODE_CONCURRENT;modified.queueFamilyIndexCount=uint32_t(s->queueFamilies.size());modified.pQueueFamilyIndices=s->queueFamilies.data();}
      const bool oldSource=ci->oldSwapchain&&argent::sfs::sourceSwapchain(d,ci->oldSwapchain);
      const auto result=argent::sfs::createSourceSwapchain(d,modified,&created);
      owned=result==VK_SUCCESS&&created;
      if(oldSource)removeLayerMirror(*s,ci->oldSwapchain);
      if(result!=VK_SUCCESS){try{argent::log("SFS_SOURCE_CREATE_FAILED result="+std::to_string(result));}catch(...){}return result;}
    }else{
      if(s->game){
        VkSurfaceCapabilitiesKHR caps{};
        const auto query=s->gipa?reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(s->gipa(s->instance,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR")):nullptr;
        copy=query&&query(s->physical,ci->surface,&caps)==VK_SUCCESS&&(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)&&ci->imageArrayLayers==1;
        if(copy)modified.imageUsage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      }
      if(!s->gdpa)return VK_ERROR_INITIALIZATION_FAILED;
      destroy=reinterpret_cast<PFN_vkDestroySwapchainKHR>(s->gdpa(d,"vkDestroySwapchainKHR"));
      const auto create=s->proc<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR");
      if(!create||!destroy)return VK_ERROR_INITIALIZATION_FAILED;
      const auto result=create(d,&modified,a,&created);
      if(result!=VK_SUCCESS)return result;
      owned=bool(created);
    }
    if(!owned)throw VK_ERROR_INITIALIZATION_FAILED;
    if(virtualSource||s->game){
        argent::Source source;source.extent=ci->imageExtent;source.format=ci->imageFormat;
        source.displaySrgb=ci->imageColorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;source.transferable=virtualSource||copy;
        source.sampled=virtualSource&&(modified.imageUsage&VK_IMAGE_USAGE_SAMPLED_BIT)!=0;
        const auto images=virtualSource?argent::sfs::sourceImages:s->proc<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR");
        if(!images)throw VK_ERROR_INITIALIZATION_FAILED;
        for(unsigned attempt=0;;++attempt){
            uint32_t count{};auto result=images(d,created,&count,nullptr);
            if(result!=VK_SUCCESS)throw result;
            if(!count)throw VK_ERROR_INITIALIZATION_FAILED;
            source.images.resize(count);result=images(d,created,&count,source.images.data());
            if(result==VK_INCOMPLETE&&attempt<2)continue;
            if(result!=VK_SUCCESS&&result!=VK_INCOMPLETE)throw result;
            if(result==VK_INCOMPLETE||!count||count>source.images.size())throw VK_ERROR_INITIALIZATION_FAILED;
            source.images.resize(count);break;
        }
        if(virtualSource){tracked=true;argent::sfs::swapchainImages(d,created,uint32_t(source.images.size()),source.images.data());}
        {std::lock_guard<std::recursive_mutex> lock(stateMutex);
         if(!s->sources.try_emplace(created,std::move(source)).second)throw VK_ERROR_INITIALIZATION_FAILED;}
    }
    if(virtualSource){
        std::unique_ptr<argent::DesktopMirror> mirror;
        try{
            mirror=std::make_unique<argent::DesktopMirror>();
            if(mirror->create(*s,*ci)){
                std::lock_guard<std::recursive_mutex> lock(stateMutex);const auto inserted=s->mirrors.try_emplace(created);
                if(inserted.second)inserted.first->second=std::move(mirror);
            }
            if(mirror){disposeLayerMirror(*s,*mirror);mirror.reset();try{argent::log("SFS_DESKTOP unavailable");}catch(...){}}
        }catch(...){if(mirror)disposeLayerMirror(*s,*mirror);try{argent::log("SFS_DESKTOP unavailable");}catch(...){}}
        try{argent::log("SFS_SOURCE_SWAPCHAIN "+std::to_string(ci->imageExtent.width)+"x"+std::to_string(ci->imageExtent.height)+" requestedPresentMode="+std::to_string(ci->presentMode));}catch(...){}
    }else if(s->game){
        try{argent::log("SWAPCHAIN "+std::to_string(ci->imageExtent.width)+"x"+std::to_string(ci->imageExtent.height)+" format="+std::to_string(ci->imageFormat)+" transferable="+std::to_string(copy));}catch(...){}
    }
    *out=created;return VK_SUCCESS;
    }catch(VkResult result){rollback();return result;}
    catch(const std::bad_alloc&){rollback();return VK_ERROR_OUT_OF_HOST_MEMORY;}
    catch(...){rollback();return VK_ERROR_INITIALIZATION_FAILED;}
}
VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice d,VkSwapchainKHR sc,const VkAllocationCallbacks* a){
    if(!sc)return;
    try{
    auto s=deviceOf(key(d));if(!s){RaiseFailFastException(nullptr,nullptr,0);std::terminate();}
    removeLayerMirror(*s,sc);
    {std::lock_guard<std::recursive_mutex> l(stateMutex);s->sources.erase(sc);}
    if(argent::sfs::sourceSwapchain(d,sc)){
        try{argent::cancelStereoFrame();}catch(...){}
        argent::retireStereoSources(d);argent::sfs::swapchainDestroyed(d,sc);argent::sfs::destroySourceSwapchain(d,sc);
    }else{
        const auto destroy=s->gdpa?reinterpret_cast<PFN_vkDestroySwapchainKHR>(s->gdpa(d,"vkDestroySwapchainKHR")):nullptr;
        if(!destroy){RaiseFailFastException(nullptr,nullptr,0);std::terminate();}destroy(d,sc,a);
    }
    }catch(...){RaiseFailFastException(nullptr,nullptr,0);std::terminate();}
}
