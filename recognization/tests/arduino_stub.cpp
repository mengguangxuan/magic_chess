#include "Arduino.h"

#include <chrono>
#include <thread>

uint32_t millis()
{
  static const auto start = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::steady_clock::now() - start;
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

void yield()
{
  std::this_thread::yield();
}
