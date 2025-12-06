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

struct VulkanContext {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;

  Queue graphicsQueue = {};
  Queue presentQueue = {};

  VkPhysicalDeviceProperties properties;
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
      appCtx.vkCtx.presentQueue.idx.value()};

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

  VkPhysicalDeviceFeatures reqDeviceFeatures{};

  VkDeviceCreateInfo devInfo = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = VK_NULL_HANDLE, // TODO add feature13, feature1
      .flags = 0u,
      .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
      .pQueueCreateInfos = queueCreateInfos.data(),
      .enabledLayerCount = 0u,
      .ppEnabledLayerNames = VK_NULL_HANDLE,
      .enabledExtensionCount = 0u, // TODO add device extensions
      .ppEnabledExtensionNames = VK_NULL_HANDLE,
      .pEnabledFeatures = &reqDeviceFeatures};

  VK_CHECK(vkCreateDevice(appCtx.vkCtx.physicalDevice, &devInfo, nullptr,
                          &appCtx.vkCtx.device),
           "Failed to create logical device");

  vkGetDeviceQueue(appCtx.vkCtx.device, appCtx.vkCtx.graphicsQueue.idx.value(),
                   0u, &appCtx.vkCtx.graphicsQueue.queueHandle);
  vkGetDeviceQueue(appCtx.vkCtx.device, appCtx.vkCtx.presentQueue.idx.value(),
                   0u, &appCtx.vkCtx.presentQueue.queueHandle);
}

int main() {

  AppContext appCtx;
  try {
    initWindow(appCtx);
    initVulkan(appCtx);
  } catch (std::exception &e) {
    std::cerr << e.what();
    return -3;
  }

  return 0;
}
