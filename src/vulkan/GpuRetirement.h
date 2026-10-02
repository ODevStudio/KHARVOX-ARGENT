#pragma once
#include <vulkan/vulkan.h>
#include <windows.h>
#include <exception>
#include <string>

namespace argent {
void log(const std::string& message);
inline VkResult requireGpuRetirement(VkResult result,const char* operation)noexcept{
    if(result!=VK_SUCCESS&&result!=VK_ERROR_DEVICE_LOST){
        log(std::string(operation)+" retirement failed result="+std::to_string(result));
        RaiseFailFastException(nullptr,nullptr,0);std::terminate();
    }
    return result;
}
}
