#include "stopwatch.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace {

template <class Clock>
constexpr bool check_clock_traits() {
  static_assert(std::is_signed_v<typename Clock::rep>);
  static_assert(noexcept(Clock::now()));
  static_assert(std::is_same_v<decltype(Clock::is_steady), const bool>);
#if __cplusplus >= 202002L
  static_assert(std::chrono::is_clock_v<Clock>);
#endif
  return true;
}

static_assert(check_clock_traits<stopwatch::default_clock>());

struct FakeClock {
  using rep = std::int64_t;
  using period = std::milli;
  using duration = std::chrono::duration<rep, period>;
  using time_point = std::chrono::time_point<FakeClock, duration>;
  static constexpr bool is_steady = true;

  static inline rep current = 0;

  static auto now() noexcept -> time_point {
    return time_point(duration(current));
  }
};

static_assert(check_clock_traits<FakeClock>());

#if defined(__x86_64__) || defined(_M_X64)
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
std::uint64_t read_rdtscp_preserving_regs(std::uint64_t a, std::uint64_t b,
                                          std::uint64_t c, std::uint64_t d) {
  // On Windows x64, `a` arrives in RCX; on System V AMD64, `d` arrives in RCX.
  // Checking both verifies that RDTSCP's write to ECX/RCX never clobbers live
  // caller registers.
  const auto t1 = stopwatch::rdtscp_clock::now();
  const auto t2 = stopwatch::rdtscp_clock::now();
  if (t2 < t1) {
    return 0;
  }
  return a ^ b ^ c ^ d;
}
#endif

}  // namespace

// ---------------------------------------------------------------------------
// Hardware clocks (x86-64 Intel/AMD & ARM64)
// ---------------------------------------------------------------------------

TEST(DefaultClock, MonotonicAndAdvances) {
  const auto t1 = stopwatch::default_clock::now();
  std::this_thread::sleep_for(2ms);
  const auto t2 = stopwatch::default_clock::now();
  EXPECT_GT(t2, t1);
}

#if defined(__x86_64__) || defined(_M_X64)
static_assert(
    std::is_same_v<stopwatch::default_clock, stopwatch::rdtscp_clock>);

TEST(RdtscpClock, PreservesRcxRegister) {
  constexpr std::uint64_t kA = 0x1122334455667788ULL;
  constexpr std::uint64_t kB = 0x99AABBCCDDEEFF00ULL;
  constexpr std::uint64_t kC = 0x0123456789ABCDEFULL;
  constexpr std::uint64_t kD = 0xDEADBEEFCAFEBABEULL;
  EXPECT_EQ(read_rdtscp_preserving_regs(kA, kB, kC, kD), kA ^ kB ^ kC ^ kD);
}

TEST(RdtscpClock, EmptyCallableHasSaneTickCount) {
  const auto samples = stopwatch::sample<16, stopwatch::rdtscp_clock>([] {});
  const auto median = samples[samples.size() / 2];
  EXPECT_GE(median.count(), 0);
  // Sanity check that high/low 32-bit halves aren't corrupted into huge counts.
  EXPECT_LT(median.count(), 100'000'000LL);
}

TEST(RdtscpClock, TimerRemainingIsSignedWhenExpired) {
  const auto now = stopwatch::rdtscp_clock::now();
  const stopwatch::timer<stopwatch::rdtscp_clock> t(now);
  const auto later = now + stopwatch::rdtscp_clock::duration(100);
  EXPECT_TRUE(t.done(later));
  EXPECT_EQ(t.remaining(later).count(), -100);
}
#elif defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
static_assert(
    std::is_same_v<stopwatch::default_clock, stopwatch::cntvct_clock>);

TEST(CntvctClock, FrequencyIsPositive) {
  EXPECT_GT(stopwatch::cntvct_clock::frequency(), 0u);
}

TEST(CntvctClock, ToNanosecondsMatchesWallTime) {
  const auto ticks = stopwatch::time<stopwatch::cntvct_clock>([] {
    std::this_thread::sleep_for(5ms);
  });
  const auto ns = stopwatch::cntvct_clock::to_nanoseconds(ticks);
  EXPECT_GE(ns, 4ms);
  EXPECT_LT(ns, 5s);
}

TEST(CntvctClock, TimerRemainingIsSignedWhenExpired) {
  const auto now = stopwatch::cntvct_clock::now();
  const stopwatch::timer<stopwatch::cntvct_clock> t(now);
  const auto later = now + stopwatch::cntvct_clock::duration(100);
  EXPECT_TRUE(t.done(later));
  EXPECT_EQ(t.remaining(later).count(), -100);
}
#endif

// ---------------------------------------------------------------------------
// timer
// ---------------------------------------------------------------------------

