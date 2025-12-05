#include <cstdint>
#include <exception>
#include <format>
#include <iostream>
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

struct VulkanContext {
  VkInstance instance;
  VkPhysicalDevice physicalDevice;
  VkDevice device;
  VkSurfaceKHR surface;

  VkQueue graphicsQueue;
  VkQueue presentQueue;

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
  }
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
