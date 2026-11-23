#ifndef STOPWATCH_H_
#define STOPWATCH_H_

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <ratio>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__x86_64__)
#include <cpuid.h>
#endif

namespace stopwatch {
namespace detail {
// Prevents the compiler from reordering memory operations across this barrier.
// This ensures that timed code stays between the two clock reads.
inline void compiler_fence() noexcept {
  std::atomic_signal_fence(std::memory_order_seq_cst);
}

#if defined(_MSC_VER) && !defined(__clang__)
// Non-inlined, unoptimized sink used by do_not_optimize() on MSVC so values
// cannot be dead-code-eliminated even under Whole Program Optimization (/GL).
#pragma warning(push)
#pragma warning(disable : 4748)
#pragma optimize("", off)
inline __declspec(noinline) void do_not_optimize_sink(
    void const volatile* ptr) noexcept {
  (void)ptr;
}
#pragma optimize("", on)
#pragma warning(pop)
#endif

#if defined(__x86_64__) || defined(_M_X64)
// Queries an extended CPUID leaf (>= 0x80000000) and returns EDX, or 0 if the
// leaf is unsupported.
inline auto cpuid_ext_edx(std::uint32_t leaf) noexcept -> std::uint32_t {
#if defined(_MSC_VER)
  int regs[4] = {0, 0, 0, 0};
  __cpuid(regs, static_cast<int>(0x80000000u));
  if (static_cast<std::uint32_t>(regs[0]) < leaf) {
    return 0u;
  }
  __cpuid(regs, static_cast<int>(leaf));
  return static_cast<std::uint32_t>(regs[3]);
#else
  unsigned int eax = 0;
  unsigned int ebx = 0;
  unsigned int ecx = 0;
  unsigned int edx = 0;
  if (__get_cpuid(leaf, &eax, &ebx, &ecx, &edx) == 0) {
    return 0u;
  }
  return static_cast<std::uint32_t>(edx);
#endif
}
#endif

#if defined(_MSC_VER) && defined(_M_ARM64)
// Encodes an AArch64 system register for MSVC's _ReadStatusReg intrinsic,
// matching ARM64_SYSREG(op0, op1, crn, crm, op2) without pulling in <winnt.h>.
constexpr int encode_arm64_sysreg(int op0, int op1, int crn, int crm,
                                  int op2) noexcept {
  return ((op0 & 0x1) << 14) | ((op1 & 0x7) << 11) | ((crn & 0xF) << 7) |
         ((crm & 0xF) << 3) | (op2 & 0x7);
}
#endif

template <class Clock, class = void>
struct is_tick_clock_impl : std::false_type {};

template <class Clock>
struct is_tick_clock_impl<
    Clock, std::enable_if_t<std::is_convertible_v<
               decltype(Clock::is_tick_clock), bool>>>
    : std::bool_constant<static_cast<bool>(Clock::is_tick_clock)> {};

template <class Clock, class = void>
struct has_frequency : std::false_type {};

template <class Clock>
struct has_frequency<
    Clock,
    std::enable_if_t<std::is_arithmetic_v<decltype(Clock::frequency())>>>
    : std::true_type {};

template <class Clock>
inline constexpr bool has_frequency_v = has_frequency<Clock>::value;
}  // namespace detail

// True if Clock::duration represents raw hardware ticks rather than calibrated
// wall-clock time units.
template <class Clock>
struct is_tick_clock : detail::is_tick_clock_impl<Clock> {};

template <class Clock>
inline constexpr bool is_tick_clock_v = is_tick_clock<Clock>::value;

// Prevents the compiler from optimizing away the given value.
#if defined(_MSC_VER) && !defined(__clang__)
template <class T>
inline void do_not_optimize(T const& value) noexcept {
  detail::do_not_optimize_sink(std::addressof(value));
  detail::compiler_fence();
}
#else
template <class T>
inline void do_not_optimize(T const& value) noexcept {
  if constexpr ((std::is_arithmetic_v<T> || std::is_enum_v<T> ||
                 std::is_pointer_v<T>) &&
                !std::is_volatile_v<T> && sizeof(T) <= sizeof(void*)) {
    __asm__ __volatile__("" : : "r,m"(value) : "memory");
  } else {
    __asm__ __volatile__("" : : "m"(value) : "memory");
  }
}

template <class T>
inline void do_not_optimize(T& value) noexcept {
  // Avoid multi-alternative "+m,r" constraints on GCC, which can drop
  // initialization of memory operands (GCC PR105519 / PR124958).
  if constexpr ((std::is_arithmetic_v<T> || std::is_enum_v<T> ||
                 std::is_pointer_v<T>) &&
                !std::is_volatile_v<T> && sizeof(T) <= sizeof(void*)) {
    __asm__ __volatile__("" : "+r"(value) : : "memory");
  } else if constexpr (std::is_assignable_v<T&, T&> && !std::is_volatile_v<T>) {
    __asm__ __volatile__("" : "+m"(value) : : "memory");
  } else {
    __asm__ __volatile__("" : : "m"(value) : "memory");
  }
}
#endif

// Prevents the compiler from caching memory values across this call.
inline void clobber_memory() noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
  detail::compiler_fence();
#else
  __asm__ __volatile__("" : : : "memory");
#endif
}

