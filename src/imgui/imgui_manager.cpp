#include "imgui_manager.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>

static ImVec4 atlasColor(int r, int g, int b, int a = 255) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

static void ApplyAtlasStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 10.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 10.0f;
    style.GrabRounding = 10.0f;
    style.PopupRounding = 10.0f;
    style.TabRounding = 10.0f;
    style.ScrollbarRounding = 12.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;

    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(12.0f, 7.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 12.0f;

    // Slightly denser tabs/docking.
    style.DockingSeparatorSize = 1.0f;

    ImVec4* colors = style.Colors;

    const ImVec4 bg0 = atlasColor(11, 14, 18);
    const ImVec4 bg1 = atlasColor(16, 20, 26);
    const ImVec4 panel = atlasColor(20, 26, 34);
    const ImVec4 panel2 = atlasColor(24, 31, 40);
    const ImVec4 stroke = atlasColor(255, 255, 255, 24);
    const ImVec4 stroke2 = atlasColor(255, 255, 255, 36);
    const ImVec4 text = atlasColor(234, 239, 245);
    const ImVec4 textDim = atlasColor(184, 194, 208);
    const ImVec4 accent = atlasColor(46, 196, 182);
    const ImVec4 accentHi = atlasColor(80, 230, 214);

    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = atlasColor(120, 128, 140);

    colors[ImGuiCol_WindowBg] = bg1;
    colors[ImGuiCol_ChildBg] = panel;
    colors[ImGuiCol_PopupBg] = atlasColor(18, 22, 28, 250);

    colors[ImGuiCol_Border] = stroke;
    colors[ImGuiCol_BorderShadow] = atlasColor(0, 0, 0, 0);

    colors[ImGuiCol_FrameBg] = panel2;
    colors[ImGuiCol_FrameBgHovered] = atlasColor(32, 40, 52);
    colors[ImGuiCol_FrameBgActive] = atlasColor(36, 45, 58);

    colors[ImGuiCol_TitleBg] = bg0;
    colors[ImGuiCol_TitleBgActive] = bg0;
    colors[ImGuiCol_TitleBgCollapsed] = bg0;

    colors[ImGuiCol_MenuBarBg] = bg0;

    colors[ImGuiCol_ScrollbarBg] = atlasColor(0, 0, 0, 0);
    colors[ImGuiCol_ScrollbarGrab] = atlasColor(255, 255, 255, 46);
    colors[ImGuiCol_ScrollbarGrabHovered] = atlasColor(255, 255, 255, 64);
    colors[ImGuiCol_ScrollbarGrabActive] = atlasColor(255, 255, 255, 82);

    colors[ImGuiCol_CheckMark] = accentHi;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accentHi;

    colors[ImGuiCol_Button] = atlasColor(32, 40, 52);
    colors[ImGuiCol_ButtonHovered] = atlasColor(40, 52, 68);
    colors[ImGuiCol_ButtonActive] = atlasColor(44, 58, 76);

    colors[ImGuiCol_Header] = atlasColor(32, 40, 52);
    colors[ImGuiCol_HeaderHovered] = atlasColor(40, 52, 68);
    colors[ImGuiCol_HeaderActive] = atlasColor(44, 58, 76);

    colors[ImGuiCol_Separator] = stroke;
    colors[ImGuiCol_SeparatorHovered] = stroke2;
    colors[ImGuiCol_SeparatorActive] = stroke2;

    colors[ImGuiCol_ResizeGrip] = atlasColor(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripHovered] = atlasColor(255, 255, 255, 30);
    colors[ImGuiCol_ResizeGripActive] = atlasColor(255, 255, 255, 46);

    colors[ImGuiCol_Tab] = atlasColor(18, 22, 28);
    colors[ImGuiCol_TabHovered] = atlasColor(40, 52, 68);
    colors[ImGuiCol_TabActive] = atlasColor(26, 33, 43);
    colors[ImGuiCol_TabUnfocused] = atlasColor(16, 20, 26);
    colors[ImGuiCol_TabUnfocusedActive] = atlasColor(22, 28, 36);

    colors[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    colors[ImGuiCol_DockingEmptyBg] = bg0;

    colors[ImGuiCol_PlotLines] = textDim;
    colors[ImGuiCol_PlotLinesHovered] = accentHi;
    colors[ImGuiCol_PlotHistogram] = accent;
    colors[ImGuiCol_PlotHistogramHovered] = accentHi;

    colors[ImGuiCol_TableHeaderBg] = atlasColor(18, 22, 28);
    colors[ImGuiCol_TableBorderStrong] = stroke;
    colors[ImGuiCol_TableBorderLight] = atlasColor(255, 255, 255, 18);
    colors[ImGuiCol_TableRowBg] = atlasColor(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt] = atlasColor(255, 255, 255, 6);

    colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    colors[ImGuiCol_NavHighlight] = ImVec4(accentHi.x, accentHi.y, accentHi.z, 0.75f);
}

void ImGuiManager::init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue, uint32_t queueFamily, VkRenderPass renderPass, GLFWwindow* window, uint32_t imageCount) {
    this->device = device;
    this->graphicsQueue = queue;


    // Create descriptor pool for ImGui
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool for ImGui!");
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Font: prefer a more "editor" feel than the default ImGui font.
    {
        ImFontConfig cfg;
        cfg.OversampleH = 3;
        cfg.OversampleV = 2;
        cfg.RasterizerMultiply = 1.10f;

        ImFont* base = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 18.0f, &cfg);
        if (!base) {
            base = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/arial.ttf", 18.0f, &cfg);
        }
        if (!base) {
            base = io.Fonts->AddFontDefault();
        }
        io.FontDefault = base;
    }

    ApplyAtlasStyle();

    io.FontGlobalScale = 1.0f;

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = physicalDevice;
    init_info.Device = device;
    init_info.QueueFamily = queueFamily;
    init_info.Queue = queue;
    init_info.PipelineCache = nullptr;
    init_info.DescriptorPool = descriptorPool;
    init_info.MinImageCount = 2;
    init_info.ImageCount = imageCount;
    init_info.Allocator = nullptr;
    init_info.PipelineInfoMain.RenderPass = renderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = nullptr;
    ImGui_ImplVulkan_Init(&init_info);

}

void ImGuiManager::newFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::render(VkCommandBuffer commandBuffer) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

void ImGuiManager::cleanup(VkDevice device) {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
}