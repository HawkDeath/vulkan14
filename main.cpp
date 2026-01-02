#include <cstdint>
#include <exception>
#include <format>
#include <iostream>
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
};

struct AppContext {
  WindowContext windowCtx;
  VulkanContext vkCtx;
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
    RT_THROW("Failed to choose Vulkan supported GPU");

  std::cout << std::format("Choosen {} GPU", appCtx.vkCtx.properties.deviceName)
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

  std::cout << appCtx.vkCtx.swapchain.presentMode << "\n";
}

int main() {

  AppContext appCtx{};
  try {
    initWindow(appCtx);
    initVulkan(appCtx);

  } catch (std::exception &e) {
    std::cerr << e.what();
    return -3;
  }

  return 0;
}
