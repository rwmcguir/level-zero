/*
 *
 * Copyright (C) 2019-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 * @file ze_null.h
 *
 */
#pragma once
#include <stdlib.h>
#include <vector>
#include <map>
#include <mutex>
#include <string>
#include <atomic>
#include "ze_ddi.h"
#include "zet_ddi.h"
#include "zes_ddi.h"
#include "ze_util.h"
#include "ze_ddi_common.h"
#include "loader/ze_loader.h"

#ifndef ZEL_NULL_DRIVER_ID
#define ZEL_NULL_DRIVER_ID 1
#endif

namespace driver
{
    extern ze_dditable_driver_t pCore;
    extern zet_dditable_driver_t pTools;
    extern zes_dditable_driver_t pSysman;
    extern zer_dditable_driver_t pRuntime;
    struct __zedlllocal BaseNullHandle : ze_handle_t {
        BaseNullHandle() {
            pCore = &driver::pCore;
            pTools = &driver::pTools;
            pSysman = &driver::pSysman;
            pRuntime = &driver::pRuntime;
        }
    };
    ///////////////////////////////////////////////////////////////////////////////
    class __zedlllocal context_t
    {
    public:
        ze_api_version_t version = ZE_API_VERSION_CURRENT;

        ze_dditable_t   zeDdiTable = {};
        zet_dditable_t  zetDdiTable = {};
        zes_dditable_t  zesDdiTable = {};
        zer_dditable_t  zerDdiTable = {};
        std::vector<BaseNullHandle*> globalBaseNullHandle;
	bool ddiExtensionSupported = false;
	std::vector<char *> env_vars{};

        // Registry for zelDriverSetExtensionFunctionCallback: maps an extension
        // function name to the prologue/epilogue the driver invokes from inside
        // that function's body. Keyed by name (order-independent vs fetch).
        struct extension_function_callbacks_t {
            void* pUserData = nullptr;
            zel_pfnDriverExtensionFunctionCb_t prologue = nullptr;
            zel_pfnDriverExtensionFunctionCb_t epilogue = nullptr;
        };
        std::mutex extensionCallbackMutex;
        std::map<std::string, extension_function_callbacks_t> extensionCallbacks;

        // Global gate for extension-function callbacks, toggled by the loader via
        // zelDriverEnableTracing. Callbacks fire only when this is set AND a
        // callback is registered for the function (two-level gate).
        std::atomic<bool> extensionCallbacksEnabled{false};

        context_t();
        ~context_t();

        void* get( void )
        {
            static uint64_t count = 0x80800000 >> ZEL_NULL_DRIVER_ID;
            if (ddiExtensionSupported) {
                globalBaseNullHandle.push_back(new BaseNullHandle());
                return reinterpret_cast<void*>(globalBaseNullHandle.back());
            } else {
                return reinterpret_cast<void*>( ++count );
            }
        }

	char *setenv_var_with_driver_id(const std::string &key, uint32_t driverId);
    };

    ze_result_t ZE_APICALL zerGetLastErrorDescription(const char **ppString);
    uint32_t ZE_APICALL zerTranslateDeviceHandleToIdentifier(ze_device_handle_t hDevice);
    ze_device_handle_t ZE_APICALL zerTranslateIdentifierToDeviceHandle(uint32_t identifier);
    ze_context_handle_t ZE_APICALL zerGetDefaultContext(void);

    ///////////////////////////////////////////////////////////////////////////
    // Extension-function callback prototype demonstration.
    //
    // "zeSampleExtFunc" is a stand-in vendor extension function reachable only by
    // name via zeDriverGetExtensionFunctionAddress. Its body invokes any
    // prologue/epilogue registered through zelDriverSetExtensionFunctionCallback,
    // passing a typed params block (the driver knows its own signature).
    typedef struct _ze_sample_ext_func_params_t
    {
        ze_driver_handle_t* phDriver;
        uint32_t* pinput;
        uint32_t** ppOutput;
    } ze_sample_ext_func_params_t;

    ze_result_t ZE_APICALL zeSampleExtFunc(
        ze_driver_handle_t hDriver, uint32_t input, uint32_t* pOutput );

    // Driver-side registration entry, resolved by name from the loader's
    // zelDriverSetExtensionFunctionCallback forward.
    ze_result_t ZE_APICALL zelDriverSetExtensionFunctionCallback(
        ze_driver_handle_t hDriver, const char* functionName, void* pUserData,
        zel_pfnDriverExtensionFunctionCb_t prologue,
        zel_pfnDriverExtensionFunctionCb_t epilogue );

    // Driver-side enable/disable of extension-function callbacks, resolved by
    // name from the loader when the tracing layer is enabled/disabled.
    ze_result_t ZE_APICALL zelDriverEnableTracing(
        ze_driver_handle_t hDriver, ze_bool_t enable );

    extern context_t context;
} // namespace driver

namespace instrumented
{
    //////////////////////////////////////////////////////////////////////////
    struct tracer_data_t
    {
        ze_bool_t enabled = false;

        void* userData = nullptr;

        ze_callbacks_t zePrologueCbs = {};
        ze_callbacks_t zeEpilogueCbs = {};
    };

    ///////////////////////////////////////////////////////////////////////////////
    class __zedlllocal context_t
    {
    public:
        ze_bool_t enableTracing = false;
        std::vector< tracer_data_t > tracerData;

        context_t();
        ~context_t() = default;
    };

    extern context_t context;
} // namespace instrumented
