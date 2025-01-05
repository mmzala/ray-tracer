#pragma once
#include <memory>
#include <vulkan/vulkan.hpp>
#include "vk_common.hpp"
#include "common.hpp"

struct VulkanInitInfo;
class VulkanContext;
class SwapChain;

class Renderer
{
public:
    Renderer(const VulkanInitInfo& initInfo, std::shared_ptr<VulkanContext> vulkanContext);
    ~Renderer();
    NON_COPYABLE(Renderer);
    NON_MOVABLE(Renderer);

private:
    void InitializeCommandBuffers();
    void InitializeSynchronizationObjects();

    std::shared_ptr<VulkanContext> _vulkanContext;
    std::unique_ptr<SwapChain> _swapChain;
    std::array<vk::CommandBuffer, MAX_FRAMES_IN_FLIGHT> _commandBuffers;
    std::array<vk::Semaphore, MAX_FRAMES_IN_FLIGHT> _imageAvailableSemaphores;
    std::array<vk::Semaphore, MAX_FRAMES_IN_FLIGHT> _renderFinishedSemaphores;
    std::array<vk::Fence, MAX_FRAMES_IN_FLIGHT> _inFlightFences;
};