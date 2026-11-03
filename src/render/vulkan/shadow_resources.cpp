#include "backend.hpp"
namespace one {
void Renderer::Impl::createFrameSceneResources() {
    for (auto& context : frames) {
        auto& s = context.scene;
        createBuffer(sizeof(SceneUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, s.uniform, s.uniformMemory);
        createImage(shadowResolution, shadowResolution, VK_FORMAT_D32_SFLOAT,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, s.shadowImage,
                    s.shadowMemory, 1, shadowCascadeCount);
        s.shadowView =
            imageView(s.shadowImage, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, 1, shadowCascadeCount);
        for (uint32_t c = 0; c < shadowCascadeCount; ++c) {
            s.shadowLayers[c] =
                imageView(s.shadowImage, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, 1, 1, c);
            VkFramebufferCreateInfo f{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            f.renderPass = sceneState.shadowPass;
            f.attachmentCount = 1;
            f.pAttachments = &s.shadowLayers[c];
            f.width = f.height = shadowResolution;
            f.layers = 1;
            check(vkCreateFramebuffer(deviceState.device, &f, nullptr, &s.shadowFramebuffers[c]),
                  "create cascade framebuffer");
        }
    }
}
void Renderer::Impl::destroyFrameSceneResources() {
    for (auto& context : frames) {
        auto& s = context.scene;
        for (auto f : s.shadowFramebuffers)
            if (f)
                vkDestroyFramebuffer(deviceState.device, f, nullptr);
        for (auto v : s.shadowLayers)
            if (v)
                vkDestroyImageView(deviceState.device, v, nullptr);
        if (s.shadowView)
            vkDestroyImageView(deviceState.device, s.shadowView, nullptr);
        if (s.shadowImage)
            vkDestroyImage(deviceState.device, s.shadowImage, nullptr);
        if (s.shadowMemory)
            vkFreeMemory(deviceState.device, s.shadowMemory, nullptr);
        if (s.uniform)
            vkDestroyBuffer(deviceState.device, s.uniform, nullptr);
        if (s.uniformMemory)
            vkFreeMemory(deviceState.device, s.uniformMemory, nullptr);
        s = {};
    }
}
void Renderer::Impl::updateUniform(bool shadows, bool classicColor, const Camera& camera) {
    sceneState.cascades = fitShadowCascades(
        camera, float(swapchainState.extent.width) / swapchainState.extent.height, shadowDistance, volumes.lighting.sunDirection);
    for (uint32_t c = 0; c < shadowCascadeCount; ++c) {
        const auto matrix = glm::transpose(sceneState.cascades.matrices[c]);
        shadowTexelsPerUnit[c] =
            .5f * shadowResolution *
            std::max(glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])));
    }
    SceneUniform u{sceneState.cascades.matrices, glm::vec4(volumes.lighting.sunDirection, 0),
                   glm::vec4(shadows ? 1.f : 0.f, classicColor ? 1.f : 0.f, float(shadowResolution), 0),
                   sceneState.cascades.splits, glm::vec4(camera.forward(), 0)};
    void* p = nullptr;
    check(vkMapMemory(deviceState.device, frame().scene.uniformMemory, 0, sizeof(u), 0, &p),
          "map cascade uniform");
    std::memcpy(p, &u, sizeof(u));
    vkUnmapMemory(deviceState.device, frame().scene.uniformMemory);
}
} // namespace one
