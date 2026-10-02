VKAPI_ATTR VkResult VKAPI_CALL runtimeCreateAdapter(VkPhysicalDevice,const VkDeviceCreateInfo* ci,const VkAllocationCallbacks* a,VkDevice* out){
    const auto create=runtimeCreate.load();const auto physical=runtimePhysical.load();const auto downstream=runtimeDownstream.load();
    if(!create||!physical||!ci||!downstream)return VK_ERROR_INITIALIZATION_FAILED;
    VkDeviceCreateInfo merged=*ci;
    merged.pNext=downstream->pNext;
    try{log("[RUNTIME-ENABLE2] vkCreateDevice adapter reattached Vulkan loader chain");}catch(...){}
    return create(physical,&merged,a,out);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL runtimeGipa(VkInstance i,const char* name){
    const auto resolved=kharvox::resolveRuntimeVulkanProc(i,name,runtimeSessionRoute.load(),simulator,
        runtimeNext.load(),publicGipa(),runtimeCreate.load()?reinterpret_cast<PFN_vkVoidFunction>(runtimeCreateAdapter):nullptr,
        runtimeDevice.load(),runtimeGdpa.load());
    static std::atomic<uint32_t> traceCount{};
    const auto trace=traceCount.fetch_add(1,std::memory_order_relaxed);
    if(trace<128){
        const char* route="downstream";
        switch(resolved.route){
        case kharvox::RuntimeDispatchRoute::CreateDevice: route="create-device"; break;
        case kharvox::RuntimeDispatchRoute::SessionLoader: route="session-loader-gipa"; break;
        case kharvox::RuntimeDispatchRoute::PhysicalLoader: route="physical-loader"; break;
        case kharvox::RuntimeDispatchRoute::RuntimeDevice: route="runtime-device-downstream"; break;
        default: break;
        }
        try{log(std::string("[RUNTIME-ENABLE2] GIPA thread=")+std::to_string(GetCurrentThreadId())+
            " name="+(name?name:"NULL")+" adapter="+route+
            " returned="+std::to_string(reinterpret_cast<uintptr_t>(resolved.function)));}catch(...){}
    }
    return resolved.function;
}
