void startSession(Device& d,VkQueue q,uint32_t family,uint32_t index){
    device=d;
    XrGraphicsRequirementsVulkanKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
    check(enable2?XR(xrGetVulkanGraphicsRequirements2KHR)(instance,system,&requirements):XR(xrGetVulkanGraphicsRequirementsKHR)(instance,system,&requirements),"XR Vulkan requirements");
    char deferredSteamDevice[8]{};
    char forceSteamEnable1ForBinding[8]{};
    const bool forcedSteamEnable1=kharvox::isSteamBackedOpenXRRuntime(runtimeKind)
        &&GetEnvironmentVariableA("ARGENT_STEAMVR_FORCE_ENABLE1",forceSteamEnable1ForBinding,sizeof(forceSteamEnable1ForBinding))==1
        &&forceSteamEnable1ForBinding[0]=='1';
    const bool steamVrDeferredDevice=kharvox::isSteamBackedOpenXRRuntime(runtimeKind)
        &&GetEnvironmentVariableA("ARGENT_STEAMVR_DEFER_XR_DEVICE",deferredSteamDevice,sizeof(deferredSteamDevice))==1
        &&deferredSteamDevice[0]=='1'
        &&!forcedSteamEnable1;
    VkPhysicalDevice selected=runtimeSelectedPhysical;
    XrVulkanGraphicsDeviceGetInfoKHR gpuInfo{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};gpuInfo.systemId=system;gpuInfo.vulkanInstance=d.instance;
    if(!selected)check(enable2?XR(xrGetVulkanGraphicsDevice2KHR)(instance,&gpuInfo,&selected):XR(xrGetVulkanGraphicsDeviceKHR)(instance,system,d.instance,&selected),"XR Vulkan graphics device");
    auto vulkan=GetModuleHandleW(L"vulkan-1.dll");
    auto publicGet=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vulkan,"vkGetInstanceProcAddr"));
    auto selectedGet=enable2&&simulator?d.gipa:publicGet;
    if(!selectedGet||!d.gipa)throw std::runtime_error("GPU UUID verification unavailable");
    auto publicProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(selectedGet(d.instance,"vkGetPhysicalDeviceProperties2KHR"));
    auto nextProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(d.gipa(d.instance,"vkGetPhysicalDeviceProperties2KHR"));
    if(!publicProperties||!nextProperties)throw std::runtime_error("GPU UUID verification unavailable");
    VkPhysicalDeviceIDProperties gameID{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES},xrID{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 gameProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2},xrProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    gameProps.pNext=&gameID;xrProps.pNext=&xrID;nextProperties(d.physical,&gameProps);publicProperties(selected,&xrProps);
    if(memcmp(gameID.deviceUUID,xrID.deviceUUID,VK_UUID_SIZE))throw std::runtime_error("XR GPU UUID does not match game GPU");
    log(std::string("XR_GPU_UUID_MATCH ")+gameProps.properties.deviceName);
    const auto bindingPhysical=steamVrDeferredDevice?d.physical:selected;
    if(steamVrDeferredDevice)log("XR_SESSION binding uses game physical for deferred SteamVR device");
    else if(forcedSteamEnable1)log("XR_SESSION binding uses runtime physical for forced SteamVR enable1 path");
    XrGraphicsBindingVulkanKHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};binding.instance=d.instance;binding.physicalDevice=bindingPhysical;binding.device=d.device;binding.queueFamilyIndex=family;binding.queueIndex=index;
    XrSessionCreateInfo ci{XR_TYPE_SESSION_CREATE_INFO};ci.next=&binding;ci.systemId=system;
    log("XR_SESSION creating Vulkan binding family="+std::to_string(family)+" index="+std::to_string(index));
    check(XR(xrCreateSession)(instance,&ci,&session),"xrCreateSession");boundDevice=d.device;boundQueue=q;boundFamily=family;
    {uint32_t n{};check(XR(xrEnumerateSwapchainFormats)(session,0,&n,nullptr),"pause image formats");
     std::vector<int64_t> formats(n);check(XR(xrEnumerateSwapchainFormats)(session,n,&n,formats.data()),"pause image formats");
     log(initializePauseBindings(formats)?"PAUSE_BINDINGS uploaded=1 pauseRootOnly=1":"PAUSE_BINDINGS unavailable; normal menu retained");}
    XrReferenceSpaceCreateInfo sci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};sci.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;sci.poseInReferenceSpace.orientation.w=1;
    check(XR(xrCreateReferenceSpace)(session,&sci,&space),"xrCreateReferenceSpace");
    sci.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
    check(XR(xrCreateReferenceSpace)(session,&sci,&localSpace),"xrCreateReferenceSpace LOCAL");
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pci.queueFamilyIndex=family;
    checkVk(VK(vkCreateCommandPool)(d.device,&pci,nullptr,&pool),"vkCreateCommandPool");
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    checkVk(VK(vkAllocateCommandBuffers)(d.device,&ai,&command),"vkAllocateCommandBuffers");
    if(d.setLoaderData)checkVk(d.setLoaderData(d.device,command),"vkSetDeviceLoaderData");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};checkVk(VK(vkCreateFence)(d.device,&fi,nullptr,&fence),"vkCreateFence");
    char gpuTimingSetting[8]{};const auto gpuTimingLength=GetEnvironmentVariableA("ARGENT_GPU_TIMING",gpuTimingSetting,sizeof(gpuTimingSetting));
    const bool explicitGpuTiming=!cleanRelease&&environmentFlag("ARGENT_GPU_TIMING")&&!(gpuTimingLength==1&&gpuTimingSetting[0]=='0');
    copyTimingStride=explicitGpuTiming?1:16;
    if(explicitGpuTiming||argent::perf::enabled()){
        uint32_t count{};
        auto queueProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(d.gipa(d.instance,"vkGetPhysicalDeviceQueueFamilyProperties"));
        if(queueProperties){
            queueProperties(d.physical,&count,nullptr);
            std::vector<VkQueueFamilyProperties> families(count);queueProperties(d.physical,&count,families.data());
            if(family<count){
                if(explicitGpuTiming)copyTiming.initialize(d.device,d.gdpa,gameProps.properties.limits.timestampPeriod,families[family].timestampValidBits);
                if(argent::perf::enabled()){
                    diagnosticCopyTiming.initialize(d.device,d.gdpa,gameProps.properties.limits.timestampPeriod,families[family].timestampValidBits);
                    log("PERF_XR_GPU_SUPPORT active="+std::to_string(bool(diagnosticCopyTiming.pool))+" periodNs="+std::to_string(diagnosticCopyTiming.period)+" validBits="+std::to_string(diagnosticCopyTiming.bits));
                }
            }
        }
    }
    log("XR_COPY_GPU_TIMING active="+std::to_string(bool(copyTiming.pool))+" stride="+std::to_string(copyTimingStride));
    controllerActions.create();
    log("XR_SESSION created: Vulkan composition ready");
}