namespace detail {
// Backwards-compatible alias for callers using
// stopwatch::detail::do_not_optimize.
using stopwatch::do_not_optimize;
}  // namespace detail

#if defined(__x86_64__) || defined(_M_X64)
// An implementation of the 'TrivialClock' concept using the x86-64 RDTSCP
// instruction (supported on Intel Nehalem+ and AMD Family 10h / Zen+ CPUs
// with an invariant TSC).
//
// Note: duration::count() represents raw TSC ticks, not wall-clock seconds.
struct rdtscp_clock {
  using rep = std::int64_t;
  using period = std::ratio<1>;
  using duration = std::chrono::duration<rep, period>;
  using time_point = std::chrono::time_point<rdtscp_clock, duration>;
  static constexpr bool is_steady = true;
  static constexpr bool is_tick_clock = true;

  // Returns true if the CPU reports RDTSCP support (CPUID 0x80000001:EDX[27]).
  [[nodiscard]] static bool is_supported() noexcept {
    return (detail::cpuid_ext_edx(0x80000001u) & (1u << 27)) != 0u;
  }

  // Returns true if the CPU reports an invariant TSC
  // (CPUID 0x80000007:EDX[8]).
  [[nodiscard]] static bool has_invariant_tsc() noexcept {
    return (detail::cpuid_ext_edx(0x80000007u) & (1u << 8)) != 0u;
  }

  static auto now() noexcept -> time_point {
#if defined(_MSC_VER)
    unsigned int aux = 0;
    const auto tsc = __rdtscp(&aux);
    _mm_lfence();
    return time_point(duration(static_cast<rep>(tsc)));
#else
    std::uint32_t hi = 0;
    std::uint32_t lo = 0;
    // RDTSCP waits for all prior instructions to execute and writes
    // IA32_TSC_AUX into ECX (zero-extending RCX). LFENCE prevents subsequent
    // instructions from executing speculatively before RDTSCP completes on
    // both Intel and modern AMD processors.
    __asm__ __volatile__(
        "rdtscp\n\t"
        "lfence"
        : "=d"(hi), "=a"(lo)
        :
        : "rcx", "memory");
    return time_point(duration(
        static_cast<rep>((static_cast<std::uint64_t>(hi) << 32) | lo)));
#endif
  }
};
using default_clock = rdtscp_clock;
#elif defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
// An implementation of the 'TrivialClock' concept using the AArch64 Generic
// Timer virtual counter (CNTVCT_EL0), readable from unprivileged user space on
// Apple Silicon, Linux ARM64 (e.g. AWS Graviton, Raspberry Pi), Android, and
// Windows on ARM64.
//
// Note: duration::count() represents raw counter ticks at frequency() Hz, not
// wall-clock seconds.
struct cntvct_clock {
  using rep = std::int64_t;
  using period = std::ratio<1>;
  using duration = std::chrono::duration<rep, period>;
  using time_point = std::chrono::time_point<cntvct_clock, duration>;
  static constexpr bool is_steady = true;
  static constexpr bool is_tick_clock = true;

