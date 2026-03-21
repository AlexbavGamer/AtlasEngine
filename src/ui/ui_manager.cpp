#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui_manager.h"
#include <imgui.h>
#include "../utils/camera_controller.h"
#include <imgui_internal.h>
#include <imgui_impl_vulkan.h>
#include <ImGuiFileDialog.h>
#include <ImGuizmo.h>
#include "../ecs/ecs.h"
#include "../ecs/components.h"
#include "../assets/asset_manager.h"
#include "../ecs/components/components.h"
#include <glm/glm.hpp>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <cfloat>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/transform.hpp>
#include <filesystem>
#include <iostream>
#include <cctype>

namespace fs = std::filesystem;

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

UIManager::UIManager(Atlas::Scene* scene) : m_Scene(scene) {}

UIManager::~UIManager() {
    clearContentTextureThumbs();
}

namespace {
ImTextureID toImTextureId(VkDescriptorSet descriptorSet) {
    return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(descriptorSet));
}

bool isImageFileExt(const std::string& ext) {
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".hdr";
}

#ifdef _WIN32
std::string quoteForExplorerArg(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    out.push_back('"');
    for (char c : s) {
        if (c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string normalizeExplorerPath(const std::string& inputPath) {
    if (inputPath.empty()) return inputPath;

    std::error_code ec;
    std::filesystem::path p(inputPath);

    // Try to make it absolute to avoid explorer opening relative to cwd.
    std::filesystem::path abs = std::filesystem::absolute(p, ec);
    if (!ec) {
        p = abs;
    }

    p = p.lexically_normal();
    p.make_preferred();
    return p.string();
}

void openExplorerSelect(const std::string& fullPath) {
    std::string norm = normalizeExplorerPath(fullPath);
    std::string arg = std::string("/select,") + quoteForExplorerArg(norm);
    ShellExecuteA(nullptr, "open", "explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
}

void openExplorerFolder(const std::string& folderPath) {
    std::string norm = normalizeExplorerPath(folderPath);
    // Opening the folder path directly is more reliable than passing it to explorer.exe.
    ShellExecuteA(nullptr, "open", norm.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void openWithDefaultApp(const std::string& fullPath) {
    std::string norm = normalizeExplorerPath(fullPath);
    ShellExecuteA(nullptr, "open", norm.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
#endif
}

ImTextureID UIManager::getOrCreateContentTextureThumb(const std::string& fullPath) {
    if (!assetManager || !renderer || fullPath.empty()) {
        return (ImTextureID)0;
    }

    auto it = m_ContentTextureThumbs.find(fullPath);
    if (it != m_ContentTextureThumbs.end() && it->second.descriptorSet != VK_NULL_HANDLE) {
        it->second.lastUsedFrame = m_ContentTextureThumbFrame;
        return toImTextureId(it->second.descriptorSet);
    }

    ContentTextureThumb thumb;
    thumb.assetIdStr = std::string("ui_thumb_srgb:") + fullPath;

    auto texture = assetManager->loadTexture(Atlas::StringID(thumb.assetIdStr), fullPath, Atlas::AssetManager::TextureColorSpace::SRGB);
    if (!texture || !texture->isValid()) {
        return (ImTextureID)0;
    }

    thumb.width = texture->getWidth();
    thumb.height = texture->getHeight();
    thumb.descriptorSet = ImGui_ImplVulkan_AddTexture(texture->getSampler(), texture->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    thumb.lastUsedFrame = m_ContentTextureThumbFrame;

    m_ContentTextureThumbs[fullPath] = thumb;
    return toImTextureId(thumb.descriptorSet);
}


void UIManager::pruneContentTextureThumbs() {
    const uint64_t now = m_ContentTextureThumbFrame;

    // First pass: remove old entries.
    for (auto it = m_ContentTextureThumbs.begin(); it != m_ContentTextureThumbs.end(); ) {
        const bool expired = (now > it->second.lastUsedFrame) && ((now - it->second.lastUsedFrame) > CONTENT_TEXTURE_THUMB_TTL_FRAMES);
        if (expired) {
            if (it->second.descriptorSet != VK_NULL_HANDLE) {
                ImGui_ImplVulkan_RemoveTexture(it->second.descriptorSet);
            }
            if (assetManager && !it->second.assetIdStr.empty()) {
                assetManager->unloadTexture(Atlas::StringID(it->second.assetIdStr));
            }
            it = m_ContentTextureThumbs.erase(it);
        } else {
            ++it;
        }
    }

    // Second pass: cap size by LRU (never evict thumbnails used this frame).
    while (m_ContentTextureThumbs.size() > MAX_CONTENT_TEXTURE_THUMBS) {
        auto lru = m_ContentTextureThumbs.end();

        for (auto it = m_ContentTextureThumbs.begin(); it != m_ContentTextureThumbs.end(); ++it) {
            if (it->second.lastUsedFrame >= now) {
                continue;
            }
            if (lru == m_ContentTextureThumbs.end() || it->second.lastUsedFrame < lru->second.lastUsedFrame) {
                lru = it;
            }
        }

        if (lru == m_ContentTextureThumbs.end()) {
            break;
        }

        if (lru->second.descriptorSet != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(lru->second.descriptorSet);
        }
        if (assetManager && !lru->second.assetIdStr.empty()) {
            assetManager->unloadTexture(Atlas::StringID(lru->second.assetIdStr));
        }
        m_ContentTextureThumbs.erase(lru);
    }
}

void UIManager::clearContentTextureThumbs() {
    for (auto& kv : m_ContentTextureThumbs) {
        if (kv.second.descriptorSet != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(kv.second.descriptorSet);
        }
        if (assetManager && !kv.second.assetIdStr.empty()) {
            assetManager->unloadTexture(Atlas::StringID(kv.second.assetIdStr));
        }
    }
    m_ContentTextureThumbs.clear();
}

void UIManager::TransformCommand::apply(Atlas::Scene* scene, const TransformState& s) {
    if (!scene) return;
    auto& registry = scene->getRegistry();
    if (entity == entt::null || !registry.valid(entity)) return;
    if (!registry.all_of<Transform>(entity)) return;

    auto& t = registry.get<Transform>(entity);
    t.position = s.position;
    t.rotation = s.rotation;
    t.scale = s.scale;
}

void UIManager::MultiTransformCommand::apply(Atlas::Scene* scene, const std::vector<TransformState>& states) {
    if (!scene) return;
    auto& registry = scene->getRegistry();

    const size_t count = std::min(entities.size(), states.size());
    for (size_t i = 0; i < count; ++i) {
        Entity e = entities[i];
        if (e == entt::null || !registry.valid(e)) continue;
        if (!registry.all_of<Transform>(e)) continue;

        auto& t = registry.get<Transform>(e);
        t.position = states[i].position;
        t.rotation = states[i].rotation;
        t.scale = states[i].scale;
    }
}

void UIManager::pushCommand(std::unique_ptr<UndoCommand> cmd) {
    if (!cmd) return;

    m_UndoStack.push_back(std::move(cmd));
    m_RedoStack.clear();

    if (m_UndoStack.size() > MAX_UNDO) {
        m_UndoStack.erase(m_UndoStack.begin());
    }
}

void UIManager::undo() {
    if (!m_Scene) return;
    if (m_UndoStack.empty()) return;

    auto cmd = std::move(m_UndoStack.back());
    m_UndoStack.pop_back();

    cmd->undo(m_Scene);
    m_RedoStack.push_back(std::move(cmd));
}

void UIManager::redo() {
    if (!m_Scene) return;
    if (m_RedoStack.empty()) return;

    auto cmd = std::move(m_RedoStack.back());
    m_RedoStack.pop_back();

    cmd->redo(m_Scene);
    m_UndoStack.push_back(std::move(cmd));
}

void UIManager::renderToolbar() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
    ImGui::Begin("Toolbar", nullptr, flags);

    auto pill = [&](const char* id, const char* label, bool active, const ImVec2& size) -> bool {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1(p0.x + size.x, p0.y + size.y);

        bool pressed = ImGui::InvisibleButton(id, size);
        bool hovered = ImGui::IsItemHovered();
        bool held = ImGui::IsItemActive();

        ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
        ImU32 bg = ImGui::GetColorU32(ImGuiCol_FrameBg);
        ImU32 fg = ImGui::GetColorU32(ImGuiCol_Text);

        if (active) {
            bg = ImGui::GetColorU32(ImVec4(0.18f, 0.77f, 0.71f, held ? 0.95f : 0.88f));
            fg = ImGui::GetColorU32(ImVec4(0.05f, 0.07f, 0.09f, 1.0f));
        } else if (held) {
            bg = ImGui::GetColorU32(ImGuiCol_FrameBgActive);
        } else if (hovered) {
            bg = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
        }

        float r = size.y * 0.5f;
        dl->AddRectFilled(p0, p1, bg, r);
        dl->AddRect(p0, p1, border, r, 0, 1.0f);

        ImVec2 ts = ImGui::CalcTextSize(label);
        ImVec2 tp(p0.x + (size.x - ts.x) * 0.5f, p0.y + (size.y - ts.y) * 0.5f);
        dl->AddText(tp, fg, label);

        return pressed;
    };

    // Branding
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Atlas");
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(14.0f, 0.0f));
    ImGui::SameLine();

    // Transform mode
    const ImVec2 modeSize(72.0f, 28.0f);
    if (pill("##move", "Move", m_TransformMode == TransformMode::Translate, modeSize)) {
        m_TransformMode = TransformMode::Translate;
    }
    ImGui::SameLine(0.0f, 8.0f);
    if (pill("##rot", "Rotate", m_TransformMode == TransformMode::Rotate, modeSize)) {
        m_TransformMode = TransformMode::Rotate;
    }
    ImGui::SameLine(0.0f, 8.0f);
    if (pill("##scale", "Scale", m_TransformMode == TransformMode::Scale, modeSize)) {
        m_TransformMode = TransformMode::Scale;
    }

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(16.0f, 0.0f));
    ImGui::SameLine();

    // Space + snapping
    const ImVec2 optSize(78.0f, 28.0f);
    if (pill("##space", m_GizmoLocal ? "Local" : "World", m_GizmoLocal, optSize)) {
        m_GizmoLocal = !m_GizmoLocal;
    }
    ImGui::SameLine(0.0f, 8.0f);
    if (pill("##snap", "Snap", m_GizmoSnap, ImVec2(64.0f, 28.0f))) {
        m_GizmoSnap = !m_GizmoSnap;
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void UIManager::render(ImTextureID viewportTexture) {
    static bool dockspaceInitialized = false;
    static ImGuiID dockspaceID = 0;

    // Background behind the dockspace (helps it feel less "stock ImGui").
    {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImDrawList* bg = ImGui::GetBackgroundDrawList(vp);
        const ImVec2 p0 = vp->Pos;
        const ImVec2 p1(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);

        const ImU32 c00 = ImGui::GetColorU32(ImVec4(0.04f, 0.05f, 0.07f, 1.0f));
        const ImU32 c10 = ImGui::GetColorU32(ImVec4(0.05f, 0.07f, 0.10f, 1.0f));
        const ImU32 c01 = ImGui::GetColorU32(ImVec4(0.06f, 0.08f, 0.11f, 1.0f));
        const ImU32 c11 = ImGui::GetColorU32(ImVec4(0.04f, 0.06f, 0.08f, 1.0f));
        bg->AddRectFilledMultiColor(p0, p1, c00, c10, c11, c01);

        // Subtle vignette
        bg->AddRectFilled(p0, p1, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.25f)));
    }

    if (!dockspaceInitialized) {
        dockspaceID = ImGui::GetID("MyDockspace");
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        
        if (ImGui::DockBuilderGetNode(dockspaceID) == nullptr) {
            ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceID, viewport->Size);
            
            ImGuiID dockMain = dockspaceID;
            ImGuiID dockLeft = 0;
            ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.2f, &dockLeft, &dockMain);
            
            ImGuiID dockLeftTop = 0;
            ImGuiID dockLeftBottom = 0;
            ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Up, 0.5f, &dockLeftTop, &dockLeftBottom);
            
            ImGuiID dockTop = 0;
            ImGuiID dockCenter = 0;
            ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Up, 0.05f, &dockTop, &dockCenter);
            
            ImGui::DockBuilderDockWindow("Toolbar", dockTop);
            ImGui::DockBuilderDockWindow("Viewport", dockCenter);
            ImGui::DockBuilderDockWindow("Hierarchy", dockLeftBottom);
            ImGui::DockBuilderDockWindow("Properties", dockLeftTop);
            ImGui::DockBuilderDockWindow("Content Explorer", dockLeftBottom);
            ImGui::DockBuilderFinish(dockspaceID);
        }
        
        dockspaceInitialized = true;
    }

    ImGui::DockSpaceOverViewport(dockspaceID, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

    // Prune selection (streaming/unload can delete entities).
    if (m_Scene) {
        auto& registry = m_Scene->getRegistry();
        for (size_t i = 0; i < m_SelectedEntities.size(); ) {
            Entity e = m_SelectedEntities[i];
            if (e == entt::null || !registry.valid(e)) {
                m_SelectedEntities.erase(m_SelectedEntities.begin() + static_cast<long long>(i));
            } else {
                ++i;
            }
        }

        if (m_PrimarySelected != entt::null && !registry.valid(m_PrimarySelected)) {
            m_PrimarySelected = entt::null;
        }
        if (m_PrimarySelected == entt::null && !m_SelectedEntities.empty()) {
            m_PrimarySelected = m_SelectedEntities.back();
        }
    }

    // Undo/redo shortcuts (global)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.WantTextInput && !ImGuizmo::IsUsing()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z)) {
                if (io.KeyShift) {
                    redo();
                } else {
                    undo();
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Y)) {
                redo();
            }
        }
    }

    renderToolbar();
    renderViewport(viewportTexture);
    renderHierarchy();
    renderProperties();
    renderContentExplorer();
    
    renderNewProjectDialog();
    renderOpenProjectDialog();
    renderMenuBar();

    renderProfilerWindow();
    renderCameraWindow();

    if (ImGuiFileDialog::Instance()->Display("OpenProject")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string folderPath = ImGuiFileDialog::Instance()->GetCurrentPath();
            ImGuiFileDialog::Instance()->Close();
            if (projectManager && !folderPath.empty()) {
                projectManager->openProject(folderPath);
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }
    
    if (ImGuiFileDialog::Instance()->Display("SaveProject")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string filePath = ImGuiFileDialog::Instance()->GetFilePathName();
            ImGuiFileDialog::Instance()->Close();
            if (onSaveProject) onSaveProject();
        }
        ImGuiFileDialog::Instance()->Close();
    }
}

