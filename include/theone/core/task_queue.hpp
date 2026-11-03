#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
namespace one {
// A bounded number of worker threads; queued jobs own their immutable input snapshots.
class TaskQueue {
    std::mutex mutex;
    std::condition_variable_any wake;
    std::deque<std::function<void()>> jobs;
    std::jthread worker;

  public:
    TaskQueue()
        : worker([this](std::stop_token stop) {
              for (;;) {
                  std::function<void()> job;
                  {
                      std::unique_lock lock(mutex);
                      wake.wait(lock, stop, [&] { return !jobs.empty(); });
                      if (jobs.empty())
                          return;
                      job = std::move(jobs.front());
                      jobs.pop_front();
                  }
                  job();
              }
          }) {}
    ~TaskQueue() {
        worker.request_stop();
        wake.notify_all();
        worker.join();
    }
    template <class F> auto submit(F fn) {
        using T = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<T()>>(std::move(fn));
        auto future = task->get_future().share();
        {
            std::lock_guard lock(mutex);
            jobs.push_back([task] { (*task)(); });
        }
        wake.notify_one();
        return future;
    }
};
} // namespace one
