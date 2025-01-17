#include "renderer.hpp"
#include "swap_chain.hpp"
#include "vulkan_context.hpp"
#include "gpu_resources.hpp"
#include "single_time_commands.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

Renderer::Renderer(const VulkanInitInfo& initInfo, std::shared_ptr<VulkanContext> vulkanContext)
    : _vulkanContext(vulkanContext)
{
    _swapChain = std::make_unique<SwapChain>(vulkanContext, glm::uvec2 { initInfo.width, initInfo.height });
    InitializeCommandBuffers();
    InitializeSynchronizationObjects();

    InitializeTriangle();
    InitializeBLAS();
    InitializeTLAS();
    InitializeDescriptorSets({ initInfo.width, initInfo.height });
}

Renderer::~Renderer()
{
    _vulkanContext->Device().destroyDescriptorSetLayout(_descriptorSetLayout);
    _vulkanContext->Device().destroyDescriptorPool(_descriptorPool);

    _vulkanContext->Device().destroyAccelerationStructureKHR(_tlas.vkStructure, nullptr, _vulkanContext->Dldi());
    _vulkanContext->Device().destroyAccelerationStructureKHR(_blas.vkStructure, nullptr, _vulkanContext->Dldi());

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        _vulkanContext->Device().destroy(_inFlightFences.at(i));
        _vulkanContext->Device().destroy(_renderFinishedSemaphores.at(i));
        _vulkanContext->Device().destroy(_imageAvailableSemaphores.at(i));
    }
}

