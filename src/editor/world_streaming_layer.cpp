// WorldStreamingLayer — "World Streaming" debug window, extracted from
// EditorApp::run(). Operates on EditorApp state via friend access; owns none.
#include "world_streaming_layer.h"

#include "editor_app.h"

#include <imgui.h>

#include <filesystem>
#include <iostream>

namespace Atlas {

WorldStreamingLayer::WorldStreamingLayer(EditorApp* app)
    : m_App(app) {}

void WorldStreamingLayer::renderUI() {
    EditorApp* app = m_App;
    if (!app || !app->m_UIManager) {
        return;
    }
    if (!app->m_UIManager->m_ShowWorldStreamingWindow) {
        return;
    }

    ImGui::Begin("World Streaming", &app->m_UIManager->m_ShowWorldStreamingWindow);
    ImGui::Checkbox("Enabled", &app->m_WorldStreamingEnabled);
    ImGui::DragFloat("Cell Size", &app->m_WorldCellSize, 1.0f, 1.0f, 8192.0f, "%.1f");
    ImGui::SliderInt("Load Radius (cells)", &app->m_WorldLoadRadius, 0, 16);
    ImGui::Text("Active: %zu", app->m_WorldActiveCells.size());
    ImGui::Text("Loading: %zu", app->m_WorldLoadingCells.size());
    ImGui::DragFloat("Fail retry (sec)", &app->m_WorldFailRetrySeconds, 0.1f, 0.0f, 30.0f, "%.1f");
    ImGui::Text("Failed: %zu", app->m_WorldFailedCells.size());
    if (ImGui::Button("Clear Failed")) {
        app->m_WorldFailedCells.clear();
    }

    if (app->m_WorldPartition) {
        auto& cfg = app->m_WorldPartition->config();
        ImGui::SeparatorText("Partition Culling");
        ImGui::Checkbox("Enabled##partition", &cfg.enabled);
        ImGui::Checkbox("Frustum Culling##partition", &cfg.useFrustumCulling);
        ImGui::DragFloat("Cell Size##partition", &cfg.cellSize, 1.0f, 1.0f, 8192.0f, "%.1f");
        ImGui::SliderInt("Load Radius##partition", &cfg.loadRadiusCells, 0, 16);
        ImGui::DragFloat("Load Range (m)", &cfg.loadRangeMeters, 5.0f, 0.0f, 5000.0f, "%.0f");
        ImGui::DragFloat("HLOD0 Range (m)", &cfg.hlod0RangeMeters, 10.0f, 0.0f, 8000.0f, "%.0f");
        ImGui::DragFloat("HLOD1 Range (m)", &cfg.hlod1RangeMeters, 25.0f, 0.0f, 20000.0f, "%.0f");
        if (ImGui::Button("Rebuild##partition")) {
            app->m_WorldPartition->markDirty();
        }
        ImGui::Text("Loaded cells: %zu / %zu", app->m_WorldPartition->getLoadedCells().size(),
                    app->m_WorldPartition->getCells().size());
        ImGui::Text("Loaded geometry: %.1f MB", app->m_WorldPartition->getLoadedMemoryMB());
        ImGui::DragFloat("Memory budget (MB)", &cfg.maxLoadedMemoryMB, 8.0f, 0.0f, 8192.0f, "%.0f");
        ImGui::SeparatorText("Disk cache (§9)");
        if (ImGui::Button("Save cell+HLOD cache")) {
            if (app->m_ProjectManager && app->m_ProjectManager->hasProject() && app->m_HLODSystem) {
                const std::string dir = app->m_ProjectManager->getAssetsPath() + "/world/cache";
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                const bool okCells = app->m_WorldPartition->saveCellIndex(dir + "/cells.idx");
                const bool okHlod = app->m_HLODSystem->saveCache(dir + "/hlod_cache.bin");
                std::cerr << "[CACHE] save cells=" << okCells << " hlod=" << okHlod << " -> " << dir << std::endl;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load cell+HLOD cache")) {
            if (app->m_ProjectManager && app->m_ProjectManager->hasProject() && app->m_HLODSystem) {
                const std::string dir = app->m_ProjectManager->getAssetsPath() + "/world/cache";
                std::error_code ec;
                const size_t cells = app->m_WorldPartition->loadCellIndex(dir + "/cells.idx");
                const size_t hlodCells = app->m_HLODSystem->loadCache(dir + "/hlod_cache.bin");
                std::cerr << "[CACHE] load cells=" << cells << " hlodCells=" << hlodCells << std::endl;
            }
        }
    }

    ImGui::SeparatorText("Culling Pipeline");
    ImGui::Checkbox("Frustum##cull", &app->m_CullingConfig.enableFrustum);
    ImGui::SameLine();
    ImGui::Checkbox("Distance##cull", &app->m_CullingConfig.enableDistance);
    ImGui::SameLine();
    ImGui::Checkbox("ScreenSize##cull", &app->m_CullingConfig.enableScreenSize);
    ImGui::Checkbox("Occlusion (software)##cull", &app->m_OcclusionConfig.enable);
    ImGui::Text("Cells: %u tested / %u culled", app->m_LastCullingStats.cellsTested, app->m_LastCullingStats.cellsCulled);
    ImGui::Text("Occluders: %u / box tests %u / culled %u", app->m_OcclusionCuller.getStats().occludersUsed,
                app->m_OcclusionCuller.getStats().boxTests, app->m_OcclusionCuller.getStats().boxesCulled);
    ImGui::Text("Objects: %u tested / %u culled (%u tiny)", app->m_LastCullingStats.objectsTested,
                app->m_LastCullingStats.objectsCulled, app->m_LastCullingStats.screenSizeCulled);

    ImGui::SeparatorText("LOD (screen-size)");
    ImGui::Checkbox("LOD##lod", &app->m_LODConfig.enable);
    ImGui::DragFloat("Impostor threshold", &app->m_LODConfig.impostorScreenSize, 0.001f, 0.001f, 0.2f, "%.3f");
    ImGui::Text("LOD0 %u / LOD1 %u / LOD2 %u", app->m_LastLODStats.lod0Count, app->m_LastLODStats.lod1Count,
                app->m_LastLODStats.lod2Count);
    ImGui::Text("Impostor %u / Culled %u", app->m_LastLODStats.impostorCount, app->m_LastLODStats.culledCount);

    ImGui::SeparatorText("HLOD");
    ImGui::Checkbox("HLOD##hlod", &app->m_HLODConfig.enableHLOD);
    if (app->m_HLODSystem) {
        ImGui::Text("Cached cells: %zu", app->m_HLODSystem->getCache().size());
    }

    ImGui::SeparatorText("Rendering (TDD §12)");
    if (app->m_Renderer) {
        bool inst = app->m_Renderer->isInstancingEnabled();
        if (ImGui::Checkbox("GPU Instancing", &inst)) {
            app->m_Renderer->setInstancingEnabled(inst);
        }
        ImGui::Text("Draw calls: %u (instanced %u)", app->m_Renderer->getLastDrawCalls(),
                    app->m_Renderer->getLastInstancedDraws());
        ImGui::Text("Instances: %u / Tris: %u", app->m_Renderer->getLastInstancedInstances(),
                    app->m_Renderer->getLastTriangles());
        bool autoLOD = app->m_Renderer->isAutoLODEnabled();
        if (ImGui::Checkbox("Auto LOD meshes", &autoLOD)) {
            app->m_Renderer->setAutoLODEnabled(autoLOD);
        }
        ImGui::Text("Simplified variants: %u / draws: %u", app->m_Renderer->getSimplifiedVariantCount(),
                    app->m_Renderer->getLastSimplifiedDraws());
        ImGui::SeparatorText("Procedural city (§12)");
        ImGui::Text("Buildings: %zu (%zu blocks)", app->m_TestCity.buildingCount, app->m_TestCity.blockCount);
        if (app->m_Renderer) {
            ImGui::Text("Impostor quads: %zu", app->m_Renderer->getImpostorDrawCount());
        }
        ImGui::SliderInt("Blocks per side", &app->m_TestCityBlocks, 2, 16);
        if (ImGui::Button("Generate city")) {
            if (app->m_Scene && app->m_Renderer) {
                Atlas::clearGeneratedCity(app->m_Scene.get(), app->m_TestCity);
                Atlas::CityGenConfig cfg;
                cfg.blocksPerSide = app->m_TestCityBlocks;
                app->m_TestCity = Atlas::generateProceduralCity(app->m_Scene.get(), app->m_Renderer.get(), cfg);
                if (app->m_WorldPartition) {
                    app->m_WorldPartition->markDirty();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear city")) {
            if (app->m_Scene) {
                Atlas::clearGeneratedCity(app->m_Scene.get(), app->m_TestCity);
                if (app->m_UIManager) {
                    app->m_UIManager->clearSelection();
                }
                if (app->m_WorldPartition) {
                    app->m_WorldPartition->markDirty();
                }
            }
        }
    }

    ImGui::End();
}

} // namespace Atlas
