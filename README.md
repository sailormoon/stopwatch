# ⏱️ stopwatch
Single-header C++11 RDTSCP clock and timing utilities released into the public domain.

# why
While developing games, I have wanted the following features which are not provided by `std::chrono`:
1. triggering events after a certain amount of time
2. timing function calls in a high precision manner

# requirements
The `RDTSCP` instruction and a compiler which supports C++11 or higher.

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
#include <iostream>
#include "stopwatch.h"

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
#include <deque>
#include <iostream>
#include <vector>
#include "stopwatch.h"

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
