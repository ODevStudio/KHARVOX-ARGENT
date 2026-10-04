#include "../src/hands/HandSceneFramebuffers.h"
#include <array>
#include <iostream>
#include <stdexcept>

static unsigned created{},destroyed{};
static bool failNext{};
static VkResult VKAPI_CALL createFramebuffer(VkDevice,const VkFramebufferCreateInfo*,
    const VkAllocationCallbacks*,VkFramebuffer* framebuffer){
    if(failNext){failNext=false;return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
    *framebuffer=reinterpret_cast<VkFramebuffer>(uintptr_t(++created));
    return VK_SUCCESS;
}
static void VKAPI_CALL destroyFramebuffer(VkDevice,VkFramebuffer,const VkAllocationCallbacks*){
    ++destroyed;
}
static void check(bool valid,const char* reason){if(!valid)throw std::runtime_error(reason);}

int main(){try{
    KharvoxVulkanDispatch vk{};vk.createFramebuffer=createFramebuffer;vk.destroyFramebuffer=destroyFramebuffer;
    kharvox::hands::HandSceneFramebuffers cache;
    std::array<VkImageView,2> attachments{{reinterpret_cast<VkImageView>(1),reinterpret_cast<VkImageView>(2)}};
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass=reinterpret_cast<VkRenderPass>(3);info.attachmentCount=2;info.pAttachments=attachments.data();
    info.width=info.height=128;info.layers=1;
    const auto first=cache.get(VK_NULL_HANDLE,vk,info);
    check(first&&cache.get(VK_NULL_HANDLE,vk,info)==first&&created==1,"Stable framebuffer not reused");
    attachments[0]=reinterpret_cast<VkImageView>(4);
    check(cache.get(VK_NULL_HANDLE,vk,info)!=first&&created==2,"Color view missing from key");
    info.renderPass=reinterpret_cast<VkRenderPass>(5);
    check(cache.get(VK_NULL_HANDLE,vk,info)&&created==3,"Render pass missing from key");
    info.width=64;
    check(cache.get(VK_NULL_HANDLE,vk,info)&&created==4,"Extent missing from key");
    const auto retiredDepth=attachments[1];
    attachments[1]=reinterpret_cast<VkImageView>(6);
    failNext=true;
    check(!cache.get(VK_NULL_HANDLE,vk,info)&&created==4,"Failed creation cached");
    const auto retained=cache.get(VK_NULL_HANDLE,vk,info);
    check(retained&&created==5,"Framebuffer retry failed");
    cache.retireDepthAfterCompletion(VK_NULL_HANDLE,vk,retiredDepth);
    check(destroyed==4,"Depth retirement retained borrowing framebuffers");
    check(cache.get(VK_NULL_HANDLE,vk,info)==retained&&created==5,"Unrelated depth invalidated");
    attachments[1]=retiredDepth;
    check(cache.get(VK_NULL_HANDLE,vk,info)&&created==6,"Retired depth reused stale framebuffer");
    cache.clearAfterCompletion(VK_NULL_HANDLE,vk);
    check(destroyed==created,"Shutdown leaked framebuffers");
    cache.clearAfterCompletion(VK_NULL_HANDLE,vk);
    check(destroyed==created,"Shutdown destroyed framebuffers twice");
    std::cout<<"PASS: reuse, attachment/pass/extent keys, creation failure, depth retirement and shutdown\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
