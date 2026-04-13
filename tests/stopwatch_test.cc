#include "stopwatch.h"

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// timer
// ---------------------------------------------------------------------------

TEST(Timer, NotDoneImmediately) {
  const auto t = stopwatch::make_timer(10s);
  EXPECT_FALSE(t.done());
}

TEST(Timer, DoneAfterExpiry) {
  const auto t = stopwatch::make_timer(std::chrono::milliseconds(1));
  std::this_thread::sleep_for(10ms);
  EXPECT_TRUE(t.done());
}

TEST(Timer, RemainingDecreases) {
  const auto t = stopwatch::make_timer(10s);
  const auto r1 = t.remaining();
  std::this_thread::sleep_for(5ms);
  const auto r2 = t.remaining();
  EXPECT_LT(r2, r1);
}

TEST(Timer, RemainingNegativeWhenExpired) {
  const auto t = stopwatch::make_timer(std::chrono::milliseconds(1));
  std::this_thread::sleep_for(10ms);
  EXPECT_LT(t.remaining().count(), 0);
}

TEST(Timer, ConstructFromTimePoint) {
  const auto expiry =
      std::chrono::system_clock::now() + std::chrono::seconds(5);
  const stopwatch::timer<> t(expiry);
  EXPECT_FALSE(t.done());
  EXPECT_EQ(t.expiry, expiry);
}

// ---------------------------------------------------------------------------
// time
// ---------------------------------------------------------------------------

TEST(Time, ReturnsPositiveDuration) {
  const auto d = stopwatch::time<std::chrono::steady_clock>([] {
    std::this_thread::sleep_for(1ms);
  });
  EXPECT_GT(d.count(), 0);
}

TEST(Time, SleeperTakesAtLeastRequested) {
  const auto d = stopwatch::time<std::chrono::steady_clock>([] {
    std::this_thread::sleep_for(5ms);
  });
  EXPECT_GE(d, 5ms);
}

// ---------------------------------------------------------------------------
// sample
// ---------------------------------------------------------------------------

TEST(Sample, ReturnsSortedArray) {
  const auto samples = stopwatch::sample<10, std::chrono::steady_clock>([] {
    std::this_thread::sleep_for(1ms);
  });
  EXPECT_TRUE(std::is_sorted(samples.begin(), samples.end()));
}

TEST(Sample, AllPositive) {
  const auto samples = stopwatch::sample<5, std::chrono::steady_clock>([] {});
  for (const auto& d : samples) {
    EXPECT_GE(d.count(), 0);
  }
}

TEST(Sample, CorrectSize) {
  const auto samples =
      stopwatch::sample<7, std::chrono::steady_clock>([] {});
  EXPECT_EQ(samples.size(), 7u);
}

// ---------------------------------------------------------------------------
// do_not_optimize
// ---------------------------------------------------------------------------

TEST(DoNotOptimize, AcceptsLvalue) {
  int x = 42;
  stopwatch::detail::do_not_optimize(x);
  EXPECT_EQ(x, 42);
}

TEST(DoNotOptimize, AcceptsConstLvalue) {
  const int x = 7;
  stopwatch::detail::do_not_optimize(x);
  EXPECT_EQ(x, 7);
}
