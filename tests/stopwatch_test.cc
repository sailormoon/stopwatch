#include "stopwatch.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
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
static_assert(!stopwatch::is_tick_clock_v<std::chrono::steady_clock>);
static_assert(!stopwatch::is_tick_clock_v<std::chrono::system_clock>);
static_assert(!stopwatch::is_tick_clock_v<FakeClock>);

template <class Clock, class Duration, class = void>
struct can_make_timer : std::false_type {};

template <class Clock, class Duration>
struct can_make_timer<
    Clock, Duration,
    std::void_t<decltype(stopwatch::make_timer<Clock>(
        std::declval<Duration>()))>> : std::true_type {};

static_assert(
    std::is_constructible_v<stopwatch::timer<std::chrono::steady_clock>,
                            std::chrono::seconds>);
static_assert(
    std::is_constructible_v<stopwatch::timer<FakeClock>,
                            std::chrono::milliseconds>);
static_assert(can_make_timer<FakeClock, std::chrono::milliseconds>::value);

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
static_assert(stopwatch::is_tick_clock_v<stopwatch::rdtscp_clock>);
static_assert(
    !std::is_constructible_v<stopwatch::timer<stopwatch::rdtscp_clock>,
                             std::chrono::seconds>);
static_assert(
    !std::is_constructible_v<stopwatch::timer<stopwatch::rdtscp_clock>,
                             std::chrono::milliseconds>);
static_assert(
    !can_make_timer<stopwatch::rdtscp_clock, std::chrono::seconds>::value);
static_assert(
    std::is_constructible_v<stopwatch::timer<stopwatch::rdtscp_clock>,
                            stopwatch::rdtscp_clock::time_point>);

TEST(RdtscpClock, CpuidFeatureChecks) {
  EXPECT_TRUE(stopwatch::rdtscp_clock::is_supported());
  // Some virtualized hosts do not advertise invariant TSC even when RDTSCP is
  // supported, so only verify the query executes cleanly.
  const bool invariant = stopwatch::rdtscp_clock::has_invariant_tsc();
  (void)invariant;
}

TEST(RdtscpClock, PreservesRcxRegister) {
  constexpr std::uint64_t kA = 0x1122334455667788ULL;
  constexpr std::uint64_t kB = 0x99AABBCCDDEEFF00ULL;
  constexpr std::uint64_t kC = 0x0123456789ABCDEFULL;
  constexpr std::uint64_t kD = 0xDEADBEEFCAFEBABEULL;
  EXPECT_EQ(read_rdtscp_preserving_regs(kA, kB, kC, kD), kA ^ kB ^ kC ^ kD);
}

TEST(RdtscpClock, EmptyCallableHasSaneTickCount) {
  const auto samples = stopwatch::sample<16, stopwatch::rdtscp_clock>([] {});
  const auto stats = stopwatch::summarize(samples);
  EXPECT_GE(stats.median.count(), 0);
  // Sanity check that high/low 32-bit halves aren't corrupted into huge counts.
  EXPECT_LT(stats.median.count(), 100'000'000LL);
}

TEST(RdtscpClock, TimerRemainingIsSignedWhenExpired) {
  const auto now = stopwatch::rdtscp_clock::now();
  const auto t = stopwatch::make_timer(now);
  const auto later = now + stopwatch::rdtscp_clock::duration(100);
  EXPECT_TRUE(t.done(later));
  EXPECT_EQ(t.remaining(later).count(), -100);
}
#elif defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
static_assert(
    std::is_same_v<stopwatch::default_clock, stopwatch::cntvct_clock>);
static_assert(stopwatch::is_tick_clock_v<stopwatch::cntvct_clock>);
static_assert(
    !std::is_constructible_v<stopwatch::timer<stopwatch::cntvct_clock>,
                             std::chrono::seconds>);
static_assert(
    !std::is_constructible_v<stopwatch::timer<stopwatch::cntvct_clock>,
                             std::chrono::milliseconds>);
static_assert(
    !can_make_timer<stopwatch::cntvct_clock, std::chrono::seconds>::value);
static_assert(
    std::is_constructible_v<stopwatch::timer<stopwatch::cntvct_clock>,
                            stopwatch::cntvct_clock::time_point>);

TEST(CntvctClock, FrequencyIsPositive) {
  EXPECT_GT(stopwatch::cntvct_clock::frequency(), 0u);
  EXPECT_DOUBLE_EQ(
      stopwatch::calibrate_frequency<stopwatch::cntvct_clock>(),
      static_cast<double>(stopwatch::cntvct_clock::frequency()));
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
  const auto t = stopwatch::make_timer(now);
  const auto later = now + stopwatch::cntvct_clock::duration(100);
  EXPECT_TRUE(t.done(later));
  EXPECT_EQ(t.remaining(later).count(), -100);
}
#else
static_assert(
    std::is_same_v<stopwatch::default_clock, std::chrono::steady_clock>);
