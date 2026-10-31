#pragma once
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <fstream>
#include "content.hpp"
#include <unordered_map>
#include <functional>

static void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string(result) + ")");
}

class Renderer {
    SDL_Window* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t family_ = 0;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> images_;
    std::vector<VkSemaphore> presentReady_;
    VkExtent2D extent_{};
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_ = VK_NULL_HANDLE;
    VkSemaphore acquired_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    struct Frame { glm::mat4 viewProjection; glm::vec4 eyeTime,color,params,lighting; };
    VkBuffer vertices_=VK_NULL_HANDLE, readback_=VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_=VK_NULL_HANDLE, readbackMemory_=VK_NULL_HANDLE;
    VkDeviceSize vertexCapacity_=0;
    struct Texture { VkImage image=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; VkImageView view=VK_NULL_HANDLE,linearView=VK_NULL_HANDLE; };
    std::unordered_map<const one::Image*,Texture> textures_;
    struct MaterialGPU { VkDescriptorSet set=VK_NULL_HANDLE; VkBuffer uniform=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; };
    std::unordered_map<const one::Material*,MaterialGPU> materials_;
    one::Image flatNormal_{"Flat normal",1,1,{128,128,255,255}};
    VkDescriptorSetLayout textureLayout_=VK_NULL_HANDLE;
    VkDescriptorPool texturePool_=VK_NULL_HANDLE;
    VkSampler sampler_=VK_NULL_HANDLE,shadowSampler_=VK_NULL_HANDLE;
    VkBuffer sceneUniform_=VK_NULL_HANDLE;
    VkDeviceMemory sceneUniformMemory_=VK_NULL_HANDLE;
    struct SceneUniform { glm::mat4 lightVP; glm::vec4 lightDirection,options; };
    glm::mat4 lightVP_{1};
    VkImage shadowImage_=VK_NULL_HANDLE;
    VkDeviceMemory shadowMemory_=VK_NULL_HANDLE;
    VkImageView shadowView_=VK_NULL_HANDLE;
    VkRenderPass shadowPass_=VK_NULL_HANDLE;
    VkFramebuffer shadowFramebuffer_=VK_NULL_HANDLE;
    VkPipeline shadowPipeline_=VK_NULL_HANDLE;
    bool guiReady_=false;
    uint32_t minImages_=2;
    VkImage depth_=VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_=VK_NULL_HANDLE;
    VkImageView depthView_=VK_NULL_HANDLE;
    VkRenderPass renderPass_=VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_=VK_NULL_HANDLE;
    VkPipeline pipeline_=VK_NULL_HANDLE,blendPipeline_=VK_NULL_HANDLE;
    VkFormat colorFormat_=VK_FORMAT_UNDEFINED;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;
    bool captureSupported_=false;
    bool validation_=false;
    unsigned validationErrors_=0;
    VkDebugUtilsMessengerEXT debug_=VK_NULL_HANDLE;
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
        auto* self=static_cast<Renderer*>(user);
        if(severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++self->validationErrors_;
        std::cerr << "Vulkan validation: " << data->pMessage << std::endl; return VK_FALSE;
    }

    bool resized_ = true;

