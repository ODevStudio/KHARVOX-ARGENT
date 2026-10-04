bool xrCreateGameInstance(PFN_vkGetInstanceProcAddr next,const VkInstanceCreateInfo* ci,const VkAllocationCallbacks* a,VkInstance* out,VkResult& result){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!instance||failed||!enable2)return false;
    result=VK_ERROR_INITIALIZATION_FAILED;
    try{
        XrVulkanInstanceCreateInfoKHR info{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
        info.systemId=system;info.pfnGetInstanceProcAddr=next;info.vulkanCreateInfo=ci;info.vulkanAllocator=a;
        const auto r=XR(xrCreateVulkanInstanceKHR)(instance,&info,out,&result);
        kharvox::finishRuntimeVulkanCreate(XR_SUCCEEDED(r),&result,out);
        try{log("XR_VULKAN_INSTANCE xr="+std::to_string(r)+" vk="+std::to_string(result));}catch(...){}
    }catch(const std::exception& e){*out=VK_NULL_HANDLE;result=VK_ERROR_INITIALIZATION_FAILED;try{log(e.what());}catch(...){}}
    return true;
}
bool xrCreateGameDevice(PFN_vkGetInstanceProcAddr next,PFN_vkGetDeviceProcAddr gdpa,VkInstance vi,VkPhysicalDevice physical,const VkDeviceCreateInfo* runtimeInfo,const VkDeviceCreateInfo* downstreamInfo,const VkAllocationCallbacks* a,VkDevice* out,VkResult& result){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!instance||failed||!enable2)return false;
    result=VK_ERROR_INITIALIZATION_FAILED;
    try{
        XrVulkanGraphicsDeviceGetInfoKHR query{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};query.systemId=system;query.vulkanInstance=vi;
        VkPhysicalDevice selected{};check(XR(xrGetVulkanGraphicsDevice2KHR)(instance,&query,&selected),"xrGetVulkanGraphicsDevice2KHR");
        auto selectedGet=simulator?next:publicGipa();
        auto properties=[&](PFN_vkGetInstanceProcAddr get,VkPhysicalDevice gpu,VkPhysicalDeviceIDProperties& id){
            auto fn=get?reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(get(vi,"vkGetPhysicalDeviceProperties2")):nullptr;
            if(!fn&&get)fn=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(get(vi,"vkGetPhysicalDeviceProperties2KHR"));
            if(!fn)throw std::runtime_error("XR GPU UUID query unavailable");
            VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};p.pNext=&id;fn(gpu,&p);
        };
        VkPhysicalDeviceIDProperties gameId{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES},xrId{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        properties(next,physical,gameId);properties(selectedGet,selected,xrId);
        if(memcmp(gameId.deviceUUID,xrId.deviceUUID,VK_UUID_SIZE))throw std::runtime_error("XR GPU UUID does not match game GPU");
        runtimeNext=next;runtimePhysical=physical;runtimeDownstream=downstreamInfo;
        runtimeSessionRoute=false;runtimeCreate=kharvox::resolveLayerCreateDevice(next,vi);
        XrVulkanDeviceCreateInfoKHR info{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
        info.systemId=system;info.pfnGetInstanceProcAddr=runtimeGipa;info.vulkanPhysicalDevice=selected;info.vulkanCreateInfo=runtimeInfo;info.vulkanAllocator=a;
        try{log("xrCreateVulkanDeviceKHR cross-thread layer adapter armed");}catch(...){}
        XrResult r{};
        {
            RestoreDeviceCreateLinks restoreLinks(downstreamInfo->pNext);
            r=XR(xrCreateVulkanDeviceKHR)(instance,&info,out,&result);
        }
        kharvox::finishRuntimeVulkanCreate(XR_SUCCEEDED(r),&result,out);
        if(result==VK_SUCCESS){runtimeSelectedPhysical=selected;runtimeSessionRoute=kharvox::isSteamBackedOpenXRRuntime(runtimeKind);}
        try{log("XR_VULKAN_DEVICE xr="+std::to_string(r)+" vk="+std::to_string(result));}catch(...){}
    }catch(const std::exception& e){*out=VK_NULL_HANDLE;result=VK_ERROR_INITIALIZATION_FAILED;try{log(e.what());}catch(...){}}
    runtimeCreate=nullptr;runtimeDownstream=nullptr;runtimePhysical=VK_NULL_HANDLE;
    return true;
}
void xrBindGameDevice(VkDevice gameDevice,PFN_vkGetDeviceProcAddr gdpa){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!enable2||failed||!gameDevice||!gdpa)return;
    runtimeGdpa=gdpa;
    runtimeDevice=gameDevice;
    try{log("[RUNTIME-ENABLE2] retained callback bound after ARGENT device initialization");}catch(...){}
}
