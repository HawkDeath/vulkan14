#include <array>
#include <cstdint>
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

#define RT_THROW(msg) throw std::runtime_error(msg);

#define VK_CHECK(x, msg)                                                       \
  do {                                                                         \
    if ((x) != VK_SUCCESS) {                                                   \
      RT_THROW(msg)                                                            \
    }                                                                          \
  } while (0)

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
};

struct TriangleContext {
  VkPipeline pipline = {VK_NULL_HANDLE};
  VkDescriptorSetLayout descriptorSetLayout;
};

struct AppContext {
  WindowContext windowCtx;
  VulkanContext vkCtx;
  TriangleContext trisCtx;
};

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

  VkApplicationInfo appInfo = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                               .pNext = VK_NULL_HANDLE,
                               .pApplicationName = "vulkan14_app",
                               .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
                               .pEngineName = "vulkan14_engine",
                               .engineVersion = VK_MAKE_VERSION(0, 0, 1),
                               .apiVersion = VK_API_VERSION_1_4};

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
      .ppEnabledExtensionNames = extensions.data()};

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

  for (auto &dev : devices) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(dev, &properties);
    std::cout << std::format("Device {}", properties.deviceName) << "\n";
  }

  for (auto &dev : devices) {
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

  for (const auto &queueFam : queueFamilies) {
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
  for (uint32_t qFam : uniqueQueueFamilies) {
    VkDeviceQueueCreateInfo queueInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = VK_NULL_HANDLE,
        .flags = 0u,
        .queueFamilyIndex = qFam,
        .queueCount = 1u,
        .pQueuePriorities = &prio};
    queueCreateInfos.push_back(queueInfo);
  }
  const std::vector<const char *> deviceExtensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};
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
  std::vector<VkFormat> preferredFormats = {VK_FORMAT_B8G8R8A8_UNORM,
                                            VK_FORMAT_R8G8B8A8_UNORM,
                                            VK_FORMAT_A8B8G8R8_UNORM_PACK32

  };

  for (auto &availFormat : surfaceFormats) {
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

  for (auto &caf : compositeAlphaFlags) {
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
        .components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,
                       VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A},
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0u,
                             .levelCount = 1u,
                             .baseArrayLayer = 0u,
                             .layerCount = 1u}

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
    VkFenceCreateInfo fenceCI = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
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
        .flags = 0u};
    VK_CHECK(vkCreateSemaphore(appCtx.vkCtx.device, &semaphoreCI, nullptr,
                               &appCtx.vkCtx.renderCompleteSemaphores[i]),
             "Failed to create render semaphore");
  }

  // create command buffers
  VkCommandPoolCreateInfo cmdPoolInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext = VK_NULL_HANDLE,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = appCtx.vkCtx.graphicsQueue.idx.value()};

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

void initResouces(AppContext &appCtx) {}

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
      .renderArea = {0, 0, appCtx.vkCtx.swapchain.extent.width,
                     appCtx.vkCtx.swapchain.extent.height},
      .layerCount = 1u,
      .viewMask = 0u,
      .colorAttachmentCount = 1u,
      .pColorAttachments = &colorAttachInfo,
      .pDepthAttachment = VK_NULL_HANDLE,
      .pStencilAttachment = VK_NULL_HANDLE

  };

  vkCmdBeginRendering(cmd, &renderingInfo);
  VkViewport viewport{0.0f,
                      0.0f,
                      (float)appCtx.vkCtx.swapchain.extent.width,
                      (float)appCtx.vkCtx.swapchain.extent.height,
                      0.0f,
                      1.0f};
  vkCmdSetViewport(cmd, 0u, 1u, &viewport);
  VkRect2D scissor{0, 0, appCtx.vkCtx.swapchain.extent.width,
                   appCtx.vkCtx.swapchain.extent.height};
  vkCmdSetScissor(cmd, 0u, 1u, &scissor);

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