    void initVulkan() {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "The One"; app.applicationVersion = VK_MAKE_VERSION(0, 3, 0);
        app.pEngineName = "The One"; app.engineVersion = app.applicationVersion; app.apiVersion = VK_API_VERSION_1_1;
        uint32_t extensionCount=0; const char* const* sdlExtensions=SDL_Vulkan_GetInstanceExtensions(&extensionCount);
        if(!sdlExtensions) throw std::runtime_error(SDL_GetError());
        std::vector<const char*> extensions(sdlExtensions,sdlExtensions+extensionCount);
        if(validation_) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        const char* layer="VK_LAYER_KHRONOS_validation";
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo=&app;
        info.enabledExtensionCount=static_cast<uint32_t>(extensions.size()); info.ppEnabledExtensionNames=extensions.data();
        if(validation_) { info.enabledLayerCount=1; info.ppEnabledLayerNames=&layer; }
        check(vkCreateInstance(&info, nullptr, &instance_), "vkCreateInstance");
        if(validation_) {
            VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debug.pfnUserCallback=debugCallback; debug.pUserData=this;
            auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_,"vkCreateDebugUtilsMessengerEXT"));
            if(!create) throw std::runtime_error("Debug utils unavailable");
            check(create(instance_,&debug,nullptr,&debug_),"create validation messenger");
        }
        if(!SDL_Vulkan_CreateSurface(window_,instance_,nullptr,&surface_)) throw std::runtime_error(SDL_GetError());
        uint32_t count = 0; check(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "enumerate GPUs");
        if (!count) throw std::runtime_error("No Vulkan GPU found");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "enumerate GPUs");
        for (auto candidate : devices) {
            uint32_t extCount = 0; check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extCount, nullptr), "enumerate device extensions");
            std::vector<VkExtensionProperties> exts(extCount);
            check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extCount, exts.data()), "enumerate device extensions");
            if (std::none_of(exts.begin(), exts.end(), [](const auto& e) { return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0; })) continue;
            uint32_t queueCount = 0; vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
            std::vector<VkQueueFamilyProperties> queues(queueCount); vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());
            for (uint32_t i = 0; i < queueCount; ++i) {
                VkBool32 present = VK_FALSE; check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface_, &present), "query surface support");
                if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { physical_ = candidate; family_ = i; break; }
            }
            if (physical_) break;
        }
        if (!physical_) throw std::runtime_error("No GPU with a graphics/present queue and swapchain support found");
        VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(physical_, &props);
        std::cout << "GPU: " << props.deviceName << std::endl;
        float priority = 1.0f;
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; q.queueFamilyIndex = family_; q.queueCount = 1; q.pQueuePriorities = &priority;
        const char* deviceExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo d{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; d.queueCreateInfoCount = 1; d.pQueueCreateInfos = &q; d.enabledExtensionCount = 1; d.ppEnabledExtensionNames = deviceExt;
        check(vkCreateDevice(physical_, &d, nullptr, &device_), "vkCreateDevice"); vkGetDeviceQueue(device_, family_, 0, &queue_);
        VkCommandPoolCreateInfo p{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; p.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; p.queueFamilyIndex = family_;
        check(vkCreateCommandPool(device_, &p, nullptr, &pool_), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; alloc.commandPool = pool_; alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device_, &alloc, &command_), "vkAllocateCommandBuffers");
        VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; check(vkCreateSemaphore(device_, &sem, nullptr, &acquired_), "vkCreateSemaphore");
        VkFenceCreateInfo f{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; f.flags = VK_FENCE_CREATE_SIGNALED_BIT; check(vkCreateFence(device_, &f, nullptr, &fence_), "vkCreateFence");
    }
    #include "renderer.inl"
    void destroySwapchain() {
        destroyRenderTargets();
        for (auto semaphore : presentReady_) vkDestroySemaphore(device_, semaphore, nullptr);
        presentReady_.clear(); images_.clear();
        if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    bool createSwapchain() {
        int width=0,height=0; SDL_GetWindowSizeInPixels(window_,&width,&height);
        if(width<=0 || height<=0 || (SDL_GetWindowFlags(window_)&SDL_WINDOW_MINIMIZED)) return false;
        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
        destroySwapchain();
        VkSurfaceCapabilitiesKHR caps{}; check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps), "query surface capabilities");
        if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) throw std::runtime_error("Surface cannot be a color attachment");
        captureSupported_=(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)!=0;
        uint32_t count = 0; check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, nullptr), "query surface formats");
        if (!count) throw std::runtime_error("Surface has no formats");
        std::vector<VkSurfaceFormatKHR> formats(count); check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, formats.data()), "query surface formats");
        auto format = formats[0];
        if (count == 1 && format.format == VK_FORMAT_UNDEFINED) format.format = VK_FORMAT_B8G8R8A8_UNORM;
        for (const auto& f : formats) if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { format = f; break; }
        extent_ = caps.currentExtent;
        if (extent_.width == std::numeric_limits<uint32_t>::max()) {
            extent_.width = std::clamp(static_cast<uint32_t>(width), caps.minImageExtent.width, caps.maxImageExtent.width);
            extent_.height = std::clamp(static_cast<uint32_t>(height), caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        minImages_=std::max(2u,caps.minImageCount);
        uint32_t imageCount = minImages_ + 1;
        if (caps.maxImageCount && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;
        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        for (auto a : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) if (caps.supportedCompositeAlpha & a) { alpha = a; break; }
        VkSwapchainCreateInfoKHR s{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        s.surface = surface_; s.minImageCount = imageCount; s.imageFormat = format.format; s.imageColorSpace = format.colorSpace; s.imageExtent = extent_;
        s.imageArrayLayers = 1; s.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (captureSupported_ ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0); s.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        s.preTransform = caps.currentTransform; s.compositeAlpha = alpha; s.presentMode = VK_PRESENT_MODE_FIFO_KHR; s.clipped = VK_TRUE;
        check(vkCreateSwapchainKHR(device_, &s, nullptr, &swapchain_), "vkCreateSwapchainKHR");
        check(vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr), "query swapchain images"); images_.resize(count);
        check(vkGetSwapchainImagesKHR(device_, swapchain_, &count, images_.data()), "query swapchain images");
        presentReady_.resize(count, VK_NULL_HANDLE);
        VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (auto& ready : presentReady_) check(vkCreateSemaphore(device_, &sem, nullptr, &ready), "create present semaphore");
        colorFormat_=format.format; createRenderTargets(format.format);
        resized_ = false; std::cout << "Swapchain: " << extent_.width << "x" << extent_.height << ", " << count << " images" << std::endl;
        return true;
    }
    bool draw(const std::vector<one::Vertex>& vertices,const std::vector<one::Draw>& draws,const one::Camera& camera,float time,bool capture,bool shadows,uint64_t selected) {
        if ((!swapchain_ || resized_) && !createSwapchain()) return false;
        check(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "wait for frame");
        uploadVertices(vertices);
        std::vector<one::Draw> ordered=draws;
        std::stable_sort(ordered.begin(),ordered.end(),[&](const auto& a,const auto& b){
            bool ab=a.material && a.material->blend,bb=b.material && b.material->blend;
            if(ab!=bb)return !ab;if(!ab)return false;
            auto center=[&](const one::Draw& d){glm::vec3 p(0);for(uint32_t i=0;i<d.count;++i)p+=vertices[d.first+i].position;return p/static_cast<float>(std::max(d.count,1u));};
            return glm::distance(center(a),camera.position)>glm::distance(center(b),camera.position);
        });
        for(const auto& draw:draws) material(draw.material,draw.image);
        updateUniform(shadows);
        uint32_t index = 0;
        VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, acquired_, VK_NULL_HANDLE, &index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) { resized_ = true; return false; }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) check(result, "acquire image");
        bool suboptimal = result == VK_SUBOPTIMAL_KHR;
        check(vkResetFences(device_, 1, &fence_), "reset fence");
        check(vkResetCommandBuffer(command_, 0), "reset command buffer");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command_, &begin), "begin command buffer");
        if(capture && !captureSupported_) throw std::runtime_error("GPU surface does not support capture");
        recordScene(index,ordered,camera,time,capture,shadows,selected);
        check(vkEndCommandBuffer(command_), "end command buffer");
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &acquired_; submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &command_; submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &presentReady_[index];
        check(vkQueueSubmit(queue_, 1, &submit, fence_), "submit frame");
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; present.waitSemaphoreCount = 1; present.pWaitSemaphores = &presentReady_[index];
        present.swapchainCount = 1; present.pSwapchains = &swapchain_; present.pImageIndices = &index;
        result = vkQueuePresentKHR(queue_, &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) resized_ = true;
        else { check(result, "present frame"); if (suboptimal) resized_ = true; }
        if(capture) verifyCapture();
        return true;
    }
