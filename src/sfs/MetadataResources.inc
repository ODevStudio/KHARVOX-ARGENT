VKAPI_ATTR VkResult VKAPI_CALL createShader(VkDevice d,const VkShaderModuleCreateInfo* i,const VkAllocationCallbacks* a,VkShaderModule* out){
    if(!out)return VK_ERROR_INITIALIZATION_FAILED;
    *out=VK_NULL_HANDLE;
    if(!i)return VK_ERROR_INITIALIZATION_FAILED;
RESULT_BEGIN
    const auto destroy=FN(vkDestroyShaderModule);
    std::vector<uint32_t> words(i->pCode,i->pCode+i->codeSize/4);
    VkShaderModule shader{};
    const auto result=FN(vkCreateShaderModule)(d,i,a,&shader);
    if(result!=VK_SUCCESS)return result;
    if(!shader)return VK_ERROR_INITIALIZATION_FAILED;
    const auto cleanup=[&](VkShaderModule h){s->shaders.erase(h);destroy(d,h,a);};
    std::unique_ptr<std::remove_pointer_t<VkShaderModule>,decltype(cleanup)> owned(shader,cleanup);
    s->shaders.emplace(shader,std::move(words));
    *out=owned.release();
    return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyShader(VkDevice d,VkShaderModule shader,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->shaders.erase(shader);FN(vkDestroyShaderModule)(d,shader,a);}
VKAPI_ATTR VkResult VKAPI_CALL captureCreateBuffer(VkDevice d,const VkBufferCreateInfo* i,const VkAllocationCallbacks* a,VkBuffer* out){
    if(!out)return VK_ERROR_INITIALIZATION_FAILED;
    *out=VK_NULL_HANDLE;
    if(!i)return VK_ERROR_INITIALIZATION_FAILED;
    try{auto s=state(d);
    const auto destroy=FN(vkDestroyBuffer);
    auto info=*i;
    if(s->waterCapture&&(info.usage&(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)))info.usage|=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer buffer{};
    const auto result=FN(vkCreateBuffer)(d,&info,a,&buffer);
    if(result!=VK_SUCCESS)return result;
    if(!buffer)return VK_ERROR_INITIALIZATION_FAILED;
    const auto cleanup=[&](VkBuffer h){if(s->waterCapture)s->waterCapture->forgetBuffer(h);destroy(d,h,a);};
    std::unique_ptr<std::remove_pointer_t<VkBuffer>,decltype(cleanup)> owned(buffer,cleanup);
    if(s->waterCapture)s->waterCapture->buffer(buffer,info);
    *out=owned.release();
    return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL captureDestroyBuffer(VkDevice d,VkBuffer b,const VkAllocationCallbacks* a){auto s=state(d);if(s->waterCapture)s->waterCapture->forgetBuffer(b);FN(vkDestroyBuffer)(d,b,a);}
VKAPI_ATTR void VKAPI_CALL captureUpdateSets(VkDevice d,uint32_t n,const VkWriteDescriptorSet* writes,uint32_t count,const VkCopyDescriptorSet* copies){auto s=state(d);FN(vkUpdateDescriptorSets)(d,n,writes,count,copies);if(s->waterCapture)s->waterCapture->update(n,writes,count,copies);}
VKAPI_ATTR VkResult VKAPI_CALL createLayout(VkDevice d,const VkDescriptorSetLayoutCreateInfo* i,const VkAllocationCallbacks* a,VkDescriptorSetLayout* out){
    if(!out)return VK_ERROR_INITIALIZATION_FAILED;
    *out=VK_NULL_HANDLE;
    if(!i)return VK_ERROR_INITIALIZATION_FAILED;
RESULT_BEGIN
    const auto destroy=FN(vkDestroyDescriptorSetLayout);
    std::vector<VkDescriptorSetLayoutBinding> bindings;if(i->bindingCount)bindings.assign(i->pBindings,i->pBindings+i->bindingCount);uint32_t dynamic=0;
    for(const auto& b:bindings){if(b.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC||b.descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)dynamic+=b.descriptorCount;}
    if(i->pNext)throw std::runtime_error("SFS descriptor layout extension chain not supported by probe");
    uint32_t reserved=0;
    while(std::any_of(bindings.begin(),bindings.end(),[&](auto& b){return b.binding==reserved;})){if(reserved==UINT32_MAX)throw std::runtime_error("No free projection binding");++reserved;}
    for(uint32_t binding:{reserved})bindings.push_back({binding,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
    auto info=*i;info.bindingCount=uint32_t(bindings.size());info.pBindings=bindings.data();
    VkDescriptorSetLayout layout{};
    const auto result=FN(vkCreateDescriptorSetLayout)(d,&info,a,&layout);
    if(result!=VK_SUCCESS)return result;
    if(!layout)return VK_ERROR_INITIALIZATION_FAILED;
    const auto cleanup=[&](VkDescriptorSetLayout h){
        s->dynamicCounts.erase(h);s->layoutBindings.erase(h);
        if(s->waterCapture)s->waterCapture->forgetLayout(h);
        destroy(d,h,a);
    };
    std::unique_ptr<std::remove_pointer_t<VkDescriptorSetLayout>,decltype(cleanup)> owned(layout,cleanup);
    s->dynamicCounts[layout]=dynamic;s->layoutBindings[layout]=reserved;
    if(s->waterCapture)s->waterCapture->layout(layout,*i);
    *out=owned.release();
    return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyLayout(VkDevice d,VkDescriptorSetLayout layout,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->dynamicCounts.erase(layout);s->layoutBindings.erase(layout);if(s->waterCapture)s->waterCapture->forgetLayout(layout);FN(vkDestroyDescriptorSetLayout)(d,layout,a);}
VKAPI_ATTR VkResult VKAPI_CALL createPipelineLayout(VkDevice d,const VkPipelineLayoutCreateInfo* i,const VkAllocationCallbacks* a,VkPipelineLayout* out){
    if(!out)return VK_ERROR_INITIALIZATION_FAILED;
    *out=VK_NULL_HANDLE;
    if(!i)return VK_ERROR_INITIALIZATION_FAILED;
RESULT_BEGIN
    const auto destroy=FN(vkDestroyPipelineLayout);
    const auto binding=i->setLayoutCount?s->layoutBindings.at(i->pSetLayouts[0]):UINT32_MAX;
    std::vector<uint32_t> counts;counts.reserve(i->setLayoutCount);
    for(uint32_t j=0;j<i->setLayoutCount;++j)counts.push_back(s->dynamicCounts.at(i->pSetLayouts[j]));
    VkPipelineLayout layout{};
    const auto result=FN(vkCreatePipelineLayout)(d,i,a,&layout);
    if(result!=VK_SUCCESS)return result;
    if(!layout)return VK_ERROR_INITIALIZATION_FAILED;
    const auto cleanup=[&](VkPipelineLayout h){s->pipelineDynamicCounts.erase(h);s->pipelineBindings.erase(h);destroy(d,h,a);};
    std::unique_ptr<std::remove_pointer_t<VkPipelineLayout>,decltype(cleanup)> owned(layout,cleanup);
    s->pipelineBindings[layout]=binding;s->pipelineDynamicCounts.emplace(layout,std::move(counts));
    *out=owned.release();
    return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyPipelineLayout(VkDevice d,VkPipelineLayout layout,const VkAllocationCallbacks* a){auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);s->pipelineLayoutRetirement.fetch_add(1,std::memory_order_release);s->pipelineDynamicCounts.erase(layout);s->pipelineBindings.erase(layout);FN(vkDestroyPipelineLayout)(d,layout,a);}
VKAPI_ATTR VkResult VKAPI_CALL createPool(VkDevice d,const VkDescriptorPoolCreateInfo* i,const VkAllocationCallbacks* a,VkDescriptorPool* out){
    if(!out)return VK_ERROR_INITIALIZATION_FAILED;
    *out=VK_NULL_HANDLE;
    if(!i)return VK_ERROR_INITIALIZATION_FAILED;
RESULT_BEGIN
    if(i->maxSets>UINT32_MAX/2)return VK_ERROR_OUT_OF_HOST_MEMORY;
    std::vector<VkDescriptorPoolSize> sizes;if(i->poolSizeCount)sizes.assign(i->pPoolSizes,i->pPoolSizes+i->poolSizeCount);sizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,i->maxSets});
    auto info=*i;info.poolSizeCount=uint32_t(sizes.size());info.pPoolSizes=sizes.data();
    VkDescriptorPool pool{};
    const auto result=FN(vkCreateDescriptorPool)(d,&info,a,&pool);
    if(result!=VK_SUCCESS)return result;
    if(!pool)return VK_ERROR_INITIALIZATION_FAILED;
    *out=pool;return VK_SUCCESS;
RESULT_END}
