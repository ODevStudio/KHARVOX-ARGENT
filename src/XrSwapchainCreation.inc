void destroySwapchain(){releaseHands();fsr1.releaseAfterCompletion();if(swapchain){XR(xrDestroySwapchain)(swapchain);swapchain=XR_NULL_HANDLE;}images.clear();for(auto& eye:stereoEyes){if(eye.handle){XR(xrDestroySwapchain)(eye.handle);eye.handle=XR_NULL_HANDLE;}eye.images.clear();eye.initialized.clear();}}
void createSwapchain(const Source& source,uint32_t layers=1,bool immersive=false){
    const bool upscale=immersive&&fsrRequested();
    const auto targetFormat=xrDisplayFormat(source.format,source.displaySrgb);
    if((layers==2?stereoEyes[0].handle:swapchain)&&sourceExtent.width==source.extent.width&&sourceExtent.height==source.extent.height&&swapchainFsrRequested==upscale&&format==source.format&&compositionFormat==targetFormat&&swapchainLayers==layers)return;
    destroySwapchain();
    const auto output=upscale?fsrOutput(source,targetFormat):source.extent;
    sourceExtent=source.extent;swapchainFsrRequested=upscale;
    if(upscale&&!fsr1.active())log("FSR1 using native eye copy");
    uint32_t count=0;check(XR(xrEnumerateSwapchainFormats)(session,0,&count,nullptr),"xrEnumerateSwapchainFormats");
    std::vector<int64_t> formats(count);check(XR(xrEnumerateSwapchainFormats)(session,count,&count,formats.data()),"xrEnumerateSwapchainFormats");
    if(std::find(formats.begin(),formats.end(),targetFormat)==formats.end())throw std::runtime_error("XR swapchain does not support required display format "+std::to_string(targetFormat));
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.usageFlags=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    char mirrorEnabled[8]{};
    if(layers==2&&GetEnvironmentVariableA("ARGENT_DESKTOP_MIRROR",mirrorEnabled,sizeof(mirrorEnabled))==1&&mirrorEnabled[0]=='1')ci.usageFlags|=XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
    ci.format=targetFormat;ci.sampleCount=1;ci.width=output.width;ci.height=output.height;ci.faceCount=1;ci.arraySize=layers==2?1:layers;ci.mipCount=1;
    if(layers==2){
        for(unsigned eye=0;eye<2;++eye){
            check(XR(xrCreateSwapchain)(session,&ci,&stereoEyes[eye].handle),"xrCreateSwapchain eye");
            check(XR(xrEnumerateSwapchainImages)(stereoEyes[eye].handle,0,&count,nullptr),"xrEnumerateSwapchainImages eye");
            stereoEyes[eye].images.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
            check(XR(xrEnumerateSwapchainImages)(stereoEyes[eye].handle,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(stereoEyes[eye].images.data())),"xrEnumerateSwapchainImages eye");
            stereoEyes[eye].initialized.assign(count,false);
        }
        extent=output;format=source.format;compositionFormat=targetFormat;swapchainLayers=layers;log("XR_EYE_SWAPCHAINS images="+std::to_string(stereoEyes[0].images.size())+"/"+std::to_string(stereoEyes[1].images.size())+" extent="+std::to_string(extent.width)+"x"+std::to_string(extent.height)+" sourceFormat="+std::to_string(format)+" compositionFormat="+std::to_string(compositionFormat));
        return;
    }
    check(XR(xrCreateSwapchain)(session,&ci,&swapchain),"xrCreateSwapchain");
    check(XR(xrEnumerateSwapchainImages)(swapchain,0,&count,nullptr),"xrEnumerateSwapchainImages");
    images.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    check(XR(xrEnumerateSwapchainImages)(swapchain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"xrEnumerateSwapchainImages");
    extent=output;format=source.format;compositionFormat=targetFormat;swapchainLayers=layers;log("XR_SWAPCHAIN images="+std::to_string(count)+" extent="+std::to_string(extent.width)+"x"+std::to_string(extent.height)+" layers="+std::to_string(layers)+" sourceFormat="+std::to_string(format)+" compositionFormat="+std::to_string(compositionFormat));
}