void UIManager::setSelectionSingle(Entity entity) {
    m_SelectedEntities.clear();
    m_PrimarySelected = entt::null;

    if (entity != entt::null) {
        m_SelectedEntities.push_back(entity);
        m_PrimarySelected = entity;
    }
}

void UIManager::setSelectedEntity(Entity entity) {
    setSelectionSingle(entity);
}

void UIManager::toggleSelectedEntity(Entity entity) {
    if (entity == entt::null) {
        return;
    }

    for (size_t i = 0; i < m_SelectedEntities.size(); ++i) {
        if (m_SelectedEntities[i] == entity) {
            m_SelectedEntities.erase(m_SelectedEntities.begin() + static_cast<long long>(i));
            if (m_PrimarySelected == entity) {
                m_PrimarySelected = m_SelectedEntities.empty() ? entt::null : m_SelectedEntities.back();
            }
            return;
        }
    }

    m_SelectedEntities.push_back(entity);
    m_PrimarySelected = entity;
}

void UIManager::clearSelection() {
    m_SelectedEntities.clear();
    m_PrimarySelected = entt::null;
}

bool UIManager::isSelected(Entity entity) const {
    for (auto e : m_SelectedEntities) {
        if (e == entity) return true;
    }
    return false;
}

Entity UIManager::getSelectedEntity() const {
    return m_PrimarySelected;
}

bool UIManager::popViewportPickRequest(uint32_t& outX, uint32_t& outY, bool& outAdditive) {
    if (!m_HasViewportPickRequest) {
        return false;
    }
    m_HasViewportPickRequest = false;
    outX = m_ViewportPickX;
    outY = m_ViewportPickY;
    outAdditive = m_ViewportPickAdditive;
    m_ViewportPickAdditive = false;
    return true;
}

void UIManager::setOnAssetDropped(std::function<void(const std::string&)> callback) {
    onAssetDropped = callback;
}

void UIManager::setProjectManager(::ProjectManager* projManager) {
    projectManager = projManager;
}