void Renderer::Render()
{
    VkCheckResult(_vulkanContext->Device().waitForFences(1, &_inFlightFences.at(_currentResourcesFrame), vk::True,
                      std::numeric_limits<uint64_t>::max()),
        "[VULKAN] Failed waiting on in flight fence!");

    uint32_t swapChainImageIndex {};
    VkCheckResult(_vulkanContext->Device().acquireNextImageKHR(_swapChain->GetSwapChain(), std::numeric_limits<uint64_t>::max(),
                      _imageAvailableSemaphores.at(_currentResourcesFrame), nullptr, &swapChainImageIndex),
        "[VULKAN] Failed to acquire swap chain image!");

    VkCheckResult(_vulkanContext->Device().resetFences(1, &_inFlightFences.at(_currentResourcesFrame)), "[VULKAN] Failed resetting fences!");

    vk::CommandBuffer commandBuffer = _commandBuffers.at(_currentResourcesFrame);
    commandBuffer.reset();

    vk::CommandBufferBeginInfo commandBufferBeginInfo {};
    VkCheckResult(commandBuffer.begin(&commandBufferBeginInfo), "[VULKAN] Failed to begin recording command buffer!");
    RecordCommands(commandBuffer, swapChainImageIndex);
    commandBuffer.end();

    vk::Semaphore waitSemaphore = _imageAvailableSemaphores.at(_currentResourcesFrame);
    vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::Semaphore signalSemaphore = _renderFinishedSemaphores.at(_currentResourcesFrame);

    vk::SubmitInfo submitInfo {};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &waitSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &signalSemaphore;
    VkCheckResult(_vulkanContext->GraphicsQueue().submit(1, &submitInfo, _inFlightFences.at(_currentResourcesFrame)), "[VULKAN] Failed submitting to graphics queue!");

    vk::SwapchainKHR swapchain = _swapChain->GetSwapChain();
    vk::PresentInfoKHR presentInfo {};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &signalSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &swapChainImageIndex;
    VkCheckResult(_vulkanContext->PresentQueue().presentKHR(&presentInfo), "[VULKAN] Failed to present swap chain image!");

    _currentResourcesFrame = (_currentResourcesFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Renderer::RecordCommands(const vk::CommandBuffer& commandBuffer, uint32_t swapChainImageIndex)
{
    VkTransitionImageLayout(commandBuffer, _swapChain->GetImage(swapChainImageIndex), _swapChain->GetFormat(),
        vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);
}

void Renderer::InitializeCommandBuffers()
{
    vk::CommandBufferAllocateInfo commandBufferAllocateInfo {};
    commandBufferAllocateInfo.commandPool = _vulkanContext->CommandPool();
    commandBufferAllocateInfo.level = vk::CommandBufferLevel::ePrimary;
    commandBufferAllocateInfo.commandBufferCount = _commandBuffers.size();

    VkCheckResult(_vulkanContext->Device().allocateCommandBuffers(&commandBufferAllocateInfo, _commandBuffers.data()), "[VULKAN] Failed allocating command buffer!");
}

void Renderer::InitializeSynchronizationObjects()
{
    vk::SemaphoreCreateInfo semaphoreCreateInfo {};
    vk::FenceCreateInfo fenceCreateInfo {};
    fenceCreateInfo.flags = vk::FenceCreateFlagBits::eSignaled;

    std::string errorMsg { "[VULKAN] Failed creating sync object!" };
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        VkCheckResult(_vulkanContext->Device().createSemaphore(&semaphoreCreateInfo, nullptr, &_imageAvailableSemaphores[i]), errorMsg);
        VkCheckResult(_vulkanContext->Device().createSemaphore(&semaphoreCreateInfo, nullptr, &_renderFinishedSemaphores[i]), errorMsg);
        VkCheckResult(_vulkanContext->Device().createFence(&fenceCreateInfo, nullptr, &_inFlightFences[i]), errorMsg);
    }
}

void Renderer::InitializeTriangle()
{
    const std::vector<Vertex> vertices =
    {
        { { 1.0f, 1.0f, 0.0f } },
        { { -1.0f, 1.0f, 0.0f } },
        { { 0.0f, -1.0f, 0.0f } }
    };

    const std::vector<uint32_t> indices = { 0, 1, 2 };

    const VkTransformMatrixKHR transformMatrix =
    {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f
    };


    // TODO: Upload to GPU friendly memory

    BufferCreation vertexBufferCreation {};
    vertexBufferCreation.SetName("Vertex Buffer")
        .SetUsageFlags(vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress)
        .SetMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
        .SetIsMappable(true)
        .SetSize(sizeof(Vertex) * vertices.size());

    _vertexBuffer = std::make_unique<Buffer>(vertexBufferCreation, _vulkanContext);
    memcpy(_vertexBuffer->mappedPtr, vertices.data(), sizeof(Vertex) * vertices.size());

    BufferCreation indexBufferCreation {};
    indexBufferCreation.SetName("Index Buffer")
        .SetUsageFlags(vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress)
        .SetMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
        .SetIsMappable(true)
        .SetSize(sizeof(uint32_t) * indices.size());

    _indexBuffer = std::make_unique<Buffer>(indexBufferCreation, _vulkanContext);
    memcpy(_indexBuffer->mappedPtr, indices.data(), sizeof(uint32_t) * indices.size());

    BufferCreation transformBufferCreation {};
    transformBufferCreation.SetName("Transform Buffer")
        .SetUsageFlags(vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress)
        .SetMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
        .SetIsMappable(true)
        .SetSize(sizeof(uint32_t) * indices.size());

    _transformBuffer = std::make_unique<Buffer>(transformBufferCreation, _vulkanContext);
    memcpy(_transformBuffer->mappedPtr, &transformMatrix, sizeof(VkTransformMatrixKHR));
}

void Renderer::InitializeBLAS()
{
    vk::DeviceOrHostAddressConstKHR vertexBufferDeviceAddress {};
    vk::DeviceOrHostAddressConstKHR indexBufferDeviceAddress {};
    vk::DeviceOrHostAddressConstKHR transformBufferDeviceAddress {};

    vertexBufferDeviceAddress.deviceAddress = _vulkanContext->Device().getBufferAddress(_vertexBuffer->buffer);
    indexBufferDeviceAddress.deviceAddress = _vulkanContext->Device().getBufferAddress(_indexBuffer->buffer);
    transformBufferDeviceAddress.deviceAddress = _vulkanContext->Device().getBufferAddress(_transformBuffer->buffer);

    vk::AccelerationStructureGeometryTrianglesDataKHR trianglesData {};
    trianglesData.vertexFormat = vk::Format::eR32G32B32Sfloat;
    trianglesData.vertexData = vertexBufferDeviceAddress;
    trianglesData.vertexStride = sizeof(Vertex);
    trianglesData.maxVertex = 0;
    trianglesData.indexType = vk::IndexType::eUint32;
    trianglesData.indexData = indexBufferDeviceAddress;
    trianglesData.transformData = transformBufferDeviceAddress;

    vk::AccelerationStructureGeometryKHR accelerationStructureGeometry{};
    accelerationStructureGeometry.flags=vk::GeometryFlagBitsKHR::eOpaque;
    accelerationStructureGeometry.geometryType = vk::GeometryTypeKHR::eTriangles;
    accelerationStructureGeometry.geometry.triangles = trianglesData;

    vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
    buildInfo.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
    buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
    buildInfo.srcAccelerationStructure = nullptr;
    buildInfo.dstAccelerationStructure = nullptr;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &accelerationStructureGeometry;
    buildInfo.scratchData = {};

    vk::AccelerationStructureBuildSizesInfoKHR buildSizesInfo = _vulkanContext->Device().getAccelerationStructureBuildSizesKHR(
        vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, 1, _vulkanContext->Dldi());

    BufferCreation structureBufferCreation {};
    structureBufferCreation.SetName("BLAS Structure Buffer")
        .SetUsageFlags(vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress)
        .SetMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
        .SetIsMappable(false)
        .SetSize(buildSizesInfo.accelerationStructureSize);
    _blas.structureBuffer = std::make_unique<Buffer>(structureBufferCreation, _vulkanContext);

    BufferCreation scratchBufferCreation {};
    scratchBufferCreation.SetName("BLAS Scratch Buffer")
    .SetUsageFlags(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress)
        .SetMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
        .SetIsMappable(false)
        .SetSize(buildSizesInfo.buildScratchSize);
    _blas.scratchBuffer = std::make_unique<Buffer>(scratchBufferCreation, _vulkanContext);

    vk::AccelerationStructureCreateInfoKHR createInfo {};
    createInfo.buffer = _blas.structureBuffer->buffer;
    createInfo.offset = 0;
    createInfo.size = buildSizesInfo.accelerationStructureSize;
    createInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
    _blas.vkStructure = _vulkanContext->Device().createAccelerationStructureKHR(createInfo, nullptr, _vulkanContext->Dldi());

    buildInfo.dstAccelerationStructure = _blas.vkStructure;
    buildInfo.scratchData.deviceAddress = _vulkanContext->Device().getBufferAddress(_blas.scratchBuffer->buffer);

    vk::AccelerationStructureBuildRangeInfoKHR buildRangeInfo {};
    buildRangeInfo.primitiveCount = 1;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.transformOffset = 0;
    std::vector<vk::AccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &buildRangeInfo };

    SingleTimeCommands singleTimeCommands{ _vulkanContext };
    singleTimeCommands.Record([&](vk::CommandBuffer commandBuffer)
    {
        commandBuffer.buildAccelerationStructuresKHR(1, &buildInfo, accelerationBuildStructureRangeInfos.data(), _vulkanContext->Dldi());
    });
    singleTimeCommands.Submit();
}

