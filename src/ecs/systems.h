#pragma once
#include "ecs.h"
#include <vulkan/vulkan.h>
#include <functional>

class VulkanEngine;

using RenderSystemFunc = std::function<void(VulkanEngine*, World&, float)>;
using CameraSystemFunc = std::function<void(World&, float)>;
