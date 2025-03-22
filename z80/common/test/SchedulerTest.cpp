#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vector>

#ifdef SPECBOLT_MODULES
import z80_common;
#else
#include "z80/common/Scheduler.hpp"
#endif

#include <iostream>

using namespace specbolt;

namespace {

using vc = std::vector<std::size_t>;

struct TestTask final : Scheduler::Task {
  vc calls;
  void run(std::size_t ts) override { calls.emplace_back(ts); }
};

struct ReschedulingTestTask final : Scheduler::Task {
  explicit ReschedulingTestTask(Scheduler &scheduler) : scheduler(scheduler) {}
  Scheduler &scheduler;
  vc calls;
  void run(std::size_t ts) override {
    calls.emplace_back(ts);
    scheduler.schedule(*this, 10);
  }
};

} // namespace

TEST_CASE("Scheduler tests") {
  Scheduler scheduler;
  TestTask task_1;
  TestTask task_2;
  TestTask task_3;
  TestTask task_4;
  SECTION("Works when empty") {
    scheduler.tick(100);
    CHECK(scheduler.cycles() == 100);
  }
  SECTION("Doesnt call tasks if not their time yet") {
    scheduler.schedule(task_1, 150);
    scheduler.tick(100);
    CHECK(scheduler.cycles() == 100);
    CHECK(task_1.calls.empty());
  }
  SECTION("Calls a single task at the right time") {
    scheduler.schedule(task_1, 50);
    scheduler.tick(100);
    CHECK(task_1.calls == vc{50});
  }
  SECTION("Calls multiple tasks at the right time") {
    TestTask task_5;
    TestTask task_6;
    scheduler.schedule(task_1, 50);
    scheduler.schedule(task_2, 5);
    scheduler.schedule(task_3, 30);
    scheduler.schedule(task_4, 30);
    scheduler.schedule(task_5, 100);
    scheduler.schedule(task_6, 102);
    scheduler.tick(100);
    CHECK(task_1.calls == vc{50});
    CHECK(task_2.calls == vc{5});
    CHECK(task_3.calls == vc{30});
    CHECK(task_4.calls == vc{30});
    CHECK(task_5.calls == vc{100});
    CHECK(task_6.calls.empty());
  }
  SECTION("Handles rescheduling tasks") {
    ReschedulingTestTask rescheduling_task(scheduler);
    scheduler.schedule(task_1, 50);
    scheduler.schedule(rescheduling_task, 5);
    scheduler.tick(30);
    CHECK(task_1.calls.empty());
    CHECK(rescheduling_task.calls == vc{5, 15, 25});
  }
}

struct Task {
  struct promise_type {
    void return_void() {}
    auto initial_suspend() noexcept { return std::suspend_never{}; }
    auto final_suspend() noexcept { return std::suspend_always{}; }
    void unhandled_exception() {}
    Task get_return_object() { return {}; }
  };

  std::coroutine_handle<promise_type> handle{nullptr};
};

struct Clock {
  struct awaitable {
    Clock &clock;
    bool await_ready() { return false; }
    auto await_suspend(std::coroutine_handle<> h) {
      clock.tasks.emplace_back(h);
      // if (!clock.running) return std::coroutine_handle<>{};
      // return clock.pick_next_task();
    }
    void await_resume() {}
  };
  void run_for(size_t cycles) {
    // Maybe? probably not? Not sure how to do this.
    // at this point I want to keep picking "next" task and run it, and then keep going until "cycles" is done,
    // and then return back to caller.
    running = true;
    while (cycles--) {
      auto next = pick_next_task();
      if (next)
        next.resume();
    }
    running = false;
  }
  awaitable tick() { return awaitable{*this}; }
  std::coroutine_handle<> pick_next_task() {
    if (tasks.empty()) {
      return std::coroutine_handle<>{};
    }
    auto task = tasks.front();
    tasks.erase(tasks.begin());
    return task;
  }
  std::vector<std::coroutine_handle<>> tasks;
  bool running{false};
};

Task cpu(Clock &clock) {
  while (true) {
    co_await clock.tick();
    std::cout << "cpu\n";
  }
}

Task video(Clock &clock) {
  while (true) {
    for (int line = 0; line < 300; ++line) {
      std::cout << "line\n";
      co_await clock.tick();
    }
  }
}

void test() {
  Clock clock;
  [[maybe_unused]] auto cpu_task = cpu(clock);
  [[maybe_unused]] auto video_task = video(clock);
  // Example code...want to block here:
  clock.run_for(10);
}

TEST_CASE("ooh") { test(); }

// https://godbolt.org/z/PME51K9Ta
// https://godbolt.org/z/nb6bYGbsP
// https://godbolt.org/z/1G94bf47q
