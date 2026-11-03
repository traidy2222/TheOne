#include "backend.hpp"
namespace one {
VkPipeline Renderer::Impl::createGraphicsPipeline(bool shadow, bool blend, bool mesh, bool mask, bool hdr) {
    VkShaderModule vert = shader(hdr    ? (mesh ? "mesh_hdr.vert.spv" : "scene_hdr.vert.spv")
                                 : mask ? (mesh ? "mesh_shadow_mask.vert.spv" : "shadow_mask.vert.spv")
                                 : mesh ? (shadow ? "mesh_shadow.vert.spv" : "mesh.vert.spv")
                                        : (shadow ? "shadow.vert.spv" : "scene.vert.spv")),
                   frag = VK_NULL_HANDLE;
    VkPipeline result = VK_NULL_HANDLE;
    try {
        if (!shadow || mask)
            frag = shader(hdr ? "scene_hdr.frag.spv" : mask ? "shadow_mask.frag.spv" : "scene.frag.spv");
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        for (auto& s : stages) {
            s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            s.pName = "main";
        }
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        VkBool32 transparent = blend;
        VkSpecializationMapEntry entry{0, 0, sizeof(transparent)};
        VkSpecializationInfo specialize{1, &entry, sizeof(transparent), &transparent};
        if (hdr)
            stages[1].pSpecializationInfo = &specialize;
        VkVertexInputBindingDescription binding{
            0, static_cast<uint32_t>(mesh ? sizeof(one::SkinVertex) : sizeof(one::Vertex)),
            VK_VERTEX_INPUT_RATE_VERTEX};
        std::array<VkVertexInputAttributeDescription, 6> attrs{
            {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(one::Vertex, position)},
             {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(one::Vertex, normal)},
             {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(one::Vertex, color)},
             {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(one::Vertex, uv)},
             {4, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(one::SkinVertex, joints)},
             {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(one::SkinVertex, weights)}}};
        if (mesh && shadow && !mask) {
            attrs[2] = attrs[4];
            attrs[3] = attrs[5];
        }
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = mesh ? (shadow && !mask ? 4 : 6) : (shadow && !mask ? 1 : 4);
        vi.pVertexAttributeDescriptions = attrs.data();
        VkPipelineInputAssemblyStateCreateInfo ia{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1;
        vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1;
        rs.depthBiasEnable = shadow;
        rs.depthBiasConstantFactor = 1.25f;
        rs.depthBiasSlopeFactor = 1.75f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = !blend;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState color{};
        color.colorWriteMask = 0xf;
        color.blendEnable = blend;
        color.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        color.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color.colorBlendOp = VK_BLEND_OP_ADD;
        color.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = shadow ? 0 : 1;
        cb.pAttachments = &color;
        std::array<VkDynamicState, 2> dynamic{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dy.dynamicStateCount = 2;
        dy.pDynamicStates = dynamic.data();
        VkGraphicsPipelineCreateInfo p{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        p.stageCount = shadow && !mask ? 1 : 2;
        p.pStages = stages.data();
        p.pVertexInputState = &vi;
        p.pInputAssemblyState = &ia;
        p.pViewportState = &vp;
        p.pRasterizationState = &rs;
        p.pMultisampleState = &ms;
        p.pDepthStencilState = &ds;
        p.pColorBlendState = &cb;
        p.pDynamicState = &dy;
        p.layout = hdr ? volumes.surfaceLayout : sceneState.pipelineLayout;
        p.renderPass = shadow ? sceneState.shadowPass
                       : hdr  ? (blend ? volumes.compositePass : volumes.hdrPass)
                              : targetsState.renderPass;
        check(vkCreateGraphicsPipelines(deviceState.device, VK_NULL_HANDLE, 1, &p, nullptr, &result),
              "create graphics pipeline");
    } catch (...) {
        if (frag)
            vkDestroyShaderModule(deviceState.device, frag, nullptr);
        vkDestroyShaderModule(deviceState.device, vert, nullptr);
        throw;
    }
    if (frag)
        vkDestroyShaderModule(deviceState.device, frag, nullptr);
    vkDestroyShaderModule(deviceState.device, vert, nullptr);
    return result;
}
} // namespace one