  static auto now() noexcept -> time_point {
#if defined(_MSC_VER)
    // CNTVCT_EL0: op0=3, op1=3, CRn=14, CRm=0, op2=2.
    constexpr int kCntvctEl0 = detail::encode_arm64_sysreg(3, 3, 14, 0, 2);
    __isb(_ARM64_BARRIER_SY);
    const auto cnt = _ReadStatusReg(kCntvctEl0);
    __isb(_ARM64_BARRIER_SY);
    return time_point(duration(static_cast<rep>(cnt)));
#else
    std::uint64_t cnt = 0;
    // ISB before and after MRS prevents speculative/out-of-order execution
    // across the counter read on ARM64.
    __asm__ __volatile__(
        "isb\n\t"
        "mrs %0, cntvct_el0\n\t"
        "isb"
        : "=r"(cnt)
        :
        : "memory");
    return time_point(duration(static_cast<rep>(cnt)));
#endif
  }

  // Returns the hardware counter frequency in ticks per second (Hz) from
  // CNTFRQ_EL0.
  [[nodiscard]] static auto frequency() noexcept -> std::uint64_t {
#if defined(_MSC_VER)
    // CNTFRQ_EL0: op0=3, op1=3, CRn=14, CRm=0, op2=0.
    constexpr int kCntfrqEl0 = detail::encode_arm64_sysreg(3, 3, 14, 0, 0);
    return static_cast<std::uint64_t>(_ReadStatusReg(kCntfrqEl0));
#else
    std::uint64_t freq = 0;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq;
#endif
  }

  // Converts a tick duration to nanoseconds using frequency().
  [[nodiscard]] static auto to_nanoseconds(duration d) noexcept
      -> std::chrono::nanoseconds {
    const auto freq = static_cast<rep>(frequency());
    if (freq <= 0) {
      return std::chrono::nanoseconds(0);
    }
    const auto ticks = d.count();
    const auto secs = ticks / freq;
    const auto rem = ticks % freq;
    return std::chrono::nanoseconds(
        secs * 1'000'000'000LL + (rem * 1'000'000'000LL) / freq);
  }
};
using default_clock = cntvct_clock;
#else
using default_clock = std::chrono::steady_clock;
#endif

// Returns the tick frequency of Clock in Hz (ticks per second).
// - For clocks with a hardware frequency() register (e.g. cntvct_clock),
//   returns Clock::frequency() immediately.
// - For calibrated wall-clock std::chrono clocks, returns the exact rate from
//   Clock::period without waiting.
// - For uncalibrated tick clocks (e.g. rdtscp_clock), measures ticks against
//   std::chrono::steady_clock over sample_window (clamped to >= 1ms).
template <class Clock = default_clock>
[[nodiscard]] auto calibrate_frequency(
    std::chrono::milliseconds sample_window = std::chrono::milliseconds(10))
    -> double {
  if constexpr (detail::has_frequency_v<Clock>) {
    (void)sample_window;
    return static_cast<double>(Clock::frequency());
  } else if constexpr (!is_tick_clock_v<Clock>) {
    (void)sample_window;
    return static_cast<double>(Clock::period::den) /
           static_cast<double>(Clock::period::num);
  } else {
    using ref_clock = std::chrono::steady_clock;
    const auto window = sample_window > std::chrono::milliseconds(0)
                            ? sample_window
                            : std::chrono::milliseconds(1);
    detail::compiler_fence();
    const auto ref_start = ref_clock::now();
    const auto tick_start = Clock::now();
    detail::compiler_fence();
    auto ref_end = ref_start;
    do {
      ref_end = ref_clock::now();
    } while (ref_end - ref_start < window);
    detail::compiler_fence();
    const auto tick_end = Clock::now();
    detail::compiler_fence();

    const auto ref_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(ref_end -
                                                             ref_start)
            .count();
    const auto ticks = (tick_end - tick_start).count();
    if (ref_ns <= 0 || ticks <= 0) {
      return 0.0;
    }
    return (static_cast<double>(ticks) / static_cast<double>(ref_ns)) * 1e9;
  }
}

