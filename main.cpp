#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include <glm/glm.hpp>

#include <slang-com-ptr.h>
#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#define RT_THROW(msg) throw std::runtime_error(msg);

#define VK_CHECK(x, msg)                                                       \
  do {                                                                         \
    if ((x) != VK_SUCCESS) {                                                   \
      RT_THROW(msg)                                                            \
    }                                                                          \
  } while (0)

struct Vertex {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::vec3 color = {1.0f, 1.0f, 1.0f};

    static VkVertexInputBindingDescription bindingDesc() {
        VkVertexInputBindingDescription bindings{};
        bindings.binding = 0u;
        bindings.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindings.stride = sizeof(Vertex);
        return bindings;
    }

    static std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 2> attrib{};
        // postion
        attrib[0].binding = 0;
        attrib[0].location = 0;
        attrib[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrib[0].offset = offsetof(Vertex, position);

        attrib[1].binding = 0;
        attrib[1].location = 1;
        attrib[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrib[1].offset = offsetof(Vertex, color);

        return attrib;
    }
};

struct WindowContext {
    GLFWwindow *window = nullptr;

    uint32_t width = 1920u;
    uint32_t height = 1080u;
};

struct Queue {
    std::optional<uint32_t> idx;
    VkQueue queueHandle = VK_NULL_HANDLE;
};

struct SwapChain {
    VkSwapchainKHR swapchainHandle = VK_NULL_HANDLE;
    VkSwapchainKHR oldSwapchainHandle = VK_NULL_HANDLE;
    VkFormat colorFormat{};
    VkColorSpaceKHR colorSpace{};
    VkExtent2D extent{};
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

    std::vector<VkImage> images{};
    std::vector<VkImageView> imageViews{};

    static constexpr int32_t MAX_SWAPCHAIN_FRAMES = {2};
    uint32_t currentFrame = {0u};
};

struct VulkanContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    SwapChain swapchain{};

    Queue graphicsQueue = {};
    Queue presentQueue = {};
    Queue computeQueue = {};

    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    VkPhysicalDeviceVulkan14Features vulkan14Features{};

    std::vector<VkSemaphore> presentSemaphores{};
    std::vector<VkSemaphore> renderCompleteSemaphores{};
    std::vector<VkFence> waitFences{};

    VkCommandPool commandPool;
    std::array<VkCommandBuffer, SwapChain::MAX_SWAPCHAIN_FRAMES> commandBuffers{};

    VmaAllocator allocator = {VK_NULL_HANDLE};
};

struct GPUBuffer {
    VmaAllocation bufferAllocation = {VK_NULL_HANDLE};
    VkBuffer buffer = {VK_NULL_HANDLE};
    VkDeviceSize size = {0u};

    void *mapped = nullptr;
};

struct TriangleContext {
    VkPipelineCache pipelineCache = {VK_NULL_HANDLE};
    VkPipeline pipline = {VK_NULL_HANDLE};
    VkPipelineLayout piplineLayout = {VK_NULL_HANDLE};

    VkDescriptorSetLayout descriptorSetLayout = {VK_NULL_HANDLE};
    std::vector<VkDescriptorSet> descriptorSets{};
    VkDescriptorPool descriptorPool = {VK_NULL_HANDLE};

    GPUBuffer gpuBuffer{}; // vertex + index buffer in one buffer
};

struct AppContext {
    WindowContext windowCtx;
    VulkanContext vkCtx;
    TriangleContext trisCtx;
};

