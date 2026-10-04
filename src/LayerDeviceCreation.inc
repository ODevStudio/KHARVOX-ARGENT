void releaseLayerDevice(VkDevice device,PFN_vkGetDeviceProcAddr gdpa,PFN_vkDestroyDevice destroy,const VkAllocationCallbacks* allocator) noexcept {
    if(!device)return;
    try{
        const auto next=destroy?destroy:gdpa?reinterpret_cast<PFN_vkDestroyDevice>(gdpa(device,"vkDestroyDevice")):nullptr;
        if(!next){RaiseFailFastException(nullptr,nullptr,0);std::terminate();}
        next(device,allocator);
    }catch(...){RaiseFailFastException(nullptr,nullptr,0);std::terminate();}
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice p,const VkDeviceCreateInfo* ci,const VkAllocationCallbacks* a,VkDevice* out){
    if(!out)return VK_ERROR_INITIALIZATION_FAILED;
    *out=VK_NULL_HANDLE;
    if(!ci)return VK_ERROR_INITIALIZATION_FAILED;
    VkDevice device{};
    PFN_vkGetDeviceProcAddr gdpa{};
    PFN_vkDestroyDevice destroy{};
    std::shared_ptr<State> s;
    bool registered{},cameraAttempted{};
    VkResult r=VK_ERROR_INITIALIZATION_FAILED;
    try{
        auto c=chain<VkLayerDeviceCreateInfo>(ci->pNext,VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,VK_LAYER_LINK_INFO);
        if(!c||!c->u.pLayerInfo)return VK_ERROR_INITIALIZATION_FAILED;
        auto cb=chain<VkLayerDeviceCreateInfo>(ci->pNext,VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,VK_LOADER_DATA_CALLBACK);
        auto setData=cb?cb->u.pfnSetDeviceLoaderData:nullptr;
        auto link=c->u.pLayerInfo;auto get=link->pfnNextGetInstanceProcAddr;gdpa=link->pfnNextGetDeviceProcAddr;
        if(!gdpa)return VK_ERROR_INITIALIZATION_FAILED;
        c->u.pLayerInfo=link->pNext;
        auto i=instanceOf(key(p));if(!i.handle)return VK_ERROR_INITIALIZATION_FAILED;
        if(i.runtimeAuxiliary){
            auto create=kharvox::resolveLayerCreateDevice(get,i.handle);
            if(!create)create=i.createDevice;
            r=create?create(p,ci,a,&device):VK_ERROR_INITIALIZATION_FAILED;
            if(r==VK_SUCCESS&&device){
                destroy=reinterpret_cast<PFN_vkDestroyDevice>(gdpa(device,"vkDestroyDevice"));
                if(!destroy){releaseLayerDevice(device,gdpa,destroy,a);return VK_ERROR_INITIALIZATION_FAILED;}
                s=std::make_shared<State>();s->device=device;s->physical=p;s->instance=i.handle;s->gipa=i.gipa;s->gdpa=gdpa;s->setLoaderData=setData;s->runtimeAuxiliary=true;s->destroy=destroy;
                {std::lock_guard<std::recursive_mutex> l(stateMutex);devices[key(device)]=s;deviceGeneration.fetch_add(1,std::memory_order_release);registered=true;}
                *out=device;device=VK_NULL_HANDLE;
                try{if(gameProcess())argent::log("SteamVR auxiliary Vulkan device passthrough");}catch(...){}
            }else{
                device=VK_NULL_HANDLE;if(r==VK_SUCCESS)r=VK_ERROR_INITIALIZATION_FAILED;
            }
            return r;
        }
        if(!get||!i.gipa)return VK_ERROR_INITIALIZATION_FAILED;
        const bool sfsRequested=i.game&&argent::sfs::nativeProbeEnabled();
        const auto create=reinterpret_cast<PFN_vkCreateDevice>(get(i.handle,"vkCreateDevice"));
        const auto families=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(i.gipa(i.handle,"vkGetPhysicalDeviceQueueFamilyProperties"));
        const auto memoryProperties=sfsRequested?reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(i.gipa(i.handle,"vkGetPhysicalDeviceMemoryProperties")):nullptr;
        const auto deviceProperties=sfsRequested?reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(i.gipa(i.handle,"vkGetPhysicalDeviceProperties")):nullptr;
        if(!create||!families||(sfsRequested&&(!memoryProperties||!deviceProperties)))return VK_ERROR_INITIALIZATION_FAILED;
        if(sfsRequested){
            auto queryFeatures=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(i.gipa(i.handle,"vkGetPhysicalDeviceFeatures2"));
            if(!queryFeatures)queryFeatures=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(i.gipa(i.handle,"vkGetPhysicalDeviceFeatures2KHR"));
            auto queryProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(i.gipa(i.handle,"vkGetPhysicalDeviceProperties2"));
            if(!queryProperties)queryProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(i.gipa(i.handle,"vkGetPhysicalDeviceProperties2KHR"));
            if(!queryFeatures||!queryProperties){try{argent::log("SFS_INIT_REFUSED Vulkan capability queries unavailable");}catch(...){}return VK_ERROR_FEATURE_NOT_PRESENT;}
            VkPhysicalDeviceMultiviewFeatures views{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&views;queryFeatures(p,&features);
            VkPhysicalDeviceMultiviewProperties viewLimits{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES};
            VkPhysicalDeviceSubgroupProperties subgroups{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};viewLimits.pNext=&subgroups;
            VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};properties.pNext=&viewLimits;queryProperties(p,&properties);
            const auto& gpu=properties.properties;
            try{argent::log("SFS_GPU device="+std::string(gpu.deviceName)+" vendor="+std::to_string(gpu.vendorID)+" deviceId="+std::to_string(gpu.deviceID)+
                " driver="+std::to_string(gpu.driverVersion)+" api="+std::to_string(gpu.apiVersion)+" multiview="+std::to_string(views.multiview)+
                " maxViews="+std::to_string(viewLimits.maxMultiviewViewCount)+" maxLayers="+std::to_string(gpu.limits.maxImageArrayLayers)+
                " subgroupSize="+std::to_string(subgroups.subgroupSize)+" subgroupStages="+std::to_string(subgroups.supportedStages)+" subgroupOps="+std::to_string(subgroups.supportedOperations));}catch(...){}
            if(const auto failure=argent::sfs::stereoCapabilityFailure(gpu,views.multiview,viewLimits.maxMultiviewViewCount,requestedEyeExtent(),ci->pNext)){
                try{argent::log(std::string("SFS_INIT_REFUSED ")+failure);}catch(...){}return VK_ERROR_FEATURE_NOT_PRESENT;
            }
            if(gpu.vendorID==0x1002)try{argent::log("SFS_GPU AMD capability checks passed; ARGENT headset rendering still requires hardware validation");}catch(...){}
        }
        auto required=i.game&&(!sfsRequested||argent::sfs::vrEnabled())?argent::xrExtensions(true):std::vector<std::string>{};
        bool gpuCheckpoints=false,gpuFaultExtension=false,gpuFault=false,addFaultFeatures=false;char diagnostics[8]{};
        VkPhysicalDeviceFaultFeaturesEXT faultFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
        bool nvDiagnostics=false,addNvFeatures=false,addNvConfig=false;
        VkPhysicalDeviceDiagnosticsConfigFeaturesNV nvFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV};
        VkDeviceDiagnosticsConfigCreateInfoNV nvConfig{VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV};
        if(sfsRequested)try{argent::log("GPU_DIAGNOSTICS_REQUESTED enabled="+std::to_string(argent::gpuCrashDiagnostics())+" extendedLogging="+std::to_string(argent::extendedLogging())+" nonuniformBulk=0");}catch(...){}
        if(sfsRequested&&GetEnvironmentVariableA("ARGENT_GPU_DIAGNOSTICS",diagnostics,8)==1&&diagnostics[0]=='1'){
            auto enumerate=reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(i.gipa(i.handle,"vkEnumerateDeviceExtensionProperties"));
            uint32_t count{};if(enumerate&&enumerate(p,nullptr,&count,nullptr)==VK_SUCCESS){std::vector<VkExtensionProperties> available(count);
                if(enumerate(p,nullptr,&count,available.data())==VK_SUCCESS)for(const auto& ext:available){
                    if(!std::strcmp(ext.extensionName,VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME))gpuCheckpoints=true;
                    if(!std::strcmp(ext.extensionName,VK_EXT_DEVICE_FAULT_EXTENSION_NAME))gpuFaultExtension=true;
                    if(!std::strcmp(ext.extensionName,VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME))nvDiagnostics=true;
                }}
            if(gpuCheckpoints)required.emplace_back(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
            try{argent::log("GPU_CHECKPOINT_EXTENSION supported="+std::to_string(gpuCheckpoints));}catch(...){}
            if(gpuFaultExtension){
                auto features=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(i.gipa(i.handle,"vkGetPhysicalDeviceFeatures2"));
                if(features){VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};supported.pNext=&faultFeatures;features(p,&supported);}
                gpuFault=faultFeatures.deviceFault==VK_TRUE;addFaultFeatures=gpuFault;
                for(auto node=static_cast<const VkBaseInStructure*>(ci->pNext);node;node=node->pNext)
                    if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT){
                        faultFeatures=*reinterpret_cast<const VkPhysicalDeviceFaultFeaturesEXT*>(node);
                        gpuFault=faultFeatures.deviceFault==VK_TRUE;addFaultFeatures=false;break;
                    }
                if(gpuFault)required.emplace_back(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
            }
            try{argent::log("GPU_FAULT_EXTENSION supported="+std::to_string(gpuFaultExtension)+" enabled="+std::to_string(gpuFault)+" vendorBinary="+std::to_string(gpuFault&&faultFeatures.deviceFaultVendorBinary));}catch(...){}
            if(nvDiagnostics){
                auto features=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(i.gipa(i.handle,"vkGetPhysicalDeviceFeatures2"));
                VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};supported.pNext=&nvFeatures;
                if(features)features(p,&supported);
                nvDiagnostics=nvFeatures.diagnosticsConfig==VK_TRUE;addNvFeatures=nvDiagnostics;addNvConfig=nvDiagnostics;
                nvConfig.flags=argent::gpuDiagnosticFlags;
                for(auto node=static_cast<const VkBaseInStructure*>(ci->pNext);node;node=node->pNext){
                    if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV){nvDiagnostics=nvDiagnostics&&reinterpret_cast<const VkPhysicalDeviceDiagnosticsConfigFeaturesNV*>(node)->diagnosticsConfig;addNvFeatures=false;}
                    if(node->sType==VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV){nvConfig.flags=reinterpret_cast<const VkDeviceDiagnosticsConfigCreateInfoNV*>(node)->flags;addNvConfig=false;}
                }
                if(nvDiagnostics)required.emplace_back(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);
                else{addNvFeatures=false;addNvConfig=false;}
            }
            try{argent::log("GPU_NV_DIAGNOSTICS enabled="+std::to_string(nvDiagnostics)+" flags="+std::to_string(nvDiagnostics?nvConfig.flags:0)+" resourceTracking="+std::to_string(nvDiagnostics&&(nvConfig.flags&VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV))+" shaderErrors="+std::to_string(nvDiagnostics&&(nvConfig.flags&VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_ERROR_REPORTING_BIT_NV)));}catch(...){}
        }
        bool waterRobustness=false,addWaterRobustness=false,addressReport=false,addAddressReport=false;
        VkPhysicalDevicePipelineRobustnessFeaturesEXT waterFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES_EXT};
        VkPhysicalDeviceAddressBindingReportFeaturesEXT addressFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT};
        if(sfsRequested){
            auto enumerate=reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(i.gipa(i.handle,"vkEnumerateDeviceExtensionProperties"));
            uint32_t count{};std::vector<VkExtensionProperties> available;
            if(enumerate&&enumerate(p,nullptr,&count,nullptr)==VK_SUCCESS){available.resize(count);if(enumerate(p,nullptr,&count,available.data())!=VK_SUCCESS)available.clear();}
            auto has=[&](const char* name){return std::any_of(available.begin(),available.end(),[&](const auto& ext){return !std::strcmp(ext.extensionName,name);});};
            auto features=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(i.gipa(i.handle,"vkGetPhysicalDeviceFeatures2"));
            if(features&&has(VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME)&&has(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME)){
                VkPhysicalDeviceRobustness2FeaturesEXT robustness2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
                VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};supported.pNext=&waterFeatures;waterFeatures.pNext=&robustness2;features(p,&supported);waterFeatures.pNext=nullptr;
                waterRobustness=waterFeatures.pipelineRobustness&&robustness2.robustBufferAccess2&&robustness2.robustImageAccess2;
                auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(i.gipa(i.handle,"vkGetPhysicalDeviceProperties2"));
                VkPhysicalDeviceDescriptorIndexingProperties indexing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES};VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};props.pNext=&indexing;
                if(properties)properties(p,&props);waterRobustness=waterRobustness&&indexing.robustBufferAccessUpdateAfterBind;
                addWaterRobustness=waterRobustness;
                for(auto node=static_cast<const VkBaseInStructure*>(ci->pNext);node;node=node->pNext){
                    if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES_EXT){waterRobustness=waterRobustness&&reinterpret_cast<const VkPhysicalDevicePipelineRobustnessFeaturesEXT*>(node)->pipelineRobustness;addWaterRobustness=false;}
                    if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES){waterRobustness=waterRobustness&&reinterpret_cast<const VkPhysicalDeviceVulkan14Features*>(node)->pipelineRobustness;addWaterRobustness=false;}
                }
                if(waterRobustness)required.emplace_back(VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME);
            }
            if(features&&i.addressMessenger&&has(VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME)){
                VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};supported.pNext=&addressFeatures;features(p,&supported);
                addressReport=addressFeatures.reportAddressBinding==VK_TRUE;addAddressReport=addressReport;
                for(auto node=static_cast<const VkBaseInStructure*>(ci->pNext);node;node=node->pNext)if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT){addressReport=addressReport&&reinterpret_cast<const VkPhysicalDeviceAddressBindingReportFeaturesEXT*>(node)->reportAddressBinding;addAddressReport=false;}
                if(addressReport)required.emplace_back(VK_EXT_DEVICE_ADDRESS_BINDING_REPORT_EXTENSION_NAME);
            }
            try{argent::log("WATER_ROBUSTNESS_FEATURE enabled="+std::to_string(waterRobustness)+" scope=water-pipeline-only");}catch(...){}
            try{argent::log("GPU_ADDRESS_REPORT enabled="+std::to_string(addressReport));}catch(...){}
        }
        if(argent::extendedLogging()){
            bool indexing=false;const char* source="absent";
            for(auto node=static_cast<const VkBaseInStructure*>(ci->pNext);node;node=node->pNext){
                if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES){indexing=reinterpret_cast<const VkPhysicalDeviceDescriptorIndexingFeatures*>(node)->shaderSampledImageArrayNonUniformIndexing!=0;source="descriptor-indexing";}
                if(node->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES){indexing=reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(node)->shaderSampledImageArrayNonUniformIndexing!=0;source="vulkan12";}
            }
            try{argent::log("GPU_MATERIAL_FEATURES sampledImageNonUniform="+std::to_string(indexing)+" source="+source);}catch(...){}
        }
        auto enabled=extensions(ci->enabledExtensionCount,ci->ppEnabledExtensionNames,required);
        auto modified=*ci;modified.enabledExtensionCount=uint32_t(enabled.size());modified.ppEnabledExtensionNames=enabled.data();
        VkPhysicalDeviceMultiviewFeatures mv{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
        auto runtimeModified=modified;
        while(runtimeModified.pNext&&static_cast<const VkBaseInStructure*>(runtimeModified.pNext)->sType==VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO)
            runtimeModified.pNext=static_cast<const VkBaseInStructure*>(runtimeModified.pNext)->pNext;
        auto runtimeWaterFeatures=waterFeatures;auto runtimeAddressFeatures=addressFeatures;
        auto runtimeNvFeatures=nvFeatures;auto runtimeNvConfig=nvConfig;
        if(addNvFeatures){nvFeatures.pNext=const_cast<void*>(modified.pNext);modified.pNext=&nvFeatures;runtimeNvFeatures.pNext=const_cast<void*>(runtimeModified.pNext);runtimeModified.pNext=&runtimeNvFeatures;}
        if(addNvConfig){nvConfig.pNext=modified.pNext;modified.pNext=&nvConfig;runtimeNvConfig.pNext=runtimeModified.pNext;runtimeModified.pNext=&runtimeNvConfig;}
        if(addWaterRobustness){waterFeatures.pNext=const_cast<void*>(modified.pNext);modified.pNext=&waterFeatures;runtimeWaterFeatures.pNext=const_cast<void*>(runtimeModified.pNext);runtimeModified.pNext=&runtimeWaterFeatures;}
        if(addAddressReport){addressFeatures.pNext=const_cast<void*>(modified.pNext);modified.pNext=&addressFeatures;runtimeAddressFeatures.pNext=const_cast<void*>(runtimeModified.pNext);runtimeModified.pNext=&runtimeAddressFeatures;}
        auto runtimeFaultFeatures=faultFeatures;
        if(addFaultFeatures){
            faultFeatures.pNext=const_cast<void*>(modified.pNext);modified.pNext=&faultFeatures;
            runtimeFaultFeatures.pNext=const_cast<void*>(runtimeModified.pNext);runtimeModified.pNext=&runtimeFaultFeatures;
        }
        VkPhysicalDeviceMultiviewFeatures runtimeMv{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
        if(sfsRequested){
            const bool present=argent::sfs::multiviewRequest(ci->pNext).present;
            if(!present){mv.multiview=VK_TRUE;mv.pNext=const_cast<void*>(modified.pNext);modified.pNext=&mv;
                runtimeMv.multiview=VK_TRUE;runtimeMv.pNext=const_cast<void*>(runtimeModified.pNext);runtimeModified.pNext=&runtimeMv;}
        }
        char steamDefer[8]{};
        const bool deferSteamXrDevice=i.game&&GetEnvironmentVariableA("ARGENT_STEAMVR_DEFER_XR_DEVICE",steamDefer,sizeof(steamDefer))==1&&steamDefer[0]=='1';
        if(deferSteamXrDevice)try{argent::log("XR_VULKAN_DEVICE deferred: SteamVR safe-start uses normal game vkCreateDevice");}catch(...){}
        if(deferSteamXrDevice||!i.game||!argent::xrCreateGameDevice(get,gdpa,i.handle,p,&runtimeModified,&modified,a,&device,r))r=create(p,&modified,a,&device);
        if(r==VK_SUCCESS&&device){
            destroy=reinterpret_cast<PFN_vkDestroyDevice>(gdpa(device,"vkDestroyDevice"));
            if(!destroy){releaseLayerDevice(device,gdpa,destroy,a);return VK_ERROR_INITIALIZATION_FAILED;}
            s=std::make_shared<State>();s->device=device;s->physical=p;s->instance=i.handle;s->gipa=i.gipa;s->gdpa=gdpa;s->setLoaderData=setData;s->game=i.game;s->destroy=destroy;
            uint32_t count=0;
            families(p,&count,nullptr);s->families.resize(count);families(p,&count,s->families.data());
            if(sfsRequested){
                VkPhysicalDeviceMemoryProperties memory{};
                memoryProperties(p,&memory);
                auto config=argent::sfs::vrEnabled()?argent::sfs::eternalProfile():argent::sfs::Configuration{};
                if(!argent::sfs::initialize(device,p,gdpa,memory,config)){
                    releaseLayerDevice(device,gdpa,destroy,a);return VK_ERROR_INITIALIZATION_FAILED;
                }
                s->sfs=true;
                if(gpuCheckpoints)argent::sfs::enableGpuCheckpoints(device,gdpa);
                if(gpuFault)argent::sfs::enableGpuFaultReport(device,gdpa);
                if(waterRobustness)argent::sfs::enableWaterRobustness(device);
                {
                    VkPhysicalDeviceProperties properties{};deviceProperties(p,&properties);
                    argent::sfs::configurePerformanceGpu(device,properties.limits.timestampPeriod,uint32_t(s->families.size()),s->families.data());
                }
                for(uint32_t qi=0;qi<ci->queueCreateInfoCount;++qi)s->queueFamilies.push_back(ci->pQueueCreateInfos[qi].queueFamilyIndex);
                try{argent::log(argent::sfs::vrEnabled()?"SFS_VR active: experimental Eternal camera and OpenXR projection":"SFS_ENGINE_PROBE active: multiview internals, desktop mono output, no XR projection");}catch(...){}
            }
            if(s->sfs&&argent::sfs::vrEnabled()){
                cameraAttempted=true;
                if(argent::camera::install())argent::dlss::install();
            }
            {std::lock_guard<std::recursive_mutex> l(stateMutex);devices[key(device)]=s;deviceGeneration.fetch_add(1,std::memory_order_release);registered=true;}
            argent::xrBindGameDevice(device,gdpa);
            *out=device;device=VK_NULL_HANDLE;
        }else{
            device=VK_NULL_HANDLE;if(r==VK_SUCCESS)r=VK_ERROR_INITIALIZATION_FAILED;
        }
        if(i.game)try{argent::log("vkCreateDevice result="+std::to_string(r));}catch(...){}
        return r;
    }catch(const std::bad_alloc&){r=VK_ERROR_OUT_OF_HOST_MEMORY;}
    catch(...){r=VK_ERROR_INITIALIZATION_FAILED;}
    try{
        if(registered){std::lock_guard<std::recursive_mutex> l(stateMutex);devices.erase(key(device));deviceGeneration.fetch_add(1,std::memory_order_release);}
        if(cameraAttempted)argent::camera::stop();
        if(s&&s->sfs)argent::sfs::shutdown(device);
    }catch(...){RaiseFailFastException(nullptr,nullptr,0);std::terminate();}
    releaseLayerDevice(device,gdpa,destroy,a);
    return r;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice d,const VkAllocationCallbacks* a){
    try{
        auto s=deviceOf(key(d));if(!s)return;
        for(auto& m:s->mirrors)m.second->destroy(*s);s->mirrors.clear();
        if(s->game){argent::camera::stop();argent::shutdownXR(d);}
        if(s->sfs)argent::sfs::shutdown(d);
        argent::capture::forgetDevice(d);
        {std::lock_guard<std::recursive_mutex> l(stateMutex);devices.erase(key(d));deviceGeneration.fetch_add(1,std::memory_order_release);}
        releaseLayerDevice(d,s->gdpa,s->destroy,a);
    }catch(...){RaiseFailFastException(nullptr,nullptr,0);std::terminate();}
}