void Renderer::InitializeTLAS()
{
    vk::AccelerationStructureGeometryKHR accelerationStructureGeometry{};
    accelerationStructureGeometry.flags = vk::GeometryFlagBitsKHR::eOpaque;
    accelerationStructureGeometry.geometryType = vk::GeometryTypeKHR::eInstances;
    // Somehow not set to the correct type, need to set myself otherwise validation layers complain
    accelerationStructureGeometry.geometry.instances.sType = vk::StructureType::eAccelerationStructureGeometryInstancesDataKHR;

    vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
    buildInfo.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
    buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
    buildInfo.srcAccelerationStructure = nullptr;
    buildInfo.dstAccelerationStructure = nullptr;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &accelerationStructureGeometry;

    vk::AccelerationStructureBuildSizesInfoKHR buildSizesInfo = _vulkanContext->Device().getAccelerationStructureBuildSizesKHR(
        vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, 1, _vulkanContext->Dldi());

    BufferCreation structureBufferCreation {};
    structureBufferCreation.SetName("TLAS Structure Buffer")
        .SetUsageFlags(vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress)
        .SetMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
        .SetIsMappable(false)
        .SetSize(buildSizesInfo.accelerationStructureSize);
    _tlas.structureBuffer = std::make_unique<Buffer>(structureBufferCreation, _vulkanContext);

    BufferCreation scratchBufferCreation {};
    scratchBufferCreation.SetName("TLAS Scratch Buffer")
    .SetUsageFlags(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress)
        .SetMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
        .SetIsMappable(false)
        .SetSize(buildSizesInfo.buildScratchSize);
    _tlas.scratchBuffer = std::make_unique<Buffer>(scratchBufferCreation, _vulkanContext);

    vk::AccelerationStructureCreateInfoKHR createInfo {};
    createInfo.buffer= _blas.structureBuffer->buffer;
    createInfo.offset = 0;
    createInfo.size = buildSizesInfo.accelerationStructureSize;
    createInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
    _tlas.vkStructure = _vulkanContext->Device().createAccelerationStructureKHR(createInfo, nullptr, _vulkanContext->Dldi());

    const VkTransformMatrixKHR identityMatrix =
    {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f
    };

    vk::AccelerationStructureInstanceKHR accelerationStructureInstance {};
    accelerationStructureInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR; // vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable
    accelerationStructureInstance.transform = identityMatrix;
    accelerationStructureInstance.instanceCustomIndex = 0;
    accelerationStructureInstance.mask = 0xFF;
    accelerationStructureInstance.instanceShaderBindingTableRecordOffset = 0;
    accelerationStructureInstance.accelerationStructureReference = _vulkanContext->Device().getAccelerationStructureAddressKHR(_blas.vkStructure, _vulkanContext->Dldi());

    BufferCreation instancesBufferCreation {};
    instancesBufferCreation.SetName("TLAS Instances Buffer")
        .SetUsageFlags(vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress)
        .SetMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
        .SetIsMappable(true)
        .SetSize(buildSizesInfo.accelerationStructureSize);
    _tlas.instancesBuffer = std::make_unique<Buffer>(instancesBufferCreation, _vulkanContext);
    memcpy(_tlas.instancesBuffer->mappedPtr, &accelerationStructureInstance, sizeof(vk::AccelerationStructureInstanceKHR));

    buildInfo.dstAccelerationStructure = _tlas.vkStructure;
    buildInfo.scratchData.deviceAddress = _vulkanContext->Device().getBufferAddress(_tlas.scratchBuffer->buffer);

    vk::AccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.arrayOfPointers = false;
    instancesData.data = _vulkanContext->Device().getBufferAddress(_tlas.instancesBuffer->buffer, _vulkanContext->Dldi());
    accelerationStructureGeometry.geometry.instances = instancesData;

    vk::AccelerationStructureBuildRangeInfoKHR buildRangeInfo {};
    buildRangeInfo.primitiveCount = 1;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.transformOffset = 0;
    std::vector<vk::AccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &buildRangeInfo };

    SingleTimeCommands singleTimeCommands{ _vulkanContext };
    singleTimeCommands.Record([&](vk::CommandBuffer commandBuffer)
    {
        commandBuffer.buildAccelerationStructuresKHR(1, &buildInfo, accelerationBuildStructureRangeInfos.data(), _vulkanContext->Dldi());
    });
    singleTimeCommands.Submit();
}

