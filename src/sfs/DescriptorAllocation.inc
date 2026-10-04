VKAPI_ATTR VkResult VKAPI_CALL allocateSets(VkDevice d,const VkDescriptorSetAllocateInfo* info,VkDescriptorSet* out){
    if(!info||!out)return VK_ERROR_INITIALIZATION_FAILED;
    std::fill_n(out,info->descriptorSetCount,VK_NULL_HANDLE);
    if(!info->descriptorSetCount||!info->pSetLayouts)return VK_ERROR_INITIALIZATION_FAILED;
RESULT_BEGIN
    const auto allocate=FN(vkAllocateDescriptorSets);
    const auto update=FN(vkUpdateDescriptorSets);
    VkDescriptorBufferInfo buffer{s->params,0,sizeof(EyeUniforms)};
    std::vector<VkDescriptorSet> sets(info->descriptorSetCount);
    std::vector<VkWriteDescriptorSet> writes(info->descriptorSetCount);
    for(uint32_t i=0;i<info->descriptorSetCount;++i){
        auto& write=writes[i];write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding=s->layoutBindings.at(info->pSetLayouts[i]);write.descriptorCount=1;
        write.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;write.pBufferInfo=&buffer;
    }
    auto capture=s->waterCapture?s->waterCapture->prepareSets(*info):WaterGpuCapture::SetNodes{};
    const auto result=allocate(d,info,sets.data());
    if(result!=VK_SUCCESS)return result;
    if(std::any_of(sets.begin(),sets.end(),[](auto set){return !set;}))return VK_ERROR_INITIALIZATION_FAILED;
    for(size_t i=0;i<sets.size();++i)writes[i].dstSet=sets[i];
    update(d,uint32_t(writes.size()),writes.data(),0,nullptr);
    if(s->waterCapture)s->waterCapture->publishSets(capture,sets.data());
    std::copy(sets.begin(),sets.end(),out);return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR VkResult VKAPI_CALL freeSets(VkDevice d,VkDescriptorPool pool,uint32_t count,const VkDescriptorSet* sets){RESULT_BEGIN
    const auto result=FN(vkFreeDescriptorSets)(d,pool,count,sets);
    if(result==VK_SUCCESS&&s->waterCapture)s->waterCapture->freeSets(count,sets);
    return result;
RESULT_END}
VKAPI_ATTR VkResult VKAPI_CALL resetPool(VkDevice d,VkDescriptorPool pool,VkDescriptorPoolResetFlags flags){RESULT_BEGIN
    const auto result=FN(vkResetDescriptorPool)(d,pool,flags);
    if(result==VK_SUCCESS&&s->waterCapture)s->waterCapture->pool(pool);
    return result;
RESULT_END}
VKAPI_ATTR void VKAPI_CALL destroyPool(VkDevice d,VkDescriptorPool pool,const VkAllocationCallbacks* allocator){
    auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
    if(s->waterCapture)s->waterCapture->pool(pool);
    FN(vkDestroyDescriptorPool)(d,pool,allocator);
}
