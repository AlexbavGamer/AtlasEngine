// HLODViewerLayer — per-entity HLOD/LOD inspector table + summary.
#include "hlod_viewer_layer.h"

#include "editor_app.h"

#include <imgui.h>

#include <cstdint>
#include <string>

namespace Atlas {

namespace {

const char* hlodLevelName(HLODLevel level) {
    switch (level) {
    case HLODLevel::FullDetail: return "Full";
    case HLODLevel::HLOD0: return "HLOD0";
    case HLODLevel::HLOD1: return "HLOD1";
    default: return "?";
    }
}

const char* lodLevelName(uint8_t level) {
    switch (static_cast<LODLevel>(level)) {
    case LODLevel::LOD0: return "LOD0";
    case LODLevel::LOD1: return "LOD1";
    case LODLevel::LOD2: return "LOD2";
    case LODLevel::Impostor: return "Impostor";
    case LODLevel::Culled: return "Culled";
    default: return "?";
    }
}

} // namespace

HLODViewerLayer::HLODViewerLayer(EditorApp* app)
    : m_App(app) {}

void HLODViewerLayer::renderUI() {
    EditorApp* app = m_App;
    if (!app || !app->m_UIManager || !app->m_Scene) {
        return;
    }
    if (!app->m_UIManager->m_ShowHLODViewerWindow) {
        return;
    }

    ImGui::Begin("HLOD Viewer", &app->m_UIManager->m_ShowHLODViewerWindow);

    auto& registry = app->m_Scene->getRegistry();

    // ---- Summary ----
    // HLOD is opt-in (Add Component > HLOD); LOD is auto-added at import.
    // The viewer tracks the union so fresh imports show up immediately.
    uint32_t fullCount = 0;
    uint32_t hlod0Count = 0;
    uint32_t hlod1Count = 0;
    uint32_t lodOnlyCount = 0;
    uint32_t rowCount = 0;
    for (auto e : app->m_Scene->getAllEntities()) {
        if (!registry.valid(e)) continue;
        const bool hasHlod = registry.all_of<ECS::HLODComponent>(e);
        const bool hasLod = registry.all_of<LODComponent>(e);
        if (!hasHlod && !hasLod) continue;
        if (hasHlod) {
            const auto& hc = registry.get<ECS::HLODComponent>(e);
            if (hc.currentLevel == HLODLevel::FullDetail) ++fullCount;
            else if (hc.currentLevel == HLODLevel::HLOD0) ++hlod0Count;
            else ++hlod1Count;
        } else {
            ++lodOnlyCount;
        }
        ++rowCount;
    }
    ImGui::Text("Tracked: %u  (HLOD Full %u / HLOD0 %u / HLOD1 %u / LOD-only %u)", rowCount, fullCount,
                hlod0Count, hlod1Count, lodOnlyCount);
    if (app->m_HLODSystem) {
        ImGui::SameLine();
        ImGui::TextDisabled("|  Cache cells: %zu", app->m_HLODSystem->getCache().size());
    }

    // ---- Filters ----
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##hlodfilter", "Search entity...", m_Filter, sizeof(m_Filter));
    ImGui::Checkbox("Only degraded", &m_OnlyDegraded);
    ImGui::SameLine();
    ImGui::TextDisabled("(hides Full detail)");

    ImGui::Separator();

    // ---- Table ----
    if (ImGui::BeginTable("##hlod_table", 6,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Entity", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("HLOD", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Bias", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("LOD", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        for (auto e : app->m_Scene->getAllEntities()) {
            if (!registry.valid(e)) continue;
            const bool hasHlod = registry.all_of<ECS::HLODComponent>(e);
            const bool hasLod = registry.all_of<LODComponent>(e);
            if (!hasHlod && !hasLod) continue;

            const auto* lodPre = registry.try_get<LODComponent>(e);
            const bool degraded =
                (hasHlod && registry.get<ECS::HLODComponent>(e).currentLevel != HLODLevel::FullDetail) ||
                (hasLod && lodPre && lodPre->current != static_cast<uint8_t>(LODLevel::LOD0));
            if (m_OnlyDegraded && !degraded) continue;

            std::string name;
            if (registry.all_of<ECS::TagComponent>(e)) {
                name = registry.get<ECS::TagComponent>(e).name;
            } else {
                name = "Entity " + std::to_string(static_cast<uint32_t>(e));
            }
            if (m_Filter[0] != '\0' &&
                !::Atlas::InspectorUI::PassesFilter(m_Filter, name.c_str())) {
                continue;
            }

            ImGui::PushID(static_cast<int>(static_cast<uint32_t>(e)));
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                app->m_UIManager->setSelectedEntity(static_cast<Entity>(e));
            }

            ImGui::TableSetColumnIndex(1);
            if (hasHlod) {
                ImGui::TextUnformatted(hlodLevelName(registry.get<ECS::HLODComponent>(e).currentLevel));
            } else {
                ImGui::TextDisabled("(none)");
            }
            ImGui::TableSetColumnIndex(2);
            if (hasHlod) {
                ImGui::TextUnformatted(hlodLevelName(registry.get<ECS::HLODComponent>(e).targetLevel));
            } else {
                ImGui::TextDisabled("-");
            }
            ImGui::TableSetColumnIndex(3);
            if (hasHlod) {
                auto& hc = registry.get<ECS::HLODComponent>(e);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::DragFloat("##bias", &hc.screenSizeBias, 0.05f, 0.1f, 8.0f, "%.2f");
            } else if (ImGui::SmallButton("Add HLOD")) {
                registry.emplace<ECS::HLODComponent>(e);
            }

            ImGui::TableSetColumnIndex(4);
            if (const auto* lod = registry.try_get<LODComponent>(e)) {
                ImGui::Text("%s (%.2f)", lodLevelName(lod->current), lod->screenSize);
            } else {
                ImGui::TextDisabled("(no LOD)");
            }
            ImGui::TableSetColumnIndex(5);
            if (const auto* lod = registry.try_get<LODComponent>(e)) {
                ImGui::Text("%.1fm", lod->distance);
            } else {
                ImGui::TextDisabled("-");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace Atlas
