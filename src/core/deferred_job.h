#pragma once

#include "common.h"

namespace wb {

struct DeferredJobContext {
  void* userdata0;
  void* userdata1;
  volatile bool request_stop;
};

using DeferredJobFn = void(*)(DeferredJobContext* job_context);
using DeferredJobHandle = uint32_t;

void init_deferred_job();
void shutdown_deferred_job();
DeferredJobHandle enqueue_deferred_job(DeferredJobFn fn, void* userdata0 = nullptr, void* userdata1 = nullptr);
void stop_deferred_job(DeferredJobHandle job_id);
bool wait_for_deferred_job(DeferredJobHandle job_id, uint64_t timeout = UINT64_MAX);
void wait_for_all_deferred_job();

}  // namespace wb