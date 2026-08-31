#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui_manager.h"
#include "../platform/native_file_dialog.h"
#include <imgui.h>
#include "../utils/camera_controller.h"
#include <imgui_internal.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>
#include "../ecs/ecs.h"
#include "../ecs/components.h"
#include "../assets/asset_manager.h"
#include "../core/runtime_console.h"
#include "../ecs/components/components.h"
#include "../scripting/script_engine.h"
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

void UIManager::copySelectedEntitiesToClipboard() {
    if (!m_Scene) return;
    auto& registry = m_Scene->getRegistry();

    m_EntityClipboard.items.clear();
    m_EntityClipboard.hasData = false;

    if (m_SelectedEntities.empty()) {
        return;
    }

    auto isHidden = [&](Entity e) -> bool {
        return registry.all_of<Atlas::ECS::EditorHiddenComponent>(e);
    };

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

    std::vector<Entity> roots;
    roots.reserve(m_SelectedEntities.size());
    for (auto e : m_SelectedEntities) {
        if (e == entt::null) continue;
        if (!registry.valid(e)) continue;
        if (isHidden(e)) continue;
        if (registry.all_of<EditorCamera>(e)) continue;
        if (hasSelectedAncestor(e)) continue;
        roots.push_back(e);
    }

    if (roots.empty()) {
        return;
    }

    std::unordered_set<Entity> visited;
    visited.reserve(256);

    std::vector<Entity> ordered;
    ordered.reserve(256);

    const auto gather = [&](auto&& self, Entity e) -> void {
        if (e == entt::null) return;
        if (!registry.valid(e)) return;
        if (isHidden(e)) return;
        if (registry.all_of<EditorCamera>(e)) return;
        if (visited.find(e) != visited.end()) return;

        visited.insert(e);
        ordered.push_back(e);

        for (auto child : m_Scene->getChildren(e)) {
            self(self, child);
        }
    };

    for (auto r : roots) {
        gather(gather, r);
    }

    m_EntityClipboard.items.reserve(ordered.size());
    for (auto e : ordered) {
        EntityClipboardItem item;
        item.source = e;

        if (registry.all_of<Atlas::ECS::TagComponent>(e)) {
            item.name = registry.get<Atlas::ECS::TagComponent>(e).name;
        } else {
            item.name = "Entity";
        }

        if (registry.all_of<Atlas::ECS::ParentComponent>(e)) {
            Entity p = registry.get<Atlas::ECS::ParentComponent>(e).parent;
            // Preserve parent if it exists; may be outside the copied subtree.
            item.parent = (p != entt::null && registry.valid(p)) ? p : entt::null;
        }

        if (registry.all_of<Transform>(e)) {
            item.hasTransform = true;
            item.transform = registry.get<Transform>(e);
        }
        if (registry.all_of<Renderable>(e)) {
            item.hasRenderable = true;
            item.renderable = registry.get<Renderable>(e);
        }
        if (registry.all_of<Camera>(e)) {
            item.hasCamera = true;
            item.camera = registry.get<Camera>(e);
        }
        if (registry.all_of<::Mesh>(e)) {
            item.hasMesh = true;
            item.mesh = registry.get<::Mesh>(e);
        }
        if (registry.all_of<Atlas::ECS::MaterialComponent>(e)) {
            item.hasMaterial = true;
            item.material = registry.get<Atlas::ECS::MaterialComponent>(e);
        }
        if (registry.all_of<Atlas::ECS::RigidBodyComponent>(e)) {
            item.hasRigidBody = true;
            item.rigidBody = registry.get<Atlas::ECS::RigidBodyComponent>(e);
        }
        if (registry.all_of<Atlas::ECS::BoxColliderComponent>(e)) {
            item.hasBoxCollider = true;
            item.boxCollider = registry.get<Atlas::ECS::BoxColliderComponent>(e);
        }
        if (registry.all_of<Atlas::ECS::SphereColliderComponent>(e)) {
            item.hasSphereCollider = true;
            item.sphereCollider = registry.get<Atlas::ECS::SphereColliderComponent>(e);
        }
        if (registry.all_of<Atlas::ECS::CapsuleColliderComponent>(e)) {
            item.hasCapsuleCollider = true;
            item.capsuleCollider = registry.get<Atlas::ECS::CapsuleColliderComponent>(e);
        }
        if (registry.all_of<Atlas::ECS::LightComponent>(e)) {
            item.hasLight = true;
            item.light = registry.get<Atlas::ECS::LightComponent>(e);
        }
        if (registry.all_of<Atlas::ECS::ScriptComponent>(e)) {
            item.hasScript = true;
            item.script = registry.get<Atlas::ECS::ScriptComponent>(e);
        }
        if (registry.all_of<Atlas::ECS::FollowCameraComponent>(e)) {
            item.hasFollowCamera = true;
            item.followCamera = registry.get<Atlas::ECS::FollowCameraComponent>(e);
        }
        if (registry.all_of<Atlas::ECS::GameCameraComponent>(e)) {
            item.hasGameCamera = true;
            item.gameCamera = registry.get<Atlas::ECS::GameCameraComponent>(e);
        }
        if (registry.all_of<::WorldChunk>(e)) {
            item.hasWorldChunk = true;
            item.worldChunk = registry.get<::WorldChunk>(e);
        }
        if (registry.all_of<::WorldTransform>(e)) {
            item.hasWorldTransform = true;
            item.worldTransform = registry.get<::WorldTransform>(e);
        }
        if (registry.all_of<Atlas::ECS::SkinnedMeshComponent>(e)) {
            item.hasSkinnedMesh = true;
            item.skinnedMesh = registry.get<Atlas::ECS::SkinnedMeshComponent>(e);
        }
        if (registry.all_of<Atlas::ECS::SkeletonComponent>(e)) {
            item.hasSkeleton = true;
            item.skeleton = registry.get<Atlas::ECS::SkeletonComponent>(e);
        }
        if (registry.all_of<Atlas::ECS::AnimationPlayerComponent>(e)) {
            item.hasAnimationPlayer = true;
            item.animPlayer = registry.get<Atlas::ECS::AnimationPlayerComponent>(e);
        }
        if (registry.all_of<Atlas::ECS::BonePoseOverrideComponent>(e)) {
            item.hasBonePoseOverride = true;
            item.bonePoseOverride = registry.get<Atlas::ECS::BonePoseOverrideComponent>(e);
        }

        m_EntityClipboard.items.push_back(std::move(item));
    }

    m_EntityClipboard.hasData = !m_EntityClipboard.items.empty();
}

void UIManager::pasteEntitiesFromClipboard() {
    if (!m_Scene) return;
    if (!m_EntityClipboard.hasData) return;

    auto& registry = m_Scene->getRegistry();

    std::unordered_map<Entity, Entity> remap;
    remap.reserve(m_EntityClipboard.items.size());

    // Small offset so paste isn't exactly overlapping.
    m_EntityPasteSerial++;
    const float step = 0.35f;
    const float k = static_cast<float>((m_EntityPasteSerial % 10) + 1);
    const glm::vec3 rootOffset(step * k, 0.0f, step * k);

    // First pass: create entities and copy components.
    for (const auto& item : m_EntityClipboard.items) {
        if (item.source == entt::null) continue;

        std::string newName = item.name.empty() ? "Entity" : item.name;
        newName += " Copy";

        Entity e = m_Scene->createEntity(newName);
        remap[item.source] = e;

        // Ensure visible and not hidden.
        if (registry.all_of<Atlas::ECS::EditorHiddenComponent>(e)) {
            registry.remove<Atlas::ECS::EditorHiddenComponent>(e);
        }

        // Copy core components.
        if (item.hasTransform && registry.all_of<Transform>(e)) {
            auto t = item.transform;
            // Apply offset only to roots (children keep local offsets).
            if (item.parent == entt::null) {
                t.position += rootOffset;
            }
            registry.get<Transform>(e) = t;
        }

        if (item.hasRenderable) {
            registry.emplace_or_replace<Renderable>(e, item.renderable);
        }

        if (item.hasCamera) {
            registry.emplace_or_replace<Camera>(e, item.camera);
        }

        if (item.hasMesh) {
            ::Mesh meshCopy = item.mesh;
            // For editor copy/paste, treat pasted meshes as owning; free happens when the last ref is destroyed.
            meshCopy.ownsGpuResources = true;
            registry.emplace_or_replace<::Mesh>(e, meshCopy);
        }

        if (item.hasMaterial) registry.emplace_or_replace<Atlas::ECS::MaterialComponent>(e, item.material);
        if (item.hasRigidBody) registry.emplace_or_replace<Atlas::ECS::RigidBodyComponent>(e, item.rigidBody);
        if (item.hasBoxCollider) registry.emplace_or_replace<Atlas::ECS::BoxColliderComponent>(e, item.boxCollider);
        if (item.hasSphereCollider) registry.emplace_or_replace<Atlas::ECS::SphereColliderComponent>(e, item.sphereCollider);
        if (item.hasCapsuleCollider) registry.emplace_or_replace<Atlas::ECS::CapsuleColliderComponent>(e, item.capsuleCollider);
        if (item.hasLight) registry.emplace_or_replace<Atlas::ECS::LightComponent>(e, item.light);
        if (item.hasScript) registry.emplace_or_replace<Atlas::ECS::ScriptComponent>(e, item.script);
        if (item.hasFollowCamera) registry.emplace_or_replace<Atlas::ECS::FollowCameraComponent>(e, item.followCamera);
        if (item.hasGameCamera) registry.emplace_or_replace<Atlas::ECS::GameCameraComponent>(e, item.gameCamera);
        if (item.hasWorldChunk) registry.emplace_or_replace<::WorldChunk>(e, item.worldChunk);
        if (item.hasWorldTransform) registry.emplace_or_replace<::WorldTransform>(e, item.worldTransform);
        if (item.hasSkinnedMesh) registry.emplace_or_replace<Atlas::ECS::SkinnedMeshComponent>(e, item.skinnedMesh);
        if (item.hasSkeleton) registry.emplace_or_replace<Atlas::ECS::SkeletonComponent>(e, item.skeleton);
        if (item.hasAnimationPlayer) registry.emplace_or_replace<Atlas::ECS::AnimationPlayerComponent>(e, item.animPlayer);
        if (item.hasBonePoseOverride) registry.emplace_or_replace<Atlas::ECS::BonePoseOverrideComponent>(e, item.bonePoseOverride);

        // Tag component
        if (registry.all_of<Atlas::ECS::TagComponent>(e)) {
            registry.get<Atlas::ECS::TagComponent>(e).name = newName;
        } else {
            registry.emplace<Atlas::ECS::TagComponent>(e, newName);
        }
    }

    // Second pass: restore parent relationships.
    for (const auto& item : m_EntityClipboard.items) {
        auto it = remap.find(item.source);
        if (it == remap.end()) continue;
        Entity newEntity = it->second;

        if (item.parent == entt::null) {
            continue;
        }

        // If parent was copied, parent to the cloned parent; else parent to original parent.
        Entity newParent = entt::null;
        auto pit = remap.find(item.parent);
        if (pit != remap.end()) {
            newParent = pit->second;
        } else if (registry.valid(item.parent) && !registry.all_of<Atlas::ECS::EditorHiddenComponent>(item.parent)) {
            newParent = item.parent;
        }

        if (newParent != entt::null) {
            m_Scene->setParent(newEntity, newParent);
        }
    }

    // Third pass: fix-up entity references.
    for (const auto& item : m_EntityClipboard.items) {
        auto it = remap.find(item.source);
        if (it == remap.end()) continue;
        Entity newEntity = it->second;

        if (item.hasFollowCamera && registry.all_of<Atlas::ECS::FollowCameraComponent>(newEntity)) {
            auto fc = registry.get<Atlas::ECS::FollowCameraComponent>(newEntity);
            if (fc.target != entt::null) {
                auto tit = remap.find(fc.target);
                fc.target = (tit != remap.end()) ? tit->second : entt::null;
            }
            registry.emplace_or_replace<Atlas::ECS::FollowCameraComponent>(newEntity, fc);
        }

        if (item.hasSkinnedMesh && registry.all_of<Atlas::ECS::SkinnedMeshComponent>(newEntity)) {
            auto sm = registry.get<Atlas::ECS::SkinnedMeshComponent>(newEntity);
            if (sm.skeletonEntity != entt::null) {
                auto sit = remap.find(sm.skeletonEntity);
                sm.skeletonEntity = (sit != remap.end()) ? sit->second : entt::null;
            }
            registry.emplace_or_replace<Atlas::ECS::SkinnedMeshComponent>(newEntity, sm);
        }
    }

    // Update selection to the pasted roots.
    m_SelectedEntities.clear();
    m_PrimarySelected = entt::null;

    for (const auto& item : m_EntityClipboard.items) {
        if (item.parent != entt::null) continue;
        auto it = remap.find(item.source);
        if (it == remap.end()) continue;
        m_SelectedEntities.push_back(it->second);
    }
    if (!m_SelectedEntities.empty()) {
        m_PrimarySelected = m_SelectedEntities.back();
    }

    m_Scene->setDirty(true);
}

