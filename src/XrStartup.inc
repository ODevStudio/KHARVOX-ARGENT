bool initializeXRImpl(){
    std::lock_guard<std::recursive_mutex> guard(mutex);if(attempted)return instance&&!failed;attempted=true;
    try{
        loader=LoadLibraryExW((runtimePath()/L"openxr_loader.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!loader)throw std::runtime_error("OpenXR loader unavailable");get=reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader,"xrGetInstanceProcAddr"));if(!get)throw std::runtime_error("xrGetInstanceProcAddr unavailable");
        uint32_t count=0;check(XR(xrEnumerateInstanceExtensionProperties)(nullptr,0,&count,nullptr),"XR extensions");
        std::vector<XrExtensionProperties> props(count,{XR_TYPE_EXTENSION_PROPERTIES});check(XR(xrEnumerateInstanceExtensionProperties)(nullptr,count,&count,props.data()),"XR extensions");
        bool vulkan=false,vulkan2=false;for(auto& p:props){if(!strcmp(p.extensionName,XR_KHR_VULKAN_ENABLE_EXTENSION_NAME))vulkan=true;if(!strcmp(p.extensionName,XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME))vulkan2=true;}
        runtimeKind=kharvox::classifyOpenXRRuntime(kharvox::activeOpenXRRuntimeManifest());
        auto path=kharvox::selectOpenXRVulkanPath(runtimeKind,false,vulkan,vulkan2);
        char forceSteamEnable1[8]{};
        if(kharvox::isSteamBackedOpenXRRuntime(runtimeKind)
            &&GetEnvironmentVariableA("ARGENT_STEAMVR_FORCE_ENABLE1",forceSteamEnable1,sizeof(forceSteamEnable1))==1
            &&forceSteamEnable1[0]=='1'
            &&vulkan)
            path=kharvox::OpenXRVulkanPath::VulkanEnable1Direct;
        if(path==kharvox::OpenXRVulkanPath::None)throw std::runtime_error("No OpenXR Vulkan graphics extension available");
        enable2=path==kharvox::OpenXRVulkanPath::VulkanEnable2RuntimeManaged;
        const char* extension=enable2?XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME:XR_KHR_VULKAN_ENABLE_EXTENSION_NAME;
        try{log(std::string("XR_VULKAN_PATH ")+extension);}catch(...){}
        XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(ci.applicationInfo.applicationName,"DOOM Eternal");
        strcpy_s(ci.applicationInfo.engineName,"idTech7");
        ci.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);ci.enabledExtensionCount=1;ci.enabledExtensionNames=&extension;
        try{log(std::string("XR_APPLICATION_IDENTITY app=")+ci.applicationInfo.applicationName+" engine="+ci.applicationInfo.engineName);}catch(...){}
        check(XR(xrCreateInstance)(&ci,&instance),"xrCreateInstance");
        XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;check(XR(xrGetSystem)(instance,&si,&system),"xrGetSystem");
        XrGraphicsRequirementsVulkanKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};check(enable2?XR(xrGetVulkanGraphicsRequirements2KHR)(instance,system,&req):XR(xrGetVulkanGraphicsRequirementsKHR)(instance,system,&req),"XR graphics requirements");
        XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};check(XR(xrGetInstanceProperties)(instance,&ip),"XR properties");simulator=!strcmp(ip.runtimeName,"OpenXR Simulator Runtime");try{log(std::string("XR_INITIALIZED runtime=")+ip.runtimeName);}catch(...){}return true;
    }catch(const std::exception& e){failed=true;try{log(std::string("XR_DISABLED ")+e.what());}catch(...){}return false;}
}
bool initializeXR(){
    try{
        const auto activeRuntime=kharvox::classifyOpenXRRuntime(kharvox::activeOpenXRRuntimeManifest());
        if(sfs::vrEnabled()&&kharvox::isSteamBackedOpenXRRuntime(activeRuntime)){
            return xrWorker().invoke([]{
                try{log("[STEAM-XR-THREAD] initializing OpenXR on worker thread="+std::to_string(GetCurrentThreadId()));}catch(...){}
                return initializeXRImpl();
            });
        }
        return initializeXRImpl();
    }catch(const std::exception& e){
        std::lock_guard<std::recursive_mutex> guard(mutex);attempted=true;failed=true;
        try{log(std::string("XR_DISABLED ")+e.what());}catch(...){}
        return false;
    }
}
std::vector<std::string> xrExtensions(bool isDevice){
    std::lock_guard<std::recursive_mutex> guard(mutex);std::vector<std::string> result;if(!instance||failed||enable2)return result;
    try{
        auto query=isDevice?reinterpret_cast<PFN_xrGetVulkanInstanceExtensionsKHR>(XR(xrGetVulkanDeviceExtensionsKHR)):XR(xrGetVulkanInstanceExtensionsKHR);
        uint32_t size=0;check(query(instance,system,0,&size,nullptr),"XR Vulkan extensions");std::vector<char> text(size?size:1);
        check(query(instance,system,uint32_t(text.size()),&size,text.data()),"XR Vulkan extensions");
        std::istringstream stream(text.data());std::string name;while(stream>>name){result.push_back(name);try{log("XR_REQUIRED_EXTENSION "+name);}catch(...){}}
    }catch(const std::exception& e){failed=true;try{log(e.what());}catch(...){}return {};}
    return result;
}
