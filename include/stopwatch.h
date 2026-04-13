#ifndef STOPWATCH_H_
#define STOPWATCH_H_

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ratio>

namespace stopwatch {
namespace detail {
// Prevents the compiler from reordering instructions across this barrier.
// This ensures that timed code stays between the two clock reads.
inline void compiler_fence() noexcept {
  std::atomic_signal_fence(std::memory_order_seq_cst);
}

// Prevents the compiler from optimizing away the given value.
template <class T>
inline void do_not_optimize(T const& value) noexcept {
  asm volatile("" : : "r,m"(value) : "memory");
}

template <class T>
inline void do_not_optimize(T& value) noexcept {
#if defined(__clang__)
  asm volatile("" : "+r,m"(value) : : "memory");
#else
  asm volatile("" : "+m,r"(value) : : "memory");
#endif
}
}  // namespace detail

#if defined(__x86_64__) || defined(_M_X64)
// An implementation of the 'TrivialClock' concept using the rdtscp instruction.
struct rdtscp_clock {
  using rep = std::uint64_t;
  using period = std::ratio<1>;
  using duration = std::chrono::duration<rep, period>;
  using time_point = std::chrono::time_point<rdtscp_clock, duration>;

  static auto now() noexcept -> time_point {
    std::uint32_t hi, lo;
    __asm__ __volatile__("rdtscp" : "=d"(hi), "=a"(lo));
    return time_point(duration((static_cast<std::uint64_t>(hi) << 32) | lo));
  }
};
using default_clock = rdtscp_clock;
#else
using default_clock = std::chrono::steady_clock;
#endif

// A timer using the specified clock.
template <class Clock = std::chrono::system_clock>
struct timer {
  using time_point = typename Clock::time_point;
  using duration = typename Clock::duration;

  timer(const duration duration) noexcept : expiry(Clock::now() + duration) {}
  timer(const time_point expiry) noexcept : expiry(expiry) {}

  [[nodiscard]] bool done(time_point now = Clock::now()) const noexcept {
    return now >= expiry;
  }

  [[nodiscard]] auto remaining(time_point now = Clock::now()) const noexcept
      -> duration {
    return expiry - now;
  }

  const time_point expiry;
};

template <class Clock = std::chrono::system_clock>
constexpr auto make_timer(typename Clock::duration duration) -> timer<Clock> {
  return timer<Clock>(duration);
}

// Times how long it takes a function to execute using the specified clock.
template <class Clock = default_clock, class Func>
[[nodiscard]] auto time(Func&& function) noexcept(
    noexcept(function())) -> typename Clock::duration {
  detail::compiler_fence();
  const auto start = Clock::now();
  detail::compiler_fence();
  std::forward<Func>(function)();
  detail::compiler_fence();
  const auto end = Clock::now();
  detail::compiler_fence();
  return end - start;
}

// Samples the given function N times using the specified clock.
template <std::size_t N, class Clock = default_clock, class Func>
[[nodiscard]] auto sample(Func&& function)
    -> std::array<typename Clock::duration, N> {
  std::array<typename Clock::duration, N> samples;

  for (std::size_t i = 0u; i < N; ++i) {
    samples[i] = time<Clock>(function);
  }

  std::sort(samples.begin(), samples.end());
  return samples;
}
}  // namespace stopwatch

#endif  // STOPWATCH_H_
