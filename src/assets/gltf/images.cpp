#include "internal.hpp"
#include "image_decode.hpp"
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
namespace one::gltf {
void loadImages(Model& model, cgltf_data* raw, const std::filesystem::path& path,
                const cgltf_options& options, std::stop_token stop) {
    // Fixed slots preserve glTF image indices and keep material pointers stable.
    model.images.resize(raw->images_count);
    const size_t workers = std::min<size_t>(4, std::max(1u, std::thread::hardware_concurrency()));
    if (raw->images_count < 8 || workers == 1) {
        for (size_t i = 0; i < raw->images_count; ++i) {
            checkCancellation(stop);
            model.images[i] = decodeImage(raw->images[i], path, options);
        }
        return;
    }
    std::atomic<size_t> next{0};
    std::atomic<bool> failed{false};
    std::exception_ptr error;
    std::mutex errorMutex;
    auto decode = [&] {
        try {
            while (!failed.load(std::memory_order_relaxed)) {
                checkCancellation(stop);
                auto i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= raw->images_count)
                    break;
                model.images[i] = decodeImage(raw->images[i], path, options);
            }
        } catch (...) {
            std::lock_guard lock(errorMutex);
            if (!error)
                error = std::current_exception();
            failed.store(true, std::memory_order_relaxed);
        }
    };
    {
        // The caller participates; all workers join before cgltf data can be released.
        std::vector<std::jthread> threads;
        for (size_t i = 1; i < workers; ++i)
            threads.emplace_back(decode);
        decode();
    }
    if (error)
        std::rethrow_exception(error);
}
} // namespace one::gltf
