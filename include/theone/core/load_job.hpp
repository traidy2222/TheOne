#pragma once
#include <theone/core/cancellation.hpp>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
namespace one {
enum class LoadState { Idle, Queued, Loading, Ready, Cancelled, Failed };
struct LoadStatus {
    LoadState state = LoadState::Idle;
    uint64_t generation = 0;
    std::string error;
};
// One worker, one latest request. Cancellation never joins a worker on the calling thread.
// Results are published atomically; failed/cancelled jobs cannot mutate live scene state.
template <class T, class Input = std::filesystem::path> class LoadJob {
    using Loader = std::function<std::shared_ptr<T>(const Input&, std::stop_token)>;
    struct Request {
        Input path;
        uint64_t generation;
        std::stop_token stop;
    };
    Loader loader_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::optional<Request> queued_;
    std::stop_source cancellation_;
    std::shared_ptr<T> result_;
    std::vector<std::shared_ptr<const void>> retired_;
    std::vector<Request> abandoned_;
    LoadStatus status_;
    std::jthread worker_;
    void run(std::stop_token stop) {
        for (;;) {
            std::optional<Request> request;
            std::vector<std::shared_ptr<const void>> retired;
            std::vector<Request> abandoned;
            bool stopping = false;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, stop,
                           [&] { return queued_.has_value() || !retired_.empty() || !abandoned_.empty(); });
                retired.swap(retired_);
                abandoned.swap(abandoned_);
                stopping = stop.stop_requested();
                if (stopping) {
                    if (result_)
                        retired.push_back(std::move(result_));
                    queued_.reset();
                } else if (queued_) {
                    request = std::move(queued_);
                    queued_.reset();
                    status_.state = LoadState::Loading;
                }
            }
            // Potentially expensive resource destruction never holds the publication mutex.
            retired.clear();
            abandoned.clear();
            if (stopping)
                return;
            if (!request)
                continue;
            std::shared_ptr<T> result;
            std::string error;
            bool cancelled = false;
            try {
                checkCancellation(request->stop);
                result = loader_(request->path, request->stop);
                checkCancellation(request->stop);
            } catch (const LoadCancelled&) {
                cancelled = true;
            } catch (const std::exception& e) {
                error = e.what();
            } catch (...) {
                error = "Unknown load failure";
            }
            {
                std::lock_guard lock(mutex_);
                if (request->generation == status_.generation) {
                    if (cancelled)
                        status_.state = LoadState::Cancelled;
                    else if (!error.empty()) {
                        status_.state = LoadState::Failed;
                        status_.error = std::move(error);
                    } else {
                        result_ = std::move(result);
                        status_.state = LoadState::Ready;
                    }
                }
            }
            // Stale/failed results also die on this worker, outside the mutex.
        }
    }

  public:
    explicit LoadJob(Loader loader)
        : loader_(std::move(loader)), worker_([this](std::stop_token stop) { run(stop); }) {}
    ~LoadJob() {
        cancel();
        worker_.request_stop();
        wake_.notify_all();
        worker_.join();
    }
    LoadJob(const LoadJob&) = delete;
    LoadJob& operator=(const LoadJob&) = delete;
    uint64_t request(Input path) {
        std::lock_guard lock(mutex_);
        cancellation_.request_stop();
        cancellation_ = std::stop_source{};
        ++status_.generation;
        status_.state = LoadState::Queued;
        status_.error.clear();
        if (result_)
            retired_.push_back(std::move(result_));
        if (queued_)
            abandoned_.push_back(std::move(*queued_));
        queued_ = Request{std::move(path), status_.generation, cancellation_.get_token()};
        wake_.notify_one();
        return status_.generation;
    }
    void cancel() {
        std::lock_guard lock(mutex_);
        cancellation_.request_stop();
        if (queued_)
            abandoned_.push_back(std::move(*queued_));
        queued_.reset();
        if (result_)
            retired_.push_back(std::move(result_));
        ++status_.generation;
        status_.state = LoadState::Cancelled;
        status_.error.clear();
        wake_.notify_one();
    }
    // Relinquish the caller's last ownership with retire(std::move(result)).
    template <class Resource> void retire(std::shared_ptr<Resource> result) {
        if (!result)
            return;
        std::lock_guard lock(mutex_);
        retired_.push_back(std::move(result));
        wake_.notify_one();
    }
    LoadStatus status() const {
        std::lock_guard lock(mutex_);
        return status_;
    }
    std::shared_ptr<T> take() {
        std::lock_guard lock(mutex_);
        if (status_.state != LoadState::Ready)
            return {};
        status_.state = LoadState::Idle;
        return std::exchange(result_, {});
    }
};
} // namespace one
