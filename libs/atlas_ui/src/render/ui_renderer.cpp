#include "atlas_ui/ui_renderer.h"

#include <cstring>
#include <stdexcept>
#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

namespace Atlas::UI {

namespace {
constexpr uint32_t kMaxVertices = 65536;
constexpr uint32_t kMaxIndices = 131072;

struct PushConstants {
    glm::vec2 viewportSize;
    glm::vec2 _pad0;
    glm::vec4 _padRect;
    int32_t  _padIndex;
    int32_t  _pad1;
};
static_assert(sizeof(PushConstants) == 40, "PushConstants size mismatch");
} // namespace

struct UIRenderer::Impl {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    uint32_t queueFamily = 0;
    uint32_t maxFrames = 2;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;

    VkImage fontImage = VK_NULL_HANDLE;
    VkDeviceMemory fontMemory = VK_NULL_HANDLE;
    VkImageView fontView = VK_NULL_HANDLE;
    VkSampler fontSampler = VK_NULL_HANDLE;
    uint32_t fontMipLevels = 1;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    bool fontUploaded = false;

    VkImageView textureViews[kMaxTextures] = {};
    VkSampler  textureSamplers[kMaxTextures] = {};
    bool       textureValid[kMaxTextures] = {};

    struct FrameBuffer {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        size_t vertexCapacity = 0;
        size_t indexCapacity = 0;
    };
    std::vector<FrameBuffer> frames;

    ShaderLoader shaderLoader = nullptr;

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return 0;
    }

    VkShaderModule createShaderModule(const uint8_t* code, size_t size) {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = size;
        info.pCode = reinterpret_cast<const uint32_t*>(code);
        VkShaderModule module = VK_NULL_HANDLE;
        vkCreateShaderModule(device, &info, nullptr, &module);
        return module;
    }

