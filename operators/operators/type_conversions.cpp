// Copyright (c) 2020, XMOS Ltd, All rights reserved
#include "operators/type_conversions.h"

extern "C" {
#include "lib_nn/api/nn_operator.h"
}

namespace xcore {
namespace type_conversions {

struct RequantizeThreadData {
  int8_t* Y;
  const int16_t* X;
  nn_requantize_16_to_8_job_t* job;
};

extern "C" {
ATTRIBUTE_THREAD_FUNCTION void requantize_16_to_8_thread_worker(void* context) {
  RequantizeThreadData* td = (RequantizeThreadData*)context;
  requantize_16_to_8(td->Y, td->X, td->job);
}
}

Requantize_16_to_8::Requantize_16_to_8()
    : stack_scratch_index_(-1), stack_size_(0) {}

void Requantize_16_to_8::Init(TfLiteContext* ctx) {
  // allocate the jobs
  ctx->AllocatePersistentBuffer(
      ctx, sizeof(nn_requantize_16_to_8_job_t) * execution_plan.GetNumThreads(),
      reinterpret_cast<void**>(&jobs_));
}

TfLiteStatus Requantize_16_to_8::Prepare(TfLiteContext* ctx, int32_t length) {
  Dispatcher* dispatcher = GetDispatcher();

  TF_LITE_REPORT_STATUS(dispatcher->GetReporter(),
                        "Requantize_16_to_8 Prepare length=%d", length);

  // allocate the stack for thread workers
  GET_STACKSIZE(stack_size_, requantize_16_to_8_thread_worker);
  TF_LITE_ENSURE_STATUS(ctx->RequestScratchBufferInArena(
      ctx, stack_size_ * execution_plan.GetNumThreads(),
      &stack_scratch_index_));

  // initialize the kernel
  requantize_16_to_8_init(jobs_, length, execution_plan.GetNumThreads());

  return kTfLiteOk;
}

TfLiteStatus Requantize_16_to_8::Eval(TfLiteContext* ctx, int8_t* Y,
                                      const int16_t* X) {
  Dispatcher* dispatcher = GetDispatcher();

  // initialize the dispatcher
  char* stack =
      static_cast<char*>(ctx->GetScratchBuffer(ctx, stack_scratch_index_));
  assert(stack);
  dispatcher->InitializeTasks(requantize_16_to_8_thread_worker, stack,
                              stack_size_);

  // create thread data and tasks
  RequantizeThreadData thread_data[execution_plan.GetNumThreads()];

  for (int i_job = 0; i_job < execution_plan.GetNumThreads(); i_job++) {
    thread_data[i_job].Y = Y;
    thread_data[i_job].X = X;
    thread_data[i_job].job = &jobs_[i_job];
    dispatcher->AddTask(reinterpret_cast<void*>(&thread_data[i_job]));
  }
  // start and wait for tasks to complete
  dispatcher->JoinTasks();

  return kTfLiteOk;
}

}  // namespace type_conversions
}  // namespace xcore
