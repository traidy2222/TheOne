#include "backend.hpp"
#include "volume_state.hpp"
#include <cstdlib>
namespace one::vulkan {
VolumeGpu::~VolumeGpu() {
    if (!device)
        return;
    if (pool)
        vkDestroyDescriptorPool(device, pool, nullptr);
    auto destroy = [&](VolumeBuffer& b) {
        if (b.mapped)
            vkUnmapMemory(device, b.memory);
        if (b.buffer)
            vkDestroyBuffer(device, b.buffer, nullptr);
        if (b.memory)
            vkFreeMemory(device, b.memory, nullptr);
    };
    for (auto* array : {&cells, &pressure, &parameters, &diagnostics, &plasma})
        for (auto& b : *array)
            destroy(b);
    destroy(rhs);
    destroy(light);
    destroy(ledger);
}
} // namespace one::vulkan
namespace one {
void Renderer::Impl::createVolumeLayout() {
    if (volumes.layout)
        return;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(deviceState.physical, &properties);
    if (properties.limits.maxPerStageDescriptorStorageBuffers < 9 ||
        properties.limits.maxDescriptorSetStorageBuffers < 10)
        throw std::runtime_error(
            "HDR volumes require nine storage buffers per shader stage and ten per pipeline layout");
    std::array<VkDescriptorSetLayoutBinding, 14> b{};
    for (uint32_t i = 0; i < b.size(); ++i) {
        auto type = i == 6             ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                    : i >= 8 && i < 12 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                       : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[i] = {i, type, 1, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    }
    VkDescriptorSetLayoutCreateInfo l{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    l.bindingCount = uint32_t(b.size());
    l.pBindings = b.data();
    check(vkCreateDescriptorSetLayout(deviceState.device, &l, nullptr, &volumes.layout),
          "volume descriptor layout");
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                             sizeof(vulkan::VolumePush)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &volumes.layout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(deviceState.device, &pl, nullptr, &volumes.pipelineLayout),
          "volume pipeline layout");
    std::array<VkDescriptorSetLayout, 3> layouts{materialsState.layout, geometryState.layout, volumes.layout};
    VkPushConstantRange surfacePush{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                    sizeof(Frame)};
    pl.setLayoutCount = uint32_t(layouts.size());
    pl.pSetLayouts = layouts.data();
    pl.pPushConstantRanges = &surfacePush;
    check(vkCreatePipelineLayout(deviceState.device, &pl, nullptr, &volumes.surfaceLayout),
          "HDR surface pipeline layout");
}
VkPipeline Renderer::Impl::createVolumePostPipeline(VkRenderPass pass, bool output, bool stellarSky) {
    auto vert = shader(stellarSky ? "stellar_sky.vert.spv" : "volume_fullscreen.vert.spv"),
         frag = shader(stellarSky ? "stellar_sky.frag.spv"
                       : output   ? "volume_output.frag.spv"
                                  : "volume_resolve.frag.spv");
    struct Modules {
        VkDevice d;
        VkShaderModule a, b;
        ~Modules() {
            vkDestroyShaderModule(d, a, nullptr);
            vkDestroyShaderModule(d, b, nullptr);
        }
    } modules{deviceState.device, vert, frag};
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    for (auto& s : stages) {
        s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        s.pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    if (stellarSky) {
        ds.depthTestEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    }
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = 15;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &attachment;
    std::array<VkDynamicState, 2> dyn{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn.data();
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages.data();
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds;
    info.pColorBlendState = &cb;
    info.pDynamicState = &dy;
    info.layout = volumes.pipelineLayout;
    info.renderPass = pass;
    VkPipeline result{};
    check(vkCreateGraphicsPipelines(deviceState.device, VK_NULL_HANDLE, 1, &info, nullptr, &result),
          "volume composite pipeline");
    return result;
}
void Renderer::Impl::createHdrTargets() {
    createVolumeLayout();
    if (!volumes.hdrPass) {
        VkFormatProperties format{};
        vkGetPhysicalDeviceFormatProperties(deviceState.physical, VK_FORMAT_R16G16B16A16_SFLOAT, &format);
        if ((format.optimalTilingFeatures &
             (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) !=
            (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
            throw std::runtime_error("HDR volumes require RGBA16F color/sampling");
        vkGetPhysicalDeviceFormatProperties(deviceState.physical, targetsState.depthFormat, &format);
        if (!(format.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
            throw std::runtime_error("HDR volumes require sampled scene depth");
        for (bool composite : {false, true}) {
            std::array<VkAttachmentDescription, 2> a{};
            a[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
            a[0].samples = VK_SAMPLE_COUNT_1_BIT;
            a[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            a[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            a[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            a[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            a[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            a[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            a[1] = a[0];
            a[1].format = targetsState.depthFormat;
            a[1].loadOp = composite ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
            a[1].initialLayout =
                composite ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
            a[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                depth{1, composite ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                   : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
            VkSubpassDescription sub{};
            sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            sub.colorAttachmentCount = 1;
            sub.pColorAttachments = &color;
            sub.pDepthStencilAttachment = &depth;
            std::array<VkSubpassDependency, 2> deps{};
            deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
            deps[0].dstSubpass = 0;
            deps[0].srcStageMask =
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            deps[0].dstStageMask =
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                    (composite ? 0 : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
            deps[1].srcSubpass = 0;
            deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            deps[1].srcStageMask =
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[1].srcAccessMask =
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
            rp.attachmentCount = 2;
            rp.pAttachments = a.data();
            rp.subpassCount = 1;
            rp.pSubpasses = &sub;
            rp.dependencyCount = 2;
            rp.pDependencies = deps.data();
            check(vkCreateRenderPass(deviceState.device, &rp, nullptr,
                                     composite ? &volumes.compositePass : &volumes.hdrPass),
                  "HDR scene render pass");
        }
        for (int mesh = 0; mesh < 2; ++mesh)
            for (int blend = 0; blend < 2; ++blend)
                volumes.surfaces[mesh * 2 + blend] =
                    createGraphicsPipeline(false, blend != 0, mesh != 0, false, true);
        volumes.stellarSky = createVolumePostPipeline(volumes.hdrPass, false, true);
        volumes.resolve = createVolumePostPipeline(volumes.compositePass, false);
        volumes.output = createVolumePostPipeline(targetsState.renderPass, true);
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.maxLod = 0;
        check(vkCreateSampler(deviceState.device, &sampler, nullptr, &volumes.sampler), "HDR sampler");
        sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
        check(vkCreateSampler(deviceState.device, &sampler, nullptr, &volumes.depthSampler),
              "scene depth sampler");
        VkPhysicalDeviceProperties pressureDevice{};
        vkGetPhysicalDeviceProperties(deviceState.physical, &pressureDevice);
        bool tiled = pressureDevice.limits.maxComputeSharedMemorySize >= 46912 &&
                     pressureDevice.limits.maxComputeWorkGroupInvocations >= 256 &&
                     pressureDevice.limits.maxComputeWorkGroupSize[0] >= 8 &&
                     pressureDevice.limits.maxComputeWorkGroupSize[1] >= 8 &&
                     pressureDevice.limits.maxComputeWorkGroupSize[2] >= 4;
        for (bool light : {false, true}) {
            auto module = shader(light ? "volume_light.comp.spv" : "volume_sim.comp.spv");
            for (uint32_t operation : light ? std::vector<uint32_t>{0, 1}
                                            : std::vector<uint32_t>{0, 1, 2, 3, 4, 5, 6, 7, 9, 12, 13}) {
                if (operation == 7 && !light && !tiled)
                    continue;
                auto pairedModule =
                    !light && operation == 7 ? shader("volume_pressure.comp.spv") : VK_NULL_HANDLE;
                const char* reference = std::getenv("THEONE_VOLUME_REFERENCE_SOLVER");
                uint32_t reuse = !(reference && std::strcmp(reference, "1") == 0);
                std::array<uint32_t, 3> values{operation, reuse, reuse};
                std::array<VkSpecializationMapEntry, 3> entries{{{0, 0, 4}, {1, 4, 4}, {2, 8, 4}}};
                VkSpecializationInfo specialization{uint32_t(entries.size()), entries.data(), sizeof(values),
                                                    values.data()};
                VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
                ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                ci.stage.module = pairedModule ? pairedModule : module;
                ci.stage.pName = "main";
                ci.stage.pSpecializationInfo = &specialization;
                ci.layout = volumes.pipelineLayout;
                auto result = vkCreateComputePipelines(deviceState.device, VK_NULL_HANDLE, 1, &ci, nullptr,
                                                       light ? &volumes.lightingPipeline[operation]
                                                             : &volumes.simulation[operation]);
                if (pairedModule)
                    vkDestroyShaderModule(deviceState.device, pairedModule, nullptr);
                if (result != VK_SUCCESS) {
                    vkDestroyShaderModule(deviceState.device, module, nullptr);
                    check(result, "specialized volume pipeline");
                }
            }
            vkDestroyShaderModule(deviceState.device, module, nullptr);
        }
    }
    if (!volumes.targets.empty())
        return;
    volumes.hdrBytes = 0;
    volumes.targets.resize(swapchainState.images.size());
    for (auto& t : volumes.targets) {
        auto w = swapchainState.extent.width, h = swapchainState.extent.height;
        createImage(w, h, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, t.color, t.colorMemory);
        t.colorView = imageView(t.color, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
        createImage(w, h, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, t.resolved,
                    t.resolvedMemory);
        t.resolvedView = imageView(t.resolved, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
        createImage(w, h, targetsState.depthFormat,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, t.depth,
                    t.depthMemory);
        t.depthView = imageView(t.depth, targetsState.depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
        for (auto image : {t.color, t.resolved, t.depth}) {
            VkMemoryRequirements req{};
            vkGetImageMemoryRequirements(deviceState.device, image, &req);
            volumes.hdrBytes += req.size;
        }
        for (bool composite : {false, true}) {
            std::array<VkImageView, 2> views{composite ? t.resolvedView : t.colorView, t.depthView};
            VkFramebufferCreateInfo f{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            f.renderPass = composite ? volumes.compositePass : volumes.hdrPass;
            f.attachmentCount = 2;
            f.pAttachments = views.data();
            f.width = w;
            f.height = h;
            f.layers = 1;
            check(vkCreateFramebuffer(deviceState.device, &f, nullptr, composite ? &t.composite : &t.opaque),
                  "HDR framebuffer");
        }
    }
    if (volumes.gpu) {
        if (volumes.gpu->pool)
            vkDestroyDescriptorPool(deviceState.device, volumes.gpu->pool, nullptr);
        volumes.gpu->pool = VK_NULL_HANDLE;
        for (auto& sets : volumes.gpu->sets)
            sets.clear();
    }
}
void Renderer::Impl::destroyHdrTargets(bool pipelines) {
    if (volumes.gpu) {
        if (volumes.gpu->pool)
            vkDestroyDescriptorPool(deviceState.device, volumes.gpu->pool, nullptr);
        volumes.gpu->pool = VK_NULL_HANDLE;
        for (auto& sets : volumes.gpu->sets)
            sets.clear();
    }
    for (auto& t : volumes.targets) {
        for (auto f : {t.opaque, t.composite})
            if (f)
                vkDestroyFramebuffer(deviceState.device, f, nullptr);
        for (auto v : {t.colorView, t.depthView, t.resolvedView})
            if (v)
                vkDestroyImageView(deviceState.device, v, nullptr);
        for (auto i : {t.color, t.depth, t.resolved})
            if (i)
                vkDestroyImage(deviceState.device, i, nullptr);
        for (auto m : {t.colorMemory, t.depthMemory, t.resolvedMemory})
            if (m)
                vkFreeMemory(deviceState.device, m, nullptr);
    }
    volumes.targets.clear();
    volumes.hdrBytes = 0;
    if (!pipelines)
        return;
    for (auto& p : volumes.surfaces) {
        if (p)
            vkDestroyPipeline(deviceState.device, p, nullptr);
        p = VK_NULL_HANDLE;
    }
    for (auto p : {volumes.resolve, volumes.output, volumes.stellarSky})
        if (p)
            vkDestroyPipeline(deviceState.device, p, nullptr);
    volumes.resolve = volumes.output = volumes.stellarSky = VK_NULL_HANDLE;
    for (auto* array : {&volumes.simulation})
        for (auto& p : *array) {
            if (p)
                vkDestroyPipeline(deviceState.device, p, nullptr);
            p = VK_NULL_HANDLE;
        }
    for (auto& p : volumes.lightingPipeline) {
        if (p)
            vkDestroyPipeline(deviceState.device, p, nullptr);
        p = VK_NULL_HANDLE;
    }
    for (auto p : {volumes.hdrPass, volumes.compositePass})
        if (p)
            vkDestroyRenderPass(deviceState.device, p, nullptr);
    volumes.hdrPass = volumes.compositePass = VK_NULL_HANDLE;
    for (auto s : {volumes.sampler, volumes.depthSampler})
        if (s)
            vkDestroySampler(deviceState.device, s, nullptr);
    volumes.sampler = volumes.depthSampler = VK_NULL_HANDLE;
}
void Renderer::Impl::createVolumeBuffers() {
    auto resolution = volumes.active ? volumes.scene.domain.resolution : glm::uvec3(4);
    if (volumes.gpu && volumes.gpu->resolution == resolution) {
        if (!volumes.gpu->pool)
            updateVolumeDescriptors();
        return;
    }
    if (volumes.gpu)
        retireVolumeBuffers();
    volumes.initialize = true;
    volumes.simulated = 0;
    volumes.state = 0;
    auto g = std::make_shared<vulkan::VolumeGpu>();
    g->device = deviceState.device;
    g->resolution = resolution;
    uint64_t count = uint64_t(resolution.x) * resolution.y * resolution.z;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(deviceState.physical, &properties);
    if (count * sizeof(VolumeCell) > properties.limits.maxStorageBufferRange)
        throw std::runtime_error("Volume grid exceeds GPU storage-buffer range");
    auto buffer = [&](vulkan::VolumeBuffer& b, uint64_t bytes, VkMemoryPropertyFlags flags,
                      VkBufferUsageFlags usage) {
        createBuffer(bytes, usage, b.buffer, b.memory, flags);
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(deviceState.device, b.buffer, &req);
        g->bytes += req.size;
        if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            check(vkMapMemory(deviceState.device, b.memory, 0, bytes, 0, &b.mapped), "map volume frame data");
    };
    for (int slot = 0; slot < 2; ++slot) {
        buffer(g->cells[slot], count * sizeof(VolumeCell), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        buffer(g->pressure[slot], count * 4, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        buffer(g->parameters[slot], sizeof(vulkan::VolumeParameters),
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        buffer(g->plasma[slot], 128 * 128 * sizeof(glm::vec4),
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        buffer(g->diagnostics[slot], 64 * 4,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }
    buffer(g->rhs, count * 4, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    buffer(g->light,
           count * 32 +
               uint64_t((resolution.x + 7) / 8) * ((resolution.y + 7) / 8) * ((resolution.z + 7) / 8) * 48,
           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    buffer(g->ledger, count * 32, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    if (volumes.active &&
        g->bytes + volumes.hdrBytes + volumes.retiredBytes > volumes.scene.domain.memoryBudget)
        throw std::runtime_error("Actual volume allocations exceed configured memory budget");
    volumes.gpu = std::move(g);
    volumes.stats.allocatedBytes = volumes.gpu->bytes + volumes.hdrBytes + volumes.retiredBytes;
    for (auto& f : frames)
        f.volumeQueryWritten = false;
    updateVolumeDescriptors();
}
void Renderer::Impl::updateVolumeDescriptors() {
    auto g = volumes.gpu;
    uint64_t count = uint64_t(g->resolution.x) * g->resolution.y * g->resolution.z;
    uint32_t sets = uint32_t(volumes.targets.size()) * 2;
    std::array<VkDescriptorPoolSize, 3> sizes{{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, sets * 9},
                                               {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, sets},
                                               {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sets * 4}}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = sets;
    pool.poolSizeCount = 3;
    pool.pPoolSizes = sizes.data();
    check(vkCreateDescriptorPool(deviceState.device, &pool, nullptr, &g->pool), "volume descriptors");
    for (int slot = 0; slot < 2; ++slot)
        for (const auto& t : volumes.targets) {
            VkDescriptorSet set{};
            VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            a.descriptorPool = g->pool;
            a.descriptorSetCount = 1;
            a.pSetLayouts = &volumes.layout;
            check(vkAllocateDescriptorSets(deviceState.device, &a, &set), "volume frame descriptor");
            g->sets[slot].push_back(set);
            std::array<VkDescriptorBufferInfo, 14> buffers{};
            std::array<vulkan::VolumeBuffer*, 8> data{
                &g->cells[0], &g->cells[1], &g->pressure[0],      &g->pressure[1],
                &g->rhs,      &g->light,    &g->parameters[slot], &g->diagnostics[slot]};
            std::array<VkDescriptorImageInfo, 4> images{
                {{volumes.depthSampler, t.depthView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
                 {volumes.sampler, t.colorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                 {materialsState.shadowSampler, frames[slot].scene.shadowView,
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
                 {volumes.sampler, t.resolvedView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}}};
            std::array<VkWriteDescriptorSet, 14> writes{};
            for (uint32_t binding = 0; binding < 14; ++binding) {
                auto& w = writes[binding];
                w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet = set;
                w.dstBinding = binding;
                w.descriptorCount = 1;
                w.descriptorType = binding == 6                   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                   : binding >= 8 && binding < 12 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                                                  : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                if (binding < 8 || binding >= 12) {
                    buffers[binding] = {binding == 13   ? g->plasma[slot].buffer
                                        : binding == 12 ? g->ledger.buffer
                                                        : data[binding]->buffer,
                                        0,
                                        binding == 13   ? 128 * 128 * sizeof(glm::vec4)
                                        : binding == 12 ? count * 32
                                        : binding < 2   ? count * sizeof(VolumeCell)
                                        : binding < 5   ? count * 4
                                        : binding == 5  ? count * 32 + uint64_t((g->resolution.x + 7) / 8) *
                                                                          ((g->resolution.y + 7) / 8) *
                                                                          ((g->resolution.z + 7) / 8) * 48
                                        : binding == 6 ? sizeof(vulkan::VolumeParameters)
                                                       : 256};
                    w.pBufferInfo = &buffers[binding];
                } else
                    w.pImageInfo = &images[binding - 8];
            }
            vkUpdateDescriptorSets(deviceState.device, 14, writes.data(), 0, nullptr);
        }
}
} // namespace one
