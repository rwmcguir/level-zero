/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "tracing.h"
#include "tracing_imp.h"
#include "ze_tracing_layer.h"
#include "layers/zel_tracing_api.h"
#include "layers/zel_tracing_ddi.h"
#include "loader/ze_loader.h"

namespace tracing {
ZE_APIEXPORT ze_result_t ZE_APICALL
zelTracerCreate(
    const zel_tracer_desc_t *desc,
    zel_tracer_handle_t *phTracer) {
    return tracing_layer::createAPITracer(desc, phTracer);
}

ZE_APIEXPORT ze_result_t ZE_APICALL
zelTracerDestroy(
    zel_tracer_handle_t hTracer) {
    return tracing_layer::APITracer::fromHandle(hTracer)->destroyTracer(hTracer);
}

ZE_APIEXPORT ze_result_t ZE_APICALL
zelTracerSetPrologues(
    zel_tracer_handle_t hTracer,
    zel_core_callbacks_t *pCoreCbs) {
    return tracing_layer::APITracer::fromHandle(hTracer)->setPrologues(pCoreCbs);
}

ZE_APIEXPORT ze_result_t ZE_APICALL
zelTracerSetEpilogues(
    zel_tracer_handle_t hTracer,
    zel_core_callbacks_t *pCoreCbs) {
    return tracing_layer::APITracer::fromHandle(hTracer)->setEpilogues(pCoreCbs);
}

ZE_APIEXPORT ze_result_t ZE_APICALL
zelTracerSetEnabled(
    zel_tracer_handle_t hTracer,
    ze_bool_t enable) {
    return tracing_layer::APITracer::fromHandle(hTracer)->enableTracer(enable);
}

}
#if defined(__cplusplus)
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////
/// @brief Exported function for filling application's TracerExp table
///        with current process' addresses
///
/// @returns
///     - ::ZE_RESULT_SUCCESS
///     - ::ZE_RESULT_ERROR_INVALID_NULL_POINTER
///     - ::ZE_RESULT_ERROR_UNSUPPORTED_VERSION
ZE_DLLEXPORT ze_result_t ZE_APICALL
zelGetTracerApiProcAddrTable(
    ze_api_version_t version,                       ///< [in] API version requested
    zel_tracer_dditable_t* pDdiTable               ///< [in,out] pointer to table of DDI function pointers
    )
{
    if( nullptr == pDdiTable )
        return ZE_RESULT_ERROR_INVALID_NULL_POINTER;

    if( tracing_layer::context.version < version )
        return ZE_RESULT_ERROR_UNSUPPORTED_VERSION;

    ze_result_t result = ZE_RESULT_SUCCESS;

    pDdiTable->pfnCreate                                 = tracing::zelTracerCreate;

    pDdiTable->pfnDestroy                                = tracing::zelTracerDestroy;

    pDdiTable->pfnSetPrologues                           = tracing::zelTracerSetPrologues;

    pDdiTable->pfnSetEpilogues                           = tracing::zelTracerSetEpilogues;

    pDdiTable->pfnSetEnabled                             = tracing::zelTracerSetEnabled;

    return result;
}

