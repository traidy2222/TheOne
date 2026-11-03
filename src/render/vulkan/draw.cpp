#include "backend.hpp"
namespace one {
void Renderer::Impl::issueGeometryDraw(const one::Draw& draw, bool useLod) {
    VkDeviceSize offset = 0;
    if (draw.geometry) {
        const auto& mesh = *resolvedMesh(draw);
        const auto& lod = mesh.levels.at(useLod ? std::min<size_t>(draw.lod, mesh.levels.size() - 1) : 0);
        if (!mesh.indexCount)
            return;
        if (geometryState.boundBuffer != mesh.buffer ||
            geometryState.boundVertexOffset != mesh.vertexOffset ||
            geometryState.boundIndexOffset != lod.offset) {
            ++stats.geometryBinds;
            vkCmdBindVertexBuffers(frame().command, 0, 1, &mesh.buffer, &mesh.vertexOffset);
            vkCmdBindIndexBuffer(frame().command, mesh.buffer, lod.offset, VK_INDEX_TYPE_UINT32);
            geometryState.boundBuffer = mesh.buffer;
            geometryState.boundVertexOffset = mesh.vertexOffset;
            geometryState.boundIndexOffset = lod.offset;
        }
        vkCmdDrawIndexed(frame().command, lod.count, draw.instances, 0, 0, draw.palette);
    } else if (draw.count) {
        if (geometryState.boundBuffer != frame().geometry.vertices || geometryState.boundVertexOffset != 0) {
            vkCmdBindVertexBuffers(frame().command, 0, 1, &frame().geometry.vertices, &offset);
            geometryState.boundBuffer = frame().geometry.vertices;
            geometryState.boundVertexOffset = 0;
        }
        vkCmdDraw(frame().command, draw.count, 1, draw.first, 0);
    }
}

void Renderer::Impl::uploadVertices(const std::vector<one::Vertex>& vertices, bool deviceLocal) {
    VkDeviceSize size = vertices.size() * sizeof(one::Vertex);
    if (frame().geometry.dynamicUpload)
        frame().geometry.dynamicUpload->cancelled = true;
    frame().geometry.dynamicUpload.reset();
    if (size == 0)
        return;
    if (size > frame().geometry.vertexCapacity || deviceLocal != frame().geometry.vertexDeviceLocal) {
        auto buffer = frame().geometry.vertices;
        auto memory = frame().geometry.vertexMemory;
        retire([this, buffer, memory] {
            if (buffer)
                vkDestroyBuffer(deviceState.device, buffer, nullptr);
            if (memory)
                vkFreeMemory(deviceState.device, memory, nullptr);
        });
        frame().geometry.vertices = VK_NULL_HANDLE;
        frame().geometry.vertexMemory = VK_NULL_HANDLE;
        frame().geometry.vertexCapacity = size * 2;
        frame().geometry.vertexDeviceLocal = deviceLocal;
        createBuffer(frame().geometry.vertexCapacity,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     frame().geometry.vertices, frame().geometry.vertexMemory,
                     deviceLocal
                         ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                         : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    if (deviceLocal) {
        auto data = std::make_shared<std::vector<uint8_t>>(size);
        std::memcpy(data->data(), vertices.data(), size_t(size));
        frame().geometry.dynamicUpload = std::make_shared<vulkan::UploadProgress>();
        queueBufferUpload(frame().geometry.vertices, data, frame().geometry.dynamicUpload);
    } else {
        void* ptr = nullptr;
        check(vkMapMemory(deviceState.device, frame().geometry.vertexMemory, 0, size, 0, &ptr),
              "map vertices");
        std::memcpy(ptr, vertices.data(), static_cast<size_t>(size));
        vkUnmapMemory(deviceState.device, frame().geometry.vertexMemory);
    }
}
} // namespace one
