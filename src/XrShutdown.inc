void shutdownXRImpl(VkDevice d){
    std::lock_guard<std::recursive_mutex> guard(mutex);if(boundDevice!=d)return;
    try{cancelStereoImpl();}catch(const std::exception& e){try{log(std::string("STEREO_SHUTDOWN_END_FAILED ")+e.what());}catch(...){}}
    controllerActions.destroy();
    std::lock_guard<std::recursive_mutex> queueGuard(*device.queueMutex);
    requireGpuRetirement(VK(vkDeviceWaitIdle)(d),"XR shutdown");copyTiming.shutdownAfterCompletion();diagnosticCopyTiming.shutdownAfterCompletion();bridge.destroy(device);destroySwapchain();releasePauseBindings();if(localSpace)XR(xrDestroySpace)(localSpace);if(space)XR(xrDestroySpace)(space);if(session)XR(xrDestroySession)(session);
    localSpace=XR_NULL_HANDLE;
    if(fence)VK(vkDestroyFence)(d,fence,nullptr);if(pool)VK(vkDestroyCommandPool)(d,pool,nullptr);
    space=XR_NULL_HANDLE;session=XR_NULL_HANDLE;fence=VK_NULL_HANDLE;pool=VK_NULL_HANDLE;boundDevice=VK_NULL_HANDLE;running=false;failed=true;
    runtimeDevice=VK_NULL_HANDLE;runtimeGdpa=nullptr;
    try{log("XR_SHUTDOWN");}catch(...){}
}