void UIManager::openProject(const std::string& path) {
    if (projectManager) {
        projectManager->openProject(path);
    }
    clearContentTextureThumbs();
}

void UIManager::setWindow(GLFWwindow* win) {
    window = win;
}

void UIManager::setCameraMatrices(glm::mat4 view, glm::mat4 proj) {
    m_ViewMatrix = view;
    m_ProjMatrix = proj;
}

void UIManager::setCameraController(CameraController* controller) {
    m_CameraController = controller;
}

void UIManager::setRenderer(Atlas::Renderer* r) {
    renderer = r;
    if (renderer) {
        m_VSyncEnabled = renderer->isVSyncEnabled();
    }
}

void UIManager::renderViewport(ImTextureID viewportTexture) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) m_TransformMode = TransformMode::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) m_TransformMode = TransformMode::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) m_TransformMode = TransformMode::Scale;
    }

    ImVec2 contentSize = ImGui::GetContentRegionAvail();

    ImVec2 imageMin = ImGui::GetCursorScreenPos();
    ImGui::Image(viewportTexture, contentSize);

    // Scroll-wheel speed adjustment while hovering viewport
    {
        ImGuiIO& io = ImGui::GetIO();
        if (m_CameraController && ImGui::IsItemHovered() && !io.WantTextInput && io.MouseWheel != 0.0f) {
            float s = m_CameraController->getSpeed();
            float factor = std::pow(1.15f, io.MouseWheel);
            s *= factor;
            s = std::clamp(s, 0.05f, 5000.0f);
            m_CameraController->setSpeed(s);
        }
    }

    if (renderer && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !m_GizmoUsing) {
        ImVec2 mousePos = ImGui::GetMousePos();
        float localX = mousePos.x - imageMin.x;
        float localY = mousePos.y - imageMin.y;

        if (contentSize.x > 0.0f && contentSize.y > 0.0f && localX >= 0.0f && localY >= 0.0f && localX < contentSize.x && localY < contentSize.y) {
            VkExtent2D extent = renderer->getSwapChainExtent();

            float u = localX / contentSize.x;
            float v = localY / contentSize.y;

            uint32_t px = static_cast<uint32_t>(u * static_cast<float>(extent.width));
            uint32_t py = static_cast<uint32_t>(v * static_cast<float>(extent.height));

            if (extent.width > 0 && extent.height > 0) {
                if (px >= extent.width) px = extent.width - 1;
                if (py >= extent.height) py = extent.height - 1;
            }

            ImGuiIO& io = ImGui::GetIO();
            m_ViewportPickX = px;
            m_ViewportPickY = py;
            m_ViewportPickAdditive = io.KeyCtrl;
            m_HasViewportPickRequest = true;
        }
    }
    
    Entity primary = m_PrimarySelected;

    if (primary != entt::null && m_Scene && m_Scene->getRegistry().all_of<Transform>(primary)) {
        // For multi-selection, move only top-level selected entities (skip selected children of selected parents)
        auto& registry = m_Scene->getRegistry();
        std::vector<Entity> targets;
        targets.reserve(m_SelectedEntities.size());

        auto hasSelectedAncestor = [&](Entity e) -> bool {
            if (!registry.all_of<Atlas::ECS::ParentComponent>(e)) return false;
            Entity p = registry.get<Atlas::ECS::ParentComponent>(e).parent;
            while (p != entt::null) {
                if (isSelected(p)) return true;
                if (!registry.all_of<Atlas::ECS::ParentComponent>(p)) break;
                p = registry.get<Atlas::ECS::ParentComponent>(p).parent;
            }
            return false;
        };

        for (auto e : m_SelectedEntities) {
            if (e == entt::null) continue;
            if (!registry.valid(e)) continue;
            if (!registry.all_of<Transform>(e)) continue;
            if (hasSelectedAncestor(e)) continue;
            targets.push_back(e);
        }

        if (targets.empty()) {
            m_GizmoUsing = false;
            m_GizmoWasUsing = false;
            m_GizmoEditEntities.clear();
            m_GizmoBeforeMulti.clear();
        } else {
            // Gizmo pivot = centroid of targets in world space
            glm::vec3 centroid(0.0f);
            for (auto e : targets) {
                glm::mat4 wm = m_Scene->getWorldTransform(e);
                centroid += glm::vec3(wm[3]);
            }
            centroid /= static_cast<float>(targets.size());

            // Gizmo matrix (we only use it as a handle)
            glm::mat4 gizmoModel = glm::translate(glm::mat4(1.0f), centroid);
            glm::mat4 deltaMatrix = glm::mat4(1.0f);

            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(imageMin.x, imageMin.y, contentSize.x, contentSize.y);

            ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
            if (m_TransformMode == TransformMode::Rotate) operation = ImGuizmo::ROTATE;
            if (m_TransformMode == TransformMode::Scale) operation = ImGuizmo::SCALE;

            const float* snap = nullptr;
            float snapValues[3] = {0.0f, 0.0f, 0.0f};
            if (m_GizmoSnap) {
                if (operation == ImGuizmo::TRANSLATE) {
                    snapValues[0] = m_GizmoSnapTranslate;
                    snapValues[1] = m_GizmoSnapTranslate;
                    snapValues[2] = m_GizmoSnapTranslate;
                } else if (operation == ImGuizmo::ROTATE) {
                    snapValues[0] = m_GizmoSnapRotate;
                    snapValues[1] = m_GizmoSnapRotate;
                    snapValues[2] = m_GizmoSnapRotate;
                } else if (operation == ImGuizmo::SCALE) {
                    snapValues[0] = m_GizmoSnapScale;
                    snapValues[1] = m_GizmoSnapScale;
                    snapValues[2] = m_GizmoSnapScale;
                }
                snap = snapValues;
            }

            ImGuizmo::MODE mode = m_GizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

            glm::mat4 manipMatrix = (targets.size() == 1) ? m_Scene->getWorldTransform(targets[0]) : gizmoModel;

            ImGuizmo::Manipulate(
                glm::value_ptr(m_ViewMatrix),
                glm::value_ptr(m_ProjMatrix),
                operation,
                mode,
                glm::value_ptr(manipMatrix),
                glm::value_ptr(deltaMatrix),
                snap
            );

            auto makeState = [&](const Transform& t) -> TransformState {
                TransformState s;
                s.position = t.position;
                s.rotation = t.rotation;
                s.scale = t.scale;
                return s;
            };

            auto anyDifferent = [&](const std::vector<TransformState>& a, const std::vector<TransformState>& b) -> bool {
                const float eps = 1e-4f;
                if (a.size() != b.size()) return true;
                for (size_t i = 0; i < a.size(); ++i) {
                    if (glm::length(a[i].position - b[i].position) > eps) return true;
                    if (glm::length(a[i].rotation - b[i].rotation) > eps) return true;
                    if (glm::length(a[i].scale - b[i].scale) > eps) return true;
                }
                return false;
            };

            bool usingNow = ImGuizmo::IsUsing();

            if (usingNow && !m_GizmoWasUsing) {
                m_GizmoEditEntities = targets;
                m_GizmoBeforeMulti.clear();
                m_GizmoBeforeMulti.reserve(m_GizmoEditEntities.size());
                for (auto e : m_GizmoEditEntities) {
                    m_GizmoBeforeMulti.push_back(makeState(registry.get<Transform>(e)));
                }
            }

            m_GizmoUsing = usingNow;

            if (m_GizmoUsing) {
                glm::vec3 skew;
                glm::vec4 perspective;
                glm::quat orientation;
                glm::vec3 newScale;
                glm::vec3 translation;

                for (auto e : targets) {
                    glm::mat4 oldWorld = m_Scene->getWorldTransform(e);
                    glm::mat4 editedWorld = deltaMatrix * oldWorld;

                    glm::mat4 parentWorld = glm::mat4(1.0f);
                    if (registry.all_of<Atlas::ECS::ParentComponent>(e)) {
                        Entity p = registry.get<Atlas::ECS::ParentComponent>(e).parent;
                        if (p != entt::null) {
                            parentWorld = m_Scene->getWorldTransform(p);
                        }
                    }

                    glm::mat4 editedLocal = parentWorld == glm::mat4(1.0f) ? editedWorld : glm::inverse(parentWorld) * editedWorld;

                    glm::vec3 localPos;
                    glm::quat localOrient;
                    glm::vec3 localScale;
                    if (glm::decompose(editedLocal, localScale, localOrient, localPos, skew, perspective)) {
                        auto& t = registry.get<Transform>(e);
                        t.position = localPos;
                        t.rotation = glm::degrees(glm::eulerAngles(localOrient));
                        t.scale = localScale;
                    }
                }
            }

            if (!usingNow && m_GizmoWasUsing && !m_GizmoEditEntities.empty()) {
                std::vector<TransformState> after;
                after.reserve(m_GizmoEditEntities.size());
                for (auto e : m_GizmoEditEntities) {
                    if (registry.valid(e) && registry.all_of<Transform>(e)) {
                        after.push_back(makeState(registry.get<Transform>(e)));
                    } else {
                        after.push_back(TransformState{});
                    }
                }

                if (anyDifferent(m_GizmoBeforeMulti, after)) {
                    auto cmd = std::make_unique<MultiTransformCommand>();
                    cmd->entities = m_GizmoEditEntities;
                    cmd->before = m_GizmoBeforeMulti;
                    cmd->after = after;
                    pushCommand(std::move(cmd));
                }

                m_GizmoEditEntities.clear();
                m_GizmoBeforeMulti.clear();
            }

            m_GizmoWasUsing = usingNow;
        }
    } else {
        m_GizmoUsing = false;
        m_GizmoWasUsing = false;
        m_GizmoEditEntities.clear();
        m_GizmoBeforeMulti.clear();
    }

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_DROP");
        if (payload != nullptr && onAssetDropped) {
            const char* assetPath = static_cast<const char*>(payload->Data);
            onAssetDropped(std::string(assetPath));
        }
        ImGui::EndDragDropTarget();
    }
    
    ImGui::End();
    ImGui::PopStyleVar();
}

