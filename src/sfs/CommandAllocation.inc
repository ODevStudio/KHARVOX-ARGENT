VKAPI_ATTR VkResult VKAPI_CALL createCommandPool(VkDevice d,const VkCommandPoolCreateInfo* info,const VkAllocationCallbacks* allocator,VkCommandPool* out){
    if(!out)return VK_ERROR_INITIALIZATION_FAILED;
    *out=VK_NULL_HANDLE;
    if(!info)return VK_ERROR_INITIALIZATION_FAILED;
RESULT_BEGIN
    const auto destroy=FN(vkDestroyCommandPool);
    VkCommandPool pool{};
    const auto result=FN(vkCreateCommandPool)(d,info,allocator,&pool);
    if(result!=VK_SUCCESS)return result;
    if(!pool)return VK_ERROR_INITIALIZATION_FAILED;
    const auto cleanup=[&](VkCommandPool handle){s->poolFamilies.erase(handle);destroy(d,handle,allocator);};
    std::unique_ptr<std::remove_pointer_t<VkCommandPool>,decltype(cleanup)> owned(pool,cleanup);
    s->poolFamilies[pool]=info->queueFamilyIndex;
    *out=owned.release();return VK_SUCCESS;
RESULT_END}
VKAPI_ATTR VkResult VKAPI_CALL allocateCommands(VkDevice d,const VkCommandBufferAllocateInfo* info,VkCommandBuffer* out){
    if(!info||!out)return VK_ERROR_INITIALIZATION_FAILED;
    std::fill_n(out,info->commandBufferCount,VK_NULL_HANDLE);
    if(!info->commandBufferCount)return VK_ERROR_INITIALIZATION_FAILED;
RESULT_BEGIN
    const auto free=FN(vkFreeCommandBuffers);
    std::vector<VkCommandBuffer> commands(info->commandBufferCount);
    const auto result=FN(vkAllocateCommandBuffers)(d,info,commands.data());
    if(result!=VK_SUCCESS)return result;
    const auto cleanup=[&](VkCommandBuffer* buffers){
        for(uint32_t i=0;i<info->commandBufferCount;++i)if(buffers[i]){
            s->commands.erase(buffers[i]);s->commandPools.erase(buffers[i]);
            free(d,info->commandPool,1,&buffers[i]);
        }
    };
    std::unique_ptr<VkCommandBuffer[],decltype(cleanup)> owned(commands.data(),cleanup);
    const auto family=s->poolFamilies.find(info->commandPool);
    for(auto cb:commands){
        if(!cb)return VK_ERROR_INITIALIZATION_FAILED;
        s->commandPools[cb]=info->commandPool;
        auto& command=s->commands.try_emplace(cb).first->second;
        command.primary=info->level==VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        if(family!=s->poolFamilies.end())command.family=family->second;
    }
    std::copy(commands.begin(),commands.end(),out);owned.release();return VK_SUCCESS;
RESULT_END}