///////////////////////////////////////////////////////////////////////////////
/// @brief Registers a prologue or epilogue callback on a tracer for a named
///        extension function of a specific driver. See loader/ze_loader.h.
ZE_DLLEXPORT ze_result_t ZE_APICALL
zelTracerDriverExtensionRegisterCallback(
    zel_tracer_handle_t hTracer,
    ze_driver_handle_t hDriver,
    const char* functionName,
    zel_tracer_reg_t callback_type,
    zel_pfnDriverExtensionFunctionCb_t pCallback
    )
{
    if( nullptr == hTracer )
        return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
    if( nullptr == hDriver )
        return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
    if( nullptr == functionName )
        return ZE_RESULT_ERROR_INVALID_NULL_POINTER;

    // Record the app callback on the tracer (only valid while disabled). The
    // per-phase deltas report whether this tracer just began (+1) or stopped (-1)
    // tracing the prologue and/or epilogue of this (hDriver, functionName), so the
    // shared driver-side wrappers can be refcounted per phase across all tracers.
    // A single call changes at most one phase.
    int prologueDelta = 0;
    int epilogueDelta = 0;
    ze_result_t result = tracing_layer::APITracer::fromHandle(hTracer)
        ->registerExtensionCallback(hDriver, functionName, callback_type, pCallback,
                                    &prologueDelta, &epilogueDelta);
    if( result != ZE_RESULT_SUCCESS )
        return result;

    // Apply the per-phase deltas to the shared driver-side install refcounts.
    // Nothing to do on the driver unless a phase just crossed its 0<->1 boundary
    // (e.g. a 2nd tracer registering the same phase is idempotent).
    auto install = tracing_layer::updateLoaderExtensionInstall(
        hDriver, functionName, prologueDelta, epilogueDelta );
    if( !install.prologueInstallChanged && !install.epilogueInstallChanged )
        return ZE_RESULT_SUCCESS;

    // Resolve the driver's by-name loader-callback setter (same mechanism as the
    // per-API GetExtensionFunctionAddress interceptor). Needed for both install
    // and uninstall.
    auto pfnGetExtensionFunctionAddress =
        tracing_layer::context.zeDdiTable.Driver.pfnGetExtensionFunctionAddress;
    if( nullptr == pfnGetExtensionFunctionAddress )
        return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;

    void* pfnRaw = nullptr;
    result = pfnGetExtensionFunctionAddress(
        hDriver, "zelDriverSetLoaderCallbackForExtension", &pfnRaw );
    if( result != ZE_RESULT_SUCCESS )
        return result;
    if( nullptr == pfnRaw )
        return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
    auto pfnSet = reinterpret_cast<zel_pfnDriverSetLoaderCallbackForExtension_t>( pfnRaw );

    // Install only the phase trampolines the app actually asked for: a null
    // pointer tells the driver to skip that phase entirely (no call-through, no
    // per-call frame allocation), which is the point of the split refcount. When
    // both phases are gone the null/null pair clears the driver's registration.
    zel_pfnDriverExtensionFunctionCb_t pfnPrologue =
        install.wantPrologue ? &tracing_layer::loaderExtensionPrologue : nullptr;
    zel_pfnDriverExtensionFunctionCb_t pfnEpilogue =
        install.wantEpilogue ? &tracing_layer::loaderExtensionEpilogue : nullptr;
    void* pLoaderContext =
        ( install.wantPrologue || install.wantEpilogue ) ? install.ctx : nullptr;

    // Order the epilogue-installed hint (read lock-free by the prologue wrapper to
    // decide whether to build an instance frame) against the driver update so a
    // concurrent call never builds a frame the driver won't hand back to an
    // epilogue: when removing the epilogue clear the hint first; when installing it
    // set the hint only after the driver already has the epilogue trampoline.
    if( install.epilogueInstallChanged && !install.wantEpilogue )
        install.ctx->epilogueInstalled.store( false, std::memory_order_relaxed );

    result = pfnSet( hDriver, functionName, pfnPrologue, pfnEpilogue, pLoaderContext );

    if( install.epilogueInstallChanged && install.wantEpilogue )
        install.ctx->epilogueInstalled.store( true, std::memory_order_relaxed );

    return result;
}

ZE_DLLEXPORT ze_result_t ZE_APICALL
zelLoaderGetVersion(zel_component_version_t *version)    
{
    if(version == nullptr)
        return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
    string_copy_s(version->component_name, TRACING_COMP_NAME, ZEL_COMPONENT_STRING_SIZE);
    version->spec_version = ZE_API_VERSION_CURRENT;
    version->component_lib_version.major = LOADER_VERSION_MAJOR;
    version->component_lib_version.minor = LOADER_VERSION_MINOR;
    version->component_lib_version.patch = LOADER_VERSION_PATCH;

    return ZE_RESULT_SUCCESS;
}


#if defined(__cplusplus)
};
#endif