void UIManager::renderHierarchy() {
    ImGui::Begin("Hierarchy", nullptr, ImGuiWindowFlags_NoCollapse);

    if (m_Scene) {
        auto& registry = m_Scene->getRegistry();

        const auto renderEntityRecursively = [&](auto&& self, entt::entity entity, std::vector<Entity>& pendingDelete) -> void {
            std::string entityName;
            if (registry.all_of<Atlas::ECS::TagComponent>(entity)) {
                entityName = registry.get<Atlas::ECS::TagComponent>(entity).name;
            } else {
                entityName = "Entity " + std::to_string(static_cast<uint32_t>(entity));
            }

            const auto children = m_Scene->getChildren(entity);
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (isSelected(entity)) flags |= ImGuiTreeNodeFlags_Selected;
            if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

            ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entity)));
            bool nodeOpen = ImGui::TreeNodeEx(entityName.c_str(), flags);

            if (ImGui::IsItemClicked()) {
                ImGuiIO& io = ImGui::GetIO();
                if (io.KeyCtrl) {
                    toggleSelectedEntity(entity);
                } else {
                    setSelectedEntity(entity);
                }
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                bool isCameraEntity = m_Scene->getRegistry().all_of<Camera>(entity);
                if (!isCameraEntity) {
                    pendingDelete.push_back(entity);
                }
            }

            if (nodeOpen && !children.empty()) {
                for (auto child : children) {
                    self(self, child, pendingDelete);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        };

        std::vector<Entity> pendingDelete;

        auto roots = m_Scene->getRootEntities();
        if (roots.empty()) {
            for (auto entity : m_Scene->getAllEntities()) {
                renderEntityRecursively(renderEntityRecursively, entity, pendingDelete);
            }
        } else {
            for (auto root : roots) {
                renderEntityRecursively(renderEntityRecursively, root, pendingDelete);
            }
        }

        for (auto entity : pendingDelete) {
            if (m_Scene->getRegistry().valid(entity)) {
                if (isSelected(entity)) {
                    toggleSelectedEntity(entity);
                }
                m_Scene->destroyEntity(entity);
            }
        }
    }

    ImGui::End();
}

