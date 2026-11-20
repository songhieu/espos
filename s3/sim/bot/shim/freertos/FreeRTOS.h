#pragma once
// FreeRTOS's queues and tasks on the computer (std::thread), as much as src/bot uses.
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

typedef int BaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) (ms)

struct SimQueue {
  size_t item, len;
  std::deque<std::vector<uint8_t>> q;
  std::mutex m;
  std::condition_variable cv;
};
typedef SimQueue* QueueHandle_t;
typedef void* TaskHandle_t;

inline QueueHandle_t xQueueCreate(size_t len, size_t item) { return new SimQueue{item, len}; }
inline BaseType_t xQueueSend(QueueHandle_t h, const void* p, TickType_t) {
  std::lock_guard<std::mutex> l(h->m);
  h->q.emplace_back((const uint8_t*)p, (const uint8_t*)p + h->item);
  h->cv.notify_one();
  return pdTRUE;
}
inline BaseType_t xQueueReceive(QueueHandle_t h, void* p, TickType_t wait) {
  std::unique_lock<std::mutex> l(h->m);
  if (h->q.empty()) {
    if (!wait) return pdFALSE;
    if (wait == portMAX_DELAY) h->cv.wait(l, [&] { return !h->q.empty(); });
    else if (!h->cv.wait_for(l, std::chrono::milliseconds(wait), [&] { return !h->q.empty(); })) return pdFALSE;
  }
  memcpy(p, h->q.front().data(), h->item);
  h->q.pop_front();
  return pdTRUE;
}
inline BaseType_t xTaskCreatePinnedToCore(void (*fn)(void*), const char*, uint32_t, void* arg, int, TaskHandle_t*, int) {
  std::thread(fn, arg).detach();
  return pdPASS;
}
