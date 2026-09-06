#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#include "../ecs/ecs.h"
#include "../ecs/components/components.h"
#include "../renderer/renderer.h"
#include "world_partition.h"

namespace Atlas {

// Instancing Configuration
struct InstancingConfig {
    // Maximum number of instances per draw call
    uint32_t maxInstancesPerBatch = 10000;
    
    // Enable/disable instancing systems
    bool enableGPUInstancing = true;
    bool enableMultiDrawIndirect = true;
    
    // Distance threshold for switching from individual to instanced rendering
    float instancingDistanceThreshold = 50.0f;
    
    // whether to use hierarchical instancing
    bool enableHierarchicalInstancing = true;
};

// Instanced Mesh Data - holds instance transforms and mesh reference
struct InstancedMesh {
    // Reference to the base mesh (vertex/index buffers)
    uint32_t meshID = 0;
    uint32_t materialID = 0;
    
    // Instance data
    uint32_t instanceCount = 0;
    
    // Transforms for each instance (flattened: model[4][4] for each)
    std::vector<glm::mat4> transforms;
    
    // Bounding box for frustum culling
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;
    glm::vec3 center;
    float radius = 0.0f;
    
    // Per-instance flags (material variations, etc.)
    std::vector<uint32_t> instanceFlags;
};

// Instancing Batch - a group of instanced meshes to draw in one command
struct InstancingBatch {
    // The instanced mesh data
    std::shared_ptr<InstancedMesh> mesh;
    
    // Number of instances to draw
    uint32_t instanceCount = 0;
    
    // Start index into the instance transform array
    uint32_t startInstance = 0;
    
    // GPU descriptor set for this batch
    uint32_t descriptorSet = 0;
    
    // Draw indirect parameters
    struct DrawIndirectParams {
        uint32_t vertexCountPerInstance;
        uint32_t instanceCount;
        uint32_t startInstanceLocation;
        uint32_t baseVertexLocation;
    } drawParams;
    
    // Compute draw indirect parameters
    void computeDrawParams();
};

// Instancing Manager - handles GPU instancing operations
class InstancingManager {
public:
    InstancingManager(Renderer* renderer);
    ~InstancingManager();
    
    // Create a new instanced mesh
    std::shared_ptr<InstancedMesh> createInstancedMesh(
        uint32_t meshID,
        uint32_t materialID,
        uint32_t initialCapacity = 1000
    );
    
    // Add instance transforms to an instanced mesh
    void addInstance(
        std::shared_ptr<InstancedMesh> mesh,
        const glm::mat4& transform
    );
    
    // Remove instance at index
    void removeInstance(
        std::shared_ptr<InstancedMesh> mesh,
        uint32_t index
    );
    
    // Update instance transform
    void updateInstance(
        std::shared_ptr<InstancedMesh> mesh,
        uint32_t index,
        const glm::mat4& transform
    );
    
    // Build instancing batches for rendering
    std::vector<InstancingBatch> buildBatches(
        const std::unordered_map<uint32_t, std::shared_ptr<InstancedMesh>>& meshes,
        const glm::mat4& viewProj,
        float cameraDistance
    );
    
    // Generate draw indirect commands
    void generateDrawIndirectCommands(
        Renderer* renderer,
        const std::vector<InstancingBatch>& batches
    );
    
    // Cleanup GPU resources
    void cleanup();
    
private:
    Renderer* m_Renderer;
    // Cached GPU resources
    uint32_t m_IndirectCommandBuffer = 0;
    uint32_t m_InstanceBuffer = 0;
    uint32_t m_DrawDescriptorSet = 0;
};

// Generate Instancing Batches from WorldPartition data
// Groups entities by material and creates instanced batches
std::vector<InstancingBatch> generateInstancingBatchesFromCell(
    Scene* scene,
    const WorldCellCoord& cellCoord,
    const InstancingConfig& config,
    Renderer* renderer
);

// Update Instancing transforms each frame
void updateInstancingTransforms(
    Scene* scene,
    const WorldPartition& worldPartition,
    float deltaTime
);

// Compute screen size for instancing decision
float computeScreenSizeForInstancing(
    const glm::vec3& entityCenter,
    const glm::vec3& entityExtents,
    const glm::mat4& viewProj
);

} // namespace Atlas