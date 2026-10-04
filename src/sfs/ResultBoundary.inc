VkResult resultFailure(const char* function,const char* message,VkResult result)noexcept{
    try{note(std::string(function)+": "+message);}catch(...){}
    return result;
}
#define RESULT_BEGIN try {auto s=state(d);std::unique_lock<std::shared_mutex> lock(s->mutex);
#define RESULT_END }catch(const std::bad_alloc& e){return resultFailure(__FUNCTION__,e.what(),VK_ERROR_OUT_OF_HOST_MEMORY);}catch(const std::exception& e){return resultFailure(__FUNCTION__,e.what(),VK_ERROR_INITIALIZATION_FAILED);}catch(...){return resultFailure(__FUNCTION__,"unknown exception",VK_ERROR_INITIALIZATION_FAILED);}