static_assert(!stopwatch::is_tick_clock_v<stopwatch::default_clock>);
#endif

// ---------------------------------------------------------------------------
// calibrate_frequency & to_nanoseconds
// ---------------------------------------------------------------------------

TEST(Calibration, WallClocksReturnExactPeriodFrequency) {
  EXPECT_DOUBLE_EQ(stopwatch::calibrate_frequency<FakeClock>(), 1000.0);
  const double expected_steady =
      static_cast<double>(std::chrono::steady_clock::period::den) /
      static_cast<double>(std::chrono::steady_clock::period::num);
  EXPECT_DOUBLE_EQ(stopwatch::calibrate_frequency<std::chrono::steady_clock>(),
                   expected_steady);
}

TEST(Calibration, DefaultClockAndToNanosecondsMatchWallTime) {
  const double hz = stopwatch::calibrate_frequency(10ms);
  EXPECT_GT(hz, 0.0);

  const auto ticks = stopwatch::time([] { std::this_thread::sleep_for(10ms); });
  const auto ns = stopwatch::to_nanoseconds(ticks, hz);
  EXPECT_GE(ns, 5ms);
  EXPECT_LT(ns, 5s);
}

TEST(Calibration, ToNanosecondsHandlesSignAndInvalidFrequency) {
  EXPECT_EQ(stopwatch::to_nanoseconds(FakeClock::duration(250), 1000.0), 250ms);
  EXPECT_EQ(stopwatch::to_nanoseconds(FakeClock::duration(-50), 1000.0), -50ms);
  EXPECT_EQ(stopwatch::to_nanoseconds(FakeClock::duration(100), 0.0), 0ns);
  EXPECT_EQ(stopwatch::to_nanoseconds(FakeClock::duration(100), -500.0), 0ns);
  EXPECT_EQ(stopwatch::to_nanoseconds(
                FakeClock::duration(100),
                std::numeric_limits<double>::quiet_NaN()),
            0ns);
}

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

