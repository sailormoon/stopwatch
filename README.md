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
| **x86-64 (Intel & AMD)** | `stopwatch::rdtscp_clock` | `RDTSCP` + `LFENCE` | Requires [Intel Nehalem (2008)](https://en.wikipedia.org/wiki/Nehalem_(microarchitecture))+ or AMD Family 10h (2007)+ / Zen+ with an invariant TSC. Query runtime CPU support via `rdtscp_clock::is_supported()` and `rdtscp_clock::has_invariant_tsc()`. `duration::count()` is in raw TSC ticks. |
| **ARM64 / AArch64** | `stopwatch::cntvct_clock` | `CNTVCT_EL0` + `ISB` | Works from user space on Apple Silicon, Linux ARM64 (AWS Graviton, Ampere, Raspberry Pi 3/4/5), Android, and Windows on ARM64. `duration::count()` is in raw counter ticks; use `cntvct_clock::frequency()` (via `CNTFRQ_EL0`) or `cntvct_clock::to_nanoseconds()`. |
| **Other (e.g. 32-bit x86/ARM)** | `std::chrono::steady_clock` | OS monotonic clock | Portable fallback. |

See [references](#references) below for the official Intel, AMD, Arm, and MSVC architecture documentation.

# installation
Copy [`include/stopwatch.h`](include/stopwatch.h) into your project, or consume it via CMake (`add_subdirectory`, `FetchContent`, or `cmake --install` + `find_package`):
```cmake
find_package(stopwatch CONFIG REQUIRED) # if installed via cmake --install
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
> **Note:** `timer` defaults to `std::chrono::steady_clock`. Constructing a `timer` from a `std::chrono::duration` is intentionally disabled at compile time for raw hardware tick clocks (`rdtscp_clock`, `cntvct_clock`) so wall-clock durations like `10s` cannot silently become raw ticks; pass an explicit `time_point` if you need tick-based deadlines.

## elapsed & lap timing (`stopwatch::watch`)
```c++
#include "stopwatch.h"
#include <iostream>

int main() {
  stopwatch::watch w;
  // ... step 1 ...
  const auto step1 = w.lap();
  // ... step 2 ...
  const auto step2 = w.elapsed();
  w.reset();
  std::cout << "step 1: " << step1.count() << " ticks, step 2: "
            << step2.count() << " ticks\n";
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

To convert raw ticks from `stopwatch::default_clock` to nanoseconds portably across x86-64, ARM64, and fallback clocks, use `stopwatch::calibrate_frequency()` and `stopwatch::to_nanoseconds()` (instant via `CNTFRQ_EL0` on ARM64 and `Clock::period` for wall clocks, or a short spin calibration against `std::chrono::steady_clock` on x86-64):
```c++
  const double hz = stopwatch::calibrate_frequency();
  const auto ns = stopwatch::to_nanoseconds(ticks, hz);
  std::cout << ns.count() << " ns (" << hz << " Hz)" << std::endl;
```
On ARM64, you can also call `stopwatch::cntvct_clock::frequency()` and `stopwatch::cntvct_clock::to_nanoseconds(ticks)` directly.

## sampling multiple calls to a function
Taking summary statistics (after 10 warmup iterations) for inserting 10000 items into the beginning of a container.
```c++
#include "stopwatch.h"
#include <deque>
#include <iostream>
#include <vector>

int main() {
  const auto deque_samples = stopwatch::sample<100, 10>([] {
    std::deque<int> deque;
    for (std::size_t i = 0; i < 10000; ++i) {
      deque.insert(deque.begin(), i);
    }
  });

  const auto vector_samples = stopwatch::sample<100, 10>([] {
    std::vector<int> vector;
    for (std::size_t i = 0; i < 10000; ++i) {
      vector.insert(vector.begin(), i);
    }
  });

  const auto deque_stats = stopwatch::summarize(deque_samples);
  const auto vector_stats = stopwatch::summarize(vector_samples);

  std::cout << "median for deque: " << deque_stats.median.count()
            << " (p95: " << deque_stats.p95.count() << ")" << std::endl;
  std::cout << "median for vector: " << vector_stats.median.count()
            << " (p95: " << vector_stats.p95.count() << ")" << std::endl;
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
Contributions of any variety are greatly appreciated. All code is formatted with `clang-format` using the repository's [`.clang-format`](.clang-format) (Google style).

# references

## Intel (x86-64)
[Intel® 64 and IA-32 Architectures Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) — Volume 2A/2B (`RDTSCP`, `LFENCE`) and Volume 3B (*Time-Stamp Counter*):

### Invariant TSC (Volume 3B)
> The time stamp counter in newer processors may support an enhancement, referred
to as invariant TSC. Processor’s support for invariant TSC is indicated by
CPUID.80000007H:EDX[8].
The invariant TSC will run at a constant rate in all ACPI P-, C-. and T-states. This is
the architectural behavior moving forward. On processors with invariant TSC
support, the OS may use the TSC for wall clock timer services (instead of ACPI or
HPET timers). TSC reads are much more efficient and do not incur the overhead
associated with a ring transition or access to a platform resource.

### `IA32_TSC_AUX` Register and `RDTSCP` Support (Volume 3B)
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

## AMD (x86-64)
- [AMD64 Architecture Programmer's Manual, Volume 2: System Programming (Pub. #24593)](https://docs.amd.com/search/all?query=24593&content-lang=en-US) — Time-Stamp Counter and Invariant TSC (`CPUID Fn8000_0007_EDX[TscInvariant]`, bit 8, supported since AMD Family 10h and all Zen generations).
- [AMD64 Architecture Programmer's Manual, Volume 3: General-Purpose and System Instructions (Pub. #24594)](https://docs.amd.com/search/all?query=24594&content-lang=en-US) — `RDTSCP` (waits for all prior instructions, returns 64-bit TSC in `EDX:EAX` and `TSC_AUX` in `ECX`) and `LFENCE`.
- [Software Techniques for Managing Speculation on AMD Processors](https://www.amd.com/content/dam/amd/en/documents/resources/software-techniques-for-managing-speculation.pdf) (Mitigation G-2) — `LFENCE` dispatch serialization via `MSR C001_1029[1] = 1` (enabled by default on modern Linux and Windows kernels; always dispatch-serializing on Family 0Fh/11h).

## Arm (ARM64 / AArch64)
- [Arm® Architecture Reference Manual for A-profile architecture (ARM DDI 0487)](https://developer.arm.com/documentation/ddi0487/latest/) — Part D (*The Generic Timer in AArch64 state* and *Generic Timer registers*):
  - `CNTVCT_EL0` (Counter-timer Virtual Count Register): 64-bit monotonic virtual counter readable from unprivileged user space (`EL0`) across Apple Silicon, Linux ARM64, Android, and Windows on ARM64.
  - `CNTFRQ_EL0` (Counter-timer Frequency Register): system counter frequency in Hz, used by `cntvct_clock::frequency()` and `cntvct_clock::to_nanoseconds()`.
  - `ISB` (Instruction Synchronization Barrier): prevents speculative/out-of-order reads across `MRS CNTVCT_EL0` on all Armv8-A+ processors (whereas self-synchronized `CNTVCTSS_EL0` requires Armv8.6-A / `FEAT_ECV`).

## MSVC Intrinsics (Windows x64 & ARM64)
- [x64 (amd64) intrinsics list](https://learn.microsoft.com/en-us/cpp/intrinsics/x64-amd64-intrinsics-list?view=msvc-170) — `__rdtscp`, `_mm_lfence`.
- [ARM64 intrinsics](https://learn.microsoft.com/en-us/cpp/intrinsics/arm64-intrinsics?view=msvc-170) — `_ReadStatusReg`, `__isb`, `_ARM64_BARRIER_SY`.
