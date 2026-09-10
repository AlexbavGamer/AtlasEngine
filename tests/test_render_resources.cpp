// Unit tests for Atlas::RenderResourceManager (opaque mesh handles).
#include "tests.h"

#include <cstdint>
#include <thread>
#include <vector>

#include "renderer/render_resources.h"

ATLAS_TEST(RenderResources, InvalidHandlesAreDead) {
    Atlas::RenderResourceManager mgr;
    EXPECT_TRUE(!mgr.isMeshAlive(Atlas::kInvalidMeshHandle));
    EXPECT_TRUE(!mgr.isMeshAlive(0xFFFFFFFFu));
    EXPECT_TRUE(!mgr.isMeshAlive(42u)); // never allocated
    // freeing garbage must be a safe no-op
    mgr.freeMesh(Atlas::kInvalidMeshHandle);
    mgr.freeMesh(0xFFFFFFFFu);
    EXPECT_EQ(mgr.liveMeshCount(), 0u);
}

ATLAS_TEST(RenderResources, AllocateReturnsAliveUniqueHandles) {
    Atlas::RenderResourceManager mgr;
    Atlas::MeshHandle a = mgr.allocateMesh();
    Atlas::MeshHandle b = mgr.allocateMesh();
    EXPECT_TRUE(a != Atlas::kInvalidMeshHandle);
    EXPECT_TRUE(b != Atlas::kInvalidMeshHandle);
    EXPECT_TRUE(a != b);
    EXPECT_TRUE(mgr.isMeshAlive(a));
    EXPECT_TRUE(mgr.isMeshAlive(b));
    EXPECT_EQ(mgr.liveMeshCount(), 2u);
}

ATLAS_TEST(RenderResources, FreeKillsHandle) {
    Atlas::RenderResourceManager mgr;
    Atlas::MeshHandle a = mgr.allocateMesh();
    mgr.freeMesh(a);
    EXPECT_TRUE(!mgr.isMeshAlive(a));
    EXPECT_EQ(mgr.liveMeshCount(), 0u);
}

ATLAS_TEST(RenderResources, StaleFreeDoesNotKillRecycledSlot) {
    Atlas::RenderResourceManager mgr;
    Atlas::MeshHandle stale = mgr.allocateMesh();
    mgr.freeMesh(stale);
    Atlas::MeshHandle fresh = mgr.allocateMesh(); // may recycle the slot
    mgr.freeMesh(stale);                          // stale double-free
    EXPECT_TRUE(mgr.isMeshAlive(fresh));
    EXPECT_EQ(mgr.liveMeshCount(), 1u);
    mgr.freeMesh(fresh);
    EXPECT_EQ(mgr.liveMeshCount(), 0u);
}

ATLAS_TEST(RenderResources, RecycleKeepsCapacityStable) {
    Atlas::RenderResourceManager mgr;
    std::vector<Atlas::MeshHandle> handles;
    for (int i = 0; i < 64; ++i) {
        handles.push_back(mgr.allocateMesh());
    }
    const uint32_t capacity = mgr.meshCapacity();
    EXPECT_EQ(capacity, 64u);
    for (Atlas::MeshHandle h : handles) {
        mgr.freeMesh(h);
    }
    EXPECT_EQ(mgr.liveMeshCount(), 0u);
    for (int i = 0; i < 64; ++i) {
        EXPECT_TRUE(mgr.isMeshAlive(mgr.allocateMesh()));
    }
    EXPECT_EQ(mgr.meshCapacity(), capacity); // slots recycled, none added
    EXPECT_EQ(mgr.liveMeshCount(), 64u);
}

ATLAS_TEST(RenderResources, MeshBindingPublishResolve) {
    Atlas::RenderResourceManager mgr;
    Atlas::MeshHandle h = mgr.allocateMesh();
    // Live but unpublished → resolve fails (caller uses legacy fallback).
    Atlas::MeshBinding out{};
    EXPECT_TRUE(!mgr.getMeshData(h, out));
    Atlas::MeshBinding binding{};
    binding.vertexBuffer = 0x1234u;
    binding.indexBuffer = 0x5678u;
    binding.vertexMemory = 0x9ABCu;
    binding.indexMemory = 0xDEF0u;
    binding.vertexCount = 100u;
    binding.indexCount = 300u;
    EXPECT_TRUE(mgr.setMeshData(h, binding));
    EXPECT_TRUE(mgr.getMeshData(h, out));
    EXPECT_EQ(out.vertexBuffer, 0x1234u);
    EXPECT_EQ(out.indexBuffer, 0x5678u);
    EXPECT_EQ(out.vertexCount, 100u);
    EXPECT_EQ(out.indexCount, 300u);
    // Stale/foreign handles never publish.
    EXPECT_TRUE(!mgr.setMeshData(Atlas::kInvalidMeshHandle, binding));
    EXPECT_TRUE(!mgr.setMeshData(0xFFFFFFFFu, binding));
    mgr.freeMesh(h);
    // Dead slot → resolve fails and re-publish is rejected.
    EXPECT_TRUE(!mgr.getMeshData(h, out));
    EXPECT_TRUE(!mgr.setMeshData(h, binding));
}

ATLAS_TEST(RenderResources, MeshBindingStaleHandleIsolation) {
    Atlas::RenderResourceManager mgr;
    Atlas::MeshHandle stale = mgr.allocateMesh();
    Atlas::MeshBinding binding{};
    binding.vertexBuffer = 0xAAAAu;
    binding.indexCount = 42u;
    EXPECT_TRUE(mgr.setMeshData(stale, binding));
    mgr.freeMesh(stale);
    Atlas::MeshHandle fresh = mgr.allocateMesh(); // may recycle the slot
    // Stale handle must not overwrite the recycled slot's (empty) binding.
    EXPECT_TRUE(!mgr.setMeshData(stale, binding));
    Atlas::MeshBinding out{};
    EXPECT_TRUE(!mgr.getMeshData(stale, out));
    EXPECT_TRUE(!mgr.getMeshData(fresh, out)); // fresh unpublished
    mgr.freeMesh(fresh);
}

ATLAS_TEST(RenderResources, ConcurrentAllocateFree) {
    Atlas::RenderResourceManager mgr;
    constexpr int kThreads = 8;
    constexpr int kIters = 500;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&mgr]() {
            for (int i = 0; i < kIters; ++i) {
                Atlas::MeshHandle h = mgr.allocateMesh();
                EXPECT_TRUE(mgr.isMeshAlive(h));
                mgr.freeMesh(h);
                EXPECT_TRUE(!mgr.isMeshAlive(h));
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    EXPECT_EQ(mgr.liveMeshCount(), 0u);
}
