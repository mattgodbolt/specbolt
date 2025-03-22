#pragma once

#include <coroutine>
#ifndef SPECBOLT_MODULES
#include <algorithm>
#include <limits>
#include <vector>
#endif

namespace specbolt {

// struct promise;
//
// struct coroutine : std::coroutine_handle<promise> {
//   using promise_type = specbolt::promise;
// };
//
// struct promise {
//   coroutine get_return_object() { return {coroutine::from_promise(*this)}; }
//   std::suspend_always initial_suspend() noexcept { return {}; }
//   std::suspend_always final_suspend() noexcept { return {}; }
//   void return_void() {}
//   void unhandled_exception() {}
// };
//
// struct task {
//   using promise_type = promise;
// };
//
SPECBOLT_EXPORT
class Scheduler {
public:
  class Task {
  public:
    Task() = default;
    virtual ~Task() = default;
    virtual void run(std::size_t cycles) = 0;
    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;
  };

  void schedule(Task &task, const std::size_t in_cycles) {
    const auto insertion_point = std::ranges::lower_bound(tasks_, in_cycles, {}, &ScheduledTask::cycle);
    tasks_.insert(insertion_point, ScheduledTask{cycles_ + in_cycles, &task});
  }

  // std::coroutine_handle<promise> wait_for(const size_t /*cycles*/) { return {}; }

  void tick(const size_t cycles) {
    const auto end_cycle = cycles_ + cycles;
    while (cycles_ < end_cycle) {
      if (tasks_.empty())
        break;
      // Take a _copy_ of the information about the front task.
      if (auto [cycle, task] = tasks_.front(); cycle <= end_cycle) {
        tasks_.erase(tasks_.begin()); // Remove immediately.

        cycles_ = cycle;
        task->run(cycle);
      }
      else {
        break;
      }
    }
    cycles_ = end_cycle;
  }

  [[nodiscard]] std::size_t headroom() const {
    return tasks_.empty() ? std::numeric_limits<size_t>::max() : tasks_.front().cycle - cycles_;
  }

  [[nodiscard]] auto cycles() const { return cycles_; }

private:
  std::size_t cycles_ = 0;
  struct ScheduledTask {
    std::size_t cycle{};
    Task *task{};
  };
  std::vector<ScheduledTask> tasks_;
};

} // namespace specbolt
