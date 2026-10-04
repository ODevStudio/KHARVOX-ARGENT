VkShaderModule compiledModule(State* s,VkShaderModule original,uint64_t variant,bool& stereoCompute,uint32_t projectionBinding,bool skipProjection=false,int indirectEye=-1,bool monoView=false,uint64_t* compileNs=nullptr){
    const auto& words=s->shaders.at(original);
    const auto keyHash=profileHash(words.data(),uint32_t(words.size()*4));
    spirv_cross::Compiler inspect(words);auto model=inspect.get_execution_model();
    stereoCompute=model==spv::ExecutionModelGLCompute&&s->configuration.stereoComputeShaders.count(keyHash)!=0;
    if(model==spv::ExecutionModelGLCompute&&s->configuration.imageComputeStereo){
      const auto resources=inspect.get_shader_resources();bool writes=false,sharedWrites=false;
      for(auto& r:resources.storage_images){auto t=inspect.get_type(r.type_id);if(t.image.dim==spv::Dim2D&&!t.image.arrayed&&!inspect.has_decoration(r.id,spv::DecorationNonWritable))writes=true;}
      for(auto& r:resources.storage_buffers)if(!inspect.has_decoration(r.id,spv::DecorationNonWritable)&&!inspect.get_buffer_block_flags(r.id).get(spv::DecorationNonWritable))sharedWrites=true;
      stereoCompute=stereoCompute||(writes&&!sharedWrites);
    }
    ShaderOptions options;options.set=0;options.binding=projectionBinding;
    const auto vk3d=s->configuration.eternalVk3d?eternalVk3dRule(keyHash):nullptr;
    if(vk3d)options.vk3dShader=keyHash;
    if(s->configuration.eternalVolumes&&eternalVolumeRule(keyHash).uv)options.volumeShader=keyHash;
    if(s->configuration.eternalLightGrids&&model==spv::ExecutionModelFragment&&eternalLightGridRule(keyHash).pixels)options.lightGridShader=keyHash;
    options.screenSpaceUi=model==spv::ExecutionModelVertex&&s->configuration.screenUiShaders.count(keyHash)!=0;
    options.uiShader=options.screenSpaceUi?keyHash:0;
    if(model==spv::ExecutionModelVertex&&!options.screenSpaceUi&&!s->configuration.screenUiShaders.empty()){
        auto cached=s->uiAliases.find(keyHash);
        if(cached==s->uiAliases.end())cached=s->uiAliases.emplace(keyHash,portableUiProfile(words)).first;
        if(cached->second&&s->configuration.screenUiShaders.count(cached->second)){
            options.screenSpaceUi=true;options.uiShader=cached->second;
        }
    }
    options.project=!skipProjection&&model==spv::ExecutionModelVertex&&(s->configuration.projectionShaders.count(keyHash)!=0||options.screenSpaceUi);
    if((options.project||options.volumeShader||options.lightGridShader||(vk3d&&vk3d->worldUniform))&&projectionBinding==UINT32_MAX)throw std::runtime_error("Projected shader has no descriptor set zero");
    options.broadcastStorageImages=s->configuration.broadcastComputeShaders.count(keyHash)!=0;
    options.computeStereo=stereoCompute;options.indirectEye=indirectEye;options.monoView=monoView;
    const auto key=shaderKey(keyHash)+(options.project?"_project":"_flat")+"_binding"+std::to_string(projectionBinding)+"_"+std::to_string(indirectEye)+(monoView?"_mono":"");
    auto cached=s->compiled.find(key);if(cached!=s->compiled.end())return cached->second;
    const auto compileStart=s->profileTiming?CommandCpuTiming::now():0;
    auto shader=compileStereoShader(words,options);
    try{
    if(options.screenSpaceUi&&!monoView)note("UI_SHADER_PROFILE shader="+shaderKey(keyHash)+" profile="+shaderKey(options.uiShader)+" metadataAlias="+std::to_string(keyHash!=options.uiShader));
    if(argent::extendedLogging()&&s->configuration.eternalVk3d&&model==spv::ExecutionModelVertex&&!monoView&&!skipProjection&&!options.project&&!vk3d)
        note("SHADER_PROFILE_UNCLASSIFIED shader="+shaderKey(keyHash)+" stage=vertex nativeProjection=retained; may be shadow/fullscreen or an unknown variant");
    if(options.project&&!monoView)note("WORLD_PROJECTION shader="+shaderKey(keyHash));
    if(s->configuration.stereoComputeShaders.count(keyHash))note("COMPUTE_EYE_POLICY shader="+shaderKey(keyHash)+" stereo="+std::to_string(stereoCompute)+" fixedEye="+std::to_string(indirectEye));
    if(vk3d)note("VK3D_PROFILE shader="+shaderKey(keyHash)+" edits="+std::to_string(vk3d->edits.size())+" sharedCompute="+std::to_string(model==spv::ExecutionModelGLCompute&&!stereoCompute));
    if(options.vk3dShader==0x24abb0e76a065289ull)note("WATER_SAMPLE_LAYER native=1 integerFetchGuard=1 eye="+std::to_string(indirectEye));
    if(options.volumeShader)note("center-volume correction shader="+shaderKey(keyHash)+" stage="+std::to_string(model)+" eye="+std::to_string(indirectEye));
    if(options.lightGridShader&&!monoView)note("center-light-grid correction shader="+shaderKey(keyHash));
    }catch(...){}
    const auto destroy=FN(vkDestroyShaderModule);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};info.codeSize=shader.size()*4;info.pCode=shader.data();VkShaderModule result{};
    if(FN(vkCreateShaderModule)(s->device,&info,nullptr,&result)!=VK_SUCCESS)throw std::runtime_error("Stereo shader creation failed");
    if(!result)throw std::runtime_error("Stereo shader creation returned no module");
    const auto cleanup=[&](VkShaderModule module){destroy(s->device,module,nullptr);};
    std::unique_ptr<std::remove_pointer_t<VkShaderModule>,decltype(cleanup)> owned(result,cleanup);
    const auto entry=s->compiled.emplace(key,result);
    if(entry.second)owned.release();
    try{if(s->compiled.size()==1||s->compiled.size()%50==0)note("compiled stereo variants="+std::to_string(s->compiled.size()));}catch(...){}
    if(compileStart){const auto end=CommandCpuTiming::now();if(compileNs)*compileNs+=end-compileStart;
        try{note("SHADER_COMPILE startUs="+std::to_string(compileStart/1000)+" endUs="+std::to_string(end/1000)+" thread="+std::to_string(GetCurrentThreadId())+" key="+key+" wallMs="+std::to_string(double(end-compileStart)/1000000.));}catch(...){}}
    return entry.first->second;
}