// Converts a duration (in Clock::duration::count() ticks) to nanoseconds using
// the given frequency in Hz (ticks per second), e.g. from
// calibrate_frequency<Clock>().
template <class Rep, class Period>
[[nodiscard]] auto to_nanoseconds(std::chrono::duration<Rep, Period> d,
                                  double frequency_hz) noexcept
    -> std::chrono::nanoseconds {
  if (!(frequency_hz > 0.0)) {
    return std::chrono::nanoseconds(0);
  }
  const double ns = (static_cast<double>(d.count()) / frequency_hz) * 1e9;
  return std::chrono::nanoseconds(
      static_cast<std::chrono::nanoseconds::rep>(std::llround(ns)));
}

// A timer using the specified clock.
template <class Clock = std::chrono::steady_clock>
struct timer {
  using clock = Clock;
  using time_point = typename Clock::time_point;
  using duration = typename Clock::duration;

  explicit timer(const time_point expiry) noexcept : expiry(expiry) {}

  // Duration construction is disabled for raw tick clocks (such as
  // rdtscp_clock and cntvct_clock), where duration::period is ratio<1> and
  // wall-clock durations like std::chrono::seconds would silently mean raw
  // ticks. Use a time_point (e.g. Clock::now() + duration(ticks)) or a
  // wall-clock such as std::chrono::steady_clock instead.
  template <class Rep, class Period, class C = Clock,
            std::enable_if_t<!is_tick_clock_v<C>, int> = 0>
  explicit timer(const std::chrono::duration<Rep, Period> d) noexcept(
      noexcept(Clock::now()))
      : expiry(Clock::now() + std::chrono::duration_cast<duration>(d)) {}

  [[nodiscard]] bool done(time_point now = Clock::now()) const noexcept {
    return now >= expiry;
  }

  [[nodiscard]] auto remaining(time_point now = Clock::now()) const noexcept
      -> duration {
    return expiry - now;
  }

  time_point expiry;
};

template <class Clock, class Duration>
timer(std::chrono::time_point<Clock, Duration>) -> timer<Clock>;

template <class Rep, class Period>
timer(std::chrono::duration<Rep, Period>) -> timer<std::chrono::steady_clock>;

template <class Clock = std::chrono::steady_clock, class Rep, class Period,
          std::enable_if_t<!is_tick_clock_v<Clock>, int> = 0>
[[nodiscard]] auto make_timer(std::chrono::duration<Rep, Period> d) noexcept(
    noexcept(Clock::now())) -> timer<Clock> {
  return timer<Clock>(d);
}

template <class Clock, class Duration>
[[nodiscard]] auto make_timer(
    std::chrono::time_point<Clock, Duration> expiry) noexcept -> timer<Clock> {
  return timer<Clock>(
      std::chrono::time_point_cast<typename Clock::duration>(expiry));
}

// A lightweight monotonic stopwatch supporting elapsed time, lap timing, and
// reset.
template <class Clock = default_clock>
class watch {
 public:
  using clock = Clock;
  using time_point = typename Clock::time_point;
  using duration = typename Clock::duration;

  watch() noexcept(noexcept(Clock::now())) : start_(read_now()) {}
  explicit watch(time_point start) noexcept : start_(start) {}

  // Returns the time elapsed since construction or the last reset()/lap().
  [[nodiscard]] auto elapsed() const noexcept(noexcept(Clock::now()))
      -> duration {
    return read_now() - start_;
  }

  // Returns the time elapsed since construction or the last reset()/lap() and
  // restarts the stopwatch from that same timestamp.
  [[nodiscard]] auto lap() noexcept(noexcept(Clock::now())) -> duration {
    const auto now = read_now();
    const auto d = now - start_;
    start_ = now;
    return d;
  }

  // Restarts the stopwatch from the current time (or an explicit timestamp).
  void reset() noexcept(noexcept(Clock::now())) { start_ = read_now(); }
  void reset(time_point start) noexcept { start_ = start; }

  // Returns the current start timestamp.
  [[nodiscard]] auto start_time() const noexcept -> time_point {
    return start_;
  }

 private:
  static auto read_now() noexcept(noexcept(Clock::now())) -> time_point {
    detail::compiler_fence();
    const auto now = Clock::now();
    detail::compiler_fence();
    return now;
  }

  time_point start_;
};

