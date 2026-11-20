# ⏱️ stopwatch
[![CI](https://github.com/sailormoon/stopwatch/actions/workflows/ci.yml/badge.svg)](https://github.com/sailormoon/stopwatch/actions/workflows/ci.yml)

Single-header C++17 hardware clock and timing utilities for x86-64 (Intel & AMD) and ARM64 (Apple Silicon, Linux, Windows), released into the public domain.

# why
While developing games, I have wanted the following features which are not provided by `std::chrono`:
1. triggering events after a certain amount of time
2. timing function calls in a high precision manner

# requirements & platform support
Requires a C++17 (or newer) compiler (GCC, Clang, or MSVC). `stopwatch::default_clock` automatically selects the best low-overhead monotonic clock for your target architecture:

| Architecture | `stopwatch::default_clock` | Hardware counter | Notes |
| --- | --- | --- | --- |
| **x86-64 (Intel & AMD)** | `stopwatch::rdtscp_clock` | `RDTSCP` + `LFENCE` | Requires [Intel Nehalem (2008)](https://en.wikipedia.org/wiki/Nehalem_(microarchitecture))+ or AMD Family 10h (2007)+ / Zen+ with an invariant TSC. `duration::count()` is in raw TSC ticks. |
| **ARM64 / AArch64** | `stopwatch::cntvct_clock` | `CNTVCT_EL0` + `ISB` | Works from user space on Apple Silicon, Linux ARM64 (AWS Graviton, Ampere, Raspberry Pi 3/4/5), Android, and Windows on ARM64. `duration::count()` is in raw counter ticks; use `cntvct_clock::frequency()` (via `CNTFRQ_EL0`) or `cntvct_clock::to_nanoseconds()`. |
| **Other (e.g. 32-bit ARM)** | `std::chrono::steady_clock` | OS monotonic clock | Portable fallback. |

The relevant sections of the [Intel Software Developer Manuals](https://www.intel.com/Assets/en_US/PDF/manual/253668.pdf) are at the bottom of this page.

# installation
Copy [`include/stopwatch.h`](include/stopwatch.h) into your project, or consume it via CMake (`add_subdirectory` or `FetchContent`):
```cmake
target_link_libraries(your_target PRIVATE stopwatch::stopwatch)
```

# usage
## timer
```c++
#include "stopwatch.h"
#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

int main() {
  const auto timer = stopwatch::make_timer(10s);
  while (!timer.done()) {
    std::cout << std::chrono::duration_cast<std::chrono::seconds>(
                     timer.remaining())
                     .count()
              << " seconds remain." << std::endl;
    std::this_thread::sleep_for(100ms);
  }
  std::cout << "10 seconds have elapsed" << std::endl;
}
```

## timing one function call
```c++
#include "stopwatch.h"
#include <iostream>

int main() {
  const auto ticks = stopwatch::time([] {
    for (std::size_t i = 0; i < 10; ++i) {
      std::cout << i << std::endl;
    }
  });

  std::cout << "To print out 10 numbers, it took " << ticks.count()
            << " ticks." << std::endl;
}
```

On ARM64, you can convert ticks from `stopwatch::cntvct_clock` to nanoseconds using the hardware counter frequency (`CNTFRQ_EL0`):
```c++
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
  const auto ns = stopwatch::cntvct_clock::to_nanoseconds(ticks);
  std::cout << ns.count() << " ns (" << stopwatch::cntvct_clock::frequency()
            << " Hz counter)" << std::endl;
#endif
```

## sampling multiple calls to a function
Taking the median number of ticks for inserting 10000 items into the beginning of a container.
```c++
#include "stopwatch.h"
#include <deque>
#include <iostream>
#include <vector>

int main() {
  const auto deque_samples = stopwatch::sample<100>([] {
    std::deque<int> deque;
    for (std::size_t i = 0; i < 10000; ++i) {
      deque.insert(deque.begin(), i);
    }
  });

  const auto vector_samples = stopwatch::sample<100>([] {
    std::vector<int> vector;
    for (std::size_t i = 0; i < 10000; ++i) {
      vector.insert(vector.begin(), i);
    }
  });

  std::cout << "median for deque: " << deque_samples[49].count() << std::endl;
  std::cout << "median for vector: " << vector_samples[49].count() << std::endl;
}
```

Output on my MacbookPro 2016:
```
median for deque:   487760
median for vector: 7595754
```

# using another clock
Using another clock is as simple as passing the clock in as a template argument. An example using `std::chrono::steady_clock` in place of `stopwatch::default_clock` for the `timing one function call` example:
```c++
  const auto elapsed = stopwatch::time<std::chrono::steady_clock>([] {
    for (std::size_t i = 0; i < 10; ++i) {
      std::cout << i << std::endl;
    }
  });
```
`stopwatch::time([] { ... })` became `stopwatch::time<std::chrono::steady_clock>([] { ... })`. That's it!

# preventing dead-code elimination
When microbenchmarking, the compiler may prove that a computed value is unused and remove the work entirely. `stopwatch::time` automatically passes non-void return values to `stopwatch::do_not_optimize`, or you can call `stopwatch::do_not_optimize` (and `stopwatch::clobber_memory`) directly:

```c++
#include "stopwatch.h"
#include <iostream>
#include <numeric>
#include <vector>

int main() {
  std::vector<int> v(10000, 1);

  const auto d = stopwatch::time([&] {
    auto sum = std::accumulate(v.begin(), v.end(), 0);
    stopwatch::do_not_optimize(sum);  // prevents dead-code elimination
  });

  std::cout << d.count() << " ticks" << std::endl;
}
```

# contributing
Contributions of any variety are greatly appreciated. All code is passed through `clang-format` using the Google style.

## [The Intel Software Developer Manuals](https://www.intel.com/Assets/en_US/PDF/manual/253668.pdf)
### Section 16.12.1
> The time stamp counter in newer processors may support an enhancement, referred
to as invariant TSC. Processor’s support for invariant TSC is indicated by
CPUID.80000007H:EDX[8].
The invariant TSC will run at a constant rate in all ACPI P-, C-. and T-states. This is
the architectural behavior moving forward. On processors with invariant TSC
support, the OS may use the TSC for wall clock timer services (instead of ACPI or
HPET timers). TSC reads are much more efficient and do not incur the overhead
associated with a ring transition or access to a platform resource.

### Section 16.12.2
> Processors based on Intel microarchitecture code name Nehalem provide an auxiliary
TSC register, IA32_TSC_AUX that is designed to be used in conjunction with
IA32_TSC. IA32_TSC_AUX provides a 32-bit field that is initialized by privileged software
with a signature value (for example, a logical processor ID).

> The primary usage of IA32_TSC_AUX in conjunction with IA32_TSC is to allow software
to read the 64-bit time stamp in IA32_TSC and signature value in
IA32_TSC_AUX with the instruction RDTSCP in an atomic operation. RDTSCP returns
the 64-bit time stamp in EDX:EAX and the 32-bit TSC_AUX signature value in ECX.
The atomicity of RDTSCP ensures that no context switch can occur between the reads
of the TSC and TSC_AUX values.
