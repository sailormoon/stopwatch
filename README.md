# ⏱️ stopwatch
Single-header C++11 RDTSCP clock and timing utilities released into the public domain.

# why
While developing games, I have wanted the following features which are not provided by `std::chrono`:
1. triggering events after a certain amount of time
2. timing function calls in a high precision manner

# requirements
1. The `RDTSCP` instruction and a compiler which supports C++11 or higher.
2. Your processor must have an [Intel Nehalem (2008)](https://en.wikipedia.org/wiki/Nehalem_(microarchitecture)) or newer processor _or_ a processeor with an invariant TSC.

If you do not meet these requirements, you can easily remove the `RDTSCP` code from the library and enjoy the other features. The relevant sections of the [The Intel Software Developer Manuals](http://www.intel.com/Assets/en_US/PDF/manual/253668.pdf) are at the bottom of this page.

# usage
## timer
```c++
#include "stopwatch.h"
#include <chrono>
#include <iostream>
#include <thread>

int main() {
  const auto timer = stopwatch::make_timer(std::chrono::seconds(10));
  while (!timer.done()) {
    std::cout << std::chrono::duration_cast<std::chrono::seconds>(
                     timer.remaining())
                     .count()
              << " seconds remain." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::cout << "10 seconds have elapsed" << std::endl;
}
```

## timing one function call
```c++
#include "stopwatch.h"
#include <iostream>

int main() {
  const auto cycles = stopwatch::time([] {
    for (std::size_t i = 0; i < 10; ++i) {
      std::cout << i << std::endl;
    }
  });

  std::cout << "To print out 10 numbers, it took " << cycles.count()
            << " cycles." << std::endl;
}
```

## sampling multiple calls to a function
Taking the median number of cycles for inserting 10000 items into the beginning of a container.
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
Using another clock is as simple as passing the clock in as a template argument. An example using `std::chrono::system_clock` inplace of `stopwatch::rdtscp_clock` for the `timing one function call` example:
```c++
  const auto cycles = stopwatch::time<std::chrono::system_clock>([] {
    for (std::size_t i = 0; i < 10; ++i) {
      std::cout << i << std::endl;
    }
  });
```
`stopwatch::time([] { ... })` became `stopwatch::time<std::chrono::system_clock>([] { ... }`. That's it!

# contributing
Contributions of any variety are greatly appreciated. All code is passed through `clang-format` using the Google style.

## [The Intel Software Developer Manuals](http://www.intel.com/Assets/en_US/PDF/manual/253668.pdf)
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
