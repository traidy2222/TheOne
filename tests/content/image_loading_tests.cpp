#include "assets/gltf/internal.hpp"
#include "assets/gltf/image_decode.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
using one::detail::need;
static void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
static std::string base64(const std::vector<unsigned char>& bytes) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (size_t i = 0; i < bytes.size(); i += 3) {
        uint32_t value = uint32_t(bytes[i]) << 16;
        if (i + 1 < bytes.size())
            value |= uint32_t(bytes[i + 1]) << 8;
        if (i + 2 < bytes.size())
            value |= bytes[i + 2];
        result += alphabet[(value >> 18) & 63];
        result += alphabet[(value >> 12) & 63];
        result += i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=';
        result += i + 2 < bytes.size() ? alphabet[value & 63] : '=';
    }
    return result;
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected fixture directory");
        const auto directory = std::filesystem::path(argv[1]) / "conversion";
        std::ifstream input(directory / "checker.png", std::ios::binary);
        require(bool(input), "Cannot read PNG fixture");
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
        auto uri = "data:image/png;base64," + base64(bytes);
        cgltf_buffer buffer{};
        buffer.data = bytes.data();
        buffer.size = bytes.size();
        cgltf_buffer_view view{};
        view.buffer = &buffer;
        view.size = bytes.size();
        cgltf_image images[12]{};
        std::string names[12];
        char external[] = "checker%2Epng";
        for (size_t i = 0; i < 12; ++i) {
            names[i] = "image-" + std::to_string(i);
            images[i].name = names[i].data();
            if (i % 3 == 0)
                images[i].buffer_view = &view;
            else
                images[i].uri = i % 3 == 1 ? uri.data() : external;
        }
        cgltf_data raw{};
        raw.images = images;
        raw.images_count = 12;
        cgltf_options options{};
        const auto path = directory / "test.gltf";
        auto check = [&](size_t count) {
            raw.images_count = count;
            one::Model model;
            one::gltf::loadImages(model, &raw, path, options);
            require(model.images.size() == count, "Image count changed");
            for (size_t i = 0; i < count; ++i) {
                auto reference = one::gltf::decodeImage(images[i], path, options);
                const auto& actual = model.images[i];
                require(actual.name == reference.name && actual.width == reference.width &&
                            actual.height == reference.height && actual.rgba == reference.rgba,
                        "Image order or decoded pixels changed");
            }
        };
        check(0);
        check(3);
        check(12);
        char invalid[] = "data:image/png;base64,AAAA";
        images[7].uri = invalid;
        bool rejected = false;
        try {
            one::Model model;
            one::gltf::loadImages(model, &raw, path, options);
        } catch (const std::runtime_error& e) {
            rejected = std::string(e.what()) == "Unsupported or oversized image: image-7";
        }
        require(rejected, "Worker failure did not propagate its diagnostic");
        images[7].uri = uri.data();
        check(12);
        need(true, "Literal message");
        need(true, std::string("Dynamic message"));
        const std::string message = "xxExpected diagnosticxx";
        rejected = false;
        try {
            need(false, std::string_view(message).substr(2, 19));
        } catch (const std::runtime_error& e) {
            rejected = std::string(e.what()) == "Expected diagnostic";
        }
        require(rejected, "Validation string view changed its error message");
        std::cout << "PASS image source variants, serial/batch parity, order, failure recovery and "
                     "validation diagnostics\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
