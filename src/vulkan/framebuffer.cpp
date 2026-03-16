#include "framebuffer.h"
#include <stdexcept>

namespace VulkanEngine {

VulkanFramebuffer::VulkanFramebuffer() {}

VulkanFramebuffer::~VulkanFramebuffer() {
    // Note: cleanup is called explicitly, so we don't do anything here to avoid double free
}

void VulkanFramebuffer::init(VkDevice device, VkRenderPass renderPass, const std::vector<VkImageView>& swapChainImageViews, VkExtent2D swapChainExtent) {
    swapChainFramebuffers.resize(swapChainImageViews.size());

    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
        VkImageView attachments[] = {
            swapChainImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapChainExtent.width;
        framebufferInfo.height = swapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

void VulkanFramebuffer::cleanup(VkDevice device) {
    for (auto framebuffer : swapChainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    swapChainFramebuffers.clear();
}

const std::vector<VkFramebuffer>& VulkanFramebuffer::getFramebuffers() const {
    return swapChainFramebuffers;
}

} // namespace VulkanEngine