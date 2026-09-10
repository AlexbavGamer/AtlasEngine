#include "renderer/render_resources.h"

namespace Atlas {

MeshHandle RenderResourceManager::makeHandle(uint32_t index, uint32_t generation) {
    return ((generation & 0xFFu) << 24) | ((index + 1u) & 0xFFFFFFu);
}

bool RenderResourceManager::splitHandle(MeshHandle handle, uint32_t& index, uint32_t& generation) {
    if (handle == kInvalidMeshHandle) {
        return false;
    }
    generation = (handle >> 24) & 0xFFu;
    const uint32_t biased = handle & 0xFFFFFFu;
    if (biased == 0) {
        return false;
    }
    index = biased - 1u;
    return true;
}

MeshHandle RenderResourceManager::allocateMesh() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_freeList.empty()) {
        const uint32_t index = m_freeList.back();
        m_freeList.pop_back();
        MeshSlot& slot = m_slots[index];
        slot.alive = true;
        ++m_liveCount;
        return makeHandle(index, slot.generation);
    }
    const uint32_t index = static_cast<uint32_t>(m_slots.size());
    // 24-bit index space; fail loudly instead of silently aliasing handles.
    if (index >= 0xFFFFFFu) {
        return kInvalidMeshHandle;
    }
    m_slots.push_back(MeshSlot{});
    m_slots.back().alive = true;
    ++m_liveCount;
    return makeHandle(index, 0);
}

void RenderResourceManager::freeMesh(MeshHandle handle) {
    uint32_t index = 0;
    uint32_t generation = 0;
    if (!splitHandle(handle, index, generation)) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index >= m_slots.size()) {
        return;
    }
    MeshSlot& slot = m_slots[index];
    // Stale handle (slot already recycled): must not kill the new owner.
    if (!slot.alive || slot.generation != generation) {
        return;
    }
    slot.alive = false;
    // Drop the GPU binding so a stale resolve can never observe freed
    // Vk handles through a dead slot, even before the slot is recycled.
    slot.hasData = false;
    slot.binding = MeshBinding{};
    // Bump generation so outstanding stale handles stay dead. 8-bit wrap is
    // acceptable: an ABA collision needs 256 free/alloc cycles of the same
    // slot while a 257-cycle-old handle is still in flight.
    slot.generation = (slot.generation + 1u) & 0xFFu;
    m_freeList.push_back(index);
    --m_liveCount;
}

bool RenderResourceManager::isMeshAlive(MeshHandle handle) const {
    uint32_t index = 0;
    uint32_t generation = 0;
    if (!splitHandle(handle, index, generation)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index >= m_slots.size()) {
        return false;
    }
    const MeshSlot& slot = m_slots[index];
    return slot.alive && slot.generation == generation;
}

bool RenderResourceManager::setMeshData(MeshHandle handle, const MeshBinding& binding) {
    uint32_t index = 0;
    uint32_t generation = 0;
    if (!splitHandle(handle, index, generation)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index >= m_slots.size()) {
        return false;
    }
    MeshSlot& slot = m_slots[index];
    if (!slot.alive || slot.generation != generation) {
        return false;
    }
    slot.binding = binding;
    slot.hasData = true;
    return true;
}

bool RenderResourceManager::getMeshData(MeshHandle handle, MeshBinding& out) const {
    uint32_t index = 0;
    uint32_t generation = 0;
    if (!splitHandle(handle, index, generation)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index >= m_slots.size()) {
        return false;
    }
    const MeshSlot& slot = m_slots[index];
    if (!slot.alive || slot.generation != generation || !slot.hasData) {
        return false;
    }
    out = slot.binding;
    return true;
}

uint32_t RenderResourceManager::liveMeshCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_liveCount;
}

uint32_t RenderResourceManager::meshCapacity() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<uint32_t>(m_slots.size());
}

} // namespace Atlas
