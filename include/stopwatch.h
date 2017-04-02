#ifndef STOPWATCH_H_
#define STOPWATCH_H_

#include <array>
#include <chrono>
#include <cstdint>

namespace stopwatch {
// An implementation of the 'TrivialClock' concept using the rdtscp instruction.
struct rdtscp_clock {
  using rep = std::uint64_t;
  using period = std::ratio<1>;
  using duration = std::chrono::duration<rep, period>;
  using time_point = std::chrono::time_point<rdtscp_clock, duration>;

  static time_point now() noexcept {
    std::uint32_t hi, lo;
    __asm__ __volatile__("rdtscp" : "=d"(hi), "=a"(lo));
    return time_point(duration((static_cast<std::uint64_t>(hi) << 32) | lo));
  }
};

// A timer using the specified clock.
template <class Clock = std::chrono::system_clock>
struct timer {
  using time_point = typename Clock::time_point;
  using duration = typename Clock::duration;

  timer(const duration duration) : expiry(Clock::now() + duration) {}
  bool done() const { return Clock::now() >= expiry; }
  bool done(const time_point now) const { return now >= expiry; }
  duration remaining() const { return expiry - Clock::now(); };
  duration remaining(const time_point now) const { return expiry - now; }
  const time_point expiry;
};

template <class Clock = std::chrono::system_clock>
constexpr timer<Clock> make_timer(const typename Clock::duration duration) {
  return timer<Clock>(duration);
}

// Times how long it takes a function to execute using the specified clock.
template <class Clock = rdtscp_clock, class Func>
typename Clock::duration time(Func&& function) {
  const auto start = Clock::now();
  function();
  return Clock::now() - start;
}

// Samples the given function N times using the specified clock.
template <std::size_t N, class Clock = rdtscp_clock, class Func>
std::array<typename Clock::duration, N> sample(Func&& function) {
  std::array<typename Clock::duration, N> samples;
  for (std::size_t i = 0u; i < N; ++i) {
    samples[i] = time<Clock>(function);
  }
  std::sort(samples.begin(), samples.end());
  return samples;
}
}  // namespace stopwatch

#endif  // STOPWATCH_H_