void UIManager::renderToolbar(bool gameModeActive, bool gameModePaused) {
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

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(24.0f, 0.0f));
    ImGui::SameLine();

    if (pill("##primitives", "Primitives", false, ImVec2(112.0f, 28.0f))) {
        ImGui::OpenPopup("toolbar_primitives");
    }
    if (ImGui::BeginPopup("toolbar_primitives")) {
        if (ImGui::MenuItem("Cube") && onCreatePrimitive) {
            onCreatePrimitive("Cube", entt::null);
        }
        if (ImGui::MenuItem("Plane") && onCreatePrimitive) {
            onCreatePrimitive("Plane", entt::null);
        }
        if (ImGui::MenuItem("Sphere") && onCreatePrimitive) {
            onCreatePrimitive("Sphere", entt::null);
        }
        if (ImGui::MenuItem("Cylinder") && onCreatePrimitive) {
            onCreatePrimitive("Cylinder", entt::null);
        }
        if (ImGui::MenuItem("Capsule") && onCreatePrimitive) {
            onCreatePrimitive("Capsule", entt::null);
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine(0.0f, 8.0f);
    if (pill("##gamecam", "Game Camera", false, ImVec2(132.0f, 28.0f))) {
        if (onCreateGameCamera) onCreateGameCamera(entt::null);
    }

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(12.0f, 0.0f));
    ImGui::SameLine();

    if (!gameModeActive) {
        if (pill("##play", "Play", false, ImVec2(72.0f, 28.0f))) {
            if (onPlay) onPlay();
        }
    } else {
        if (pill("##pause", gameModePaused ? "Resume" : "Pause", gameModePaused, ImVec2(82.0f, 28.0f))) {
            if (onPause) onPause();
        }
        ImGui::SameLine(0.0f, 8.0f);
        if (pill("##stop", "Stop", false, ImVec2(72.0f, 28.0f))) {
            if (onStop) onStop();
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void UIManager::render(ImTextureID viewportTexture, ImTextureID gameViewportTexture, bool gameModeActive, bool gameModePaused) {
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

    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        dockspaceID = ImGui::GetID("AtlasDockspace_v2");

        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking |
                                  ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus |
                                  ImGuiWindowFlags_NoNavFocus |
                                  ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("##dockspace_host", nullptr, hostFlags);
        ImGui::PopStyleVar(3);

        // Build the default layout only when the node is missing.
        if (ImGui::DockBuilderGetNode(dockspaceID) == nullptr) {
            ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceID, viewport->WorkSize);

            ImGuiID dockMain = dockspaceID;

            // Left: Hierarchy/Content Explorer
            ImGuiID dockLeft = 0;
            // Right: Properties
            ImGuiID dockRight = 0;

            ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.28f, &dockRight, &dockMain);
            ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.24f, &dockLeft, &dockMain);

            ImGuiID dockTop = 0;
            ImGuiID dockMainCenter = 0;
            ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Up, 0.07f, &dockTop, &dockMainCenter);

            ImGuiID dockCenterBottom = 0;
            ImGuiID dockCenterTop = 0;
            ImGui::DockBuilderSplitNode(dockMainCenter, ImGuiDir_Down, 0.28f, &dockCenterBottom, &dockCenterTop);

            ImGuiID dockLeftTop = 0;
            ImGuiID dockLeftBottom = 0;
            ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Down, 0.45f, &dockLeftBottom, &dockLeftTop);

            ImGui::DockBuilderDockWindow("Toolbar", dockTop);
            ImGui::DockBuilderDockWindow("Viewport", dockCenterTop);
            ImGui::DockBuilderDockWindow("Game", dockCenterTop);
            ImGui::DockBuilderDockWindow("Console", dockCenterBottom);
            ImGui::DockBuilderDockWindow("Hierarchy", dockLeftTop);
            ImGui::DockBuilderDockWindow("Content Explorer", dockLeftBottom);
            ImGui::DockBuilderDockWindow("Properties", dockRight);
            ImGui::DockBuilderFinish(dockspaceID);

        } else {
            // Keep the dockspace node sized to the work area.
            ImGui::DockBuilderSetNodeSize(dockspaceID, viewport->WorkSize);
        }

        ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::End();
    }

    // Prune selection (streaming/unload can delete entities).
    if (m_Scene) {
        auto& registry = m_Scene->getRegistry();
        for (size_t i = 0; i < m_SelectedEntities.size(); ) {
            Entity e = m_SelectedEntities[i];
            if (e == entt::null || !registry.valid(e) || registry.all_of<Atlas::ECS::EditorHiddenComponent>(e)) {
                m_SelectedEntities.erase(m_SelectedEntities.begin() + static_cast<long long>(i));
            } else {
                ++i;
            }
        }

        if (m_PrimarySelected != entt::null && (!registry.valid(m_PrimarySelected) || registry.all_of<Atlas::ECS::EditorHiddenComponent>(m_PrimarySelected))) {
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

            if (ImGui::IsKeyPressed(ImGuiKey_C)) {
                copySelectedEntitiesToClipboard();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_V)) {
                pasteEntitiesFromClipboard();
            }
        }

        // Delete selected entity (soft delete)
        if (!io.WantTextInput && !ImGuizmo::IsUsing() && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            if (m_Scene) {
                auto& reg = m_Scene->getRegistry();
                Entity selected = m_PrimarySelected;
                if (selected != entt::null && reg.valid(selected) && !reg.all_of<Atlas::ECS::EditorHiddenComponent>(selected)) {
                    bool isProtectedCameraEntity = reg.all_of<EditorCamera>(selected);
                    if (!isProtectedCameraEntity) {
                        // Reuse the same soft-delete behavior as the Hierarchy context menu.
                        std::vector<Entity> subtree;
                        subtree.reserve(32);

                        const auto collect = [&](auto&& self, Entity e) -> void {
                            if (e == entt::null) return;
                            if (!reg.valid(e)) return;
                            if (reg.all_of<Atlas::ECS::EditorHiddenComponent>(e)) return;

                            subtree.push_back(e);
                            for (auto c : m_Scene->getChildren(e)) {
                                self(self, c);
                            }
                        };

                        collect(collect, selected);

                        for (Entity e : subtree) {
                            if (e == entt::null || !reg.valid(e)) continue;
                            reg.emplace_or_replace<Atlas::ECS::EditorHiddenComponent>(e, Atlas::ECS::EditorHiddenComponent{});
                            if (isSelected(e)) {
                                toggleSelectedEntity(e);
                            }
                        }

                        auto cmd = std::make_unique<SoftDeleteCommand>();
                        cmd->entities = subtree;
                        pushCommand(std::move(cmd));
                    }
                }
            }
        }
    }

    // Keep previous frame focus info so Play and game input can react to prior focus.
    m_ViewportFocusedPrevFrame = m_ViewportFocusedLastFrame;
    m_GameViewportFocusedPrevFrame = m_GameViewportFocusedLastFrame;
    m_ViewportAllowCameraInput = false;
    m_ViewportFocusedLastFrame = false;
    m_GameViewportFocusedLastFrame = false;

    renderToolbar(gameModeActive, gameModePaused);

    if (m_ShowViewportWindow) {
        renderViewport(viewportTexture);
    }
    if (m_ShowGameViewportWindow) {
        renderGameViewport(gameViewportTexture);
    }
    if (m_ShowHierarchyWindow) {
        renderHierarchy();
    }
    if (m_ShowPropertiesWindow) {
        renderProperties();
    }
    if (m_ShowConsoleWindow) {
        renderConsoleWindow();
    }
    renderContentExplorer();
    
    renderNewProjectDialog();
    renderOpenProjectDialog();
    renderMenuBar();

    renderProfilerWindow();
    renderCameraWindow();
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