TEST(Timer, ConstructFromTimePointDurationAndCtad) {
  const stopwatch::timer duration_timer(5s);
  static_assert(
      std::is_same_v<decltype(duration_timer),
                     const stopwatch::timer<std::chrono::steady_clock>>);
  EXPECT_FALSE(duration_timer.done());

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
// watch
// ---------------------------------------------------------------------------

TEST(Watch, DeterministicElapsedLapAndReset) {
  FakeClock::current = 100;
  stopwatch::watch<FakeClock> w;
  EXPECT_EQ(w.start_time().time_since_epoch().count(), 100);
  EXPECT_EQ(w.elapsed().count(), 0);

  FakeClock::current = 140;
  EXPECT_EQ(w.elapsed().count(), 40);
  EXPECT_EQ(w.lap().count(), 40);
  EXPECT_EQ(w.start_time().time_since_epoch().count(), 140);
  EXPECT_EQ(w.elapsed().count(), 0);

  FakeClock::current = 155;
  EXPECT_EQ(w.lap().count(), 15);
  EXPECT_EQ(w.start_time().time_since_epoch().count(), 155);

  FakeClock::current = 200;
  w.reset();
  EXPECT_EQ(w.elapsed().count(), 0);
  EXPECT_EQ(w.start_time().time_since_epoch().count(), 200);

  w.reset(FakeClock::time_point(FakeClock::duration(180)));
  EXPECT_EQ(w.elapsed().count(), 20);
}

TEST(Watch, CtadAndDefaultClockAdvances) {
  FakeClock::current = 50;
  const stopwatch::watch w_fake(FakeClock::now());
  static_assert(
      std::is_same_v<decltype(w_fake), const stopwatch::watch<FakeClock>>);
  FakeClock::current = 75;
  EXPECT_EQ(w_fake.elapsed().count(), 25);

  stopwatch::watch w;
  static_assert(
      std::is_same_v<decltype(w), stopwatch::watch<stopwatch::default_clock>>);
  std::this_thread::sleep_for(2ms);
  EXPECT_GT(w.elapsed().count(), 0);
  EXPECT_GT(w.lap().count(), 0);
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

TEST(Sample, WarmupIterationsAreExcludedFromResults) {
  FakeClock::current = 0;
  int calls = 0;
  const auto samples = stopwatch::sample<4, FakeClock, 3>([&] {
    ++calls;
    // Warmup calls take 100ms; measured calls take 40, 30, 20, 10ms.
    FakeClock::current += (calls <= 3) ? 100 : (8 - calls) * 10;
  });
  EXPECT_EQ(calls, 7);
  ASSERT_EQ(samples.size(), 4u);
  EXPECT_EQ(samples[0], 10ms);
  EXPECT_EQ(samples[1], 20ms);
  EXPECT_EQ(samples[2], 30ms);
  EXPECT_EQ(samples[3], 40ms);

  // Convenience overloads: sample<N, Warmup, Clock> and sample<N, Warmup>.
  int calls_swapped = 0;
  const auto swapped = stopwatch::sample<3, 2, FakeClock>([&] {
    ++calls_swapped;
    FakeClock::current += 5;
  });
  EXPECT_EQ(calls_swapped, 5);
  EXPECT_EQ(swapped[0], 5ms);

  int default_calls = 0;
  const auto def_samples = stopwatch::sample<4, 2>([&] { ++default_calls; });
  EXPECT_EQ(default_calls, 6);
  EXPECT_TRUE(std::is_sorted(def_samples.begin(), def_samples.end()));

  int warmup_only_calls = 0;
  const auto empty =
      stopwatch::sample<0, FakeClock, 3>([&] { ++warmup_only_calls; });
  EXPECT_EQ(warmup_only_calls, 3);
  EXPECT_TRUE(empty.empty());
}

// ---------------------------------------------------------------------------
// summarize
// ---------------------------------------------------------------------------

TEST(Summarize, OddAndEvenSampleCounts) {
  const std::array<FakeClock::duration, 5> odd = {10ms, 20ms, 30ms, 40ms,
                                                  100ms};
  const auto odd_stats = stopwatch::summarize(odd);
  EXPECT_EQ(odd_stats.min, 10ms);
  EXPECT_EQ(odd_stats.median, 30ms);
  EXPECT_EQ(odd_stats.p95, 40ms);
  EXPECT_EQ(odd_stats.p99, 40ms);
  EXPECT_EQ(odd_stats.max, 100ms);
  EXPECT_DOUBLE_EQ(odd_stats.mean, 40.0);

  const std::array<FakeClock::duration, 4> even = {10ms, 20ms, 30ms, 40ms};
  const auto even_stats = stopwatch::summarize(even);
  EXPECT_EQ(even_stats.min, 10ms);
  EXPECT_EQ(even_stats.median, 25ms);
  EXPECT_EQ(even_stats.max, 40ms);
  EXPECT_DOUBLE_EQ(even_stats.mean, 25.0);
}

TEST(Summarize, HundredSamplePercentiles) {
  std::array<FakeClock::duration, 100> samples{};
  for (std::size_t i = 0; i < samples.size(); ++i) {
    samples[i] = FakeClock::duration(static_cast<FakeClock::rep>(i * 2));
  }
  const auto stats = stopwatch::summarize(samples);
  EXPECT_EQ(stats.min.count(), 0);
  EXPECT_EQ(stats.median.count(), 99);
  EXPECT_EQ(stats.p95.count(), 188);
  EXPECT_EQ(stats.p99.count(), 196);
  EXPECT_EQ(stats.max.count(), 198);
  EXPECT_DOUBLE_EQ(stats.mean, 99.0);
}

TEST(Summarize, SingleZeroAndUnsortedInput) {
  const std::array<FakeClock::duration, 1> single = {42ms};
  const auto single_stats = stopwatch::summarize(single);
  EXPECT_EQ(single_stats.min, 42ms);
  EXPECT_EQ(single_stats.median, 42ms);
  EXPECT_EQ(single_stats.p95, 42ms);
  EXPECT_EQ(single_stats.p99, 42ms);
  EXPECT_EQ(single_stats.max, 42ms);
  EXPECT_DOUBLE_EQ(single_stats.mean, 42.0);

  const std::array<FakeClock::duration, 0> empty{};
  const auto empty_stats = stopwatch::summarize(empty);
  EXPECT_EQ(empty_stats.min, 0ms);
  EXPECT_EQ(empty_stats.median, 0ms);
  EXPECT_EQ(empty_stats.p95, 0ms);
  EXPECT_EQ(empty_stats.p99, 0ms);
  EXPECT_EQ(empty_stats.max, 0ms);
  EXPECT_DOUBLE_EQ(empty_stats.mean, 0.0);

  const std::array<FakeClock::duration, 5> unsorted = {50ms, 10ms, 30ms, 40ms,
                                                       20ms};
  const auto unsorted_stats = stopwatch::summarize(unsorted);
  EXPECT_EQ(unsorted_stats.min, 10ms);
  EXPECT_EQ(unsorted_stats.median, 30ms);
  EXPECT_EQ(unsorted_stats.max, 50ms);
  EXPECT_DOUBLE_EQ(unsorted_stats.mean, 30.0);
  // Caller's array must not be mutated.
  EXPECT_EQ(unsorted[0], 50ms);
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
