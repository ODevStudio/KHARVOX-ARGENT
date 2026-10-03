void recordPipelineBuild(State* s,const char* kind,const std::string& shaders,uint64_t startNs,uint64_t cpuStartUs,uint64_t lockNs,uint64_t compileNs,uint64_t driverNs)noexcept{try{
    if(!startNs)return;
    const auto endNs=CommandCpuTiming::now(),wallNs=endNs-startNs;
    const bool cpuMeasured=wallNs>=50000000;
    const auto cpuEndUs=cpuMeasured?threadCpuUs():0,cpuUs=cpuMeasured&&cpuEndUs>=cpuStartUs?cpuEndUs-cpuStartUs:0;
    s->pipelineBuilds.fetch_add(1,std::memory_order_relaxed);
    s->pipelineBuildWallNs.fetch_add(wallNs,std::memory_order_relaxed);
    if(cpuMeasured){s->pipelineBuildCpuUs.fetch_add(cpuUs,std::memory_order_relaxed);s->pipelineBuildCpuSamples.fetch_add(1,std::memory_order_relaxed);}
    s->pipelineBuildCompileNs.fetch_add(compileNs,std::memory_order_relaxed);
    s->pipelineBuildDriverNs.fetch_add(driverNs,std::memory_order_relaxed);
    s->pipelineBuildLockNs.fetch_add(lockNs,std::memory_order_relaxed);
    CommandCpuTiming::maximum(s->pipelineBuildMaxNs,wallNs);
    if(wallNs>=1000000)note("PIPELINE_BUILD kind="+std::string(kind)+" startUs="+std::to_string(startNs/1000)+" endUs="+std::to_string(endNs/1000)
        +" thread="+std::to_string(GetCurrentThreadId())+" wallMs="+std::to_string(double(wallNs)/1000000.)
        +" threadCpuMs="+(cpuMeasured?std::to_string(double(cpuUs)/1000.):"na")+" lockMs="+std::to_string(double(lockNs)/1000000.)
        +" compileMs="+std::to_string(double(compileNs)/1000000.)+" driverMs="+std::to_string(double(driverNs)/1000000.)+" shaders="+shaders);
}catch(...){}
}
void forgetPipelineMetadata(State* s,VkPipeline pipeline){
    if(s->waterCapture)s->waterCapture->forgetPipeline(pipeline);
    handDepth::handScenePipelineDestroyed(pipeline);
    s->stereoPipelines.erase(pipeline);s->indirectPipelines.erase(pipeline);s->computeStereo.erase(pipeline);
    s->pipelineMarkers.erase(pipeline);s->dispatchEndMarkers.erase(pipeline);
}
VKAPI_ATTR VkResult VKAPI_CALL graphics(VkDevice d,VkPipelineCache cache,uint32_t count,const VkGraphicsPipelineCreateInfo* infos,const VkAllocationCallbacks* a,VkPipeline* out){
    if(!out)return VK_ERROR_INITIALIZATION_FAILED;
    for(uint32_t j=0;j<count;++j)out[j]=VK_NULL_HANDLE;
    if(!infos)return VK_ERROR_INITIALIZATION_FAILED;
try {const auto requestNs=argent::cleanRelease?0:CommandCpuTiming::now();auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);const auto batchLockNs=argent::cleanRelease?0:CommandCpuTiming::now()-requestNs;
    const auto destroy=FN(vkDestroyPipeline);
    const auto cleanup=[&](VkPipeline pipeline){forgetPipelineMetadata(s,pipeline);destroy(d,pipeline,a);};
    using OwnedPipeline=std::unique_ptr<std::remove_pointer_t<VkPipeline>,decltype(cleanup)>;
    for(uint32_t j=0;j<count;++j){const auto buildStart=s->profileTiming?(j==0?requestNs:CommandCpuTiming::now()):0;const auto cpuStart=buildStart?threadCpuUs():0;uint64_t compileNs{},driverNs{};std::string shaderHashes;
        auto info=infos[j];std::vector<VkPipelineShaderStageCreateInfo> stages(info.pStages,info.pStages+info.stageCount);const auto seed=pipelineSeed(info);
        if(false&&s->materialDiagnostics<512
            &&info.pColorBlendState&&info.pColorBlendState->attachmentCount){
            bool blended=false;
            for(uint32_t k=0;k<info.pColorBlendState->attachmentCount;++k)
                blended|=info.pColorBlendState->pAttachments[k].blendEnable!=0;
            if(blended){
                ++s->materialDiagnostics;
                std::string signature;
                for(const auto& stage:stages){const auto& code=s->shaders.at(stage.module);
                    signature+=" stage"+std::to_string(stage.stage)+"="+shaderKey(profileHash(code.data(),uint32_t(code.size()*4)))
                        +"_"+shaderKey(profileHash(code.data(),uint32_t(code.size()*4),seed));}
                const auto* depth=info.pDepthStencilState;
                note("[SFS-MATERIAL] translucent subpass="+std::to_string(info.subpass)
                    +" depthTest="+std::to_string(depth?depth->depthTestEnable:0)
                    +" depthWrite="+std::to_string(depth?depth->depthWriteEnable:0)
                    +" depthCompare="+std::to_string(depth?depth->depthCompareOp:0)+signature);
                for(uint32_t k=0;k<info.pColorBlendState->attachmentCount;++k){const auto& b=info.pColorBlendState->pAttachments[k];
                    note("[SFS-MATERIAL] attachment="+std::to_string(k)+" blend="+std::to_string(b.blendEnable)
                        +" src="+std::to_string(b.srcColorBlendFactor)+" dst="+std::to_string(b.dstColorBlendFactor)
                        +" op="+std::to_string(b.colorBlendOp)+" mask="+std::to_string(b.colorWriteMask));}
            }
        }
        const bool skipProjection=s->configuration.projectDepthOnly&&(!info.pDepthStencilState||!info.pDepthStencilState->depthTestEnable);
        auto monoStages=stages;bool captureMaterial=false;
        for(size_t k=0;k<stages.size();++k){auto& stage=stages[k];const auto original=stage.module;
            const auto& code=s->shaders.at(original);const auto variant=profileHash(code.data(),uint32_t(code.size()*4),seed);bool compute{};
            captureMaterial|=argent::gpuCrashDiagnostics()&&stage.stage==VK_SHADER_STAGE_FRAGMENT_BIT&&profileHash(code.data(),uint32_t(code.size()*4))==0xd300c0135fbca8b0ull;
            if(buildStart){if(k)shaderHashes+=',';shaderHashes+=std::to_string(stage.stage)+":"+shaderKey(profileHash(code.data(),uint32_t(code.size()*4)));}
            stage.module=compiledModule(s,original,variant,compute,s->pipelineBindings.at(info.layout),skipProjection,-1,false,&compileNs);
            monoStages[k].module=compiledModule(s,original,variant,compute,s->pipelineBindings.at(info.layout),skipProjection,-1,true,&compileNs);
        }
        if(captureMaterial&&s->materialCaptureCount++>=128){
            if(s->materialCaptureCount==129)try{note("MATERIAL_PIPELINE_CAPTURE skipped=128-pipeline-limit");}catch(...){}
            captureMaterial=false;
        }
        info.pStages=monoStages.data();
        if(info.flags&VK_PIPELINE_CREATE_DERIVATIVE_BIT)throw std::runtime_error("SFS derivative pipelines need batch remapping");
        const auto stereoPass=s->passes.at(info.renderPass);
        OwnedPipeline mono(nullptr,cleanup),stereo(nullptr,cleanup);
        {
         UnlockedDriverScope unlocked(lock);
         MaterialPipelineCapture materialCache(d,s->resolver,captureMaterial);
         if(captureMaterial&&!materialCache.active())try{note("MATERIAL_PIPELINE_CAPTURE unavailable");}catch(...){}
         const auto started=s->profileTiming?CommandCpuTiming::now():0;
         VkPipeline created{};
         auto r=FN(vkCreateGraphicsPipelines)(d,materialCache.cache(cache),1,&info,a,&created);
         if(r!=VK_SUCCESS){try{note("PIPELINE_CREATE_FAILED kind=graphics variant=mono result="+std::to_string(r)+" shaders="+shaderHashes);}catch(...){}return r;}
         if(!created)return VK_ERROR_INITIALIZATION_FAILED;
         mono.reset(created);
         info.pStages=stages.data();info.renderPass=stereoPass;
         created=VK_NULL_HANDLE;
         r=FN(vkCreateGraphicsPipelines)(d,materialCache.cache(cache),1,&info,a,&created);
         if(r!=VK_SUCCESS){try{note("PIPELINE_CREATE_FAILED kind=graphics variant=stereo result="+std::to_string(r)+" shaders="+shaderHashes);}catch(...){}return r;}
         if(!created)return VK_ERROR_INITIALIZATION_FAILED;
         stereo.reset(created);
         if(materialCache.active())try{
          const auto data=materialCache.data();wchar_t path[32768]{};const auto length=GetEnvironmentVariableW(L"ARGENT_LOG",path,32768);
          if(!length||length>=32768)throw std::runtime_error("ARGENT_LOG unavailable");
          const auto directory=std::filesystem::path(std::wstring(path)+L".pipelines");std::filesystem::create_directories(directory);
          const auto filename="material-"+std::to_string(reinterpret_cast<uint64_t>(stereo.get()))+".bin";
          std::ofstream file(directory/filename,std::ios::binary);file.write(data.data(),data.size());file.close();
          if(!file)throw std::runtime_error("Cannot save material pipeline cache");
          note("MATERIAL_PIPELINE_CAPTURE mono="+std::to_string(reinterpret_cast<uint64_t>(mono.get()))+" stereo="+std::to_string(reinterpret_cast<uint64_t>(stereo.get()))+" bytes="+std::to_string(data.size())+" file="+filename);
         }catch(const std::exception& e){resultFailure("MATERIAL_PIPELINE_CAPTURE",e.what(),VK_SUCCESS);}catch(...){}
         if(started){driverNs=CommandCpuTiming::now()-started;const auto ms=double(driverNs)/1000000.;if(ms>=2)try{note("pipeline driver type=graphics variants=2 wallMs="+std::to_string(ms));}catch(...){}}
        }
        const auto pipeline=mono.get();
        if(s->waterCapture){std::vector<std::pair<uint32_t,uint64_t>> hashes;for(uint32_t k=0;k<infos[j].stageCount;++k){const auto& stage=infos[j].pStages[k];const auto& code=s->shaders.at(stage.module);hashes.push_back({stage.stage,profileHash(code.data(),uint32_t(code.size()*4))});}s->waterCapture->graphicsPipeline(pipeline,infos[j],hashes);}
        handDepth::handSceneGraphicsPipelinesCreated(1,&infos[j],&pipeline);s->stereoPipelines[pipeline]=stereo.get();
        try{
        if(s->checkpoint){std::string label="graphics";for(uint32_t k=0;k<infos[j].stageCount;++k){const auto& stage=infos[j].pStages[k];const auto& code=s->shaders.at(stage.module);label+=" stage"+std::to_string(stage.stage)+"="+shaderKey(profileHash(code.data(),uint32_t(code.size()*4)));}registerMarker(s,pipeline,label+" pipeline="+std::to_string(reinterpret_cast<uint64_t>(pipeline)));registerMarker(s,stereo.get(),label+" multiview pipeline="+std::to_string(reinterpret_cast<uint64_t>(stereo.get())));}
        if(argent::perf::enabled())note("PERF_PIPELINE kind=graphics pipeline="+std::to_string(reinterpret_cast<uint64_t>(pipeline))+" shaders="+shaderHashes);
        }catch(...){}
        recordPipelineBuild(s,"graphics",shaderHashes,buildStart,cpuStart,j==0?batchLockNs:0,compileNs,driverNs);
        stereo.release();out[j]=mono.release();
    }return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR VkResult VKAPI_CALL compute(VkDevice d,VkPipelineCache cache,uint32_t count,const VkComputePipelineCreateInfo* infos,const VkAllocationCallbacks* a,VkPipeline* out){
    if(!out)return VK_ERROR_INITIALIZATION_FAILED;
    for(uint32_t j=0;j<count;++j)out[j]=VK_NULL_HANDLE;
    if(!infos)return VK_ERROR_INITIALIZATION_FAILED;
try {const auto requestNs=argent::cleanRelease?0:CommandCpuTiming::now();auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);const auto batchLockNs=argent::cleanRelease?0:CommandCpuTiming::now()-requestNs;
    const auto destroy=FN(vkDestroyPipeline);
    const auto cleanup=[&](VkPipeline pipeline){forgetPipelineMetadata(s,pipeline);destroy(d,pipeline,a);};
    using OwnedPipeline=std::unique_ptr<std::remove_pointer_t<VkPipeline>,decltype(cleanup)>;
    for(uint32_t j=0;j<count;++j){
        const auto buildStart=s->profileTiming?(j==0?requestNs:CommandCpuTiming::now()):0;const auto cpuStart=buildStart?threadCpuUs():0;uint64_t compileNs{},driverNs{};
        auto info=infos[j];const auto original=info.stage.module;bool stereo{};
        if(info.flags&VK_PIPELINE_CREATE_DERIVATIVE_BIT)return VK_ERROR_FEATURE_NOT_PRESENT;
        const auto& originalCode=s->shaders.at(original);const auto profile=profileHash(originalCode.data(),uint32_t(originalCode.size()*4));const auto shaderHash=buildStart?shaderKey(profile):std::string{};
        VkPipelineRobustnessCreateInfoEXT waterRobustness{};
        if(protectWaterPipeline(profile,s->waterRobustness,info,waterRobustness))try{note("WATER_ROBUSTNESS shader="+shaderKey(profile)+" buffers=robust2 images=robust2");}catch(...){}
        info.stage.module=compiledModule(s,original,0,stereo,s->pipelineBindings.at(info.layout),false,-1,false,&compileNs);
        const bool indirect=stereo;
        std::array<VkShaderModule,2> modules{};
        if(indirect)for(int eye=0;eye<2;++eye){bool ignored{};modules[eye]=compiledModule(s,original,0,ignored,s->pipelineBindings.at(info.layout),false,eye,false,&compileNs);}
        OwnedPipeline pipeline(nullptr,cleanup);
        std::array<OwnedPipeline,2> ownedEyes{OwnedPipeline(nullptr,cleanup),OwnedPipeline(nullptr,cleanup)};
        std::array<VkPipeline,2> eyes{};
        {
        UnlockedDriverScope unlocked(lock);
        const auto started=s->profileTiming?CommandCpuTiming::now():0;
        VkPipeline created{};
        auto r=FN(vkCreateComputePipelines)(d,cache,1,&info,a,&created);if(r!=VK_SUCCESS)return r;
        if(!created)return VK_ERROR_INITIALIZATION_FAILED;
        pipeline.reset(created);
        if(indirect)for(int eye=0;eye<2;++eye){info.stage.module=modules[eye];created=VK_NULL_HANDLE;r=FN(vkCreateComputePipelines)(d,cache,1,&info,a,&created);
            if(r!=VK_SUCCESS)return r;
            if(!created)return VK_ERROR_INITIALIZATION_FAILED;
            ownedEyes[eye].reset(created);eyes[eye]=created;}
        if(started){driverNs=CommandCpuTiming::now()-started;const auto ms=double(driverNs)/1000000.;if(ms>=2)try{note("pipeline driver type=compute variants="+std::to_string(indirect?3:1)+" wallMs="+std::to_string(ms));}catch(...){}}
        }
        if(indirect)s->indirectPipelines[pipeline.get()]=eyes;
        s->computeStereo[pipeline.get()]=stereo;
        if(s->waterCapture)s->waterCapture->pipeline(pipeline.get(),profile);
        try{
        if(profile==0x24abb0e76a065289ull)note("WATER_DISPATCH mode=per-eye shader="+shaderKey(profile));
        if(s->checkpoint){const auto label="compute shader="+shaderKey(profile);registerMarker(s,pipeline.get(),label);for(unsigned eye=0;eye<2;++eye)if(eyes[eye])registerMarker(s,eyes[eye],label+" eye="+std::to_string(eye));}
        if(argent::perf::enabled())note("PERF_PIPELINE kind=compute pipeline="+std::to_string(reinterpret_cast<uint64_t>(pipeline.get()))+" shaders="+shaderHash);
        }catch(...){}
        recordPipelineBuild(s,"compute",shaderHash,buildStart,cpuStart,j==0?batchLockNs:0,compileNs,driverNs);
        for(auto& eye:ownedEyes)eye.release();out[j]=pipeline.release();
    }return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyPipeline(VkDevice d,VkPipeline pipeline,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->pipelineRetirement.fetch_add(1,std::memory_order_release);auto it=s->stereoPipelines.find(pipeline);if(it!=s->stereoPipelines.end()){s->pipelineMarkers.erase(it->second);s->dispatchEndMarkers.erase(it->second);FN(vkDestroyPipeline)(d,it->second,a);}auto indirect=s->indirectPipelines.find(pipeline);if(indirect!=s->indirectPipelines.end()){for(auto eye:indirect->second){s->pipelineMarkers.erase(eye);s->dispatchEndMarkers.erase(eye);FN(vkDestroyPipeline)(d,eye,a);}}forgetPipelineMetadata(s,pipeline);FN(vkDestroyPipeline)(d,pipeline,a);}
