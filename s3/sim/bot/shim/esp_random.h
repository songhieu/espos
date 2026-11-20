#pragma once
#include <cstdint>
#include <random>
inline uint32_t esp_random() {
  static std::random_device rd;
  return rd();
}