public:
    Renderer()=default;
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;
    ~Renderer() { close(); }
    void initialize(SDL_Window* window,bool validate) { window_=window; validation_=validate; initVulkan(); createSharedResources(); createSwapchain(); }
    bool beginFrame() {
        if((!swapchain_ || resized_) && !createSwapchain()) return false;
        check(vkWaitForFences(device_,1,&fence_,VK_TRUE,UINT64_MAX),"wait frame");
        ImGui_ImplVulkan_NewFrame(); return true;
    }
    void resize() { resized_=true; }
    bool render(const std::vector<one::Vertex>& vertices,const std::vector<one::Draw>& draws,const one::Camera& camera,float time,bool capture=false,bool shadows=true,uint64_t selected=0) {
        return draw(vertices,draws,camera,time,capture,shadows,selected);
    }
    void clearAssets() { check(vkDeviceWaitIdle(device_),"wait asset reset"); destroyTextures(); }
    unsigned errors() const { return validationErrors_; }
    void close() {
        if(device_) {
            vkDeviceWaitIdle(device_); destroySwapchain(); destroySharedResources();
            if(vertices_) vkDestroyBuffer(device_,vertices_,nullptr); if(vertexMemory_) vkFreeMemory(device_,vertexMemory_,nullptr);
            if(fence_) vkDestroyFence(device_,fence_,nullptr); if(acquired_) vkDestroySemaphore(device_,acquired_,nullptr);
            if(pool_) vkDestroyCommandPool(device_,pool_,nullptr); vkDestroyDevice(device_,nullptr); device_=VK_NULL_HANDLE;
        }
        if(surface_) { vkDestroySurfaceKHR(instance_,surface_,nullptr); surface_=VK_NULL_HANDLE; }
        if(debug_) { auto destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_,"vkDestroyDebugUtilsMessengerEXT")); if(destroy) destroy(instance_,debug_,nullptr); debug_=VK_NULL_HANDLE; }
        if(instance_) { vkDestroyInstance(instance_,nullptr); instance_=VK_NULL_HANDLE; }
    }
};