void Renderer::InitializeDescriptorSets(glm::ivec2 windowSize)
{
    struct UniformData
    {
        glm::mat4 viewInverse {};
        glm::mat4 projInverse {};
    };

    UniformData cameraData {};
    cameraData.viewInverse = glm::inverse(glm::lookAt(glm::vec3(0.0f, 0.0f, -2.5f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    cameraData.projInverse = glm::inverse(glm::perspective(glm::radians(60.0f), static_cast<float>(windowSize.x) / static_cast<float>(windowSize.y), 0.0f, 1000.0f));

    constexpr vk::DeviceSize uniformBufferSize = sizeof(UniformData);
    BufferCreation uniformBufferCreation {};
    uniformBufferCreation.SetName("Camera Uniform Buffer")
        .SetUsageFlags(vk::BufferUsageFlagBits::eUniformBuffer)
        .SetMemoryUsage(VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
        .SetIsMappable(true)
        .SetSize(uniformBufferSize);
    _uniformBuffer = std::make_unique<Buffer>(uniformBufferCreation, _vulkanContext);

    std::array<vk::DescriptorSetLayoutBinding, 3> bindingLayouts {};

    vk::DescriptorSetLayoutBinding& imageLayout = bindingLayouts.at(0);
    imageLayout.binding = 0;
    imageLayout.descriptorType = vk::DescriptorType::eStorageImage;
    imageLayout.descriptorCount = 1;
    imageLayout.stageFlags = vk::ShaderStageFlagBits::eRaygenKHR;

    vk::DescriptorSetLayoutBinding& accelerationStructureLayout = bindingLayouts.at(1);
    accelerationStructureLayout.binding = 1;
    accelerationStructureLayout.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
    accelerationStructureLayout.descriptorCount = 1;
    accelerationStructureLayout.stageFlags = vk::ShaderStageFlagBits::eRaygenKHR;

    vk::DescriptorSetLayoutBinding& cameraLayout = bindingLayouts.at(2);
    cameraLayout.binding = 2;
    cameraLayout.descriptorType = vk::DescriptorType::eUniformBuffer;
    cameraLayout.descriptorCount = 1;
    cameraLayout.stageFlags = vk::ShaderStageFlagBits::eRaygenKHR;

    vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo {};
    descriptorSetLayoutCreateInfo.bindingCount = bindingLayouts.size();
    descriptorSetLayoutCreateInfo.pBindings = bindingLayouts.data();
    _descriptorSetLayout = _vulkanContext->Device().createDescriptorSetLayout(descriptorSetLayoutCreateInfo);

    std::array<vk::DescriptorPoolSize, 3> poolSizes {};

    vk::DescriptorPoolSize& imagePoolSize = poolSizes.at(0);
    imagePoolSize.type = vk::DescriptorType::eStorageImage;
    imagePoolSize.descriptorCount = 1;

    vk::DescriptorPoolSize& accelerationStructureSize = poolSizes.at(0);
    accelerationStructureSize.type = vk::DescriptorType::eAccelerationStructureKHR;
    accelerationStructureSize.descriptorCount = 1;

    vk::DescriptorPoolSize& cameraSize = poolSizes.at(0);
    cameraSize.type = vk::DescriptorType::eUniformBuffer;
    cameraSize.descriptorCount = 1;

    vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo {};
    descriptorPoolCreateInfo.maxSets = 1;
    descriptorPoolCreateInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descriptorPoolCreateInfo.pPoolSizes = poolSizes.data();
    _descriptorPool = _vulkanContext->Device().createDescriptorPool(descriptorPoolCreateInfo);

    vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo {};
    descriptorSetAllocateInfo.descriptorPool = _descriptorPool;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    descriptorSetAllocateInfo.pSetLayouts = &_descriptorSetLayout;
    _descriptorSet = _vulkanContext->Device().allocateDescriptorSets(descriptorSetAllocateInfo).front();

    vk::DescriptorImageInfo descriptorImageInfo {};
    // descriptorImageInfo.imageView = // TODO: Make render target image
    descriptorImageInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::WriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo {};
    descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
    descriptorAccelerationStructureInfo.pAccelerationStructures = &_tlas.vkStructure;

    vk::DescriptorBufferInfo descriptorBufferInfo {};
    descriptorBufferInfo.buffer = _uniformBuffer->buffer;
    descriptorBufferInfo.offset = 0;
    descriptorBufferInfo.range = uniformBufferSize;

    std::array<vk::WriteDescriptorSet, 3> descriptorWrites {};

    vk::WriteDescriptorSet& imageWrite = descriptorWrites.at(0);
    imageWrite.dstSet = _descriptorSet;
    imageWrite.dstBinding = 0;
    imageWrite.dstArrayElement = 0;
    imageWrite.descriptorCount = 1;
    imageWrite.descriptorType = vk::DescriptorType::eStorageImage;
    imageWrite.pImageInfo = &descriptorImageInfo;

    vk::WriteDescriptorSet& accelerationStructureWrite = descriptorWrites.at(1);
    accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
    accelerationStructureWrite.dstSet = _descriptorSet;
    accelerationStructureWrite.dstBinding = 1;
    accelerationStructureWrite.dstArrayElement = 0;
    accelerationStructureWrite.descriptorCount = 1;
    accelerationStructureWrite.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;

    vk::WriteDescriptorSet& uniformBufferWrite = descriptorWrites.at(2);
    uniformBufferWrite.dstSet = _descriptorSet;
    uniformBufferWrite.dstBinding = 2;
    uniformBufferWrite.dstArrayElement = 0;
    uniformBufferWrite.descriptorCount = 1;
    uniformBufferWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
    uniformBufferWrite.pBufferInfo = &descriptorBufferInfo;

    _vulkanContext->Device().updateDescriptorSets(static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}