    bool createBuffers(FrameBuffer& fb) {
        VkBufferCreateInfo vbInfo{};
        vbInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vbInfo.size = kMaxVertices * sizeof(UIVertex);
        vbInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &vbInfo, nullptr, &fb.vertexBuffer) != VK_SUCCESS) return false;

        VkMemoryRequirements vbReq;
        vkGetBufferMemoryRequirements(device, fb.vertexBuffer, &vbReq);
        VkMemoryAllocateInfo vbAlloc{};
        vbAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        vbAlloc.allocationSize = vbReq.size;
        vbAlloc.memoryTypeIndex = findMemoryType(vbReq.memoryTypeBits,
                                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(device, &vbAlloc, nullptr, &fb.vertexMemory) != VK_SUCCESS) return false;
        vkBindBufferMemory(device, fb.vertexBuffer, fb.vertexMemory, 0);
        fb.vertexCapacity = vbReq.size / sizeof(UIVertex);

        VkBufferCreateInfo ibInfo{};
        ibInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ibInfo.size = kMaxIndices * sizeof(uint32_t);
        ibInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        ibInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &ibInfo, nullptr, &fb.indexBuffer) != VK_SUCCESS) return false;

        VkMemoryRequirements ibReq;
        vkGetBufferMemoryRequirements(device, fb.indexBuffer, &ibReq);
        VkMemoryAllocateInfo ibAlloc{};
        ibAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ibAlloc.allocationSize = ibReq.size;
        ibAlloc.memoryTypeIndex = findMemoryType(ibReq.memoryTypeBits,
                                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(device, &ibAlloc, nullptr, &fb.indexMemory) != VK_SUCCESS) return false;
        vkBindBufferMemory(device, fb.indexBuffer, fb.indexMemory, 0);
        fb.indexCapacity = ibReq.size / sizeof(uint32_t);
        return true;
    }

    void destroyBuffers(FrameBuffer& fb) {
        if (fb.vertexBuffer) vkDestroyBuffer(device, fb.vertexBuffer, nullptr);
        if (fb.vertexMemory) vkFreeMemory(device, fb.vertexMemory, nullptr);
        if (fb.indexBuffer) vkDestroyBuffer(device, fb.indexBuffer, nullptr);
        if (fb.indexMemory) vkFreeMemory(device, fb.indexMemory, nullptr);
        fb = {};
    }

    void ensureDescriptorSet() {
        if (descriptorSet) return;

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = kMaxTextures;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dsli{};
        dsli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsli.bindingCount = 1;
        dsli.pBindings = &binding;
        vkCreateDescriptorSetLayout(device, &dsli, nullptr, &descriptorSetLayout);

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(PushConstants);
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &descriptorSetLayout;
        pli.pushConstantRangeCount = 1;
        pli.pPushConstantRanges = &pcRange;
        vkCreatePipelineLayout(device, &pli, nullptr, &pipelineLayout);

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = kMaxTextures;
        VkDescriptorPoolCreateInfo dpi{};
        dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpi.poolSizeCount = 1;
        dpi.pPoolSizes = &poolSize;
        dpi.maxSets = 1;
        vkCreateDescriptorPool(device, &dpi, nullptr, &descriptorPool);

        VkDescriptorSetAllocateInfo dai{};
        dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dai.descriptorPool = descriptorPool;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &descriptorSetLayout;
        vkAllocateDescriptorSets(device, &dai, &descriptorSet);

        for (uint32_t i = 0; i < kMaxTextures; ++i) {
            writeTextureDescriptor(i);
        }
    }

    void writeTextureDescriptor(uint32_t slot) {
        if (!descriptorSet || slot >= kMaxTextures) return;
        VkDescriptorImageInfo info{};
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (textureValid[slot] && textureViews[slot] && textureSamplers[slot]) {
            info.imageView = textureViews[slot];
            info.sampler = textureSamplers[slot];
        } else {
            info.imageView = fontView;
            info.sampler = fontSampler;
        }
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = slot;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &info;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    bool createPipeline() {
        ensureDescriptorSet();

        // Load vertex shader
        size_t vertSize = 0;
        const uint8_t* vertData = shaderLoader ? shaderLoader("ui_vert", &vertSize) : nullptr;
        if (!vertData || vertSize == 0) return false;
        vertModule = createShaderModule(vertData, vertSize);

        // Load fragment shader
        size_t fragSize = 0;
        const uint8_t* fragData = shaderLoader ? shaderLoader("ui_frag", &fragSize) : nullptr;
        if (!fragData || fragSize == 0) return false;
        fragModule = createShaderModule(fragData, fragSize);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "main";

        VkVertexInputBindingDescription vertexBinding{};
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(UIVertex);
        vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription attrs[4]{};
        attrs[0].binding = 0; attrs[0].location = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = offsetof(UIVertex, pos);
        attrs[1].binding = 0; attrs[1].location = 1; attrs[1].format = VK_FORMAT_R32G32_SFLOAT; attrs[1].offset = offsetof(UIVertex, uv);
        attrs[2].binding = 0; attrs[2].location = 2; attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[2].offset = offsetof(UIVertex, color);
        attrs[3].binding = 0; attrs[3].location = 3; attrs[3].format = VK_FORMAT_R32_UINT; attrs[3].offset = offsetof(UIVertex, texture);

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &vertexBinding;
        vi.vertexAttributeDescriptionCount = 4;
        vi.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount = 1;
        vs.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState ba{};
        ba.blendEnable = VK_TRUE;
        ba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ba.colorBlendOp = VK_BLEND_OP_ADD;
        ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ba.alphaBlendOp = VK_BLEND_OP_ADD;
        ba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo bs{};
        bs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        bs.attachmentCount = 1;
        bs.pAttachments = &ba;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_FALSE;
        ds.depthWriteEnable = VK_FALSE;
        ds.stencilTestEnable = VK_FALSE;

        VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount = 2;
        pi.pStages = stages;
        pi.pVertexInputState = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vs;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pColorBlendState = &bs;
        pi.pDepthStencilState = &ds;
        pi.pDynamicState = &dyn;
        pi.layout = pipelineLayout;
        pi.renderPass = renderPass;
        pi.subpass = 0;
        VkResult res = vkCreateGraphicsPipelines(device, pipelineCache, 1, &pi, nullptr, &pipeline);
        return res == VK_SUCCESS;
    }

    void destroyPipeline() {
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        if (descriptorSetLayout) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
    }

    // ... rest of methods (createBuffers, destroyBuffers, ensureDescriptorSet, writeTextureDescriptor)
};

} // namespace Atlas::UI