void UIManager::renderProperties() {
    ImGui::Begin("Properties", nullptr, ImGuiWindowFlags_NoCollapse);

    Entity selectedEntity = m_PrimarySelected;

    if (selectedEntity != entt::null && m_Scene && m_Scene->getRegistry().valid(selectedEntity)) {
        ImGui::Text("Entity ID: %u", static_cast<uint32_t>(selectedEntity));

        bool isCameraEntity = m_Scene->getRegistry().all_of<Camera>(selectedEntity);
        if (isCameraEntity) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Camera entity cannot be deleted");
        }

        bool deleted = false;
        ImGui::BeginDisabled(isCameraEntity);
        if (ImGui::Button("Delete Entity")) {
            m_Scene->destroyEntity(selectedEntity);
            clearSelection();
            selectedEntity = entt::null;
            deleted = true;
        }
        ImGui::EndDisabled();

        if (deleted) {
            ImGui::End();
            return;
        }

        auto renderComponent = [this, selectedEntity](auto&& component, const char* name) {
            if (ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen)) {
                ecs::renderComponentProperties(component, static_cast<uint32_t>(selectedEntity));
            }
        };

        if (m_Scene->getRegistry().all_of<Transform>(selectedEntity)) {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto& t = m_Scene->getRegistry().get<Transform>(selectedEntity);

                auto makeState = [&](const Transform& tr) -> TransformState {
                    TransformState s;
                    s.position = tr.position;
                    s.rotation = tr.rotation;
                    s.scale = tr.scale;
                    return s;
                };

                auto isDifferent = [&](const TransformState& a, const TransformState& b) -> bool {
                    const float eps = 1e-4f;
                    return glm::length(a.position - b.position) > eps ||
                           glm::length(a.rotation - b.rotation) > eps ||
                           glm::length(a.scale - b.scale) > eps;
                };

                bool deactivated = false;

                ImGui::DragFloat3("Position##T", &t.position.x, 0.1f);
                if (ImGui::IsItemActivated()) {
                    m_PropTransformEditing = true;
                    m_PropTransformEntity = selectedEntity;
                    m_PropTransformBefore = makeState(t);
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    deactivated = true;
                }

                ImGui::DragFloat3("Rotation##T", &t.rotation.x, 1.0f);
                if (ImGui::IsItemActivated()) {
                    m_PropTransformEditing = true;
                    m_PropTransformEntity = selectedEntity;
                    m_PropTransformBefore = makeState(t);
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    deactivated = true;
                }

                ImGui::DragFloat3("Scale##T", &t.scale.x, 0.1f);
                if (ImGui::IsItemActivated()) {
                    m_PropTransformEditing = true;
                    m_PropTransformEntity = selectedEntity;
                    m_PropTransformBefore = makeState(t);
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    deactivated = true;
                }

                if (deactivated && m_PropTransformEditing && m_PropTransformEntity == selectedEntity) {
                    TransformState after = makeState(t);
                    if (isDifferent(m_PropTransformBefore, after)) {
                        auto cmd = std::make_unique<TransformCommand>();
                        cmd->entity = selectedEntity;
                        cmd->before = m_PropTransformBefore;
                        cmd->after = after;
                        pushCommand(std::move(cmd));
                    }
                    m_PropTransformEditing = false;
                    m_PropTransformEntity = entt::null;
                }
            }
        }

        if (m_Scene->getRegistry().all_of<Renderable>(selectedEntity)) {
            renderComponent(m_Scene->getRegistry().get<Renderable>(selectedEntity), "Renderable");
        }

        if (m_Scene->getRegistry().all_of<::Mesh>(selectedEntity)) {
            if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto& registry = m_Scene->getRegistry();
                auto& mesh = registry.get<::Mesh>(selectedEntity);

                // Mesh info
                if (ImGui::BeginTable("##mesh_info", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
                    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted("Path");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", mesh.meshPath.c_str());

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted("Vertices");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%u", mesh.vertexCount);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted("Indices");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%u", mesh.indexCount);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted("Bounds");
                    ImGui::TableSetColumnIndex(1);
                    if (mesh.hasBounds) {
                        glm::vec3 size = mesh.boundsMax - mesh.boundsMin;
                        ImGui::Text("Min %.2f %.2f %.2f", mesh.boundsMin.x, mesh.boundsMin.y, mesh.boundsMin.z);
                        ImGui::Text("Max %.2f %.2f %.2f", mesh.boundsMax.x, mesh.boundsMax.y, mesh.boundsMax.z);
                        ImGui::Text("Size %.2f %.2f %.2f", size.x, size.y, size.z);
                    } else {
                        ImGui::TextUnformatted("(none)");
                    }

                    ImGui::EndTable();
                }

                // Collect all materials for this object (this entity + descendants)
                std::vector<Entity> materialEntities;
                materialEntities.reserve(32);

                auto addIfMaterial = [&](Entity e) {
                    if (e == entt::null) return;
                    if (!registry.valid(e)) return;
                    if (registry.all_of<Atlas::ECS::MaterialComponent>(e)) {
                        materialEntities.push_back(e);
                    }
                };

                // The "object" is the top-most parent in the hierarchy.
                Entity objectRoot = selectedEntity;
                while (registry.all_of<Atlas::ECS::ParentComponent>(objectRoot)) {
                    Entity p = registry.get<Atlas::ECS::ParentComponent>(objectRoot).parent;
                    if (p == entt::null || !registry.valid(p)) break;
                    objectRoot = p;
                }

                addIfMaterial(objectRoot);
                const auto gather = [&](auto&& self, Entity parent) -> void {
                    for (auto child : m_Scene->getChildren(parent)) {
                        addIfMaterial(child);
                        self(self, child);
                    }
                };
                gather(gather, objectRoot);

                ImGui::SeparatorText("Materials");

                {
                    std::string rootName;
                    if (registry.all_of<Atlas::ECS::TagComponent>(objectRoot)) {
                        rootName = registry.get<Atlas::ECS::TagComponent>(objectRoot).name;
                    } else {
                        rootName = "Entity " + std::to_string(static_cast<uint32_t>(objectRoot));
                    }

                    ImGui::Text("Object: %s", rootName.c_str());
                    ImGui::SameLine();
                    ImGui::Text("(%zu material(s))", materialEntities.size());
                }

                if (materialEntities.empty()) {
                    ImGui::TextUnformatted("No materials found on this object.");
                } else {
                    // Default selection
                    auto hasEntityInList = [&](uint32_t id) -> bool {
                        for (auto e : materialEntities) {
                            if (static_cast<uint32_t>(e) == id) return true;
                        }
                        return false;
                    };

                    if (!hasEntityInList(m_MaterialInspectEntityId)) {
                        m_MaterialInspectEntityId = static_cast<uint32_t>(materialEntities.front());
                    }

                    // Materials list
                    float listH = ImGui::GetTextLineHeightWithSpacing() * (float)ImMin<size_t>(8, materialEntities.size()) + 10.0f;
                    if (ImGui::BeginListBox("##mat_list", ImVec2(-FLT_MIN, listH))) {
                        for (auto e : materialEntities) {
                            uint32_t id = static_cast<uint32_t>(e);

                            std::string label;
                            if (registry.all_of<Atlas::ECS::TagComponent>(e)) {
                                label = registry.get<Atlas::ECS::TagComponent>(e).name;
                            } else {
                                label = "Entity " + std::to_string(id);
                            }
                            label += "##" + std::to_string(id);

                            bool selected = (id == m_MaterialInspectEntityId);
                            if (ImGui::Selectable(label.c_str(), selected)) {
                                m_MaterialInspectEntityId = id;
                            }
                        }
                        ImGui::EndListBox();
                    }

                    // Material editor
                    Entity matEntity = static_cast<Entity>(m_MaterialInspectEntityId);
                    if (matEntity != entt::null && registry.valid(matEntity) && registry.all_of<Atlas::ECS::MaterialComponent>(matEntity)) {
                        auto& mat = registry.get<Atlas::ECS::MaterialComponent>(matEntity);

                        if (m_MaterialEditEntityId != m_MaterialInspectEntityId) {
                            m_MaterialEditEntityId = m_MaterialInspectEntityId;

                            auto syncBuf = [](char* dst, size_t dstSize, const std::string& src) {
                                std::memset(dst, 0, dstSize);
                                if (!src.empty()) {
                                    std::strncpy(dst, src.c_str(), dstSize - 1);
                                }
                            };

                            syncBuf(m_AlbedoTexturePathBuf, sizeof(m_AlbedoTexturePathBuf), mat.albedoTexturePath);
                            syncBuf(m_NormalTexturePathBuf, sizeof(m_NormalTexturePathBuf), mat.normalTexturePath);
                            syncBuf(m_MetallicRoughnessTexturePathBuf, sizeof(m_MetallicRoughnessTexturePathBuf), mat.metallicRoughnessTexturePath);
                            syncBuf(m_AOTexturePathBuf, sizeof(m_AOTexturePathBuf), mat.aoTexturePath);
                            syncBuf(m_EmissiveTexturePathBuf, sizeof(m_EmissiveTexturePathBuf), mat.emissiveTexturePath);
                        }

                        if (ImGui::SmallButton("Select Entity##Mat")) {
                            setSelectionSingle(matEntity);
                        }
                        ImGui::SameLine();
                        ImGui::Text("Editing material on entity %u", static_cast<uint32_t>(matEntity));

                        ImGui::SeparatorText("Surface");
                        ImGui::ColorEdit4("Base Color##Mat", &mat.baseColor.x, ImGuiColorEditFlags_Float);
                        ImGui::DragFloat("Metallic##Mat", &mat.metallic, 0.01f, 0.0f, 1.0f);
                        ImGui::DragFloat("Roughness##Mat", &mat.roughness, 0.01f, 0.0f, 1.0f);
                        ImGui::DragFloat("AO##Mat", &mat.ambientOcclusion, 0.01f, 0.0f, 1.0f);
                        ImGui::ColorEdit3("Emissive Factor##Mat", &mat.emissiveFactor.x, ImGuiColorEditFlags_Float);

                        ImGui::SeparatorText("Alpha");
                        const char* alphaItems[] = {"Opaque", "Mask", "Blend"};
                        int alphaIdx = (mat.alphaMode == Atlas::ECS::MaterialComponent::AlphaMode::Mask) ? 1 :
                                       (mat.alphaMode == Atlas::ECS::MaterialComponent::AlphaMode::Blend) ? 2 : 0;
                        if (ImGui::Combo("Alpha Mode##Mat", &alphaIdx, alphaItems, 3)) {
                            if (alphaIdx == 1) mat.alphaMode = Atlas::ECS::MaterialComponent::AlphaMode::Mask;
                            else if (alphaIdx == 2) mat.alphaMode = Atlas::ECS::MaterialComponent::AlphaMode::Blend;
                            else mat.alphaMode = Atlas::ECS::MaterialComponent::AlphaMode::Opaque;
                        }
                        if (mat.alphaMode == Atlas::ECS::MaterialComponent::AlphaMode::Mask) {
                            ImGui::DragFloat("Alpha Cutoff##Mat", &mat.alphaCutoff, 0.01f, 0.0f, 1.0f);
                        }
                        ImGui::Checkbox("Double Sided##Mat", &mat.doubleSided);

                        auto editTexture = [&](const char* label, const char* dialogId, const char* filter,
                                               Atlas::AssetManager::TextureColorSpace colorSpace,
                                               bool& useFlag, int32_t& texIndex, Atlas::StringID& texId, std::string& texPath,
                                               char* pathBuf, size_t pathBufSize) {
                            ImGui::PushID(label);

                            auto clearTexture = [&]() {
                                useFlag = false;
                                texIndex = -1;
                                texId = Atlas::StringID::null();
                                texPath.clear();
                                std::memset(pathBuf, 0, pathBufSize);
                            };

                            bool prevUse = useFlag;
                            if (ImGui::Checkbox("Use##use", &useFlag)) {
                                if (prevUse && !useFlag) {
                                    clearTexture();
                                }
                            }
                            ImGui::SameLine();
                            ImGui::TextUnformatted(label);

                            bool picked = false;
                            bool applyNow = false;

                            ImGui::BeginDisabled(!useFlag);

                            ImGui::SetNextItemWidth(320.0f);
                            ImGui::InputText("##path", pathBuf, pathBufSize);
                            ImGui::SameLine();

                            if (ImGui::Button("Browse")) {
                                IGFD::FileDialogConfig cfg;
                                cfg.path = ".";
                                ImGuiFileDialog::Instance()->OpenDialog(dialogId, label, filter, cfg);
                            }

                            if (ImGuiFileDialog::Instance()->Display(dialogId)) {
                                if (ImGuiFileDialog::Instance()->IsOk()) {
                                    std::string filePath = ImGuiFileDialog::Instance()->GetFilePathName();
                                    std::memset(pathBuf, 0, pathBufSize);
                                    std::strncpy(pathBuf, filePath.c_str(), pathBufSize - 1);
                                    picked = true;
                                }
                                ImGuiFileDialog::Instance()->Close();
                            }

                            applyNow = ImGui::Button("Apply") || picked;

                            ImGui::EndDisabled();

                            ImGui::SameLine();
                            if (ImGui::Button("Clear")) {
                                clearTexture();
                            }

                            if (applyNow) {
                                std::string newPath(pathBuf);
                                if (useFlag && !newPath.empty() && assetManager && renderer) {
                                    std::string cacheKey = newPath + ((colorSpace == Atlas::AssetManager::TextureColorSpace::Linear) ? "#linear" : "#srgb");
                                    auto tex = assetManager->loadTexture(Atlas::StringID(cacheKey), newPath, colorSpace);
                                    if (tex && tex->isValid()) {
                                        uint32_t slot = 0;
                                        if (texIndex > 0) {
                                            renderer->updateTexture(static_cast<uint32_t>(texIndex), tex->getImageView(), tex->getSampler());
                                            slot = static_cast<uint32_t>(texIndex);
                                        } else {
                                            slot = renderer->bindTexture(tex->getImageView(), tex->getSampler());
                                        }

                                        if (slot != 0) {
                                            texIndex = static_cast<int32_t>(slot);
                                            texId = Atlas::StringID(newPath);
                                            texPath = newPath;
                                        }
                                    }
                                }

                                if (!useFlag) {
                                    clearTexture();
                                }
                            }

                            if (useFlag && (!assetManager || !renderer)) {
                                ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "Texture swapping requires AssetManager+Renderer");
                            }

                            ImGui::PopID();
                        };

                        ImGui::SeparatorText("Textures");
                        editTexture("Albedo", "SelectAlbedoTex", ".png,.jpg,.jpeg,.tga,.bmp,.hdr", Atlas::AssetManager::TextureColorSpace::SRGB,
                                    mat.useAlbedoTexture, mat.albedoTextureIndex, mat.albedoTextureId, mat.albedoTexturePath,
                                    m_AlbedoTexturePathBuf, sizeof(m_AlbedoTexturePathBuf));
                        editTexture("Normal", "SelectNormalTex", ".png,.jpg,.jpeg,.tga,.bmp,.hdr", Atlas::AssetManager::TextureColorSpace::Linear,
                                    mat.useNormalTexture, mat.normalTextureIndex, mat.normalTextureId, mat.normalTexturePath,
                                    m_NormalTexturePathBuf, sizeof(m_NormalTexturePathBuf));
                        editTexture("Metal/Rough", "SelectMetalRoughTex", ".png,.jpg,.jpeg,.tga,.bmp,.hdr", Atlas::AssetManager::TextureColorSpace::Linear,
                                    mat.useMetallicRoughnessTexture, mat.metallicRoughnessTextureIndex, mat.metallicRoughnessTextureId, mat.metallicRoughnessTexturePath,
                                    m_MetallicRoughnessTexturePathBuf, sizeof(m_MetallicRoughnessTexturePathBuf));
                        editTexture("AO", "SelectAOTex", ".png,.jpg,.jpeg,.tga,.bmp,.hdr", Atlas::AssetManager::TextureColorSpace::Linear,
                                    mat.useAOTexture, mat.aoTextureIndex, mat.aoTextureId, mat.aoTexturePath,
                                    m_AOTexturePathBuf, sizeof(m_AOTexturePathBuf));
                        editTexture("Emissive", "SelectEmissiveTex", ".png,.jpg,.jpeg,.tga,.bmp,.hdr", Atlas::AssetManager::TextureColorSpace::SRGB,
                                    mat.useEmissiveTexture, mat.emissiveTextureIndex, mat.emissiveTextureId, mat.emissiveTexturePath,
                                    m_EmissiveTexturePathBuf, sizeof(m_EmissiveTexturePathBuf));
                    }
                }
            }
        }

        if (m_Scene->getRegistry().all_of<Camera>(selectedEntity)) {
            renderComponent(m_Scene->getRegistry().get<Camera>(selectedEntity), "Camera");
        }
    } else {
        ImGui::Text("No entity selected");
    }

    ImGui::End();
}

