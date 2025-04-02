#pragma once

#ifndef SPECBOLT_MODULES
#include <algorithm>
#include <coroutine>
#include <limits>
#include <vector>
#endif

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
struct CycleCount {
  std::size_t count;
};

struct ScheduledPromiseType;

SPECBOLT_EXPORT
class Clock {
  static constexpr std::size_t Capacity = 8;

public:
  void run() {
    while (!paused_)
      tick();
    paused_ = false;
  }

  void tick();

  void schedule(const std::coroutine_handle<ScheduledPromiseType> coro) { tasks_[task_count_++] = coro; }

  void pause() { paused_ = true; }

  constexpr auto begin() const { return tasks_.begin(); }
  constexpr auto end() const { return tasks_.begin() + task_count_; }

  [[nodiscard]] constexpr auto now() const { return now_; }

private:
  std::size_t now_{0};
  size_t task_count_{0};
  std::array<std::coroutine_handle<ScheduledPromiseType>, Capacity> tasks_;
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
struct ScheduledPromiseType {
  Clock &clock;
  std::size_t tick_to_resume{0};

  [[nodiscard]] constexpr bool ready(const std::size_t current_clock) const noexcept {
    return tick_to_resume == current_clock;
  }

  template<typename... Args>
  explicit constexpr ScheduledPromiseType(Clock &clock, Args &&...) noexcept : clock{clock} {
    // this will register this task with clock
    clock.schedule(std::coroutine_handle<ScheduledPromiseType>::from_promise(*this));
  }

  // this will take just simple Tick with number of ticks
  // and transform it into something we can await on
  constexpr MaybeSuspend await_transform(const CycleCount t) noexcept {
    tick_to_resume = clock.now() + t.count;
    return MaybeSuspend{t.count == 0};
  }

  constexpr void return_void() noexcept {}
  constexpr auto initial_suspend() noexcept { return std::suspend_always{}; }
  constexpr auto final_suspend() noexcept { return std::suspend_never{}; }
  constexpr void unhandled_exception() noexcept {}
  constexpr auto get_return_object() noexcept {
    return std::coroutine_handle<ScheduledPromiseType>::from_promise(*this);
  }
};


SPECBOLT_EXPORT
class Task {
public:
  // ReSharper disable once CppNonExplicitConvertingConstructor
  Task(const std::coroutine_handle<ScheduledPromiseType> h) noexcept : handle_{h} {}
  Task(const Task &) = delete;
  Task(Task &&) = delete;
  constexpr ~Task() noexcept {
    if (handle_)
      handle_.destroy();
  }

private:
  std::coroutine_handle<ScheduledPromiseType> handle_;
};

inline void Clock::tick() {
  for (auto &task: *this) {
    if (task.promise().ready(now_)) {
      task.resume();
    }
  }

  ++now_;
}

} // namespace specbolt

template<typename... Args>
struct std::coroutine_traits<specbolt::Task, Args...> {
  using promise_type = specbolt::ScheduledPromiseType;
};