uint32_t findMemoryType(const VulkanContext &vkCtx, uint32_t type, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(vkCtx.physicalDevice, &memProps);

    for (uint32_t i = 0u; i < memProps.memoryTypeCount; ++i) {
        if ((type & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    RT_THROW("Failed to find suitable memory type");
}

VkCommandBuffer beginSingleTimeCommands(const VulkanContext &vkCtx) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = vkCtx.commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(vkCtx.device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void endSingleTimeCommands(const VulkanContext &vkCtx, VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(vkCtx.graphicsQueue.queueHandle, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vkCtx.graphicsQueue.queueHandle);

    vkFreeCommandBuffers(vkCtx.device, vkCtx.commandPool, 1, &commandBuffer);
}

void loadShader(const std::string &path) {
    slang::ISession *slangSession;
}

void initWindow(AppContext &appCtx) {
    glfwSetErrorCallback([](int code, const char *desc) -> void {
        std::cerr << std::format("[GLFW] {}: {}", code, desc) << std::endl;
    });

    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    appCtx.windowCtx.window = glfwCreateWindow(
        static_cast<int>(appCtx.windowCtx.width),
        static_cast<int>(appCtx.windowCtx.height), "vulkan14", nullptr, nullptr);

    if (appCtx.windowCtx.window == nullptr) {
        glfwTerminate();
        exit(-1);
    }

    glfwSetWindowUserPointer(appCtx.windowCtx.window, &appCtx.windowCtx);
}

void initVulkan(AppContext &appCtx) {
    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = VK_NULL_HANDLE,
        .pApplicationName = "vulkan14_app",
        .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
        .pEngineName = "vulkan14_engine",
        .engineVersion = VK_MAKE_VERSION(0, 0, 1),
        .apiVersion = VK_API_VERSION_1_4
    };

    uint32_t reqCount = 0u;
    const char **glfwReq = glfwGetRequiredInstanceExtensions(&reqCount);
    std::vector<const char *> extensions(glfwReq, glfwReq + reqCount);

    VkInstanceCreateInfo instInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = VK_NULL_HANDLE,
        .flags = 0u,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = 0u,
        .ppEnabledLayerNames = VK_NULL_HANDLE,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data()
    };

    VK_CHECK(vkCreateInstance(&instInfo, nullptr, &appCtx.vkCtx.instance),
             "Failed to create instance");

    VK_CHECK(glfwCreateWindowSurface(appCtx.vkCtx.instance,
                 appCtx.windowCtx.window, nullptr,
                 &appCtx.vkCtx.surface),
             "Failed to create Surface");

    uint32_t deviceCount = 0u;
    vkEnumeratePhysicalDevices(appCtx.vkCtx.instance, &deviceCount, nullptr);
    if (deviceCount == 0)
        RT_THROW("Failed to find Vulkan supported GPU");

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(appCtx.vkCtx.instance, &deviceCount,
                               devices.data());

    for (auto &dev: devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(dev, &properties);
        std::cout << std::format("Device {}", properties.deviceName) << "\n";
    }

    for (auto &dev: devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(dev, &properties);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            appCtx.vkCtx.physicalDevice = dev;
            appCtx.vkCtx.properties = properties;
            break;
        }
    }

    if (appCtx.vkCtx.physicalDevice == VK_NULL_HANDLE)
        RT_THROW("Failed to select Vulkan supported GPU");

    std::cout << std::format("Selected {} GPU",
                             appCtx.vkCtx.properties.deviceName)
            << "\n";

    uint32_t queueFamilyCount = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(appCtx.vkCtx.physicalDevice,
                                             &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(
        appCtx.vkCtx.physicalDevice, &queueFamilyCount, queueFamilies.data());

    uint32_t i = 0;

    for (const auto &queueFam: queueFamilies) {
        if (queueFam.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            appCtx.vkCtx.graphicsQueue.idx = i;
        }

        if (queueFam.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            appCtx.vkCtx.computeQueue.idx = i;
        }

        VkBool32 presentSupported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(appCtx.vkCtx.physicalDevice, i,
                                             appCtx.vkCtx.surface,
                                             &presentSupported);

        if (presentSupported) {
            appCtx.vkCtx.presentQueue.idx = i;
        }

        if (appCtx.vkCtx.presentQueue.idx.has_value() &&
            appCtx.vkCtx.graphicsQueue.idx.has_value()) {
            break;
        }
        ++i;
    }

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        appCtx.vkCtx.graphicsQueue.idx.value(),
        appCtx.vkCtx.presentQueue.idx.value()

    };

    float prio = 1.0f;
    for (uint32_t qFam: uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = VK_NULL_HANDLE,
            .flags = 0u,
            .queueFamilyIndex = qFam,
            .queueCount = 1u,
            .pQueuePriorities = &prio
        };
        queueCreateInfos.push_back(queueInfo);
    }
    const std::vector<const char *> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    // prepare Vulkan1.4 features
    appCtx.vkCtx.vulkan13Features.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    appCtx.vkCtx.vulkan13Features.pNext = VK_NULL_HANDLE;
    appCtx.vkCtx.vulkan13Features.dynamicRendering = VK_TRUE;
    appCtx.vkCtx.vulkan13Features.synchronization2 = VK_TRUE;
    appCtx.vkCtx.vulkan14Features.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
    appCtx.vkCtx.vulkan14Features.pNext = &appCtx.vkCtx.vulkan13Features;

    VkPhysicalDeviceFeatures enabledFeatures{};

    VkPhysicalDeviceFeatures2 reqDeviceFeatures{};
    reqDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    reqDeviceFeatures.features = enabledFeatures;
    reqDeviceFeatures.pNext = &appCtx.vkCtx.vulkan14Features;

    VkDeviceCreateInfo devInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &reqDeviceFeatures,
        .flags = 0u,
        .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
        .pQueueCreateInfos = queueCreateInfos.data(),
        .enabledLayerCount = 0u,
        .ppEnabledLayerNames = VK_NULL_HANDLE,
        .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data(),
        .pEnabledFeatures = VK_NULL_HANDLE

    };

    VK_CHECK(vkCreateDevice(appCtx.vkCtx.physicalDevice, &devInfo, nullptr,
                 &appCtx.vkCtx.device),
             "Failed to create logical device");

    // VMA init
    VmaVulkanFunctions vmaVkFUnctions {.vkGetInstanceProcAddr = ::vkGetInstanceProcAddr, .vkGetDeviceProcAddr = ::vkGetDeviceProcAddr, .vkCreateImage = ::vkCreateImage};
    VmaAllocatorCreateInfo vmaAllocInfo {.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT, .physicalDevice = appCtx.vkCtx.physicalDevice, .device = appCtx.vkCtx.device, .pVulkanFunctions = &vmaVkFUnctions, .instance = appCtx.vkCtx.instance};
    VK_CHECK(vmaCreateAllocator(&vmaAllocInfo, &appCtx.vkCtx.allocator), "Failed to create VMA allocator");

    vkGetDeviceQueue(appCtx.vkCtx.device, appCtx.vkCtx.graphicsQueue.idx.value(),
                     0u, &appCtx.vkCtx.graphicsQueue.queueHandle);
    vkGetDeviceQueue(appCtx.vkCtx.device, appCtx.vkCtx.presentQueue.idx.value(),
                     0u, &appCtx.vkCtx.presentQueue.queueHandle);
    vkGetDeviceQueue(appCtx.vkCtx.device, appCtx.vkCtx.computeQueue.idx.value(),
                     0u, &appCtx.vkCtx.computeQueue.queueHandle);

    // Create Swapchain
    // pick formats for swapchain

    uint32_t formatCount = 0u;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        appCtx.vkCtx.physicalDevice, appCtx.vkCtx.surface, &formatCount, nullptr);
    if (formatCount == 0u)
        RT_THROW("Not found ANY surface color format!!!!");

    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(appCtx.vkCtx.physicalDevice,
                                         appCtx.vkCtx.surface, &formatCount,
                                         surfaceFormats.data());

    VkSurfaceFormatKHR selectedFormat =
            surfaceFormats[0]; // get first available format as default
    std::vector preferredFormats = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_A8B8G8R8_UNORM_PACK32

    };

    for (auto &availFormat: surfaceFormats) {
        if (std::find(preferredFormats.begin(), preferredFormats.end(),
                      availFormat.format) != preferredFormats.end()) {
            selectedFormat = availFormat;
            break;
        }
    }

    appCtx.vkCtx.swapchain.colorFormat = selectedFormat.format;
    appCtx.vkCtx.swapchain.colorSpace = selectedFormat.colorSpace;

    auto &swapchain = appCtx.vkCtx.swapchain;

    swapchain.oldSwapchainHandle = swapchain.swapchainHandle;

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        appCtx.vkCtx.physicalDevice, appCtx.vkCtx.surface, &surfaceCapabilities);

    if (surfaceCapabilities.currentExtent.width == uint32_t(-1)) {
        swapchain.extent.width = appCtx.windowCtx.width;
        swapchain.extent.height = appCtx.windowCtx.height;
    } else {
        swapchain.extent = surfaceCapabilities.currentExtent;
        appCtx.windowCtx.width = swapchain.extent.width;
        appCtx.windowCtx.height = swapchain.extent.height;
    }

    uint32_t presentModeCount = 0u;
    vkGetPhysicalDeviceSurfacePresentModesKHR(appCtx.vkCtx.physicalDevice,
                                              appCtx.vkCtx.surface,
                                              &presentModeCount, nullptr);
    if (presentModeCount == 0u)
        RT_THROW("Not found ANY present mode");

    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        appCtx.vkCtx.physicalDevice, appCtx.vkCtx.surface, &presentModeCount,
        presentModes.data());

    // find preferred mailbox format
    appCtx.vkCtx.swapchain.presentMode =
            *std::find_if(presentModes.begin(), presentModes.end(),
                          [](const VkPresentModeKHR &mode) {
                              return mode == VK_PRESENT_MODE_MAILBOX_KHR;
                          });

    uint32_t numberSwapchainImages = surfaceCapabilities.minImageCount + 1u;
    if ((surfaceCapabilities.maxImageCount > 0) &&
        (numberSwapchainImages > surfaceCapabilities.maxImageCount)) {
        numberSwapchainImages = surfaceCapabilities.maxImageCount;
    }

    VkSurfaceTransformFlagsKHR perTransform =
            surfaceCapabilities.currentTransform;
    if (surfaceCapabilities.supportedTransforms &
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
        perTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    }

    VkCompositeAlphaFlagBitsKHR compositeAlpha =
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    std::vector<VkCompositeAlphaFlagBitsKHR> compositeAlphaFlags = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR

    };

    for (auto &caf: compositeAlphaFlags) {
        if (surfaceCapabilities.supportedCompositeAlpha & caf) {
            compositeAlpha = caf;
            break;
        }
    }

    VkSwapchainCreateInfoKHR swapchainCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = VK_NULL_HANDLE,
        .flags = 0u,
        .surface = appCtx.vkCtx.surface,
        .minImageCount = numberSwapchainImages,
        .imageFormat = appCtx.vkCtx.swapchain.colorFormat,
        .imageColorSpace = appCtx.vkCtx.swapchain.colorSpace,
        .imageExtent = appCtx.vkCtx.swapchain.extent,
        .imageArrayLayers = 1u,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0u,
        .pQueueFamilyIndices = VK_NULL_HANDLE,
        .preTransform = static_cast<VkSurfaceTransformFlagBitsKHR>(perTransform),
        .compositeAlpha = compositeAlpha,
        .presentMode = appCtx.vkCtx.swapchain.presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = appCtx.vkCtx.swapchain.oldSwapchainHandle

    };

    VK_CHECK(vkCreateSwapchainKHR(appCtx.vkCtx.device, &swapchainCreateInfo,
                 nullptr,
                 &appCtx.vkCtx.swapchain.swapchainHandle),
             "Failed to create swapchain");

    vkGetSwapchainImagesKHR(appCtx.vkCtx.device,
                            appCtx.vkCtx.swapchain.swapchainHandle,
                            &numberSwapchainImages, nullptr);
    appCtx.vkCtx.swapchain.images.resize(numberSwapchainImages);
    appCtx.vkCtx.swapchain.imageViews.resize(numberSwapchainImages);
    vkGetSwapchainImagesKHR(
        appCtx.vkCtx.device, appCtx.vkCtx.swapchain.swapchainHandle,
        &numberSwapchainImages, appCtx.vkCtx.swapchain.images.data());

    for (size_t i = 0; i < numberSwapchainImages; ++i) {
        VkImageViewCreateInfo ivCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = VK_NULL_HANDLE,
            .flags = 0u,
            .image = appCtx.vkCtx.swapchain.images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = appCtx.vkCtx.swapchain.colorFormat,
            .components = {
                VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,
                VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0u,
                .levelCount = 1u,
                .baseArrayLayer = 0u,
                .layerCount = 1u
            }

        };
        VK_CHECK(vkCreateImageView(appCtx.vkCtx.device, &ivCreateInfo, nullptr,
                     &appCtx.vkCtx.swapchain.imageViews[i]),
                 "Failed to create image view - swapchain");
    }

    // create sync objects
    appCtx.vkCtx.waitFences.resize(appCtx.vkCtx.swapchain.MAX_SWAPCHAIN_FRAMES);
    appCtx.vkCtx.presentSemaphores.resize(
        appCtx.vkCtx.swapchain.MAX_SWAPCHAIN_FRAMES);
    appCtx.vkCtx.renderCompleteSemaphores.resize(
        appCtx.vkCtx.swapchain.images.size());

    for (size_t i = 0; i < appCtx.vkCtx.swapchain.MAX_SWAPCHAIN_FRAMES; ++i) {
        VkFenceCreateInfo fenceCI = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = VK_NULL_HANDLE,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT

        };

        VK_CHECK(vkCreateFence(appCtx.vkCtx.device, &fenceCI, nullptr,
                     &appCtx.vkCtx.waitFences[i]),
                 "Failed to create wait fence");
    }

    for (size_t i = 0; i < appCtx.vkCtx.swapchain.MAX_SWAPCHAIN_FRAMES; ++i) {
        VkSemaphoreCreateInfo semaphoreCI = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = VK_NULL_HANDLE,
            .flags = 0u

        };
        VK_CHECK(vkCreateSemaphore(appCtx.vkCtx.device, &semaphoreCI, nullptr,
                     &appCtx.vkCtx.presentSemaphores[i]),
                 "Failed to create present semaphore");
    }

    for (size_t i = 0; i < appCtx.vkCtx.swapchain.images.size(); ++i) {
        VkSemaphoreCreateInfo semaphoreCI = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = VK_NULL_HANDLE,
            .flags = 0u
        };
        VK_CHECK(vkCreateSemaphore(appCtx.vkCtx.device, &semaphoreCI, nullptr,
                     &appCtx.vkCtx.renderCompleteSemaphores[i]),
                 "Failed to create render semaphore");
    }

    // create command buffers
    VkCommandPoolCreateInfo cmdPoolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = VK_NULL_HANDLE,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = appCtx.vkCtx.graphicsQueue.idx.value()
    };

    VK_CHECK(vkCreateCommandPool(appCtx.vkCtx.device, &cmdPoolInfo, nullptr,
                 &appCtx.vkCtx.commandPool),
             "Failed to create command pool");

    VkCommandBufferAllocateInfo cmdBufAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = VK_NULL_HANDLE,
        .commandPool = appCtx.vkCtx.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount =
        static_cast<uint32_t>(appCtx.vkCtx.commandBuffers.size())

    };

    VK_CHECK(vkAllocateCommandBuffers(appCtx.vkCtx.device, &cmdBufAllocInfo,
                 appCtx.vkCtx.commandBuffers.data()),
             "Failed to allocate command buffers");
}

