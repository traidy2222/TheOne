#include <theone/assets/material.hpp>
#include "gltf/image_decode.hpp"
namespace one {
void Image::reload(const std::filesystem::path& path) {
    auto uri = std::filesystem::absolute(path).generic_string();
    // glTF URI decoding must not reinterpret literal percent characters in a file path.
    for (size_t i = 0; i < uri.size(); ++i)
        if (uri[i] == '%') {
            uri.replace(i, 1, "%25");
            i += 2;
        }
    cgltf_image source{};
    source.uri = uri.data();
    auto replacement = gltf::decodeImage(source, path, {});
    replacement.name = name;
    replacement.revision = revision + 1;
    replacement.preparedRevision=replacement.revision;
    *this = std::move(replacement);
}
} // namespace one
