void releaseLayerInstance(const Instance& state,const VkAllocationCallbacks* allocator) noexcept {
    if(!state.handle)return;
    try{
        const auto destroy=state.destroy?state.destroy:state.gipa?reinterpret_cast<PFN_vkDestroyInstance>(state.gipa(state.handle,"vkDestroyInstance")):nullptr;
        if(!destroy||(state.addressMessenger&&!state.destroyMessenger)){RaiseFailFastException(nullptr,nullptr,0);std::terminate();}
        if(state.addressMessenger)state.destroyMessenger(state.handle,state.addressMessenger,nullptr);
        destroy(state.handle,allocator);
    }catch(...){RaiseFailFastException(nullptr,nullptr,0);std::terminate();}
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* ci,const VkAllocationCallbacks* a,VkInstance* out){
    if(!out)return VK_ERROR_INITIALIZATION_FAILED;
    *out=VK_NULL_HANDLE;
    if(!ci)return VK_ERROR_INITIALIZATION_FAILED;
    Instance state{};
    VkResult r=VK_ERROR_INITIALIZATION_FAILED;
    try{
        InstanceCreateScope createScope;
        auto c=chain<VkLayerInstanceCreateInfo>(ci->pNext,VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO,VK_LAYER_LINK_INFO);
        if(!c||!c->u.pLayerInfo||!c->u.pLayerInfo->pfnNextGetInstanceProcAddr)return VK_ERROR_INITIALIZATION_FAILED;
        auto link=c->u.pLayerInfo;auto get=link->pfnNextGetInstanceProcAddr;auto pg=link->pfnNextGetPhysicalDeviceProcAddr;c->u.pLayerInfo=link->pNext;
        bool game=gameProcess()&&!createScope.nested;
        const char* app=ci->pApplicationInfo?ci->pApplicationInfo->pApplicationName:nullptr;
        if(app&&std::strstr(app,"steamvr"))game=false;
        const bool nested=createScope.nested;
        const auto runtimeKind=kharvox::classifyOpenXRRuntime(kharvox::activeOpenXRRuntimeManifest());
        const bool steamRuntimeAuxiliary=gameProcess()&&kharvox::shouldPassthroughSteamRuntimeAuxiliary(runtimeKind,nested,app?app:"");
        if(game){argent::installStartupCrashTrace();try{argent::log(std::string("vkCreateInstance app=")+(app?app:"unknown"));}catch(...){}}
        if(steamRuntimeAuxiliary)try{argent::log(std::string("SteamVR auxiliary Vulkan instance passthrough app=")+(app?app:"unknown")+" nested="+std::to_string(nested));}catch(...){}
        if(game&&argent::sfs::vrEnabled()&&argent::sfs::sourceRingRequested()){
            auto extent=requestedEyeExtent();
            if(extent.width&&extent.height&&!argent::camera::installRenderExtent(extent.width,extent.height)){
                try{argent::log("ETERNAL_RENDER_EXTENT refused: unsupported engine accessors");}catch(...){}
                return VK_ERROR_INITIALIZATION_FAILED;
            }
        }
        auto required=game&&(!argent::sfs::nativeProbeEnabled()||argent::sfs::vrEnabled())&&argent::initializeXR()?argent::xrExtensions(false):std::vector<std::string>{};
        bool addressDebug=false;char gpuDiagnostics[8]{};
        if(game&&GetEnvironmentVariableA("ARGENT_GPU_DIAGNOSTICS",gpuDiagnostics,8)==1&&gpuDiagnostics[0]=='1'){
            auto enumerate=reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(get(nullptr,"vkEnumerateInstanceExtensionProperties"));
            uint32_t count{};if(enumerate&&enumerate(nullptr,&count,nullptr)==VK_SUCCESS){std::vector<VkExtensionProperties> props(count);
                if(enumerate(nullptr,&count,props.data())==VK_SUCCESS)for(const auto& prop:props)if(!std::strcmp(prop.extensionName,VK_EXT_DEBUG_UTILS_EXTENSION_NAME))addressDebug=true;}
            if(addressDebug)required.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        auto enabled=extensions(ci->enabledExtensionCount,ci->ppEnabledExtensionNames,required);
        auto modified=*ci;modified.enabledExtensionCount=uint32_t(enabled.size());modified.ppEnabledExtensionNames=enabled.data();
        auto create=reinterpret_cast<PFN_vkCreateInstance>(get(nullptr,"vkCreateInstance"));
        state.gipa=get;state.physicalProc=pg;state.game=game;state.runtimeAuxiliary=steamRuntimeAuxiliary;
        if(steamRuntimeAuxiliary)r=create?create(ci,a,&state.handle):VK_ERROR_INITIALIZATION_FAILED;
        else if(!game||!argent::xrCreateGameInstance(get,&modified,a,&state.handle,r))r=create?create(&modified,a,&state.handle):VK_ERROR_INITIALIZATION_FAILED;
        if(r!=VK_SUCCESS)state.handle=VK_NULL_HANDLE;
        else if(!state.handle)r=VK_ERROR_INITIALIZATION_FAILED;
        else{
            state.destroy=reinterpret_cast<PFN_vkDestroyInstance>(get(state.handle,"vkDestroyInstance"));
            if(!state.destroy){releaseLayerInstance(state,a);return VK_ERROR_INITIALIZATION_FAILED;}
            state.createDevice=reinterpret_cast<PFN_vkCreateDevice>(get(state.handle,"vkCreateDevice"));
            if(addressDebug){
                state.destroyMessenger=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(get(state.handle,"vkDestroyDebugUtilsMessengerEXT"));
                auto createMessenger=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(get(state.handle,"vkCreateDebugUtilsMessengerEXT"));
                VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};info.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;info.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT;info.pfnUserCallback=argent::gpuAddressCallback;
                VkDebugUtilsMessengerEXT messenger{};
                const auto mr=createMessenger&&state.destroyMessenger?createMessenger(state.handle,&info,nullptr,&messenger):VK_ERROR_EXTENSION_NOT_PRESENT;
                if(mr==VK_SUCCESS)state.addressMessenger=messenger;
                try{argent::log("GPU_ADDRESS_MESSENGER result="+std::to_string(mr));}catch(...){}
            }
            {std::lock_guard<std::recursive_mutex> lock(stateMutex);instances[key(state.handle)]=state;}
            *out=state.handle;state.handle=VK_NULL_HANDLE;
        }
        if(game)try{argent::log("vkCreateInstance result="+std::to_string(r));}catch(...){}
        return r;
    }catch(const std::bad_alloc&){r=VK_ERROR_OUT_OF_HOST_MEMORY;}
    catch(...){r=VK_ERROR_INITIALIZATION_FAILED;}
    releaseLayerInstance(state,a);
    return r;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance i,const VkAllocationCallbacks* a){
    try{
        auto state=instanceOf(key(i));
        {std::lock_guard<std::recursive_mutex> lock(stateMutex);instances.erase(key(i));}
        releaseLayerInstance(state,a);
    }catch(...){RaiseFailFastException(nullptr,nullptr,0);std::terminate();}
}
