#include "asset_loading.hpp"
#include "assets/cache/dependency_manifest.hpp"
#include <theone/core/cancellation.hpp>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unordered_map>
namespace one::detail {
namespace {
constexpr uint64_t workingBudget = 128ull * 1024 * 1024;
struct AssetTask {
    std::filesystem::path path;
    std::shared_ptr<Model> model;
    ModelLoadStats stats;
    uint64_t weight = workingBudget;
};
uint64_t estimate(const std::filesystem::path& path) {
    // Cached archives have a known payload size. Uncached imports run exclusively;
    // compressed source size is not a safe predictor of decoded working storage.
    try {
        auto cache = asset_cache::cachePath({0, asset_cache::sourceLocation(path)});
        if (!cache.empty() && std::filesystem::exists(cache))
            return std::clamp<uint64_t>(std::filesystem::file_size(cache) * 3, 1024 * 1024, workingBudget);
    } catch (...) {
    }
    return workingBudget;
}
void accumulate(ModelLoadStats& a, const ModelLoadStats& b) {
    a.parseMs += b.parseMs;
    a.buffersMs += b.buffersMs;
    a.validationMs += b.validationMs;
    a.imagesMs += b.imagesMs;
    a.materialsMs += b.materialsMs;
    a.meshesMs += b.meshesMs;
    a.rigMs += b.rigMs;
    a.animationsMs += b.animationsMs;
    a.cacheMs += b.cacheMs;
    a.cacheHashMs += b.cacheHashMs;
    a.cacheReadMs += b.cacheReadMs;
    a.cacheDecodeMs += b.cacheDecodeMs;
    a.models += b.models;
    a.cacheHits += b.cacheHits;
}
} // namespace
void loadSceneAssets(World& world, SceneLoadStats* stats, std::stop_token stop) {
    std::vector<AssetTask> tasks;
    std::unordered_map<std::string, size_t> indices;
    std::vector<size_t> entities(world.entities.size(), SIZE_MAX);
    for (size_t i = 0; i < world.entities.size(); ++i)
        if (!world.entities[i].asset.empty()) {
            auto path =
                std::filesystem::weakly_canonical(world.source.parent_path() / world.entities[i].asset);
            auto [it, inserted] = indices.emplace(path.generic_string(), tasks.size());
            if (inserted)
                tasks.push_back({path, {}, {}, estimate(path)});
            entities[i] = it->second;
        }
    if (tasks.empty())
        return;
    size_t workers = 2;
    if (auto value = std::getenv("THEONE_ASSET_WORKERS"))
        workers = size_t(std::clamp(std::atoi(value), 1, 4));
    workers = std::min(workers, tasks.size());
    std::mutex mutex;
    std::condition_variable_any changed;
    size_t next = 0, active = 0, peakWorkers = 0;
    uint64_t reserved = 0, peakReserved = 0;
    std::exception_ptr failure;
    auto worker = [&] {
        for (;;) {
            size_t index;
            {
                std::unique_lock lock(mutex);
                if (failure || next == tasks.size() || stop.stop_requested())
                    return;
                index = next++;
                if (!changed.wait(lock, stop,
                                  [&] { return failure || reserved + tasks[index].weight <= workingBudget; }))
                    return;
                if (failure || stop.stop_requested())
                    return;
                reserved += tasks[index].weight;
                peakReserved = std::max(peakReserved, reserved);
                peakWorkers = std::max(peakWorkers, ++active);
            }
            try {
                tasks[index].model =
                    Model::load(tasks[index].path, stats ? &tasks[index].stats : nullptr, stop);
            } catch (...) {
                std::lock_guard lock(mutex);
                if (!failure)
                    failure = std::current_exception();
            }
            {
                std::lock_guard lock(mutex);
                reserved -= tasks[index].weight;
                --active;
            }
            changed.notify_all();
        }
    };
    std::vector<std::jthread> threads;
    for (size_t i = 1; i < workers; ++i)
        threads.emplace_back(worker);
    worker();
    for (auto& thread : threads)
        thread.join();
    checkCancellation(stop);
    if (failure)
        std::rethrow_exception(failure);
    // Publish only once all tasks succeed, in deterministic scene order.
    for (size_t i = 0; i < entities.size(); ++i)
        if (entities[i] != SIZE_MAX)
            world.entities[i].model = tasks[entities[i]].model;
    for (const auto& task : tasks) {
        if (stats)
            accumulate(stats->models, task.stats);
        for (const auto& message : task.model->diagnostics)
            world.diagnostics.push_back(task.path.generic_string() + ": " + message);
    }
    if (stats) {
        stats->assetWorkers = peakWorkers;
        stats->assetWorkingEstimate = peakReserved;
    }
}
} // namespace one::detail