void initResouces(AppContext &appCtx) {
    // prepare geometry
    std::array<Vertex, 3> trisGeom{};
    trisGeom[0].position = {-0.5f, -0.5f, 1.0f};
    trisGeom[0].color = {0.0f, 0.0f, 1.0f};

    trisGeom[1].position = {0.5f, -0.5f, 1.0f};
    trisGeom[1].color = {0.0f, 1.0f, 0.0f};

    trisGeom[2].position = {0.0f, 0.5f, 1.0f};
    trisGeom[2].color = {1.0f, 0.0f, 0.0f};

    uint32_t indices[] = {0, 1, 2};


    constexpr auto vbuffSize = static_cast<VkDeviceSize>(sizeof(Vertex) * trisGeom.size());
    constexpr auto ibuffSize = static_cast<VkDeviceSize>(sizeof(uint32_t) * 3);
    appCtx.trisCtx.gpuBuffer.size = vbuffSize + ibuffSize;
    VkBufferCreateInfo buffCI {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = appCtx.trisCtx.gpuBuffer.size, .usage =  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |  VK_BUFFER_USAGE_INDEX_BUFFER_BIT};
    VmaAllocationCreateInfo buffAllocCI {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO};
    VK_CHECK(vmaCreateBuffer(appCtx.vkCtx.allocator, &buffCI, &buffAllocCI, &appCtx.trisCtx.gpuBuffer.buffer, &appCtx.trisCtx.gpuBuffer.bufferAllocation, nullptr), "Failed to create tris buffer");

    void* pBuffMap = nullptr; // address of GPU memory
    VK_CHECK(vmaMapMemory(appCtx.vkCtx.allocator, appCtx.trisCtx.gpuBuffer.bufferAllocation, &pBuffMap), "Failed to map buffer memory");
    memcpy(pBuffMap, trisGeom.data(), vbuffSize); // copy vertices
    memcpy(((char*)pBuffMap) + vbuffSize, indices, ibuffSize); // copy indices
    vmaUnmapMemory(appCtx.vkCtx.allocator, appCtx.trisCtx.gpuBuffer.bufferAllocation);

    // create descriptor pool
    std::array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount =
            static_cast<uint32_t>(SwapChain::MAX_SWAPCHAIN_FRAMES);

    VkDescriptorPoolCreateInfo descPoolCI = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = VK_NULL_HANDLE,
        .flags = 0u,
        .maxSets = static_cast<uint32_t>(SwapChain::MAX_SWAPCHAIN_FRAMES),
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()

    };

    VK_CHECK(vkCreateDescriptorPool(appCtx.vkCtx.device, &descPoolCI, nullptr,
                 &appCtx.trisCtx.descriptorPool),
             "Failed to create descriptorPool");
    // create descriptor set layout
    /*
  VkDescriptorSetLayoutCreateInfo descSetLayoutCI{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = VK_NULL_HANDLE,
      .flags = 0u,
      .bindingCount = 0u, // TODO add binding
      .pBindings = VK_NULL_HANDLE,

  };

  VK_CHECK(vkCreateDescriptorSetLayout(appCtx.vkCtx.device, &descSetLayoutCI,
                                       nullptr,
                                       &appCtx.trisCtx.descriptorSetLayout),
           "Failed to create descriptorSetLayout");
  // create descriptor set

  std::vector<VkDescriptorSetLayout> layouts{
      SwapChain::MAX_SWAPCHAIN_FRAMES, appCtx.trisCtx.descriptorSetLayout};

  VkDescriptorSetAllocateInfo allocInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .pNext = VK_NULL_HANDLE,
      .descriptorPool = appCtx.trisCtx.descriptorPool,
      .descriptorSetCount = 0u,
      .pSetLayouts = VK_NULL_HANDLE

  };

  appCtx.trisCtx.descriptorSets.resize(SwapChain::MAX_SWAPCHAIN_FRAMES);
  VK_CHECK(vkAllocateDescriptorSets(appCtx.vkCtx.device, &allocInfo,
                                    appCtx.trisCtx.descriptorSets.data()),
           "Failed to allocate descriptors");
*/
    // create pipeline layout
    VkPipelineLayoutCreateInfo pipLayoutCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = VK_NULL_HANDLE,
        .flags = 0u,
        .setLayoutCount = 0u,
        .pSetLayouts = VK_NULL_HANDLE,
        .pushConstantRangeCount = 0u,
        .pPushConstantRanges = VK_NULL_HANDLE
    };
    VK_CHECK(vkCreatePipelineLayout(appCtx.vkCtx.device, &pipLayoutCI, nullptr, &appCtx.trisCtx.piplineLayout),
             "Failed to create pipeline layout");

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR

    };
    VkPipelineDynamicStateCreateInfo dynamicStateCI = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicStateCI.pDynamicStates = dynamicStates.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
    };
    inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterizationStateCI{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
    };
    rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
    rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizationStateCI.depthClampEnable = VK_FALSE;
    rasterizationStateCI.rasterizerDiscardEnable = VK_FALSE;
    rasterizationStateCI.depthBiasEnable = VK_FALSE;
    rasterizationStateCI.lineWidth = 1.0f;

    // Color blend state describes how blend factors are calculated (if used)
    // We need one blend attachment state per color attachment (even if blending is not used)
    VkPipelineColorBlendAttachmentState blendAttachmentState{};
    blendAttachmentState.colorWriteMask = 0xf;
    blendAttachmentState.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlendStateCI{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlendStateCI.attachmentCount = 1;
    colorBlendStateCI.pAttachments = &blendAttachmentState;

    // Viewport state sets the number of viewports and scissor used in this pipeline
    // Note: This is actually overridden by the dynamic states (see below)
    VkPipelineViewportStateCreateInfo viewportStateCI{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportStateCI.viewportCount = 1;
    viewportStateCI.scissorCount = 1;

    auto vBindingins = Vertex::bindingDesc();
    auto vAttribs = Vertex::attributeDescriptions();
    VkPipelineVertexInputStateCreateInfo pipVertInputCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = VK_NULL_HANDLE,
        .flags = 0u,
        .vertexBindingDescriptionCount = 1u,
        .pVertexBindingDescriptions = &vBindingins,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vAttribs.size()),
        .pVertexAttributeDescriptions = vAttribs.data()
    };

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].pNext = VK_NULL_HANDLE;
    shaderStages[0].flags = 0u;
    shaderStages[0].pNext = "vertexMain";
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].pSpecializationInfo = VK_NULL_HANDLE;
    // shaderStages[0].module =

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].pNext = VK_NULL_HANDLE;
    shaderStages[1].flags = 0u;
    shaderStages[1].pNext = "fragmentMain";
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].pSpecializationInfo = VK_NULL_HANDLE;
    // shaderStages[1].module =
    // TODO - add loading shader - slang

    VkPipelineRenderingCreateInfo pipRenderingCI = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = VK_NULL_HANDLE,
        .colorAttachmentCount = 1u,
        .pColorAttachmentFormats = &appCtx.vkCtx.swapchain.colorFormat,
        .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };

    VkPipelineMultisampleStateCreateInfo mulisampleCI = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    mulisampleCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &pipRenderingCI,
        .flags = 0u,
        .stageCount = static_cast<uint32_t>(shaderStages.size()),
        .pStages = shaderStages.data(),
        .pVertexInputState = &pipVertInputCI,
        .pInputAssemblyState = &inputAssemblyStateCI,
        .pTessellationState = VK_NULL_HANDLE,
        .pViewportState = &viewportStateCI,
        .pRasterizationState = &rasterizationStateCI,
        .pMultisampleState = &mulisampleCI,
        .pDepthStencilState = VK_NULL_HANDLE,
        .pColorBlendState = &colorBlendStateCI,
        .pDynamicState = &dynamicStateCI,
        .layout = appCtx.trisCtx.piplineLayout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0u,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = 0u
    };
    VkPipelineCacheCreateInfo pipCacheCI = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    VK_CHECK(vkCreatePipelineCache(appCtx.vkCtx.device, &pipCacheCI, nullptr, &appCtx.trisCtx.pipelineCache),
             "Failed to create pipeline cache object");
    // VK_CHECK(vkCreateGraphicsPipelines(appCtx.vkCtx.device, appCtx.trisCtx.pipelineCache, 1u, &pipelineInfo, nullptr, &appCtx.trisCtx.pipline), "Failed to create pipeline");
}

