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
  explicit ReschedulingTestTask(Scheduler &scheduler_) : scheduler(scheduler_) {}
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
  SECTION("Handles scheduling tasks after an initial start") {
    scheduler.tick(10000);
    scheduler.schedule(task_1, 50);
    scheduler.schedule(task_2, 30);
    scheduler.schedule(task_3, 100);
    scheduler.tick(100);
    CHECK(task_1.calls == vc{10050});
    CHECK(task_2.calls == vc{10030});
    CHECK(task_3.calls == vc{10100});
    scheduler.schedule(task_1, 50);
    scheduler.tick(100);
    CHECK(task_1.calls == vc{10050, 10150});
    CHECK(task_2.calls == vc{10030});
    CHECK(task_3.calls == vc{10100});
  }
}

Task cpu(auto & /*clock*/) noexcept {
  while (true) {
    puts("CPU");
    co_await CycleCount{2};
  }
}

Task video(auto &clock) noexcept {
  while (true) {
    for (int line = 0; line < 300; ++line) {
      printf("LINE %d\n", line);
      co_await CycleCount{5};
    }
    clock.pause(); // will pause after drawing one screen
  }
}

Task print_info_about_tick(auto &clock) noexcept {
  while (true) {
    printf("\ntick: %zu ...\n", clock.counter);
    co_await CycleCount{1};
  }
}

void test() {
  Clock clock;
  [[maybe_unused]] auto cpu_task = cpu(clock);
  [[maybe_unused]] auto video_task = video(clock);
  clock.run();
}

TEST_CASE("ooh") { test(); }

// https://godbolt.org/z/PME51K9Ta
// https://godbolt.org/z/nb6bYGbsP
// https://godbolt.org/z/1G94bf47q