TEST(Timer, DeterministicBoundaryAndRemaining) {
  FakeClock::current = 1000;
  const auto t = stopwatch::make_timer<FakeClock>(500ms);
  EXPECT_EQ(t.expiry.time_since_epoch().count(), 1500);

  const auto before = FakeClock::time_point(FakeClock::duration(1499));
  EXPECT_FALSE(t.done(before));
  EXPECT_EQ(t.remaining(before).count(), 1);

  const auto exact = FakeClock::time_point(FakeClock::duration(1500));
  EXPECT_TRUE(t.done(exact));
  EXPECT_EQ(t.remaining(exact).count(), 0);

  const auto after = FakeClock::time_point(FakeClock::duration(1750));
  EXPECT_TRUE(t.done(after));
  EXPECT_EQ(t.remaining(after).count(), -250);
}

TEST(Timer, AssignableAndAcceptsFloatingPointDuration) {
  auto t = stopwatch::make_timer(1.5s);
  EXPECT_FALSE(t.done());
  t = stopwatch::make_timer(10s);
  EXPECT_FALSE(t.done());
}

TEST(Timer, NotDoneImmediately) {
  const auto t = stopwatch::make_timer(10s);
  EXPECT_FALSE(t.done());
}

TEST(Timer, DoneAfterExpiry) {
  const auto t = stopwatch::make_timer(1ms);
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
  const auto t = stopwatch::make_timer(1ms);
  std::this_thread::sleep_for(10ms);
  EXPECT_LT(t.remaining().count(), 0);
}

TEST(Timer, ConstructFromTimePointAndCtad) {
  const auto expiry = std::chrono::steady_clock::now() + 5s;
  const stopwatch::timer t(expiry);
  EXPECT_FALSE(t.done());
  EXPECT_EQ(t.expiry, expiry);

  const auto sys_expiry = std::chrono::system_clock::now() + 5s;
  const auto sys_timer = stopwatch::make_timer(sys_expiry);
  EXPECT_FALSE(sys_timer.done());
  EXPECT_EQ(sys_timer.expiry, sys_expiry);
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

TEST(Time, DefaultClockMeasuresWork) {
  const auto d = stopwatch::time([] { std::this_thread::sleep_for(2ms); });
  EXPECT_GT(d.count(), 0);
}

TEST(Time, ForwardsArgumentsAndReturnValue) {
  int calls = 0;
  const auto d = stopwatch::time(
      [&](int a, int b) {
        ++calls;
        return a + b;
      },
      20, 22);
  EXPECT_EQ(calls, 1);
  EXPECT_GE(d.count(), 0);
}

TEST(Time, AcceptsRvalueOnlyCallableAndPropagatesNoexcept) {
  struct OnceNoexcept {
    void operator()() && noexcept {}
  };
  struct MayThrow {
    void operator()() const {}
  };

  static_assert(noexcept(stopwatch::time(OnceNoexcept{})));
  static_assert(!noexcept(stopwatch::time(MayThrow{})));

  const auto d = stopwatch::time(OnceNoexcept{});
  EXPECT_GE(d.count(), 0);
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

TEST(Sample, AllNonNegative) {
  const auto samples = stopwatch::sample<5, std::chrono::steady_clock>([] {});
  for (const auto& d : samples) {
    EXPECT_GE(d.count(), 0);
  }
}

TEST(Sample, CorrectSizeAndZeroSamples) {
  const auto samples =
      stopwatch::sample<7, std::chrono::steady_clock>([] {});
  EXPECT_EQ(samples.size(), 7u);

  const auto empty = stopwatch::sample<0>([] {});
  EXPECT_TRUE(empty.empty());
}

TEST(Sample, DefaultClockSamplesAreSorted) {
  const auto samples = stopwatch::sample<8>([] { return 42; });
  EXPECT_TRUE(std::is_sorted(samples.begin(), samples.end()));
  for (const auto& d : samples) {
    EXPECT_GE(d.count(), 0);
  }
}

// ---------------------------------------------------------------------------
// do_not_optimize & clobber_memory
// ---------------------------------------------------------------------------

TEST(DoNotOptimize, AcceptsLvalueConstRvalueAndContainers) {
  int x = 42;
  stopwatch::do_not_optimize(x);
  EXPECT_EQ(x, 42);

  double d = 3.5;
  stopwatch::do_not_optimize(d);
  EXPECT_DOUBLE_EQ(d, 3.5);

  const int cx = 7;
  stopwatch::do_not_optimize(cx);
  EXPECT_EQ(cx, 7);

  stopwatch::do_not_optimize(123);

  std::vector<int> v{1, 2, 3};
  stopwatch::do_not_optimize(v);
  EXPECT_EQ(v.size(), 3u);

  std::string s = "stopwatch";
  stopwatch::do_not_optimize(s);
  EXPECT_EQ(s, "stopwatch");

  struct OddSize {
    char bytes[3];
  };
  OddSize odd{{1, 2, 3}};
  stopwatch::do_not_optimize(odd);
  EXPECT_EQ(odd.bytes[2], 3);

  struct WithConst {
    const int value = 9;
  };
  WithConst wc{};
  stopwatch::do_not_optimize(wc);
  EXPECT_EQ(wc.value, 9);

  int arr[3] = {10, 20, 30};
  stopwatch::do_not_optimize(arr);
  EXPECT_EQ(arr[1], 20);

  // Backwards-compatible detail:: alias.
  stopwatch::detail::do_not_optimize(x);
  stopwatch::clobber_memory();
}
