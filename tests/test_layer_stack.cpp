// Unit tests for Atlas::LayerStack (src/editor/layer_stack.h, stdlib only).
#include "tests.h"

#include <string>
#include <vector>

#include "editor/layer_stack.h"

namespace {

struct StubLayer {
    std::string name;
    std::vector<std::string>* log = nullptr;

    int attachCount = 0;
    int detachCount = 0;
    int updateCount = 0;
    int renderCount = 0;
    int renderUICount = 0;
    int resizeCount = 0;
    float lastDt = 0.0f;
    int lastW = 0;
    int lastH = 0;

    explicit StubLayer(std::string n = "", std::vector<std::string>* l = nullptr)
        : name(std::move(n)), log(l) {}

    void onAttach() { ++attachCount; }
    void onDetach() { ++detachCount; }
    void update(float dt) { ++updateCount; lastDt = dt; if (log) log->push_back("update:" + name); }
    void render() { ++renderCount; }
    void renderUI() { ++renderUICount; if (log) log->push_back("ui:" + name); }
    void onResize(int w, int h) { ++resizeCount; lastW = w; lastH = h; }
};

} // namespace

ATLAS_TEST(LayerStack, PushAttachesAndCounts) {
    Atlas::LayerStack<StubLayer> stack;
    StubLayer* a = stack.pushLayer<StubLayer>("a");
    StubLayer* b = stack.pushOverlay<StubLayer>("b");
    EXPECT_EQ(stack.size(), 2u);
    EXPECT_EQ(stack.layerCount(), 1u);
    EXPECT_EQ(stack.overlayCount(), 1u);
    EXPECT_EQ(a->attachCount, 1);
    EXPECT_EQ(b->attachCount, 1);
}

ATLAS_TEST(LayerStack, OverlaysRunAfterLayers) {
    Atlas::LayerStack<StubLayer> stack;
    std::vector<std::string> log;
    stack.pushOverlay<StubLayer>("overlay", &log);
    stack.pushLayer<StubLayer>("layer", &log);
    stack.renderUI();
    EXPECT_EQ(log.size(), 2u);
    EXPECT_TRUE(log[0] == "ui:layer");
    EXPECT_TRUE(log[1] == "ui:overlay");
    log.clear();
    stack.update(0.016f);
    EXPECT_TRUE(log[0] == "update:layer");
    EXPECT_TRUE(log[1] == "update:overlay");
}

ATLAS_TEST(LayerStack, PopDetachesOnlyItsKind) {
    Atlas::LayerStack<StubLayer> stack;
    StubLayer* layer = stack.pushLayer<StubLayer>("l");
    StubLayer* overlay = stack.pushOverlay<StubLayer>("o");
    // Wrong-kind pops fail and detach nothing.
    EXPECT_TRUE(!stack.popLayer(overlay));
    EXPECT_TRUE(!stack.popOverlay(layer));
    EXPECT_EQ(layer->detachCount, 0);
    EXPECT_EQ(overlay->detachCount, 0);
    // Right-kind pops detach exactly once.
    EXPECT_TRUE(stack.popOverlay(overlay));
    EXPECT_EQ(overlay->detachCount, 1);
    EXPECT_TRUE(stack.popLayer(layer));
    EXPECT_EQ(layer->detachCount, 1);
    EXPECT_TRUE(stack.empty());
    // Double pop fails.
    EXPECT_TRUE(!stack.popLayer(layer));
}

ATLAS_TEST(LayerStack, ResizeBroadcastsToAll) {
    Atlas::LayerStack<StubLayer> stack;
    StubLayer* a = stack.pushLayer<StubLayer>("a");
    StubLayer* b = stack.pushOverlay<StubLayer>("b");
    stack.onResize(1280, 720);
    EXPECT_EQ(a->resizeCount, 1);
    EXPECT_EQ(b->resizeCount, 1);
    EXPECT_EQ(a->lastW, 1280);
    EXPECT_EQ(b->lastH, 720);
}

ATLAS_TEST(LayerStack, ClearDetachesEverything) {
    Atlas::LayerStack<StubLayer> stack;
    StubLayer* a = stack.pushLayer<StubLayer>("a");
    StubLayer* b = stack.pushOverlay<StubLayer>("b");
    stack.clear();
    EXPECT_EQ(a->detachCount, 1);
    EXPECT_EQ(b->detachCount, 1);
    EXPECT_TRUE(stack.empty());
    EXPECT_EQ(stack.layerCount(), 0u);
}
