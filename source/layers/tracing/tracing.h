/*
 * Copyright (C) 2020-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "ze_api.h"
#include "layers/zel_tracing_api.h"
#include "layers/zel_tracing_register_cb.h"
#include "loader/ze_loader.h"
#include "ze_tracing_cb_structs.h"
#include "zer_tracing_cb_structs.h"

#include <string>
#include <vector>

struct _zel_tracer_handle_t {};

#define TRACING_COMP_NAME "tracing layer"
namespace tracing_layer {

// A single (hDriver, functionName) extension registration a tracer still holds,
// with which phases are live. Used to release the tracer's share of the shared
// driver-side install refcounts when it is destroyed.
struct TracerExtensionRegistration {
    ze_driver_handle_t hDriver;
    std::string functionName;
    bool hasPrologue;
    bool hasEpilogue;
};

struct APITracer : _zel_tracer_handle_t {
    static APITracer *create();
    virtual ~APITracer() = default;
    static APITracer *fromHandle(zel_tracer_handle_t handle) { return static_cast<APITracer *>(handle); }
    inline zel_tracer_handle_t toHandle() { return this; }
    virtual ze_result_t destroyTracer(zel_tracer_handle_t phTracer) = 0;
    virtual ze_result_t setPrologues(zel_core_callbacks_t *pCoreCbs) = 0;
    virtual ze_result_t setEpilogues(zel_core_callbacks_t *pCoreCbs) = 0;
    virtual zel_ze_all_callbacks_t& getZeProEpilogues(zel_tracer_reg_t callback_type, ze_result_t& result) = 0;
    virtual zel_zer_all_callbacks_t& getZerProEpilogues(zel_tracer_reg_t callback_type, ze_result_t& result) = 0;
    virtual ze_result_t resetAllCallbacks() = 0;
    virtual ze_result_t enableTracer(ze_bool_t enable) = 0;
    // Records/clears one extension-function prologue or epilogue slot for
    // (hDriver, functionName). pPrologueDelta / pEpilogueDelta (optional) each
    // report whether this tracer's slot for that phase transitioned empty->active
    // (+1), active->empty (-1), or was unchanged (0), so the caller can refcount
    // the driver-side wrapper install/uninstall per phase. A single call changes
    // at most one phase.
    virtual ze_result_t registerExtensionCallback(ze_driver_handle_t hDriver,
                                                  const char *functionName,
                                                  zel_tracer_reg_t callback_type,
                                                  zel_pfnDriverExtensionFunctionCb_t pCallback,
                                                  int *pPrologueDelta = nullptr,
                                                  int *pEpilogueDelta = nullptr) = 0;
    // Returns a copy of every extension registration this tracer currently holds,
    // so the caller can release the tracer's share of the driver-side install
    // refcounts when it is destroyed.
    virtual std::vector<TracerExtensionRegistration>
    snapshotExtensionRegistrations() = 0;
};

ze_result_t createAPITracer(const zel_tracer_desc_t *desc, zel_tracer_handle_t *phTracer);

struct APITracerContext {
    virtual ~APITracerContext() = default;
    virtual void *getActiveTracersList() = 0;
    virtual void releaseActivetracersList() = 0;
};

} // namespace tracing_layer
