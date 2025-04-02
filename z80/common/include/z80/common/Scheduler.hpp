#pragma once

#ifndef SPECBOLT_MODULES
#include <algorithm>
#include <coroutine>
#include <limits>
#include <vector>
#endif
#include <c++/14/bits/elements_of.h>

namespace specbolt {

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

  private:
    bool scheduled_{};
    friend class Scheduler;
  };

  void schedule(Task &task, const std::size_t in_cycles) {
    if (task.scheduled_)
      return;
    const auto when_to_run = cycles_ + in_cycles;
    const auto insertion_point = std::ranges::lower_bound(tasks_, when_to_run, {}, &ScheduledTask::cycle);
    tasks_.insert(insertion_point, ScheduledTask{when_to_run, &task});
    task.scheduled_ = true;
  }

  // std::coroutine_handle<promise> wait_for(const size_t /*cycles*/) { return {}; }

  void tick(const size_t cycles) {
    const auto end_cycle = cycles_ + cycles;
    while (cycles_ < end_cycle) {
      if (tasks_.empty())
        break;
      // Take a _copy_ of the information about the front task.
      if (auto [cycle, task] = tasks_.front(); cycle <= end_cycle) {
        task->scheduled_ = false;
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

SPECBOLT_EXPORT
class Clock;

SPECBOLT_EXPORT
struct CycleAwait {
  Clock &clock;
  std::size_t ready_at{0};

  constexpr void await_suspend(std::coroutine_handle<>) noexcept;
  constexpr void await_resume() const noexcept {}
  [[nodiscard]] constexpr bool await_ready() const noexcept;
};

SPECBOLT_EXPORT
struct ScheduledPromiseTypeBase;

SPECBOLT_EXPORT
class Clock {
  static constexpr std::size_t Capacity = 8;

public:
  void run() {
    while (!paused_)
      run_tick();
    paused_ = false;
  }

  CycleAwait tick(const size_t ticks) { return {*this, ticks + now_}; }

  void run_tick();

  void schedule(const std::coroutine_handle<> coro, const std::size_t when_to_run) {
    const auto insertion_point = std::ranges::lower_bound(tasks_, when_to_run, {}, &ScheduledTask::tick_to_resume);
    tasks_.insert(insertion_point, ScheduledTask{when_to_run, coro});
  }

  void pause() { paused_ = true; }

  [[nodiscard]] constexpr auto now() const { return now_; }

private:
  std::size_t now_{0};
  struct ScheduledTask {
    std::size_t tick_to_resume;
    std::coroutine_handle<> handle;
  };
  std::vector<ScheduledTask> tasks_;
  bool paused_{false};
};

SPECBOLT_EXPORT
struct MaybeSuspend {
  const bool do_not_suspend;
  [[nodiscard]] constexpr bool await_ready() const noexcept { return do_not_suspend; }
  constexpr void await_suspend(std::coroutine_handle<>) noexcept {}
  constexpr void await_resume() const noexcept {}
};

SPECBOLT_EXPORT
struct ScheduledPromiseTypeBase {
  std::size_t tick_to_resume{0};

  [[nodiscard]] constexpr bool ready(const std::size_t current_clock) const noexcept {
    return tick_to_resume == current_clock;
  }
};

SPECBOLT_EXPORT
template<typename ReturnType>
struct ScheduledPromiseType : ScheduledPromiseTypeBase {
  ReturnType result;

  constexpr void return_value(ReturnType type) noexcept { result = type; }
  constexpr auto initial_suspend() noexcept { return std::suspend_never{}; }
  constexpr auto final_suspend() noexcept { return std::suspend_never{}; }
  constexpr void unhandled_exception() noexcept {}
  constexpr auto get_return_object() noexcept {
    return std::coroutine_handle<ScheduledPromiseType>::from_promise(*this);
  }
};


SPECBOLT_EXPORT
template<typename ReturnType>
class Task {
public:
  // ReSharper disable once CppNonExplicitConvertingConstructor
  Task(const std::coroutine_handle<ScheduledPromiseType<ReturnType>> h) noexcept : handle_{h} {}
  Task(const Task &) = delete;
  Task(Task &&) = delete;
  constexpr ~Task() noexcept {
    if (handle_)
      handle_.destroy();
  }

  using promise_type = ScheduledPromiseType<ReturnType>;

  [[nodiscard]] constexpr bool await_ready() const noexcept { return handle_.done(); }
  constexpr void await_suspend(std::coroutine_handle<>) noexcept {}
  constexpr void await_resume() const noexcept {}

private:
  std::coroutine_handle<ScheduledPromiseType<ReturnType>> handle_;
};

inline void Clock::run_tick() {
  if (tasks_.empty()) {
    ++now_;
    return;
  }
  now_ = tasks_.front().tick_to_resume;
  while (!tasks_.empty() && tasks_.front().tick_to_resume == now_) {
    auto coro = tasks_.front().handle;
    tasks_.erase(tasks_.begin());
    coro.resume();
  }
  ++now_;
}

constexpr void CycleAwait::await_suspend(std::coroutine_handle<> handle) noexcept {
  clock.schedule(handle, this->ready_at);
}
constexpr bool CycleAwait::await_ready() const noexcept { return ready_at <= clock.now(); }

} // namespace specbolt
