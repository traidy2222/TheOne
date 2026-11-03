#include "backend.hpp"
#include "wait_timing.hpp"
namespace one {
bool Renderer::Impl::reserveUpload(uint64_t bytes) {
    if (uploadSpent &&
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - assetPrepareStart)
                .count() > uploads.budgetMs)
        return false;
    // One oversized resource is allowed to make progress; subsequent resources wait for another frame.
    if (uploadSpent &&
        (bytes > uploadBudget - std::min(uploadSpent, uploadBudget) || !uploads.pending.empty()))
        return false;
    uploadSpent += bytes;
    return true;
}
void Renderer::Impl::stageUpload(vulkan::StagedUpload upload, const std::function<void(VkCommandBuffer)>& record) {
    try {
        check(vkResetCommandBuffer(upload.command,0),"reset staging command");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(upload.command,&begin),"begin staged commands");
        record(upload.command);
        check(vkEndCommandBuffer(upload.command),"end staged commands");
        frame().uploads.push_back(upload);
    }catch(...) {destroyStaging(upload);throw;}
    ++stats.uploadCommands;
}
void Renderer::Impl::submitUploadsOnly() {
    if (frame().uploads.empty())
        return;
    std::vector<VkCommandBuffer> commands;
    for (const auto& upload : frame().uploads)
        commands.push_back(upload.command);
    VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    s.commandBufferCount = uint32_t(commands.size());
    s.pCommandBuffers = commands.data();
    check(vkResetFences(deviceState.device, 1, &frame().fence), "reset upload-only fence");
    frame().submitHostMs = 0;
    check(vkQueueSubmit(deviceState.queue, 1, &s, frame().fence), "submit uploads without swapchain");
    frame().serial = ++submitted;
    frame().submittedAtMs=vulkan::clockMs();
    frame().queryWritten = false;
}
} // namespace one
