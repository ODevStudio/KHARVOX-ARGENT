#pragma once
#include "HandDispatch.h"
#include <algorithm>
#include <vector>

namespace kharvox::hands {
class HandSceneFramebuffers {
    struct Entry {
        VkRenderPass pass{};
        VkImageView color{},depth{};
        VkExtent2D extent{};
        VkFramebuffer framebuffer{};
    };
    std::vector<Entry> entries_;
public:
    VkFramebuffer get(VkDevice device,const KharvoxVulkanDispatch& vk,
        const VkFramebufferCreateInfo& info){
        if(info.pNext||info.flags||info.attachmentCount!=2||!info.pAttachments||info.layers!=1)return VK_NULL_HANDLE;
        const auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& entry){
            return entry.pass==info.renderPass&&entry.color==info.pAttachments[0]
                &&entry.depth==info.pAttachments[1]&&entry.extent.width==info.width
                &&entry.extent.height==info.height;
        });
        if(found!=entries_.end())return found->framebuffer;
        VkFramebuffer framebuffer{};
        if(vk.createFramebuffer(device,&info,nullptr,&framebuffer)!=VK_SUCCESS)return VK_NULL_HANDLE;
        entries_.push_back({info.renderPass,info.pAttachments[0],info.pAttachments[1],
            {info.width,info.height},framebuffer});
        return framebuffer;
    }
    void retireDepthAfterCompletion(VkDevice device,const KharvoxVulkanDispatch& vk,VkImageView depth){
        for(auto entry=entries_.begin();entry!=entries_.end();){
            if(entry->depth!=depth){++entry;continue;}
            vk.destroyFramebuffer(device,entry->framebuffer,nullptr);
            entry=entries_.erase(entry);
        }
    }
    void clearAfterCompletion(VkDevice device,const KharvoxVulkanDispatch& vk){
        for(const auto& entry:entries_)vk.destroyFramebuffer(device,entry.framebuffer,nullptr);
        entries_.clear();
    }
};
}
