// Copyright (c) 2020, XMOS Ltd, All rights reserved
#include "lib_ops/api/pooling.h"

#include "lib_ops/api/benchmarking.h"
#include "lib_ops/api/tracing.h"

extern "C" {
#include "lib_nn/api/nn_types.h"
}

namespace xcore {
namespace pooling {

//**************************************
//**************************************
//**************************************
// MaxPool
//**************************************
//**************************************
//**************************************

MaxPool::MaxPool(const PoolingParams& params,
                 const ExecutionPlan& execution_plan)
    : params(params), execution_plan(execution_plan) {}

XCoreStatus MaxPool::Init(int32_t X_h, int32_t X_w, int32_t C_in, int32_t Y_h,
                          int32_t Y_w, int32_t C_out) {
  TRACE_INFO(
      "MaxPool Init id=%p X_h=%ld X_w=%ld C_in=%ld Y_h=%ld Y_w=%ld C_out=%ld\n",
      this, X_h, X_w, C_in, Y_h, Y_w, C_out);

  nn_image_params_t in_params;
  in_params.height = X_h;
  in_params.width = X_w;
  in_params.channels = C_in;

  nn_image_params_t out_params;
  out_params.height = Y_h;
  out_params.width = Y_w;
  out_params.channels = C_out;

  nn_window_op_config_t config;
  nn_window_op_config_simple(&config, &in_params, &out_params, params.pool_h,
                             params.pool_w, params.stride_h, params.stride_w);

  maxpool2d_init(&plan_, &in_params, &out_params, &config);

  return kXCoreOk;
}

XCoreStatus MaxPool::Eval(int8_t* Y, const int8_t* X) {
  TRACE_INFO("MaxPool Eval id=%p\n", this);
  TIMER_START();

  maxpool2d(Y, X, &plan_);

  TIMER_STOP("MaxPool id=%p", this);
  return kXCoreOk;
}

//**************************************
//**************************************
//**************************************
// AvgPool
//**************************************
//**************************************
//**************************************
AvgPool::AvgPool(const PoolingParams& params,
                 const ExecutionPlan& execution_plan)
    : params(params), execution_plan(execution_plan) {}

XCoreStatus AvgPool::Init(int32_t X_h, int32_t X_w, int32_t C_in, int32_t Y_h,
                          int32_t Y_w, int32_t C_out) {
  TRACE_INFO(
      "AvgPool Init id=%p X_h=%ld X_w=%ld C_in=%ld Y_h=%ld Y_w=%ld C_out=%ld\n",
      this, X_h, X_w, C_in, Y_h, Y_w, C_out);
  nn_image_params_t in_params;
  in_params.height = X_h;
  in_params.width = X_w;
  in_params.channels = C_in;

  nn_image_params_t out_params;
  out_params.height = Y_h;
  out_params.width = Y_w;
  out_params.channels = C_out;

  nn_window_op_config_t config;
  nn_window_op_config_simple(&config, &in_params, &out_params, params.pool_h,
                             params.pool_w, params.stride_h, params.stride_w);

  avgpool2d_init(&plan_, &in_params, &out_params, &config);

  return kXCoreOk;
}

XCoreStatus AvgPool::Eval(int8_t* Y, const int8_t* X) {
  TRACE_INFO("AvgPool Eval id=%p\n", this);
  TIMER_START();

  avgpool2d(Y, X, &plan_);

  TIMER_STOP("AvgPool id=%p", this);
  return kXCoreOk;
}

//**************************************
//**************************************
//**************************************
// AvgPool_Global
//**************************************
//**************************************
//**************************************

AvgPool_Global::AvgPool_Global(const ExecutionPlan& execution_plan)
    : execution_plan(execution_plan) {}

XCoreStatus AvgPool_Global::Init(int32_t bias, int32_t shift, int32_t scale) {
  TRACE_INFO("AvgPool_Global Init id=%p\n", this);
  bias_ = bias;
  shift_ = shift;
  scale_ = scale;

  return kXCoreOk;
}

XCoreStatus AvgPool_Global::Eval(int8_t* Y, const int8_t* X, int32_t X_h,
                                 int32_t X_w, uint32_t C_in) {
  TRACE_INFO("AvgPool_Global Eval id=%p\n", this);
  TIMER_START();

  avgpool2d_global(Y, X, X_h, X_w, C_in, bias_, shift_, scale_);

  TIMER_STOP("AvgPool_Global id=%p", this);
  return kXCoreOk;
}

}  // namespace pooling
}  // namespace xcore