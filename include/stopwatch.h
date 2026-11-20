#ifndef STOPWATCH_H_
#define STOPWATCH_H_

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <ratio>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace stopwatch {
namespace detail {
// Prevents the compiler from reordering memory operations across this barrier.
// This ensures that timed code stays between the two clock reads.
inline void compiler_fence() noexcept {
  std::atomic_signal_fence(std::memory_order_seq_cst);
}

#if defined(_MSC_VER) && defined(_M_ARM64)
// Encodes an AArch64 system register for MSVC's _ReadStatusReg intrinsic,
// matching ARM64_SYSREG(op0, op1, crn, crm, op2) without pulling in <winnt.h>.
constexpr int encode_arm64_sysreg(int op0, int op1, int crn, int crm,
                                  int op2) noexcept {
  return ((op0 & 0x1) << 14) | ((op1 & 0x7) << 11) | ((crn & 0xF) << 7) |
         ((crm & 0xF) << 3) | (op2 & 0x7);
}
#endif
}  // namespace detail

// Prevents the compiler from optimizing away the given value.
#if defined(_MSC_VER) && !defined(__clang__)
template <class T>
inline void do_not_optimize(T const& value) noexcept {
  auto const* volatile sink = std::addressof(value);
  (void)sink;
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

// A timer using the specified clock.
template <class Clock = std::chrono::steady_clock>
struct timer {
  using clock = Clock;
  using time_point = typename Clock::time_point;
  using duration = typename Clock::duration;

  explicit timer(const duration d) noexcept(noexcept(Clock::now()))
      : expiry(Clock::now() + d) {}
  explicit timer(const time_point expiry) noexcept : expiry(expiry) {}

  template <class Rep, class Period>
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

template <class Clock = std::chrono::steady_clock, class Rep, class Period>
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

// Samples the given function N times using the specified clock.
template <std::size_t N, class Clock = default_clock, class Func,
          class... Args>
[[nodiscard]] auto sample(Func&& function, Args&&... args)
    -> std::array<typename Clock::duration, N> {
  std::array<typename Clock::duration, N> samples;

  for (std::size_t i = 0u; i < N; ++i) {
    samples[i] = time<Clock>(function, args...);
  }

  std::sort(samples.begin(), samples.end());
  return samples;
}
}  // namespace stopwatch

#endif  // STOPWATCH_H_