void renderScene(AppContext &appCtx) {
    /*
        auto &cmd = appCtx.vkCtx.commandBuffers[appCtx.vkCtx.swapchain.currentFrame];

       // vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
       //                       appCtx.trisCtx.piplineLayout, 0u, 1u,
       //                       &appCtx.trisCtx.descriptorSets[0], 0u, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, appCtx.trisCtx.pipline);

        VkDeviceSize offsets[1]{ 0 };
        vkCmdBindVertexBuffers(cmd, 0u, 1u, &appCtx.trisCtx.vertexBuffer.buffer, offsets);
        vkCmdBindIndexBuffer(cmd, appCtx.trisCtx.indicesBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);


       vkCmdDrawIndexed(cmd, 3u, 1u, 0u, 0u, 0u);
       */
}

void draw(AppContext &appCtx) {
    auto &currentFrame = appCtx.vkCtx.swapchain.currentFrame;
    vkWaitForFences(appCtx.vkCtx.device, 1u,
                    &appCtx.vkCtx.waitFences[currentFrame], VK_TRUE,
                    std::numeric_limits<uint64_t>::max());

    VK_CHECK(vkResetFences(appCtx.vkCtx.device, 1u,
                 &appCtx.vkCtx.waitFences[currentFrame]),
             "Failed to reset fence");

    uint32_t imageIdx = {0u};
    auto res = vkAcquireNextImageKHR(
        appCtx.vkCtx.device, appCtx.vkCtx.swapchain.swapchainHandle,
        std::numeric_limits<uint64_t>::max(),
        appCtx.vkCtx.presentSemaphores[currentFrame], VK_NULL_HANDLE, &imageIdx);

    auto &cmd = appCtx.vkCtx.commandBuffers[currentFrame];
    vkResetCommandBuffer(cmd, 0u);
    VkCommandBufferBeginInfo cmdBegInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = VK_NULL_HANDLE,
        .flags = 0u,
        .pInheritanceInfo = VK_NULL_HANDLE

    };

    vkBeginCommandBuffer(cmd, &cmdBegInfo);
    // image barrier
    VkImageMemoryBarrier imgBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    imgBarrier.srcAccessMask = 0u;
    imgBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imgBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgBarrier.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    imgBarrier.image = appCtx.vkCtx.swapchain.images[imageIdx];
    imgBarrier.subresourceRange =
            VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u, 0u,
                         nullptr, 0u, nullptr, 1u, &imgBarrier);

    // rendering here
    VkRenderingAttachmentInfo colorAttachInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = VK_NULL_HANDLE,
        .imageView = appCtx.vkCtx.swapchain.imageViews[imageIdx],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {1.125f, 0.125f, 0.125f, 1.0f}}

    };

    // TODO: add depth buffer

    VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = VK_NULL_HANDLE,
        .flags = 0u,
        .renderArea = {
            0, 0, appCtx.vkCtx.swapchain.extent.width,
            appCtx.vkCtx.swapchain.extent.height
        },
        .layerCount = 1u,
        .viewMask = 0u,
        .colorAttachmentCount = 1u,
        .pColorAttachments = &colorAttachInfo,
        .pDepthAttachment = VK_NULL_HANDLE,
        .pStencilAttachment = VK_NULL_HANDLE

    };

    vkCmdBeginRendering(cmd, &renderingInfo);
    VkViewport viewport{
        0.0f,
        0.0f,
        (float) appCtx.vkCtx.swapchain.extent.width,
        (float) appCtx.vkCtx.swapchain.extent.height,
        0.0f,
        1.0f
    };
    vkCmdSetViewport(cmd, 0u, 1u, &viewport);
    VkRect2D scissor{
        0, 0, appCtx.vkCtx.swapchain.extent.width,
        appCtx.vkCtx.swapchain.extent.height
    };
    vkCmdSetScissor(cmd, 0u, 1u, &scissor);

    renderScene(appCtx);

    vkCmdEndRendering(cmd);

    imgBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    imgBarrier.dstAccessMask = 0u;
    imgBarrier.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    imgBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_2_NONE, 0u, 0u, nullptr, 0u, nullptr,
                         1u, &imgBarrier);

    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.pWaitDstStageMask = &waitStageMask;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.commandBufferCount = 1u;

    submitInfo.pWaitSemaphores = &appCtx.vkCtx.presentSemaphores[currentFrame];
    submitInfo.waitSemaphoreCount = 1u;

    submitInfo.pSignalSemaphores =
            &appCtx.vkCtx.renderCompleteSemaphores[imageIdx];
    submitInfo.signalSemaphoreCount = 1u;

    vkQueueSubmit(appCtx.vkCtx.graphicsQueue.queueHandle, 1u, &submitInfo,
                  appCtx.vkCtx.waitFences[currentFrame]);

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1u;
    presentInfo.pWaitSemaphores =
            &appCtx.vkCtx.renderCompleteSemaphores[imageIdx];
    presentInfo.swapchainCount = 1u;
    presentInfo.pSwapchains = &appCtx.vkCtx.swapchain.swapchainHandle;
    presentInfo.pImageIndices = &imageIdx;
    res = vkQueuePresentKHR(appCtx.vkCtx.presentQueue.queueHandle, &presentInfo);

    currentFrame = (currentFrame + 1) % SwapChain::MAX_SWAPCHAIN_FRAMES;
}

void loop(AppContext &appCtx) {
    while (!glfwWindowShouldClose(appCtx.windowCtx.window)) {
        glfwPollEvents(); // input

        draw(appCtx);
    }
}

int main() {
    AppContext appCtx{};
    try {
        initWindow(appCtx);
        initVulkan(appCtx);
        initResouces(appCtx);
        loop(appCtx);
        // TODO: add shutdown - release resources
    } catch (std::exception &e) {
        std::cerr << e.what();
        return -3;
    }

    return 0;
}
