#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <cstdlib>
#include <theone/geometry/prepared_image.hpp>
#include "image_decode.hpp"
#include "internal.hpp"
namespace one::gltf {
Image decodeImage(const cgltf_image& image, const std::filesystem::path& path, const cgltf_options& options) {
    Image output;
    output.name = name(image.name, "image");
    stbi_uc* decoded = nullptr;
    int channels = 0;
    if (image.buffer_view) {
        need(image.buffer_view->size < 64 * 1024 * 1024, "Encoded texture exceeds import limit");
        decoded = stbi_load_from_memory(cgltf_buffer_view_data(image.buffer_view),
                                        static_cast<int>(image.buffer_view->size), &output.width,
                                        &output.height, &channels, 4);
    } else if (image.uri) {
        std::string uri = image.uri;
        if (uri.starts_with("data:")) {
            auto comma = uri.find(',');
            need(comma != std::string::npos && uri.substr(0, comma).ends_with(";base64"),
                 "Unsupported image data URI");
            auto encoded = uri.substr(comma + 1);
            size_t bytes = encoded.size() / 4 * 3;
            if (encoded.ends_with("=="))
                bytes -= 2;
            else if (encoded.ends_with("="))
                --bytes;
            void* memory = nullptr;
            need(bytes < 64 * 1024 * 1024 && cgltf_load_buffer_base64(&options, bytes, encoded.c_str(),
                                                                      &memory) == cgltf_result_success,
                 "Invalid texture base64");
            decoded = stbi_load_from_memory(static_cast<stbi_uc*>(memory), static_cast<int>(bytes),
                                            &output.width, &output.height, &channels, 4);
            std::free(memory);
        } else {
            std::vector<char> decodedURI(uri.begin(), uri.end());
            decodedURI.push_back(0);
            cgltf_decode_uri(decodedURI.data());
            decoded = stbi_load((path.parent_path() / decodedURI.data()).string().c_str(), &output.width,
                                &output.height, &channels, 4);
        }
    }
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(decoded, stbi_image_free);
    need(decoded && output.width > 0 && output.height > 0 && output.width <= 8192 && output.height <= 8192,
         "Unsupported or oversized image: " + output.name);
    output.rgba.assign(decoded, decoded + static_cast<size_t>(output.width) * output.height * 4);
    output.prepared=prepareImage(output);output.preparedRevision=output.revision;
    return output;
}
} // namespace one::gltf