static std::vector<std::string> folderStack;

std::string getFileIcon(const std::string& filename, bool isFolder) {
    if (isFolder) return "[D]";
    std::string ext = fs::path(filename).extension().string();
    if (ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".dae" || ext == ".blend") return "[M]";
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".hdr") return "";
    if (ext == ".mat" || ext == ".material") return "[*]";
    if (ext == ".scene" || ext == ".json") return "[S]";
    return "[F]";
}

::ProjectManager::FileEntry getFolderAtPath(::ProjectManager* pm, const std::vector<std::string>& path) {
    auto tree = pm->getAssetTree();
    
    if (path.empty()) return tree;
    
    ::ProjectManager::FileEntry* current = &tree;
    
    for (const auto& folderName : path) {
        bool found = false;
        for (auto& child : current->children) {
            if (child.name == folderName && child.isFolder) {
                current = &child;
                found = true;
                break;
            }
        }
        if (!found) return tree;
    }
    return *current;
}

void UIManager::renderContentExplorer() {
    ImGui::Begin("Content Explorer", nullptr, ImGuiWindowFlags_NoCollapse);

    m_ContentTextureThumbFrame++;

    if (!projectManager || !projectManager->hasProject()) {
        ImGui::TextUnformatted("No project open. Open a project to see assets.");
        ImGui::End();
        return;
    }

    // Top bar: breadcrumbs
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));

        if (ImGui::Button("Home")) {
            folderStack.clear();
        }

        for (size_t i = 0; i < folderStack.size(); ++i) {
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextUnformatted("/");
            ImGui::SameLine(0.0f, 6.0f);

            std::string label = folderStack[i];
            if (ImGui::Button(label.c_str())) {
                folderStack.erase(folderStack.begin() + static_cast<long long>(i) + 1, folderStack.end());
                break;
            }
        }

        ImGui::PopStyleVar(2);
    }

    ImGui::Separator();

    auto currentFolder = getFolderAtPath(projectManager, folderStack);

    const std::filesystem::path dstDir = std::filesystem::path(currentFolder.fullPath);

    bool openRenamePopup = false;
    bool openDeletePopup = false;

    auto makeUniquePath = [&](std::filesystem::path base) -> std::filesystem::path {
        if (!std::filesystem::exists(base)) return base;
        std::filesystem::path dir = base.parent_path();
        std::string stem = base.stem().string();
        std::string ext = base.extension().string();

        for (int v = 2; v < 1000; ++v) {
            std::filesystem::path cand = dir / (stem + "_v" + std::to_string(v) + ext);
            if (!std::filesystem::exists(cand)) return cand;
        }
        return base;
    };

    auto doPaste = [&]() {
        if (m_ContentClipboard.fullPath.empty()) {
            return;
        }

        std::error_code ec;
        std::filesystem::path src(m_ContentClipboard.fullPath);
        if (!std::filesystem::exists(src, ec)) {
            m_ContentClipboard.fullPath.clear();
            m_ContentClipboard.cut = false;
            return;
        }

        std::filesystem::path dst = makeUniquePath(dstDir / src.filename());

        if (m_ContentClipboard.cut) {
            std::filesystem::rename(src, dst, ec);
            if (!ec) {
                m_ContentClipboard.fullPath.clear();
                m_ContentClipboard.cut = false;
            }
        } else {
            if (std::filesystem::is_directory(src, ec)) {
                std::filesystem::copy(src, dst, std::filesystem::copy_options::recursive, ec);
            } else {
                std::filesystem::copy_file(src, dst, std::filesystem::copy_options::skip_existing, ec);
            }
        }
    };

    auto drawCtxMenu = [&](bool hasTarget) {
        if (hasTarget) {
            if (ImGui::MenuItem("Open")) {
                if (m_ContentCtxTarget.isFolder) {
                    folderStack.push_back(m_ContentCtxTarget.name);
                }
#ifdef _WIN32
                else {
                    openWithDefaultApp(m_ContentCtxTarget.fullPath);
                }
#endif
            }

            if (ImGui::MenuItem("Open File Location")) {
#ifdef _WIN32
                // For both files and folders, selecting is the most intuitive behavior.
                openExplorerSelect(m_ContentCtxTarget.fullPath);
#endif
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Copy")) {
                m_ContentClipboard.fullPath = m_ContentCtxTarget.fullPath;
                m_ContentClipboard.cut = false;
            }

            if (ImGui::MenuItem("Cut")) {
                m_ContentClipboard.fullPath = m_ContentCtxTarget.fullPath;
                m_ContentClipboard.cut = true;
            }

            if (ImGui::MenuItem("Copy Full Path")) {
                ImGui::SetClipboardText(m_ContentCtxTarget.fullPath.c_str());
            }

            if (ImGui::MenuItem("Copy Relative Path")) {
                ImGui::SetClipboardText(m_ContentCtxTarget.relativePath.c_str());
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Rename")) {
                std::memset(m_RenameAssetBuf, 0, sizeof(m_RenameAssetBuf));
                std::strncpy(m_RenameAssetBuf, m_ContentCtxTarget.name.c_str(), sizeof(m_RenameAssetBuf) - 1);
                openRenamePopup = true;
            }

            if (ImGui::MenuItem("Delete")) {
                openDeletePopup = true;
            }

            ImGui::Separator();
        }

        if (ImGui::MenuItem("Paste", nullptr, false, !m_ContentClipboard.fullPath.empty())) {
            doPaste();
        }

        if (ImGui::MenuItem("New Folder")) {
            std::error_code ec;
            std::filesystem::path p = dstDir / "New Folder";
            p = makeUniquePath(p);
            std::filesystem::create_directory(p, ec);
        }
    };

    // Grid layout
    const float tileW = 132.0f;
    const float tileH = 122.0f;
    const float gap = 12.0f;

    float avail = ImGui::GetContentRegionAvail().x;
    int cols = (int)((avail + gap) / (tileW + gap));
    if (cols < 1) cols = 1;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(gap * 0.5f, gap * 0.5f));

    if (ImGui::BeginTable("##content_grid", cols, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) {
        bool hasItems = false;

        for (const auto& child : currentFolder.children) {
            hasItems = true;

            ImGui::TableNextColumn();
            ImGui::PushID(child.relativePath.c_str());

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 sz(tileW, tileH);
            ImVec2 p1(p0.x + sz.x, p0.y + sz.y);

            bool pressed = ImGui::InvisibleButton("##tile", sz);
            bool hovered = ImGui::IsItemHovered();

            if (ImGui::BeginPopupContextItem("content_ctx")) {
                m_ContentCtxTarget.name = child.name;
                m_ContentCtxTarget.fullPath = child.fullPath;
                m_ContentCtxTarget.relativePath = child.relativePath;
                m_ContentCtxTarget.isFolder = child.isFolder;
                m_ContentCtxHasTarget = true;

                drawCtxMenu(true);
                ImGui::EndPopup();
            }

            const float r = 14.0f;

            ImU32 bg = ImGui::GetColorU32(ImGuiCol_ChildBg);
            ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
            ImU32 fg = ImGui::GetColorU32(ImGuiCol_Text);

            if (hovered) {
                bg = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
            }

            dl->AddRectFilled(p0, p1, bg, r);
            dl->AddRect(p0, p1, border, r);

            // Simple icon area
            ImVec2 iconP0(p0.x + 14.0f, p0.y + 12.0f);
            ImVec2 iconP1(p0.x + sz.x - 14.0f, p0.y + 72.0f);
            ImU32 iconBg = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            if (child.isFolder) {
                iconBg = ImGui::GetColorU32(ImVec4(0.18f, 0.77f, 0.71f, hovered ? 0.24f : 0.18f));
            }
            dl->AddRectFilled(iconP0, iconP1, iconBg, 10.0f);

            // Texture thumbnail preview (for image files)
            if (!child.isFolder) {
                std::string ext = fs::path(child.name).extension().string();
                for (char& c : ext) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }

                if (isImageFileExt(ext)) {
                    ImTextureID thumbId = getOrCreateContentTextureThumb(child.fullPath);
                    if (thumbId) {
                        ImVec2 pad(6.0f, 6.0f);
                        ImVec2 boxP0(iconP0.x + pad.x, iconP0.y + pad.y);
                        ImVec2 boxP1(iconP1.x - pad.x, iconP1.y - pad.y);

                        float boxW = std::max(1.0f, boxP1.x - boxP0.x);
                        float boxH = std::max(1.0f, boxP1.y - boxP0.y);

                        float texW = 1.0f;
                        float texH = 1.0f;
                        if (auto tIt = m_ContentTextureThumbs.find(child.fullPath); tIt != m_ContentTextureThumbs.end()) {
                            if (tIt->second.width > 0 && tIt->second.height > 0) {
                                texW = static_cast<float>(tIt->second.width);
                                texH = static_cast<float>(tIt->second.height);
                            }
                        }

                        float scale = std::min(boxW / texW, boxH / texH);
                        float drawW = texW * scale;
                        float drawH = texH * scale;

                        ImVec2 imgP0(boxP0.x + (boxW - drawW) * 0.5f, boxP0.y + (boxH - drawH) * 0.5f);
                        ImVec2 imgP1(imgP0.x + drawW, imgP0.y + drawH);

                        dl->AddImage(thumbId, imgP0, imgP1);
                    }
                }
            }

            // Badge
            std::string badge = getFileIcon(child.name, child.isFolder);
            if (!badge.empty()) {
                ImVec2 bt = ImGui::CalcTextSize(badge.c_str());
                ImVec2 bp0(iconP0.x + 10.0f, iconP0.y + 8.0f);
                ImVec2 bp1(bp0.x + bt.x + 12.0f, bp0.y + bt.y + 6.0f);
                dl->AddRectFilled(bp0, bp1, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.22f)), 999.0f);
                dl->AddText(ImVec2(bp0.x + 6.0f, bp0.y + 3.0f), fg, badge.c_str());
            }

            // Name (ellipsized)
            std::string name = child.name;
            float maxTextW = sz.x - 22.0f;
            ImVec2 namePos(p0.x + 12.0f, p0.y + 82.0f);

            // crude ellipsis
            while (!name.empty() && ImGui::CalcTextSize(name.c_str()).x > maxTextW) {
                if (name.size() > 3) {
                    name.resize(name.size() - 1);
                    if (name.size() > 3) {
                        name[name.size() - 1] = '.';
                        name[name.size() - 2] = '.';
                        name[name.size() - 3] = '.';
                    }
                } else {
                    break;
                }
            }
            dl->AddText(namePos, fg, name.c_str());

            if (child.isFolder) {
                if (pressed) {
                    folderStack.push_back(child.name);
                }
            } else {
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("ASSET_DROP", child.relativePath.c_str(), child.relativePath.length() + 1);
                    ImGui::TextUnformatted(child.name.c_str());
                    ImGui::EndDragDropSource();
                }
            }

            ImGui::PopID();
        }

        if (!hasItems) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Empty folder");
        }

        ImGui::EndTable();
    }

    if (ImGui::BeginPopupContextWindow("content_ctx_window", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        m_ContentCtxHasTarget = false;
        drawCtxMenu(false);
        ImGui::EndPopup();
    }

    if (openRenamePopup) {
        m_ShowRenameAssetPopup = true;
        ImGui::OpenPopup("##rename_asset");
    }
    if (openDeletePopup) {
        m_ShowDeleteAssetPopup = true;
        ImGui::OpenPopup("##delete_asset");
    }

    if (ImGui::BeginPopupModal("##rename_asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Rename");
        ImGui::Separator();
        ImGui::InputText("##new_name", m_RenameAssetBuf, sizeof(m_RenameAssetBuf));

        if (ImGui::Button("OK")) {
            std::error_code ec;
            std::filesystem::path src(m_ContentCtxTarget.fullPath);
            std::filesystem::path dst = src.parent_path() / std::string(m_RenameAssetBuf);
            std::filesystem::rename(src, dst, ec);
            m_ShowRenameAssetPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_ShowRenameAssetPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("##delete_asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Delete this asset?");
        ImGui::Separator();
        ImGui::TextUnformatted(m_ContentCtxHasTarget ? m_ContentCtxTarget.name.c_str() : "");

        if (ImGui::Button("Delete")) {
            std::error_code ec;
            std::filesystem::path p(m_ContentCtxTarget.fullPath);
            if (m_ContentCtxTarget.isFolder) {
                std::filesystem::remove_all(p, ec);
            } else {
                std::filesystem::remove(p, ec);
            }
            m_ShowDeleteAssetPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_ShowDeleteAssetPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar();

    pruneContentTextureThumbs();

    // Drop target for external file drops
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_DROP")) {
            if (payload->DataSize > 0 && onAssetDropped) {
                const char* droppedPath = static_cast<const char*>(payload->Data);
                onAssetDropped(droppedPath);
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::End();
}

void UIManager::renderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Project")) {
            if (ImGui::MenuItem("New Project", "Ctrl+N")) {
                showNewProjectDialog = true;
            }
            if (ImGui::MenuItem("Open Project", "Ctrl+O")) {
                IGFD::FileDialogConfig config;
                config.path = ".";
                ImGuiFileDialog::Instance()->OpenDialog("OpenProject", "Open Project Folder", nullptr, config);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Project", "Ctrl+S")) {
                IGFD::FileDialogConfig saveConfig;
                saveConfig.path = ".";
                ImGuiFileDialog::Instance()->OpenDialog("SaveProject", "Save Project", nullptr, saveConfig);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                if (onExit) onExit();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Import Model...", "Ctrl+I")) {
            }
            if (ImGui::MenuItem("Import Texture...", "Ctrl+T")) {
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Viewport", NULL, true);
            ImGui::MenuItem("Hierarchy", NULL, true);
            ImGui::MenuItem("Properties", NULL, true);
            ImGui::MenuItem("Content Explorer", NULL, true);
            ImGui::Separator();
            ImGui::MenuItem("Profiler", NULL, &m_ShowProfilerWindow);
            ImGui::MenuItem("Camera", NULL, &m_ShowCameraWindow);
            ImGui::Separator();
            if (renderer) {
                glm::vec4 color = renderer->getClearColor();
                if (ImGui::ColorEdit3("Background", &color.x, ImGuiColorEditFlags_Float)) {
                    renderer->setClearColor(color);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void UIManager::updateProfiler(float deltaTime)
{
    m_FrameTimeMs = deltaTime * 1000.0f;
    m_Fps = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;

    m_FrameTimeHistory[m_FrameTimeIndex] = m_FrameTimeMs;
    m_FrameTimeIndex = (m_FrameTimeIndex + 1) % PROFILER_HISTORY;
}

void UIManager::renderProfilerWindow()
{
    if (!m_ShowProfilerWindow) return;

    ImGui::Begin("Profiler", &m_ShowProfilerWindow, ImGuiWindowFlags_NoCollapse);

    ImGui::Text("FPS: %.1f", m_Fps);
    ImGui::Text("Frame: %.3f ms", m_FrameTimeMs);

    float minTime = FLT_MAX;
    float maxTime = 0.0f;
    for (int i = 0; i < PROFILER_HISTORY; ++i) {
        float v = m_FrameTimeHistory[i];
        if (v > 0.0f) {
            minTime = ImMin(minTime, v);
            maxTime = ImMax(maxTime, v);
        }
    }

    if (minTime == FLT_MAX) minTime = 0.0f;

    ImGui::Text("Frame history (last %d frames)", PROFILER_HISTORY);
    ImGui::PlotLines("##FrameTimes", m_FrameTimeHistory, PROFILER_HISTORY, m_FrameTimeIndex, nullptr, minTime, ImMax(maxTime, 1.0f), ImVec2(320, 80));

    ImGui::SeparatorText("Frame limiting");

    bool prevVsync = m_VSyncEnabled;
    ImGui::Checkbox("VSync", &m_VSyncEnabled);
    if (prevVsync != m_VSyncEnabled && renderer) {
        renderer->setVSyncEnabled(m_VSyncEnabled);
    }

    ImGui::SliderInt("Max FPS (0=Unlimited)", &m_MaxFps, 0, 240);

#ifdef TRACY_ENABLE
    bool connected = tracy::GetProfiler().IsConnected();
    ImGui::Text("Tracy status: %s", connected ? "Connected" : "Disconnected");
#else
    ImGui::Text("Tracy status: disabled (TRACY_ENABLE not set)");
#endif

    ImGui::Checkbox("Show Tracy Connection", &m_ShowTracyConnection);

    ImGui::End();
}

void UIManager::renderCameraWindow() {
    if (!m_ShowCameraWindow) return;

    ImGui::Begin("Camera", &m_ShowCameraWindow, ImGuiWindowFlags_NoCollapse);

    if (m_CameraController) {
        bool enabled = m_CameraController->isEnabled();
        if (ImGui::Checkbox("Enabled##Cam", &enabled)) {
            m_CameraController->setEnabled(enabled);
        }

        bool requireRmb = m_CameraController->getRequireRmbForMove();
        if (ImGui::Checkbox("Require RMB to move", &requireRmb)) {
            m_CameraController->setRequireRmbForMove(requireRmb);
        }

        bool lockCursor = m_CameraController->getLockCursorOnLook();
        if (ImGui::Checkbox("Lock cursor on RMB", &lockCursor)) {
            m_CameraController->setLockCursorOnLook(lockCursor);
        }

        float speed = m_CameraController->getSpeed();
        if (ImGui::DragFloat("Move speed", &speed, 0.1f, 0.05f, 5000.0f, "%.2f")) {
            m_CameraController->setSpeed(speed);
        }

        float boost = m_CameraController->getBoostMultiplier();
        if (ImGui::DragFloat("Shift boost", &boost, 0.1f, 1.0f, 50.0f, "%.2f")) {
            m_CameraController->setBoostMultiplier(boost);
        }

        float slow = m_CameraController->getSlowMultiplier();
        if (ImGui::DragFloat("Ctrl slow", &slow, 0.01f, 0.01f, 1.0f, "%.2f")) {
            m_CameraController->setSlowMultiplier(slow);
        }

        float sens = m_CameraController->getSensitivity();
        if (ImGui::DragFloat("Mouse sensitivity", &sens, 0.01f, 0.01f, 5.0f, "%.2f")) {
            m_CameraController->setSensitivity(sens);
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Shortcuts:");
        ImGui::TextUnformatted("- Hold RMB to look");
        ImGui::TextUnformatted("- Shift = boost, Ctrl = slow");
        ImGui::TextUnformatted("- Scroll over viewport adjusts speed");
    } else {
        ImGui::TextUnformatted("No camera controller bound");
    }

    ImGui::End();
}

void UIManager::renderNewProjectDialog() {
    if (ImGuiFileDialog::Instance()->Display("SelectNewProjectFolder")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string folderPath = ImGuiFileDialog::Instance()->GetFilePathName();
            ImGuiFileDialog::Instance()->Close();
            strncpy(newProjectPath, folderPath.c_str(), sizeof(newProjectPath) - 1);
            showNewProjectDialog = true;
        }
        ImGuiFileDialog::Instance()->Close();
    }
    
    if (!showNewProjectDialog) return;
    
    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_Always);
    ImGui::OpenPopup("New Project");
    if (ImGui::BeginPopupModal("New Project", &showNewProjectDialog)) {
        ImGui::Text("Project Name:");
        ImGui::InputText("##name", newProjectName, IM_ARRAYSIZE(newProjectName));
        
        ImGui::Text("Location:");
        ImGui::InputText("##path", newProjectPath, IM_ARRAYSIZE(newProjectPath));
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            showNewProjectDialog = false;
            IGFD::FileDialogConfig config;
            config.path = ".";
            ImGuiFileDialog::Instance()->OpenDialog("SelectNewProjectFolder", "Select Project Folder", nullptr, config);
        }
        
        ImGui::Separator();
        
        if (ImGui::Button("Create")) {
            if (projectManager && strlen(newProjectName) > 0 && strlen(newProjectPath) > 0) {
                std::string fullPath = std::string(newProjectPath) + "/" + newProjectName;
                projectManager->createNewProject(newProjectName, fullPath);
                if (onNewProject) onNewProject();
            }
            showNewProjectDialog = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            showNewProjectDialog = false;
        }
        
        ImGui::EndPopup();
    }
}

void UIManager::renderOpenProjectDialog() {
    if (!showOpenProjectDialog) return;
    
    ImGui::OpenPopup("Open Project");
    if (ImGui::BeginPopupModal("Open Project", &showOpenProjectDialog, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Select Project Folder:");
        ImGui::InputText("##openpath", projectPathBuffer, IM_ARRAYSIZE(projectPathBuffer));
        
        ImGui::Separator();
        
        if (ImGui::Button("Open")) {
            if (projectManager && strlen(projectPathBuffer) > 0) {
                projectManager->openProject(projectPathBuffer);
                if (onOpenProject) onOpenProject();
            }
            showOpenProjectDialog = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            showOpenProjectDialog = false;
        }
        
        ImGui::EndPopup();
    }
}