template <class Clock, class Duration>
watch(std::chrono::time_point<Clock, Duration>) -> watch<Clock>;

// Times how long it takes a function to execute using the specified clock.
// If the callable returns a value, it is passed to do_not_optimize() to
// prevent dead-code elimination.
template <class Clock = default_clock, class Func, class... Args>
[[nodiscard]] auto time(Func&& function, Args&&... args) noexcept(
    noexcept(Clock::now()) &&
    noexcept(std::invoke(std::forward<Func>(function),
                         std::forward<Args>(args)...)))
    -> typename Clock::duration {
  detail::compiler_fence();
  const auto start = Clock::now();
  detail::compiler_fence();
  if constexpr (std::is_void_v<std::invoke_result_t<Func, Args...>>) {
    std::invoke(std::forward<Func>(function), std::forward<Args>(args)...);
  } else {
    const auto& result =
        std::invoke(std::forward<Func>(function), std::forward<Args>(args)...);
    do_not_optimize(result);
  }
  detail::compiler_fence();
  const auto end = Clock::now();
  detail::compiler_fence();
  return end - start;
}

// Samples the given function N times (after Warmup unrecorded iterations)
// using the specified clock, returning the sorted array of durations.
template <std::size_t N, class Clock = default_clock, std::size_t Warmup = 0u,
          class Func, class... Args>
[[nodiscard]] auto sample(Func&& function, Args&&... args)
    -> std::array<typename Clock::duration, N> {
  if constexpr (Warmup == 0u && N == 0u) {
    (void)function;
    ((void)args, ...);
  }
  if constexpr (Warmup > 0u) {
    for (std::size_t i = 0u; i < Warmup; ++i) {
      (void)time<Clock>(function, args...);
    }
  }

  std::array<typename Clock::duration, N> samples{};
  if constexpr (N > 0u) {
    for (std::size_t i = 0u; i < N; ++i) {
      samples[i] = time<Clock>(function, args...);
    }

    std::sort(samples.begin(), samples.end());
  }
  return samples;
}

// Convenience overload supporting sample<N, Warmup>(fn, ...) and
// sample<N, Warmup, Clock>(fn, ...).
template <std::size_t N, std::size_t Warmup, class Clock = default_clock,
          class Func, class... Args>
[[nodiscard]] auto sample(Func&& function, Args&&... args)
    -> std::array<typename Clock::duration, N> {
  return sample<N, Clock, Warmup>(std::forward<Func>(function),
                                  std::forward<Args>(args)...);
}

// Summary statistics for a fixed-size array of sampled durations.
template <class Duration>
struct summary {
  Duration min = Duration::zero();
  Duration median = Duration::zero();
  Duration p95 = Duration::zero();
  Duration p99 = Duration::zero();
  Duration max = Duration::zero();
  double mean = 0.0;
};

// Computes min, median, p95, p99, max, and mean (in Duration::count() units)
// for a sample array. Accepts sorted or unsorted arrays without mutating the
// caller's input.
template <class Duration, std::size_t N>
[[nodiscard]] auto summarize(const std::array<Duration, N>& samples)
    -> summary<Duration> {
  summary<Duration> result{};
  if constexpr (N == 0u) {
    (void)samples;
    return result;
  } else {
    auto sorted = samples;
    if (!std::is_sorted(sorted.begin(), sorted.end())) {
      std::sort(sorted.begin(), sorted.end());
    }

    result.min = sorted.front();
    result.max = sorted.back();
    if constexpr (N % 2u == 1u) {
      result.median = sorted[N / 2u];
    } else {
      const auto lo = sorted[(N / 2u) - 1u];
      const auto hi = sorted[N / 2u];
      result.median = lo + (hi - lo) / 2;
    }
    result.p95 = sorted[((N - 1u) * std::size_t{95}) / std::size_t{100}];
    result.p99 = sorted[((N - 1u) * std::size_t{99}) / std::size_t{100}];

    double total = 0.0;
    for (const auto& d : sorted) {
      total += static_cast<double>(d.count());
    }
    result.mean = total / static_cast<double>(N);
    return result;
  }
}
}  // namespace stopwatch

#endif  // STOPWATCH_H_