bool UIManager::popViewportPickRequest(uint32_t& outX, uint32_t& outY, bool& outAdditive, bool& outDeselectOnMiss) {
    if (!m_HasViewportPickRequest) {
        return false;
    }
    m_HasViewportPickRequest = false;
    outX = m_ViewportPickX;
    outY = m_ViewportPickY;
    outAdditive = m_ViewportPickAdditive;
    outDeselectOnMiss = m_ViewportPickDeselectOnMiss;
    m_ViewportPickAdditive = false;
    m_ViewportPickDeselectOnMiss = false;
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
        if (onOpenProject) onOpenProject();
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
    ImGui::Begin("Viewport", &m_ShowViewportWindow, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) m_TransformMode = TransformMode::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) m_TransformMode = TransformMode::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) m_TransformMode = TransformMode::Scale;
    }

    ImVec2 contentSize = ImGui::GetContentRegionAvail();

    ImVec2 imageMin = ImGui::GetCursorScreenPos();
    ImGui::Image(viewportTexture, contentSize);

    {
        ImGuiIO& io = ImGui::GetIO();
        const bool viewportHovered = ImGui::IsItemHovered();
        const bool viewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        m_ViewportFocusedLastFrame = viewportFocused;
        m_ViewportAllowCameraInput = viewportHovered && viewportFocused && !io.WantTextInput;
    }

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
            m_ViewportPickDeselectOnMiss = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            m_HasViewportPickRequest = true;
        }
    }
    
    auto projectToViewport = [&](const glm::vec3& worldPos, ImVec2& out) -> bool {
        glm::vec4 clipPos = m_ProjMatrix * m_ViewMatrix * glm::vec4(worldPos, 1.0f);
        if (clipPos.w <= 1e-5f) return false;

        glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
        if (ndc.z < -1.5f || ndc.z > 1.5f) return false;

        float sx = (ndc.x * 0.5f + 0.5f) * contentSize.x + imageMin.x;
        float sy = (-ndc.y * 0.5f + 0.5f) * contentSize.y + imageMin.y;
        out = ImVec2(sx, sy);
        return true;
    };

    Entity primary = m_PrimarySelected;

    bool rigEditActive = false;

    if (m_Scene && m_RigEditEntity != entt::null && m_Scene->getRegistry().valid(m_RigEditEntity)) {
        auto& registry = m_Scene->getRegistry();
        Entity rigEntity = m_RigEditEntity;

        if (registry.all_of<Atlas::ECS::SkeletonComponent>(rigEntity)) {
            auto& skc = registry.get<Atlas::ECS::SkeletonComponent>(rigEntity);
            const Atlas::Anim::Skeleton* skel = skc.skeleton.get();

            if (skel && skel->boneCount() > 0) {
                const Atlas::Anim::AnimationClip* clip = nullptr;
                float tSeconds = 0.0f;

                if (registry.all_of<Atlas::ECS::AnimationPlayerComponent>(rigEntity)) {
                    const auto& ap = registry.get<Atlas::ECS::AnimationPlayerComponent>(rigEntity).player;
                    tSeconds = ap.timeSeconds;
                    if (ap.clipIndex >= 0 && static_cast<size_t>(ap.clipIndex) < skc.clips.size()) {
                        clip = &skc.clips[static_cast<size_t>(ap.clipIndex)];
                    }
                }

                Atlas::Anim::PoseOverrides overrides;
                auto* ov = registry.try_get<Atlas::ECS::BonePoseOverrideComponent>(rigEntity);
                const bool editPose = (ov && ov->enabled);
                if (editPose) {
                    // Ensure override arrays are the right size.
                    const uint32_t bc = skel->boneCount();
                    if (ov->hasRotation.size() != bc) ov->hasRotation.assign(bc, uint8_t(0));
                    if (ov->rotation.size() != bc) ov->rotation.assign(bc, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

                    overrides.hasRotation = &ov->hasRotation;
                    overrides.rotation = &ov->rotation;
                }

                std::vector<glm::mat4> globals;
                Atlas::Anim::evaluateGlobals(*skel, clip, tSeconds, overrides, globals);

                glm::mat4 entityWorld = glm::mat4(1.0f);
                if (m_Scene->hasTransform(rigEntity)) {
                    entityWorld = m_Scene->getWorldTransform(rigEntity);
                }

                if (m_RigShowSkeleton) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 col = IM_COL32(120, 200, 255, 200);
                    ImU32 colSel = IM_COL32(255, 200, 80, 240);

                    const uint32_t bc = static_cast<uint32_t>(globals.size());
                    for (uint32_t i = 0; i < bc; ++i) {
                        int32_t p = (i < skel->parentIndex.size()) ? skel->parentIndex[i] : -1;
                        if (p < 0 || static_cast<uint32_t>(p) >= bc) continue;

                        glm::vec3 aM = glm::vec3(globals[static_cast<uint32_t>(p)][3]);
                        glm::vec3 bM = glm::vec3(globals[i][3]);
                        glm::vec3 aW = glm::vec3(entityWorld * glm::vec4(aM, 1.0f));
                        glm::vec3 bW = glm::vec3(entityWorld * glm::vec4(bM, 1.0f));

                        ImVec2 aS, bS;
                        if (!projectToViewport(aW, aS) || !projectToViewport(bW, bS)) continue;

                        bool highlight = (m_RigSelectedBone >= 0) && (static_cast<uint32_t>(m_RigSelectedBone) == i || static_cast<uint32_t>(m_RigSelectedBone) == static_cast<uint32_t>(p));
                        dl->AddLine(aS, bS, highlight ? colSel : col, highlight ? 2.5f : 1.0f);
                    }
                }

                if (editPose && m_RigSelectedBone >= 0 && static_cast<uint32_t>(m_RigSelectedBone) < globals.size()) {
                    rigEditActive = true;

                    const uint32_t bi = static_cast<uint32_t>(m_RigSelectedBone);
                    glm::mat4 boneWorld = entityWorld * globals[bi];
                    glm::mat4 oldBoneWorld = boneWorld;
                    glm::mat4 manipMatrix = boneWorld;
                    glm::mat4 deltaMatrix(1.0f);

                    ImGuizmo::SetOrthographic(false);
                    ImGuizmo::SetDrawlist();
                    ImGuizmo::SetRect(imageMin.x, imageMin.y, contentSize.x, contentSize.y);

                    ImGuizmo::Manipulate(
                        glm::value_ptr(m_ViewMatrix),
                        glm::value_ptr(m_ProjMatrix),
                        ImGuizmo::ROTATE,
                        ImGuizmo::LOCAL,
                        glm::value_ptr(manipMatrix),
                        glm::value_ptr(deltaMatrix),
                        nullptr
                    );

                    bool usingNow = ImGuizmo::IsUsing();
                    m_GizmoUsing = usingNow;

                    if (usingNow && ov) {
                        glm::mat4 editedWorld = deltaMatrix * oldBoneWorld;
                        glm::mat4 editedModel = glm::inverse(entityWorld) * editedWorld;

                        glm::mat4 parentGlobal = glm::mat4(1.0f);
                        int32_t p = (bi < skel->parentIndex.size()) ? skel->parentIndex[bi] : -1;
                        if (p >= 0 && static_cast<uint32_t>(p) < globals.size()) {
                            parentGlobal = globals[static_cast<uint32_t>(p)];
                        }

                        glm::mat4 localM = glm::inverse(parentGlobal) * editedModel;

                        glm::vec3 col0(localM[0]);
                        glm::vec3 col1(localM[1]);
                        glm::vec3 col2(localM[2]);
                        float sx = glm::length(col0);
                        float sy = glm::length(col1);
                        float sz = glm::length(col2);
                        if (sx > 0.0f) col0 /= sx;
                        if (sy > 0.0f) col1 /= sy;
                        if (sz > 0.0f) col2 /= sz;

                        glm::mat3 rotM(col0, col1, col2);
                        glm::quat localRot = glm::normalize(glm::quat_cast(rotM));

                        if (bi < ov->hasRotation.size() && bi < ov->rotation.size()) {
                            ov->hasRotation[bi] = 1;
                            ov->rotation[bi] = localRot;
                        }
                    }
                }
            }
        }
    }

    if (primary != entt::null && m_Scene && m_Scene->getRegistry().valid(primary) &&
        (m_Scene->getRegistry().all_of<Camera>(primary) || m_Scene->getRegistry().all_of<EditorCamera>(primary))) {
        auto& registry = m_Scene->getRegistry();
        CameraBase cam;
        if (registry.all_of<Camera>(primary)) {
            cam = registry.get<Camera>(primary);

            if (registry.all_of<Atlas::ECS::GameCameraComponent>(primary) && registry.all_of<Transform>(primary)) {
                glm::mat4 world = m_Scene->getWorldTransform(primary);
                glm::vec3 position = glm::vec3(world[3]);
                glm::vec3 forwardFromTransform = glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
                glm::vec3 upFromTransform = glm::vec3(world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));

                if (glm::length(forwardFromTransform) > 1e-5f) {
                    cam.position = position;
                    cam.target = position + glm::normalize(forwardFromTransform);
                }
                if (glm::length(upFromTransform) > 1e-5f) {
                    cam.up = glm::normalize(upFromTransform);
                }
            }
        } else {
            cam = registry.get<EditorCamera>(primary);
        }

        glm::vec3 forward = glm::normalize(cam.target - cam.position);
        if (glm::length(forward) < 1e-5f) {
            forward = glm::vec3(0.0f, 0.0f, -1.0f);
        }
        glm::vec3 right = glm::normalize(glm::cross(forward, cam.up));
        if (glm::length(right) < 1e-5f) {
            right = glm::vec3(1.0f, 0.0f, 0.0f);
        }
        glm::vec3 up = glm::normalize(glm::cross(right, forward));

        const float tanHalfFov = std::tan(glm::radians(cam.fov) * 0.5f);
        const float nearDist = std::max(cam.nearPlane, 0.01f);
        const float farDist = std::max(cam.farPlane, nearDist + 0.01f);
        const float aspect = (cam.aspectRatio > 0.0f) ? cam.aspectRatio : (contentSize.x / std::max(contentSize.y, 1.0f));
        const float nearH = tanHalfFov * nearDist;
        const float nearW = nearH * aspect;
        const float farH = tanHalfFov * farDist;
        const float farW = farH * aspect;

        const glm::vec3 nc = cam.position + forward * nearDist;
        const glm::vec3 fc = cam.position + forward * farDist;

        const glm::vec3 ntl = nc + up * nearH - right * nearW;
        const glm::vec3 ntr = nc + up * nearH + right * nearW;
        const glm::vec3 nbl = nc - up * nearH - right * nearW;
        const glm::vec3 nbr = nc - up * nearH + right * nearW;
        const glm::vec3 ftl = fc + up * farH - right * farW;
        const glm::vec3 ftr = fc + up * farH + right * farW;
        const glm::vec3 fbl = fc - up * farH - right * farW;
        const glm::vec3 fbr = fc - up * farH + right * farW;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 col = IM_COL32(255, 220, 120, 210);
        const ImU32 fillCol = IM_COL32(255, 220, 120, 40);

        auto drawSegment = [&](const glm::vec3& a, const glm::vec3& b) {
            ImVec2 as, bs;
            if (projectToViewport(a, as) && projectToViewport(b, bs)) {
                dl->AddLine(as, bs, col, 1.5f);
            }
        };

        auto drawLoop = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d) {
            drawSegment(a, b);
            drawSegment(b, c);
            drawSegment(c, d);
            drawSegment(d, a);
        };

        ImVec2 centerS;
        if (projectToViewport(cam.position, centerS)) {
            dl->AddCircleFilled(centerS, 4.0f, col);
        }
        drawLoop(ntl, ntr, nbr, nbl);
        drawLoop(ftl, ftr, fbr, fbl);
        drawSegment(cam.position, ntl);
        drawSegment(cam.position, ntr);
        drawSegment(cam.position, nbl);
        drawSegment(cam.position, nbr);
        drawSegment(ntl, ftl);
        drawSegment(ntr, ftr);
        drawSegment(nbl, fbl);
        drawSegment(nbr, fbr);

        ImVec2 nearPts[4];
        if (projectToViewport(ntl, nearPts[0]) && projectToViewport(ntr, nearPts[1]) &&
            projectToViewport(nbr, nearPts[2]) && projectToViewport(nbl, nearPts[3])) {
            dl->AddConvexPolyFilled(nearPts, 4, fillCol);
        }
    }

    if (!rigEditActive && primary != entt::null && m_Scene && m_Scene->getRegistry().all_of<Transform>(primary)) {
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
                    glm::mat4 editedWorld = (targets.size() == 1) ? manipMatrix : (deltaMatrix * oldWorld);

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

void UIManager::renderGameViewport(ImTextureID viewportTexture) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Game", &m_ShowGameViewportWindow, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

    if (m_RequestFocusGameViewport) {
        ImGui::SetWindowFocus();
        m_RequestFocusGameViewport = false;
    }

    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    ImGui::Image(viewportTexture, contentSize);

    ImGuiIO& io = ImGui::GetIO();
    bool gameFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    m_GameViewportFocusedLastFrame = gameFocused;

    if (gameFocused && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (onReleaseGameFocus) {
            onReleaseGameFocus();
        }
        ImGui::SetWindowFocus("Toolbar");
        m_GameViewportFocusedLastFrame = false;
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void UIManager::renderHierarchy() {
    ImGui::Begin("Hierarchy", &m_ShowHierarchyWindow, ImGuiWindowFlags_NoCollapse);

    if (m_Scene) {
        auto& registry = m_Scene->getRegistry();

        struct PendingReparent {
            Entity child = entt::null;
            Entity afterParent = entt::null;
        };

        std::vector<PendingReparent> pendingReparents;
        pendingReparents.reserve(16);

        std::vector<Entity> pendingCreateChildren;
        pendingCreateChildren.reserve(8);

        bool pendingCreateRoot = false;

        auto queueReparent = [&](Entity child, Entity afterParent) {
            if (!m_Scene) return;
            if (child == entt::null) return;
            if (afterParent == child) return;
            if (afterParent != entt::null && !registry.valid(afterParent)) return;

            for (auto& pr : pendingReparents) {
                if (pr.child == child) {
                    pr.afterParent = afterParent;
                    return;
                }
            }

            pendingReparents.push_back(PendingReparent{child, afterParent});
        };

        auto softDeleteSubtree = [&](Entity entity) {
            if (!m_Scene) return;
            if (!registry.valid(entity)) return;

            bool isProtectedCameraEntity = registry.all_of<EditorCamera>(entity);
            if (isProtectedCameraEntity) {
                return;
            }

            // Soft delete: hide entity subtree (keeps GPU resources alive, supports undo).
            std::vector<Entity> subtree;
            subtree.reserve(32);

            const auto collect = [&](auto&& self2, Entity e) -> void {
                if (e == entt::null) return;
                if (!registry.valid(e)) return;

                subtree.push_back(e);
                for (auto c : m_Scene->getChildren(e)) {
                    self2(self2, c);
                }
            };

            collect(collect, entity);

            for (Entity e : subtree) {
                if (e == entt::null || !registry.valid(e)) continue;
                registry.emplace_or_replace<Atlas::ECS::EditorHiddenComponent>(e, Atlas::ECS::EditorHiddenComponent{});
                if (isSelected(e)) {
                    toggleSelectedEntity(e);
                }
            }

            auto cmd = std::make_unique<SoftDeleteCommand>();
            cmd->entities = subtree;
            pushCommand(std::move(cmd));
        };

        const auto renderEntityRecursively = [&](auto&& self, entt::entity entity) -> void {
            if (registry.all_of<Atlas::ECS::EditorHiddenComponent>(entity)) {
                return;
            }

            std::string entityName;
            if (registry.all_of<Atlas::ECS::TagComponent>(entity)) {
                entityName = registry.get<Atlas::ECS::TagComponent>(entity).name;
            } else {
                entityName = "Entity " + std::to_string(static_cast<uint32_t>(entity));
            }

            const auto& children = m_Scene->getChildren(entity);
            Atlas::Anim::Skeleton* entitySkeleton = nullptr;
            if (registry.all_of<Atlas::ECS::SkeletonComponent>(entity)) {
                entitySkeleton = registry.get<Atlas::ECS::SkeletonComponent>(entity).skeleton.get();
            }
            const bool hasBoneHierarchy = (entitySkeleton && entitySkeleton->boneCount() > 0);

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (isSelected(entity)) flags |= ImGuiTreeNodeFlags_Selected;
            if (children.empty() && !hasBoneHierarchy) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

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
            // Context menu
            if (ImGui::BeginPopupContextItem("hierarchy_ctx")) {
                // Right-click should select the item being operated on.
                if (!isSelected(entity)) {
                    setSelectedEntity(entity);
                }

                const bool isProtectedCameraEntity = registry.all_of<EditorCamera>(entity);

                if (ImGui::MenuItem("Rename...")) {
                    m_ShowHierarchyRenamePopup = true;
                    m_HierarchyRenameEntityId = static_cast<uint32_t>(entity);
                    std::memset(m_HierarchyRenameBuf, 0, sizeof(m_HierarchyRenameBuf));
                    if (registry.all_of<Atlas::ECS::TagComponent>(entity)) {
                        const auto& tag = registry.get<Atlas::ECS::TagComponent>(entity);
                        if (!tag.name.empty()) {
                            std::strncpy(m_HierarchyRenameBuf, tag.name.c_str(), sizeof(m_HierarchyRenameBuf) - 1);
                        }
                    }
                }

                if (ImGui::MenuItem("Delete", nullptr, false, !isProtectedCameraEntity)) {
                    softDeleteSubtree(entity);
                }

                if (registry.all_of<Atlas::ECS::ParentComponent>(entity)) {
                    if (ImGui::MenuItem("Unparent")) {
                        queueReparent(entity, entt::null);
                    }
                }

                if (ImGui::MenuItem("Create Child")) {
                    pendingCreateChildren.push_back(entity);
                }

                if (ImGui::MenuItem("Create Child Game Camera") && onCreateGameCamera) {
                    onCreateGameCamera(entity);
                }

                if (ImGui::BeginMenu("Add Primitive")) {
                    if (ImGui::MenuItem("Cube") && onCreatePrimitive) {
                        onCreatePrimitive("Cube", entity);
                    }
                    if (ImGui::MenuItem("Plane") && onCreatePrimitive) {
                        onCreatePrimitive("Plane", entity);
                    }
                    if (ImGui::MenuItem("Sphere") && onCreatePrimitive) {
                        onCreatePrimitive("Sphere", entity);
                    }
                    if (ImGui::MenuItem("Cylinder") && onCreatePrimitive) {
                        onCreatePrimitive("Cylinder", entity);
                    }
                    if (ImGui::MenuItem("Capsule") && onCreatePrimitive) {
                        onCreatePrimitive("Capsule", entity);
                    }
                    ImGui::EndMenu();
                }

                ImGui::EndPopup();
            }

            // Drag/drop reparenting
            {
                uint32_t id = static_cast<uint32_t>(entity);
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("ENTITY", &id, sizeof(id));
                    ImGui::TextUnformatted(entityName.c_str());
                    ImGui::EndDragDropSource();
                }

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY")) {
                        if (payload->DataSize == sizeof(uint32_t) && m_Scene) {
                            uint32_t draggedId = *static_cast<const uint32_t*>(payload->Data);
                            Entity dragged = static_cast<Entity>(draggedId);

                            if (dragged != entt::null && dragged != entity && registry.valid(dragged)) {
                                // Prevent cycles: don't parent under own descendant.
                                bool cycle = false;
                                Entity p = entity;
                                while (registry.all_of<Atlas::ECS::ParentComponent>(p)) {
                                    Entity pp = registry.get<Atlas::ECS::ParentComponent>(p).parent;
                                    if (pp == entt::null || !registry.valid(pp)) break;
                                    if (pp == dragged) {
                                        cycle = true;
                                        break;
                                    }
                                    p = pp;
                                }

                                if (!cycle) {
                                    Entity beforeParent = entt::null;
                                    if (registry.all_of<Atlas::ECS::ParentComponent>(dragged)) {
                                        beforeParent = registry.get<Atlas::ECS::ParentComponent>(dragged).parent;
                                    }

                                    if (beforeParent != entity) {
                                        queueReparent(dragged, entity);
                                    }
                                }
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }

            const bool entityLeaf = children.empty() && !hasBoneHierarchy;
            if (nodeOpen && !entityLeaf) {
                for (auto child : children) {
                    self(self, child);
                }

                if (hasBoneHierarchy) {
                    std::vector<std::vector<uint32_t>> boneChildren(entitySkeleton->boneCount());
                    std::vector<uint32_t> boneRoots;
                    boneRoots.reserve(entitySkeleton->boneCount());

                    for (uint32_t bi = 0; bi < entitySkeleton->boneCount(); ++bi) {
                        int32_t parent = (bi < entitySkeleton->parentIndex.size()) ? entitySkeleton->parentIndex[bi] : -1;
                        if (parent >= 0 && static_cast<uint32_t>(parent) < entitySkeleton->boneCount()) {
                            boneChildren[static_cast<uint32_t>(parent)].push_back(bi);
                        } else {
                            boneRoots.push_back(bi);
                        }
                    }

                    auto renderBoneRecursively = [&](auto&& selfBone, uint32_t boneIndex) -> void {
                        const bool boneSelected = (m_RigEditEntity == entity) && (m_RigSelectedBone == static_cast<int32_t>(boneIndex));
                        const bool boneLeaf = boneIndex >= boneChildren.size() || boneChildren[boneIndex].empty();
                        ImGuiTreeNodeFlags boneFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                        if (boneSelected) boneFlags |= ImGuiTreeNodeFlags_Selected;
                        if (boneLeaf) boneFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

                        const char* boneName = (boneIndex < entitySkeleton->boneNames.size() && !entitySkeleton->boneNames[boneIndex].empty())
                            ? entitySkeleton->boneNames[boneIndex].c_str()
                            : "<bone>";

                        ImGui::PushID(static_cast<int>(boneIndex) + 1000000);
                        bool boneOpen = ImGui::TreeNodeEx(boneName, boneFlags);
                        if (ImGui::IsItemClicked()) {
                            setSelectedEntity(entity);
                            m_RigEditEntity = entity;
                            m_RigSelectedBone = static_cast<int32_t>(boneIndex);
                        }

                        if (boneOpen && !boneLeaf) {
                            for (uint32_t childBone : boneChildren[boneIndex]) {
                                selfBone(selfBone, childBone);
                            }
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    };

                    ImGui::PushID("__skeleton_tree");
                    ImGuiTreeNodeFlags skelFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
                    bool skeletonNodeOpen = ImGui::TreeNodeEx("Skeleton", skelFlags);
                    if (skeletonNodeOpen) {
                        for (uint32_t rootBone : boneRoots) {
                            renderBoneRecursively(renderBoneRecursively, rootBone);
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }

                ImGui::TreePop();
            }
            ImGui::PopID();
        };


        auto roots = m_Scene->getRootEntities();
        if (roots.empty()) {
            auto all = m_Scene->getAllEntities();

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(all.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    renderEntityRecursively(renderEntityRecursively, all[static_cast<size_t>(i)]);
                }
            }
            clipper.End();
        } else {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(roots.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    renderEntityRecursively(renderEntityRecursively, roots[static_cast<size_t>(i)]);
                }
            }
            clipper.End();
        }

        // Right-click empty area: context menu
        if (ImGui::BeginPopupContextWindow("hierarchy_empty_ctx", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            const Entity selected = m_PrimarySelected;
            const bool selectedValid = (selected != entt::null) && registry.valid(selected) && !registry.all_of<Atlas::ECS::EditorHiddenComponent>(selected);

            if (ImGui::MenuItem("Create Entity")) {
                pendingCreateRoot = true;
            }

            if (ImGui::MenuItem("Create Game Camera") && onCreateGameCamera) {
                onCreateGameCamera(entt::null);
            }

            if (ImGui::BeginMenu("Add Primitive")) {
                if (ImGui::MenuItem("Cube") && onCreatePrimitive) {
                    onCreatePrimitive("Cube", entt::null);
                }
                if (ImGui::MenuItem("Plane") && onCreatePrimitive) {
                    onCreatePrimitive("Plane", entt::null);
                }
                if (ImGui::MenuItem("Sphere") && onCreatePrimitive) {
                    onCreatePrimitive("Sphere", entt::null);
                }
                if (ImGui::MenuItem("Cylinder") && onCreatePrimitive) {
                    onCreatePrimitive("Cylinder", entt::null);
                }
                if (ImGui::MenuItem("Capsule") && onCreatePrimitive) {
                    onCreatePrimitive("Capsule", entt::null);
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Create Child", nullptr, false, selectedValid)) {
                pendingCreateChildren.push_back(selected);
            }

            if (ImGui::MenuItem("Create Sibling", nullptr, false, selectedValid)) {
                if (registry.all_of<Atlas::ECS::ParentComponent>(selected)) {
                    Entity parent = registry.get<Atlas::ECS::ParentComponent>(selected).parent;
                    if (parent != entt::null && registry.valid(parent)) {
                        pendingCreateChildren.push_back(parent);
                    } else {
                        pendingCreateRoot = true;
                    }
                } else {
                    pendingCreateRoot = true;
                }
            }

            if (ImGui::MenuItem("Rename Selected...", nullptr, false, selectedValid)) {
                m_ShowHierarchyRenamePopup = true;
                m_HierarchyRenameEntityId = static_cast<uint32_t>(selected);
                std::memset(m_HierarchyRenameBuf, 0, sizeof(m_HierarchyRenameBuf));
                if (registry.all_of<Atlas::ECS::TagComponent>(selected)) {
                    const auto& tag = registry.get<Atlas::ECS::TagComponent>(selected);
                    if (!tag.name.empty()) {
                        std::strncpy(m_HierarchyRenameBuf, tag.name.c_str(), sizeof(m_HierarchyRenameBuf) - 1);
                    }
                }
            }

            const bool canUnparent = selectedValid && registry.all_of<Atlas::ECS::ParentComponent>(selected);
            if (ImGui::MenuItem("Unparent Selected", nullptr, false, canUnparent)) {
                queueReparent(selected, entt::null);
            }

            if (ImGui::MenuItem("Delete Selected", nullptr, false, selectedValid)) {
                softDeleteSubtree(selected);
            }

            if (ImGui::MenuItem("Clear Selection", nullptr, false, !m_SelectedEntities.empty())) {
                clearSelection();
            }

            ImGui::EndPopup();
        }

        // Drop on empty space to unparent
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY")) {
                if (payload->DataSize == sizeof(uint32_t) && m_Scene) {
                    uint32_t draggedId = *static_cast<const uint32_t*>(payload->Data);
                    Entity dragged = static_cast<Entity>(draggedId);
                    if (dragged != entt::null && registry.valid(dragged)) {
                        Entity beforeParent = entt::null;
                        if (registry.all_of<Atlas::ECS::ParentComponent>(dragged)) {
                            beforeParent = registry.get<Atlas::ECS::ParentComponent>(dragged).parent;
                        }

                        if (beforeParent != entt::null) {
                            queueReparent(dragged, entt::null);
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Apply deferred hierarchy mutations after traversal to avoid invalidating child lists
        // while ImGui is building the tree.
        for (const auto& pr : pendingReparents) {
            if (!m_Scene) break;
            if (pr.child == entt::null) continue;
            if (!registry.valid(pr.child)) continue;
            if (pr.afterParent != entt::null && !registry.valid(pr.afterParent)) continue;

            Entity beforeParent = entt::null;
            if (registry.all_of<Atlas::ECS::ParentComponent>(pr.child)) {
                beforeParent = registry.get<Atlas::ECS::ParentComponent>(pr.child).parent;
            }

            if (beforeParent == pr.afterParent) {
                continue;
            }

            m_Scene->setParent(pr.child, pr.afterParent);

            auto cmd = std::make_unique<ReparentCommand>();
            cmd->child = pr.child;
            cmd->beforeParent = beforeParent;
            cmd->afterParent = pr.afterParent;
            pushCommand(std::move(cmd));
        }

        Entity lastCreatedChild = entt::null;

        if (pendingCreateRoot) {
            lastCreatedChild = m_Scene->createEntity("Entity");
        }

        for (Entity parent : pendingCreateChildren) {
            if (parent == entt::null) continue;
            if (!registry.valid(parent)) continue;

            Entity child = m_Scene->createEntity("Entity");
            m_Scene->setParent(child, parent);
            lastCreatedChild = child;
        }

        if (lastCreatedChild != entt::null) {
            setSelectedEntity(lastCreatedChild);
        }

    }

    // Rename modal
    if (m_ShowHierarchyRenamePopup) {
        ImGui::OpenPopup("Rename Entity");
    }

    if (ImGui::BeginPopupModal("Rename Entity", &m_ShowHierarchyRenamePopup, ImGuiWindowFlags_AlwaysAutoResize)) {
        Entity e = static_cast<Entity>(m_HierarchyRenameEntityId);
        bool valid = m_Scene && m_Scene->getRegistry().valid(e);

        ImGui::BeginDisabled(!valid);
        ImGui::InputText("Name", m_HierarchyRenameBuf, sizeof(m_HierarchyRenameBuf));
        ImGui::EndDisabled();

        bool submit = ImGui::Button("OK");
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_ShowHierarchyRenamePopup = false;
        }

        if (submit && valid) {
            auto& reg = m_Scene->getRegistry();
            std::string after = std::string(m_HierarchyRenameBuf);

            std::string before;
            if (reg.all_of<Atlas::ECS::TagComponent>(e)) {
                before = reg.get<Atlas::ECS::TagComponent>(e).name;
                reg.get<Atlas::ECS::TagComponent>(e).name = after;
            } else {
                reg.emplace<Atlas::ECS::TagComponent>(e, after);
            }

            if (before != after) {
                auto cmd = std::make_unique<RenameCommand>();
                cmd->entity = e;
                cmd->before = before;
                cmd->after = after;
                pushCommand(std::move(cmd));
            }

            m_ShowHierarchyRenamePopup = false;
        }

        ImGui::EndPopup();
    }

    ImGui::End();
}

void UIManager::renderProperties() {
    ImGui::Begin("Properties", &m_ShowPropertiesWindow, ImGuiWindowFlags_NoCollapse);

    Entity selectedEntity = m_PrimarySelected;

    if (selectedEntity != entt::null && m_Scene && m_Scene->getRegistry().valid(selectedEntity)) {
        ImGui::Text("Entity ID: %u", static_cast<uint32_t>(selectedEntity));

        // Name / tag (undoable)
        {
            auto& registry = m_Scene->getRegistry();
            if (registry.all_of<Atlas::ECS::TagComponent>(selectedEntity)) {
                auto& tag = registry.get<Atlas::ECS::TagComponent>(selectedEntity);
                uint32_t sid = static_cast<uint32_t>(selectedEntity);

                if (m_PropNameEditEntityId != sid) {
                    m_PropNameEditEntityId = sid;
                    m_PropNameEditing = false;
                    m_PropNameBefore.clear();
                    std::memset(m_PropNameBuf, 0, sizeof(m_PropNameBuf));
                    if (!tag.name.empty()) {
                        std::strncpy(m_PropNameBuf, tag.name.c_str(), sizeof(m_PropNameBuf) - 1);
                    }
                }

                bool changed = ImGui::InputText("Name##entity", m_PropNameBuf, sizeof(m_PropNameBuf));
                if (ImGui::IsItemActivated()) {
                    m_PropNameEditing = true;
                    m_PropNameBefore = tag.name;
                }
                if (changed) {
                    tag.name = std::string(m_PropNameBuf);
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && m_PropNameEditing) {
                    m_PropNameEditing = false;
                    if (m_PropNameBefore != tag.name) {
                        auto cmd = std::make_unique<RenameCommand>();
                        cmd->entity = selectedEntity;
                        cmd->before = m_PropNameBefore;
                        cmd->after = tag.name;
                        pushCommand(std::move(cmd));
                    }
                }
            }
        }

bool isProtectedCameraEntity = m_Scene->getRegistry().all_of<EditorCamera>(selectedEntity);
        if (isProtectedCameraEntity) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Editor camera cannot be deleted here");
        }

        bool deleted = false;
        ImGui::BeginDisabled(isProtectedCameraEntity);
        if (ImGui::Button("Delete Entity")) {
            auto& registry = m_Scene->getRegistry();

            std::vector<Entity> subtree;
            subtree.reserve(32);

            const auto collect = [&](auto&& self, Entity e) -> void {
                if (e == entt::null) return;
                if (!registry.valid(e)) return;

                subtree.push_back(e);
                for (auto c : m_Scene->getChildren(e)) {
                    self(self, c);
                }
            };

            collect(collect, selectedEntity);

            for (Entity e : subtree) {
                if (e == entt::null || !registry.valid(e)) continue;
                registry.emplace_or_replace<Atlas::ECS::EditorHiddenComponent>(e, Atlas::ECS::EditorHiddenComponent{});
            }

            auto cmd = std::make_unique<SoftDeleteCommand>();
            cmd->entities = subtree;
            pushCommand(std::move(cmd));

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

        // Script inspector (V1)
        {
            auto& registry = m_Scene->getRegistry();

            if (!registry.all_of<Atlas::ECS::ScriptComponent>(selectedEntity)) {
                if (ImGui::Button("Add Script Component")) {
                    Atlas::ECS::ScriptComponent sc;
                    sc.scripts.push_back(Atlas::ECS::ScriptEntry{});
                    registry.emplace<Atlas::ECS::ScriptComponent>(selectedEntity, std::move(sc));
                    m_ScriptInspectError.clear();
                }
            } else if (ImGui::CollapsingHeader("Scripts", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto& sc = registry.get<Atlas::ECS::ScriptComponent>(selectedEntity);

                if (ImGui::Button("Add Script##entry")) {
                    sc.scripts.push_back(Atlas::ECS::ScriptEntry{});
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove All Scripts")) {
                    registry.remove<Atlas::ECS::ScriptComponent>(selectedEntity);
                    m_ScriptInspectError.clear();
                }

                if (!m_ScriptInspectError.empty()) {
                    ImGui::TextWrapped("Schema error: %s", m_ScriptInspectError.c_str());
                }

                for (size_t scriptIndex = 0; scriptIndex < sc.scripts.size();) {
                    auto& entry = sc.scripts[scriptIndex];
                    ImGui::PushID(static_cast<int>(scriptIndex));

                    std::string header = entry.scriptPath.empty()
                        ? (std::string("Script ") + std::to_string(scriptIndex + 1))
                        : entry.scriptPath;

                    bool removeEntry = false;
                    if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::Checkbox("Enabled", &entry.enabled);

                        char pathBuf[512] = {};
                        if (!entry.scriptPath.empty()) {
                            std::strncpy(pathBuf, entry.scriptPath.c_str(), sizeof(pathBuf) - 1);
                        }
                        if (ImGui::InputText("Script Path", pathBuf, sizeof(pathBuf))) {
                            entry.scriptPath = std::string(pathBuf);
                        }

                        if (ImGui::Button("Reload Schema")) {
                            std::string inspectPath = entry.scriptPath;
                            if (projectManager && projectManager->hasProject() && !inspectPath.empty() && !std::filesystem::path(inspectPath).is_absolute()) {
                                inspectPath = projectManager->getAssetFullPath(inspectPath);
                            }
                            auto defs = Atlas::Scripting::ScriptEngine::inspectScript(inspectPath, &m_ScriptInspectError);
                            Atlas::Scripting::ScriptEngine::syncComponentFields(defs, entry.fields);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Remove Script")) {
                            removeEntry = true;
                        }

                        for (auto& [name, value] : entry.fields) {
                            const Atlas::Scripting::ScriptFieldType type = Atlas::Scripting::getFieldType(value);
                            switch (type) {
                            case Atlas::Scripting::ScriptFieldType::Bool: {
                                bool v = std::get<bool>(value);
                                if (ImGui::Checkbox(name.c_str(), &v)) value = v;
                                break;
                            }
                            case Atlas::Scripting::ScriptFieldType::Int: {
                                int v = std::get<int>(value);
                                if (ImGui::DragInt(name.c_str(), &v, 1.0f)) value = v;
                                break;
                            }
                            case Atlas::Scripting::ScriptFieldType::Float: {
                                float v = std::get<float>(value);
                                if (ImGui::DragFloat(name.c_str(), &v, 0.1f)) value = v;
                                break;
                            }
                            case Atlas::Scripting::ScriptFieldType::String: {
                                char buf[256] = {};
                                const auto& s = std::get<std::string>(value);
                                std::strncpy(buf, s.c_str(), sizeof(buf) - 1);
                                if (ImGui::InputText(name.c_str(), buf, sizeof(buf))) value = std::string(buf);
                                break;
                            }
                            case Atlas::Scripting::ScriptFieldType::Vec2: {
                                auto v = std::get<glm::vec2>(value);
                                if (ImGui::DragFloat2(name.c_str(), &v.x, 0.1f)) value = v;
                                break;
                            }
                            case Atlas::Scripting::ScriptFieldType::Vec3: {
                                auto v = std::get<glm::vec3>(value);
                                if (ImGui::DragFloat3(name.c_str(), &v.x, 0.1f)) value = v;
                                break;
                            }
                            case Atlas::Scripting::ScriptFieldType::Vec4: {
                                auto v = std::get<glm::vec4>(value);
                                if (ImGui::DragFloat4(name.c_str(), &v.x, 0.1f)) value = v;
                                break;
                            }
                            default:
                                break;
                            }
                        }
                    }

                    ImGui::PopID();

                    if (removeEntry) {
                        sc.scripts.erase(sc.scripts.begin() + static_cast<std::ptrdiff_t>(scriptIndex));
                        continue;
                    }
                    ++scriptIndex;
                }

                if (sc.scripts.empty()) {
                    registry.remove<Atlas::ECS::ScriptComponent>(selectedEntity);
                }
            }
        }

        // Skeletal animation inspector (V1)
        {
            auto& registry = m_Scene->getRegistry();

            Entity skelEntity = entt::null;
            if (registry.all_of<Atlas::ECS::SkeletonComponent>(selectedEntity)) {
                skelEntity = selectedEntity;
            } else if (registry.all_of<Atlas::ECS::SkinnedMeshComponent>(selectedEntity)) {
                skelEntity = registry.get<Atlas::ECS::SkinnedMeshComponent>(selectedEntity).skeletonEntity;
            }

            if (skelEntity != entt::null && registry.valid(skelEntity) && registry.all_of<Atlas::ECS::SkeletonComponent>(skelEntity)) {
                if (ImGui::CollapsingHeader("Skeleton", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& skc = registry.get<Atlas::ECS::SkeletonComponent>(skelEntity);
                    Atlas::Anim::Skeleton* skel = skc.skeleton.get();

                    if (!skel) {
                        ImGui::TextUnformatted("No skeleton data");
                    } else {
                        ImGui::Text("Bones: %u", skel->boneCount());
                        ImGui::Text("Clips: %zu", skc.clips.size());

                        if (!registry.all_of<Atlas::ECS::AnimationPlayerComponent>(skelEntity)) {
                            registry.emplace<Atlas::ECS::AnimationPlayerComponent>(skelEntity);
                        }
                        auto& ap = registry.get<Atlas::ECS::AnimationPlayerComponent>(skelEntity).player;

                        if (!registry.all_of<Atlas::ECS::BonePoseOverrideComponent>(skelEntity)) {
                            registry.emplace<Atlas::ECS::BonePoseOverrideComponent>(skelEntity);
                        }
                        auto& ov = registry.get<Atlas::ECS::BonePoseOverrideComponent>(skelEntity);

                        // Edit pose toggle (pauses playback)
                        bool editPose = ov.enabled;
                        if (ImGui::Checkbox("Edit Pose", &editPose)) {
                            ov.enabled = editPose;
                            if (ov.enabled) {
                                ap.playing = false;
                                const uint32_t bc = skel->boneCount();
                                ov.hasRotation.assign(bc, uint8_t(0));
                                ov.rotation.assign(bc, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
                                m_RigEditEntity = skelEntity;
                                if (m_RigSelectedBone >= static_cast<int32_t>(bc)) {
                                    m_RigSelectedBone = (bc > 0) ? 0 : -1;
                                }
                            }
                        }

                        ImGui::SameLine();
                        ImGui::Checkbox("Show Skeleton", &m_RigShowSkeleton);

                        // Clip selection + playback controls
                        if (skc.clips.empty()) {
                            ImGui::TextUnformatted("No animation clips");
                            ap.clipIndex = -1;
                            ap.playing = false;
                        } else {
                            if (ap.clipIndex < 0 || static_cast<size_t>(ap.clipIndex) >= skc.clips.size()) {
                                ap.clipIndex = 0;
                            }

                            const char* preview = skc.clips[static_cast<size_t>(ap.clipIndex)].name.empty()
                                ? "<unnamed>"
                                : skc.clips[static_cast<size_t>(ap.clipIndex)].name.c_str();

                            if (ImGui::BeginCombo("Clip", preview)) {
                                for (int i = 0; i < static_cast<int>(skc.clips.size()); ++i) {
                                    const auto& clip = skc.clips[static_cast<size_t>(i)];
                                    const char* name = clip.name.empty() ? "<unnamed>" : clip.name.c_str();
                                    bool sel = (i == ap.clipIndex);
                                    if (ImGui::Selectable(name, sel)) {
                                        ap.clipIndex = i;
                                        ap.timeSeconds = 0.0f;
                                    }
                                    if (sel) ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }

                            const float dur = skc.clips[static_cast<size_t>(ap.clipIndex)].durationSeconds;

                            ImGui::BeginDisabled(ov.enabled);
                            ImGui::Checkbox("Playing", &ap.playing);
                            ImGui::SameLine();
                            ImGui::Checkbox("Loop", &ap.loop);
                            ImGui::EndDisabled();

                            const bool rootMotionToggled = ImGui::Checkbox("Enable Root Motion", &ap.enableRootMotion);
                            if (rootMotionToggled && ap.enableRootMotion && skel->rootMotionBoneIndex < 0) {
                                skel->rootMotionBoneIndex = Atlas::Anim::chooseRootMotionBone(*skel, skc.clips);
                            }

                            ImGui::BeginDisabled(!ap.enableRootMotion);
                            if (ImGui::BeginCombo("Root Motion Bone", (skel->rootMotionBoneIndex >= 0 && static_cast<size_t>(skel->rootMotionBoneIndex) < skel->boneNames.size()) ? skel->boneNames[static_cast<size_t>(skel->rootMotionBoneIndex)].c_str() : "<none>")) {
                                bool noneSelected = (skel->rootMotionBoneIndex < 0);
                                if (ImGui::Selectable("<none>", noneSelected)) {
                                    skel->rootMotionBoneIndex = -1;
                                }
                                if (noneSelected) ImGui::SetItemDefaultFocus();
                                for (uint32_t i = 0; i < skel->boneCount(); ++i) {
                                    const char* name = (i < skel->boneNames.size() && !skel->boneNames[i].empty()) ? skel->boneNames[i].c_str() : "<unnamed>";
                                    bool selected = (skel->rootMotionBoneIndex == static_cast<int32_t>(i));
                                    if (ImGui::Selectable(name, selected)) {
                                        skel->rootMotionBoneIndex = static_cast<int32_t>(i);
                                    }
                                    if (selected) ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Auto Detect")) {
                                skel->rootMotionBoneIndex = Atlas::Anim::chooseRootMotionBone(*skel, skc.clips);
                            }
                            ImGui::Checkbox("Root Motion Rotation", &ap.rootMotionApplyRotation);
                            ImGui::Checkbox("Root Motion Y", &ap.rootMotionApplyY);
                            ImGui::EndDisabled();

                            ImGui::DragFloat("Speed", &ap.speed, 0.05f, -5.0f, 5.0f, "%.2f");

                            if (dur > 0.0f) {
                                float t = ap.timeSeconds;
                                if (ImGui::SliderFloat("Time", &t, 0.0f, dur, "%.3f")) {
                                    ap.timeSeconds = t;
                                }
                            }
                        }

                        // Bone list
                        if (skel->boneCount() > 0) {
                            if (m_RigEditEntity != skelEntity) {
                                m_RigEditEntity = skelEntity;
                                m_RigSelectedBone = 0;
                            }

                            ImGui::SeparatorText("Bones");

                            const uint32_t bc = skel->boneCount();
                            if (m_RigSelectedBone >= static_cast<int32_t>(bc)) {
                                m_RigSelectedBone = 0;
                            }

                            ImGui::BeginChild("##bone_list", ImVec2(0, 180), true);
                            for (uint32_t i = 0; i < bc; ++i) {
                                int depth = 0;
                                int32_t p = (i < skel->parentIndex.size()) ? skel->parentIndex[i] : -1;
                                while (p >= 0 && depth < 32) {
                                    ++depth;
                                    p = (static_cast<uint32_t>(p) < skel->parentIndex.size()) ? skel->parentIndex[static_cast<uint32_t>(p)] : -1;
                                }

                                std::string label;
                                label.reserve(64);
                                for (int d = 0; d < depth; ++d) label += "  ";
                                if (i < skel->boneNames.size() && !skel->boneNames[i].empty()) label += skel->boneNames[i];
                                else label += ("Bone_" + std::to_string(i));

                                bool sel = (static_cast<int32_t>(i) == m_RigSelectedBone);
                                if (ImGui::Selectable(label.c_str(), sel)) {
                                    m_RigEditEntity = skelEntity;
                                    m_RigSelectedBone = static_cast<int32_t>(i);
                                }
                            }
                            ImGui::EndChild();

                            if (ov.enabled && m_RigSelectedBone >= 0) {
                                ImGui::BeginDisabled(m_RigSelectedBone < 0);
                                if (ImGui::Button("Reset Selected Bone")) {
                                    const uint32_t i = static_cast<uint32_t>(m_RigSelectedBone);
                                    if (i < ov.hasRotation.size()) ov.hasRotation[i] = 0;
                                }
                                ImGui::EndDisabled();
                            }
                        }
                    }
                }
            }
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
                            m_MatScalarEditing = false;
                            m_MatScalarEntity = entt::null;

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

                        auto scalarFromMat = [&](const Atlas::ECS::MaterialComponent& m) -> MaterialScalarState {
                            MaterialScalarState s;
                            s.baseColor = m.baseColor;
                            s.metallic = m.metallic;
                            s.roughness = m.roughness;
                            s.ambientOcclusion = m.ambientOcclusion;
                            s.emissiveFactor = m.emissiveFactor;
                            s.alphaMode = m.alphaMode;
                            s.alphaCutoff = m.alphaCutoff;
                            s.doubleSided = m.doubleSided;
                            s.invertCulling = m.invertCulling;
                            return s;
                        };

                        auto scalarDifferent = [&](const MaterialScalarState& a, const MaterialScalarState& b) -> bool {
                            const float eps = 1e-4f;
                            return glm::length(a.baseColor - b.baseColor) > eps ||
                                   std::abs(a.metallic - b.metallic) > eps ||
                                   std::abs(a.roughness - b.roughness) > eps ||
                                   std::abs(a.ambientOcclusion - b.ambientOcclusion) > eps ||
                                   glm::length(a.emissiveFactor - b.emissiveFactor) > eps ||
                                   a.alphaMode != b.alphaMode ||
                                   std::abs(a.alphaCutoff - b.alphaCutoff) > eps ||
                                   a.doubleSided != b.doubleSided ||
                                   a.invertCulling != b.invertCulling;
                        };

                        auto beginMatScalarEdit = [&]() {
                            if (!m_MatScalarEditing || m_MatScalarEntity != matEntity) {
                                m_MatScalarEditing = true;
                                m_MatScalarEntity = matEntity;
                                m_MatScalarBefore = scalarFromMat(mat);
                            }
                        };

                        auto endMatScalarEdit = [&]() {
                            if (!m_MatScalarEditing || m_MatScalarEntity != matEntity) return;
                            MaterialScalarState after = scalarFromMat(mat);
                            if (scalarDifferent(m_MatScalarBefore, after)) {
                                auto cmd = std::make_unique<MaterialScalarCommand>();
                                cmd->entity = matEntity;
                                cmd->before = m_MatScalarBefore;
                                cmd->after = after;
                                pushCommand(std::move(cmd));
                            }
                            m_MatScalarEditing = false;
                            m_MatScalarEntity = entt::null;
                        };

                        ImGui::SeparatorText("Surface");
                        ImGui::ColorEdit4("Base Color##Mat", &mat.baseColor.x, ImGuiColorEditFlags_Float);
                        if (ImGui::IsItemActivated()) beginMatScalarEdit();
                        if (ImGui::IsItemDeactivatedAfterEdit()) endMatScalarEdit();

                        ImGui::DragFloat("Metallic##Mat", &mat.metallic, 0.01f, 0.0f, 1.0f);
                        if (ImGui::IsItemActivated()) beginMatScalarEdit();
                        if (ImGui::IsItemDeactivatedAfterEdit()) endMatScalarEdit();

                        ImGui::DragFloat("Roughness##Mat", &mat.roughness, 0.01f, 0.0f, 1.0f);
                        if (ImGui::IsItemActivated()) beginMatScalarEdit();
                        if (ImGui::IsItemDeactivatedAfterEdit()) endMatScalarEdit();

                        ImGui::DragFloat("AO##Mat", &mat.ambientOcclusion, 0.01f, 0.0f, 1.0f);
                        if (ImGui::IsItemActivated()) beginMatScalarEdit();
                        if (ImGui::IsItemDeactivatedAfterEdit()) endMatScalarEdit();

                        ImGui::ColorEdit3("Emissive Factor##Mat", &mat.emissiveFactor.x, ImGuiColorEditFlags_Float);
                        if (ImGui::IsItemActivated()) beginMatScalarEdit();
                        if (ImGui::IsItemDeactivatedAfterEdit()) endMatScalarEdit();

                        ImGui::SeparatorText("Alpha");
                        const char* alphaItems[] = {"Opaque", "Mask", "Blend"};
                        int alphaIdx = (mat.alphaMode == Atlas::ECS::MaterialComponent::AlphaMode::Mask) ? 1 :
                                       (mat.alphaMode == Atlas::ECS::MaterialComponent::AlphaMode::Blend) ? 2 : 0;
                        if (ImGui::Combo("Alpha Mode##Mat", &alphaIdx, alphaItems, 3)) {
                            if (alphaIdx == 1) mat.alphaMode = Atlas::ECS::MaterialComponent::AlphaMode::Mask;
                            else if (alphaIdx == 2) mat.alphaMode = Atlas::ECS::MaterialComponent::AlphaMode::Blend;
                            else mat.alphaMode = Atlas::ECS::MaterialComponent::AlphaMode::Opaque;
                        }
                        if (ImGui::IsItemActivated()) beginMatScalarEdit();
                        if (ImGui::IsItemDeactivatedAfterEdit()) endMatScalarEdit();

                        if (mat.alphaMode == Atlas::ECS::MaterialComponent::AlphaMode::Mask) {
                            ImGui::DragFloat("Alpha Cutoff##Mat", &mat.alphaCutoff, 0.01f, 0.0f, 1.0f);
                            if (ImGui::IsItemActivated()) beginMatScalarEdit();
                            if (ImGui::IsItemDeactivatedAfterEdit()) endMatScalarEdit();
                        }

                        if (ImGui::Checkbox("Double Sided##Mat", &mat.doubleSided)) {
                            beginMatScalarEdit();
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit()) endMatScalarEdit();

                        ImGui::SameLine();
                        if (ImGui::Checkbox("Invert Culling##Mat", &mat.invertCulling)) {
                            beginMatScalarEdit();
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit()) endMatScalarEdit();

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
                                auto file = Atlas::Platform::openFileDialog(label, ".");
                                if (file) {
                                    std::memset(pathBuf, 0, pathBufSize);
                                    std::strncpy(pathBuf, file->c_str(), pathBufSize - 1);
                                    picked = true;
                                }
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

        auto& registry = m_Scene->getRegistry();
        if (m_Scene->getRegistry().all_of<Camera>(selectedEntity) || m_Scene->getRegistry().all_of<EditorCamera>(selectedEntity)) {
            const bool isRuntimeCamera = registry.all_of<Camera>(selectedEntity);
            const bool isEditorCamera = registry.all_of<EditorCamera>(selectedEntity);

            if (isRuntimeCamera && ImGui::CollapsingHeader("Game Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
                bool hasGameCamera = registry.all_of<Atlas::ECS::GameCameraComponent>(selectedEntity);
                if (!hasGameCamera) {
                    if (ImGui::Button("Add Game Camera Component")) {
                        Atlas::ECS::GameCameraComponent gcc;
                        auto gameCameraView = registry.view<Camera, Atlas::ECS::GameCameraComponent>();
                        gcc.primary = (gameCameraView.begin() == gameCameraView.end());
                        registry.emplace<Atlas::ECS::GameCameraComponent>(selectedEntity, gcc);
                    }
                } else {
                    auto& gcc = registry.get<Atlas::ECS::GameCameraComponent>(selectedEntity);
                    bool primaryInPlay = gcc.primary;
                    if (ImGui::Checkbox("Primary In Play Mode", &primaryInPlay)) {
                        gcc.primary = primaryInPlay;
                        if (gcc.primary) {
                            auto gameCameraView = registry.view<Camera, Atlas::ECS::GameCameraComponent>();
                            for (auto e : gameCameraView) {
                                if (e != selectedEntity) {
                                    gameCameraView.get<Atlas::ECS::GameCameraComponent>(e).primary = false;
                                }
                            }
                        }
                    }
                    if (ImGui::Button("Remove Game Camera")) {
                        registry.remove<Atlas::ECS::GameCameraComponent>(selectedEntity);
                    }
                }
            }

            if (isRuntimeCamera && ImGui::CollapsingHeader("Follow Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
                bool hasFollow = registry.all_of<Atlas::ECS::FollowCameraComponent>(selectedEntity);

                if (!hasFollow) {
                    if (ImGui::Button("Add Follow Camera")) {
                        registry.emplace<Atlas::ECS::FollowCameraComponent>(selectedEntity);
                    }
                } else {
                    auto& follow = registry.get<Atlas::ECS::FollowCameraComponent>(selectedEntity);

                    std::string targetLabel = "None";
                    if (follow.target != entt::null && registry.valid(follow.target) && registry.all_of<Atlas::ECS::TagComponent>(follow.target)) {
                        targetLabel = registry.get<Atlas::ECS::TagComponent>(follow.target).name;
                    }
                    ImGui::Text("Target: %s", targetLabel.c_str());

                    int targetId = (follow.target == entt::null) ? -1 : static_cast<int>(static_cast<uint32_t>(follow.target));
                    if (ImGui::InputInt("Target Entity ID", &targetId)) {
                        if (targetId < 0) {
                            follow.target = entt::null;
                        } else {
                            Entity candidate = static_cast<Entity>(static_cast<uint32_t>(targetId));
                            follow.target = registry.valid(candidate) ? candidate : entt::null;
                        }
                    }

                    Entity otherSelected = entt::null;
                    for (Entity e : m_SelectedEntities) {
                        if (e != selectedEntity && registry.valid(e)) {
                            otherSelected = e;
                            break;
                        }
                    }
                    if (ImGui::Button("Use Other Selected") && otherSelected != entt::null) {
                        follow.target = otherSelected;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Clear Target")) {
                        follow.target = entt::null;
                    }

                    ImGui::DragFloat3("Offset", &follow.offset.x, 0.1f);
                    ImGui::DragFloat("Smoothness", &follow.smoothness, 0.1f, 0.0f, 30.0f, "%.2f");
                    ImGui::Checkbox("Look At Target", &follow.lookAtTarget);

                    if (ImGui::Button("Remove Follow Camera")) {
                        registry.remove<Atlas::ECS::FollowCameraComponent>(selectedEntity);
                    }
                }
            }

            if (isRuntimeCamera) {
                renderComponent(m_Scene->getRegistry().get<Camera>(selectedEntity), "Runtime Camera");
            } else if (isEditorCamera) {
                renderComponent(m_Scene->getRegistry().get<EditorCamera>(selectedEntity), "Editor Camera");
            }
        }

        if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
            const bool hasRigidBody = registry.all_of<Atlas::ECS::RigidBodyComponent>(selectedEntity);
            const bool hasBox = registry.all_of<Atlas::ECS::BoxColliderComponent>(selectedEntity);
            const bool hasSphere = registry.all_of<Atlas::ECS::SphereColliderComponent>(selectedEntity);
            const bool hasCapsule = registry.all_of<Atlas::ECS::CapsuleColliderComponent>(selectedEntity);
            const bool hasAnyCollider = hasBox || hasSphere || hasCapsule;

            if (!hasRigidBody) {
                if (ImGui::Button("Add Rigid Body")) {
                    registry.emplace<Atlas::ECS::RigidBodyComponent>(selectedEntity);
                }
            } else {
                auto& rb = registry.get<Atlas::ECS::RigidBodyComponent>(selectedEntity);
                const char* motionItems[] = {"Static", "Dynamic", "Kinematic"};
                int motion = static_cast<int>(rb.motionType);
                if (ImGui::Combo("Motion Type", &motion, motionItems, IM_ARRAYSIZE(motionItems))) {
                    rb.motionType = static_cast<Atlas::ECS::PhysicsMotionType>(motion);
                }
                ImGui::DragFloat("Friction", &rb.friction, 0.01f, 0.0f, 2.0f, "%.2f");
                ImGui::DragFloat("Restitution", &rb.restitution, 0.01f, 0.0f, 2.0f, "%.2f");
                ImGui::DragFloat("Linear Damping", &rb.linearDamping, 0.01f, 0.0f, 10.0f, "%.2f");
                ImGui::DragFloat("Angular Damping", &rb.angularDamping, 0.01f, 0.0f, 10.0f, "%.2f");
                ImGui::DragFloat("Gravity Scale", &rb.gravityScale, 0.01f, -10.0f, 10.0f, "%.2f");
                ImGui::Checkbox("Continuous Collision", &rb.continuous);
                ImGui::Checkbox("Allow Sleep", &rb.allowSleep);
                if (ImGui::Button("Remove Rigid Body")) {
                    registry.remove<Atlas::ECS::RigidBodyComponent>(selectedEntity);
                }
            }

            ImGui::SeparatorText("Collider");
            if (!hasAnyCollider) {
                if (ImGui::Button("Add Box Collider")) {
                    Atlas::ECS::BoxColliderComponent c;
                    if (registry.all_of<::Mesh>(selectedEntity)) {
                        const auto& mesh = registry.get<::Mesh>(selectedEntity);
                        if (mesh.hasBounds) {
                            c.halfExtent = glm::max((mesh.boundsMax - mesh.boundsMin) * 0.5f, glm::vec3(0.01f));
                        }
                    }
                    registry.emplace<Atlas::ECS::BoxColliderComponent>(selectedEntity, c);
                }
                ImGui::SameLine();
                if (ImGui::Button("Add Sphere Collider")) {
                    registry.emplace<Atlas::ECS::SphereColliderComponent>(selectedEntity);
                }
                ImGui::SameLine();
                if (ImGui::Button("Add Capsule Collider")) {
                    registry.emplace<Atlas::ECS::CapsuleColliderComponent>(selectedEntity);
                }
            }

            if (hasBox) {
                auto& c = registry.get<Atlas::ECS::BoxColliderComponent>(selectedEntity);
                ImGui::DragFloat3("Box Half Extent", &c.halfExtent.x, 0.01f, 0.01f, 100.0f, "%.2f");
                ImGui::DragFloat3("Box Offset", &c.offset.x, 0.01f, -100.0f, 100.0f, "%.2f");
                ImGui::Checkbox("Box Trigger", &c.isTrigger);
                if (ImGui::Button("Remove Box Collider")) {
                    registry.remove<Atlas::ECS::BoxColliderComponent>(selectedEntity);
                }
            }

            if (hasSphere) {
                auto& c = registry.get<Atlas::ECS::SphereColliderComponent>(selectedEntity);
                ImGui::DragFloat("Sphere Radius", &c.radius, 0.01f, 0.01f, 100.0f, "%.2f");
                ImGui::DragFloat3("Sphere Offset", &c.offset.x, 0.01f, -100.0f, 100.0f, "%.2f");
                ImGui::Checkbox("Sphere Trigger", &c.isTrigger);
                if (ImGui::Button("Remove Sphere Collider")) {
                    registry.remove<Atlas::ECS::SphereColliderComponent>(selectedEntity);
                }
            }

            if (hasCapsule) {
                auto& c = registry.get<Atlas::ECS::CapsuleColliderComponent>(selectedEntity);
                ImGui::DragFloat("Capsule Radius", &c.radius, 0.01f, 0.01f, 100.0f, "%.2f");
                ImGui::DragFloat("Capsule Half Height", &c.halfHeight, 0.01f, 0.01f, 100.0f, "%.2f");
                ImGui::DragFloat3("Capsule Offset", &c.offset.x, 0.01f, -100.0f, 100.0f, "%.2f");
                ImGui::Checkbox("Capsule Trigger", &c.isTrigger);
                if (ImGui::Button("Remove Capsule Collider")) {
                    registry.remove<Atlas::ECS::CapsuleColliderComponent>(selectedEntity);
                }
            }
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

void UIManager::renderContentExplorer() 
{
    ImGui::Begin("Content Explorer", &m_ShowContentExplorerWindow, ImGuiWindowFlags_NoCollapse);

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

        if (!ec && projectManager) {
            projectManager->invalidateAssetTreeCache();
        }
    };

    auto drawCtxMenu = [&](bool hasTarget) {
        if (hasTarget) {
            if (ImGui::MenuItem("Open")) {
                if (m_ContentCtxTarget.isFolder) {
                    folderStack.push_back(m_ContentCtxTarget.name);
                } else {
                    std::string ext = fs::path(m_ContentCtxTarget.name).extension().string();
                    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                    if (ext == ".scene") {
                        if (onOpenSceneAsset) {
                            onOpenSceneAsset(m_ContentCtxTarget.relativePath);
                        }
                    }
#ifdef _WIN32
                    else {
                        openWithDefaultApp(m_ContentCtxTarget.fullPath);
                    }
#endif
                }
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
            if (!ec && projectManager) {
                projectManager->invalidateAssetTreeCache();
            }
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
        bool hasItems = !currentFolder.children.empty();

        auto drawTile = [&](const ::ProjectManager::FileEntry& child) {
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
                const bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                if (doubleClicked) {
                    std::string ext = fs::path(child.name).extension().string();
                    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (ext == ".scene") {
                        if (onOpenSceneAsset) {
                            onOpenSceneAsset(child.relativePath);
                        }
                    }
#ifdef _WIN32
                    else {
                        openWithDefaultApp(child.fullPath);
                    }
#endif
                }

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("ASSET_DROP", child.relativePath.c_str(), child.relativePath.length() + 1);
                    ImGui::TextUnformatted(child.name.c_str());
                    ImGui::EndDragDropSource();
                }
            }

            ImGui::PopID();
        };

        if (hasItems) {
            const int itemCount = static_cast<int>(currentFolder.children.size());
            const int rows = (itemCount + cols - 1) / cols;

            ImGuiListClipper clipper;
            clipper.Begin(rows);
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    ImGui::TableNextRow();
                    for (int col = 0; col < cols; ++col) {
                        const int idx = row * cols + col;
                        ImGui::TableSetColumnIndex(col);
                        if (idx >= itemCount) {
                            continue;
                        }
                        drawTile(currentFolder.children[static_cast<size_t>(idx)]);
                    }
                }
            }
            clipper.End();
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
            if (!ec && projectManager) {
                projectManager->invalidateAssetTreeCache();
            }
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
            if (!ec && projectManager) {
                projectManager->invalidateAssetTreeCache();
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
                auto folder = Atlas::Platform::openFolderDialog("Open Project Folder", ".");
                if (folder && projectManager) {
                    projectManager->openProject(*folder);
                    if (onOpenProject) onOpenProject();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Project", "Ctrl+S")) {
                if (onSaveProject) onSaveProject();
            }
            if (ImGui::BeginMenu("Export Game")) {
                if (ImGui::MenuItem("Windows")) {
                    if (onExportGame) onExportGame();
                }
                if (ImGui::MenuItem("Linux")) {
                    if (onExportGameLinux) onExportGameLinux();
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                if (onExit) onExit();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("File")) {
            if (projectManager && projectManager->hasProject()) {
                if (ImGui::MenuItem("New Scene")) {
                    if (onNewScene) onNewScene();
                }
                if (ImGui::MenuItem("Load Scene...")) {
                    std::vector<Atlas::Platform::FileDialogFilter> filters;
                    filters.push_back({"Scenes", {"scene"}});

                    auto file = Atlas::Platform::openFileDialog("Load Scene", projectManager->getAssetsPath(), filters);
                    if (file && onOpenSceneAsset) {
                        std::error_code ec;
                        std::filesystem::path assetsRoot(projectManager->getAssetsPath());
                        std::filesystem::path sceneFull(*file);
                        assetsRoot = assetsRoot.lexically_normal();
                        sceneFull = sceneFull.lexically_normal();
                        std::filesystem::path rel = std::filesystem::relative(sceneFull, assetsRoot, ec);
                        if (!ec && !rel.empty()) {
                            onOpenSceneAsset(rel.generic_string());
                        }
                    }
                }
                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                    if (onSaveProject) onSaveProject();
                }
                ImGui::Separator();
            }

            if (ImGui::MenuItem("Import Model...", "Ctrl+I")) {
            }
            if (ImGui::MenuItem("Import Texture...", "Ctrl+T")) {
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Viewport", NULL, &m_ShowViewportWindow);
            ImGui::MenuItem("Game", NULL, &m_ShowGameViewportWindow);
            ImGui::MenuItem("Console", NULL, &m_ShowConsoleWindow);
            ImGui::MenuItem("Hierarchy", NULL, &m_ShowHierarchyWindow);
            ImGui::MenuItem("Properties", NULL, &m_ShowPropertiesWindow);
            ImGui::MenuItem("Content Explorer", NULL, &m_ShowContentExplorerWindow);
            ImGui::MenuItem("World Streaming", NULL, &m_ShowWorldStreamingWindow);
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

void UIManager::renderConsoleWindow()
{
    if (!m_ShowConsoleWindow) return;

    ImGui::Begin("Console", &m_ShowConsoleWindow, ImGuiWindowFlags_NoCollapse);

    if (ImGui::Button("Clear")) {
        Atlas::RuntimeConsole::instance().clear();
    }
    ImGui::Separator();

    const auto entries = Atlas::RuntimeConsole::instance().snapshot();
    ImGui::BeginChild("console_scroll");
    for (const auto& entry : entries) {
        ImVec4 color(0.85f, 0.87f, 0.90f, 1.0f);
        if (entry.level == Atlas::RuntimeConsole::Level::Warn) {
            color = ImVec4(0.95f, 0.75f, 0.25f, 1.0f);
        } else if (entry.level == Atlas::RuntimeConsole::Level::Error) {
            color = ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
        }
        ImGui::TextColored(color, "%s", entry.text.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    ImGui::End();
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
            auto folder = Atlas::Platform::openFolderDialog("Select Project Folder", ".");
            if (folder) {
                std::memset(newProjectPath, 0, IM_ARRAYSIZE(newProjectPath));
                std::strncpy(newProjectPath, folder->c_str(), IM_ARRAYSIZE(newProjectPath) - 1);
            }
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
