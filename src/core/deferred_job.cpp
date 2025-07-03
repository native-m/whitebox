#include "deferred_job.h"

#include <condition_variable>
#include <mutex>

#include "queue.h"
#include "thread.h"

#define WB_MAX_DEFERRED_JOB 256

namespace wb {

struct alignas(64) DeferredJobSharedData {
  std::atomic_uint32_t pos;
  std::atomic_uint32_t should_signal;
};

struct DeferredJobItem {
  DeferredJobFn fn;
  DeferredJobContext context;
  uint32_t id;
};

static std::atomic_bool running = true;
static DeferredJobItem* job_items;
static DeferredJobSharedData writer_data;
static DeferredJobSharedData reader_data;
static std::mutex waiter_mtx;
static std::condition_variable waiter_cv;
static std::thread deferred_job_thread;
alignas(64) static std::atomic_uint32_t current_job_id;
static uint32_t job_generation;

static void deferred_worker_thread() {
#ifndef NDEBUG
  set_current_thread_name("Whitebox Deferred Job Runner");
#endif

  while (true) {
    uint32_t wpos = writer_data.pos.load(std::memory_order_acquire);
    uint32_t rpos = reader_data.pos.load(std::memory_order_relaxed);

    // Wait for write position to move
    if (wpos == rpos) {
      writer_data.should_signal.store(1, std::memory_order_release);
      writer_data.pos.wait(wpos, std::memory_order_relaxed);
      if (!running.load(std::memory_order_relaxed)) {
        return;
      }
      continue;
    }

    DeferredJobItem job_item = job_items[rpos];
    DeferredJobContext* ctx = &job_items[rpos].context;
    rpos = (rpos + 1) % WB_MAX_DEFERRED_JOB;
    reader_data.pos.store(rpos, std::memory_order_release);
    if (reader_data.should_signal.exchange(0, std::memory_order_release))
      reader_data.pos.notify_all();  // Notify writer thread

    job_item.fn(ctx);
    current_job_id.fetch_add(1, std::memory_order_release);
    waiter_cv.notify_all();
  }
}

void init_deferred_job() {
  job_items = new DeferredJobItem[WB_MAX_DEFERRED_JOB];
  deferred_job_thread = std::thread(deferred_worker_thread);
}

void shutdown_deferred_job() {
  wait_for_all_deferred_job();
  running = false;
  writer_data.pos.fetch_add(1, std::memory_order_acq_rel);
  writer_data.pos.notify_one();
  deferred_job_thread.join();
  delete[] job_items;
}

DeferredJobHandle enqueue_deferred_job(DeferredJobFn fn, void* userdata0, void* userdata1) {
  for (;;) {
    uint32_t wpos = writer_data.pos.load(std::memory_order_relaxed);
    uint32_t rpos = reader_data.pos.load(std::memory_order_acquire);
    uint32_t next_write_pos = (wpos + 1) % WB_MAX_DEFERRED_JOB;

    if (next_write_pos == rpos) {
      // The worker thread is still cookin', let's wait
      reader_data.should_signal.store(1, std::memory_order_release);
      reader_data.pos.wait(rpos, std::memory_order_relaxed);
      continue;
    }

    job_items[wpos] = {
      .fn = fn,
      .context = { userdata0, userdata1 },
      .id = job_generation,
    };

    writer_data.pos.store(next_write_pos, std::memory_order_release);
    if (writer_data.should_signal.exchange(0, std::memory_order_release))
      writer_data.pos.notify_all();  // Notify reader thread

    return job_generation++;
  }
  return 0;
}

void stop_deferred_job(DeferredJobHandle job_id) {
  uint32_t i = job_id % WB_MAX_DEFERRED_JOB;
  if (job_items[i].id == job_id) {
    job_items[i].context.request_stop = true;
  }
}

bool wait_for_deferred_job(DeferredJobHandle job_id, uint64_t timeout) {
  uint32_t i = job_id % WB_MAX_DEFERRED_JOB;

  // Check if this is a valid job
  if (job_items[i].id != job_id) {
    return false;
  }

  std::unique_lock lock(waiter_mtx);
  if (timeout == UINT64_MAX) {
    waiter_cv.wait(lock, [job_id] { return job_id < current_job_id.load(std::memory_order_relaxed); });
    return true;
  }

  waiter_cv.wait_for(
      lock, std::chrono::nanoseconds(timeout), [job_id] { return job_id < current_job_id.load(std::memory_order_relaxed); });

  return true;
}

void wait_for_all_deferred_job() {
  std::unique_lock lock(waiter_mtx);
  waiter_cv.wait(lock, [&] { return job_generation == current_job_id.load(std::memory_order_relaxed); });
}

}  // namespace wb