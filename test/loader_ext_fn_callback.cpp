/*
 *
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "gtest/gtest.h"

#include "loader/ze_loader.h"
#include "ze_api.h"

#include <cstdint>

namespace {

// Signature of the sample extension function exposed by the null driver.
typedef ze_result_t (ZE_APICALL *pfnSampleExtFunc_t)(
    ze_driver_handle_t, uint32_t, uint32_t*);

constexpr uintptr_t kInstanceSentinel = 0xABCD1234u;

// State the prologue/epilogue callbacks record into, reached via pTracerUserData.
struct CallbackState {
  int prologCount = 0;
  int epilogCount = 0;
  void* prologUserData = nullptr;
  void* epilogUserData = nullptr;
  ze_result_t epilogResult = ZE_RESULT_FORCE_UINT32;
  uintptr_t instanceValueSeenInEpilog = 0;
  bool prologRanBeforeEpilog = false;
};

void ZE_APICALL prologueCb(void* /*pParams*/, ze_result_t /*result*/,
                           void* pTracerUserData, void** ppTracerInstanceUserData) {
  auto* s = static_cast<CallbackState*>(pTracerUserData);
  s->prologCount++;
  s->prologUserData = pTracerUserData;
  *ppTracerInstanceUserData = reinterpret_cast<void*>(kInstanceSentinel);
}

void ZE_APICALL epilogueCb(void* /*pParams*/, ze_result_t result,
                           void* pTracerUserData, void** ppTracerInstanceUserData) {
  auto* s = static_cast<CallbackState*>(pTracerUserData);
  s->epilogCount++;
  s->epilogUserData = pTracerUserData;
  s->epilogResult = result;
  s->prologRanBeforeEpilog = (s->prologCount == 1);
  s->instanceValueSeenInEpilog =
      reinterpret_cast<uintptr_t>(*ppTracerInstanceUserData);
}

ze_driver_handle_t getFirstDriver() {
  EXPECT_EQ(ZE_RESULT_SUCCESS, zeInit(0));
  uint32_t count = 0;
  EXPECT_EQ(ZE_RESULT_SUCCESS, zeDriverGet(&count, nullptr));
  EXPECT_GT(count, 0u);
  count = 1;
  ze_driver_handle_t hDriver = nullptr;
  EXPECT_EQ(ZE_RESULT_SUCCESS, zeDriverGet(&count, &hDriver));
  EXPECT_NE(nullptr, hDriver);
  return hDriver;
}

pfnSampleExtFunc_t getSampleExtFunc(ze_driver_handle_t hDriver) {
  void* addr = nullptr;
  EXPECT_EQ(ZE_RESULT_SUCCESS,
            zeDriverGetExtensionFunctionAddress(hDriver, "zeSampleExtFunc", &addr));
  EXPECT_NE(nullptr, addr);
  return reinterpret_cast<pfnSampleExtFunc_t>(addr);
}

void unregister(ze_driver_handle_t hDriver, const char* name) {
  zelDriverSetExtensionFunctionCallback(hDriver, name, nullptr, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
// Dynamic control suite: tracing is toggled at runtime via
// zelEnableTracingLayer/zelDisableTracingLayer (no ZE_ENABLE_TRACING_LAYER env).
// Each test balances enable/disable and unregisters so process state stays clean.
// ---------------------------------------------------------------------------

TEST(ExtFnCallback, PrologueAndEpilogueFireOnCall) {
  auto hDriver = getFirstDriver();
  auto fn = getSampleExtFunc(hDriver);
  ASSERT_NE(nullptr, fn);

  ASSERT_EQ(ZE_RESULT_SUCCESS, zelEnableTracingLayer());
  CallbackState state;
  EXPECT_EQ(ZE_RESULT_SUCCESS,
            zelDriverSetExtensionFunctionCallback(hDriver, "zeSampleExtFunc",
                                                  &state, prologueCb, epilogueCb));

  uint32_t out = 0;
  EXPECT_EQ(ZE_RESULT_SUCCESS, fn(hDriver, 21, &out));

  EXPECT_EQ(42u, out);
  EXPECT_EQ(1, state.prologCount);
  EXPECT_EQ(1, state.epilogCount);
  EXPECT_EQ(&state, state.prologUserData);
  EXPECT_EQ(&state, state.epilogUserData);
  EXPECT_TRUE(state.prologRanBeforeEpilog);
  EXPECT_EQ(ZE_RESULT_SUCCESS, state.epilogResult);
  EXPECT_EQ(kInstanceSentinel, state.instanceValueSeenInEpilog);

  unregister(hDriver, "zeSampleExtFunc");
  EXPECT_EQ(ZE_RESULT_SUCCESS, zelDisableTracingLayer());
}

TEST(ExtFnCallback, RegisterBeforeFetchStillFires) {
  auto hDriver = getFirstDriver();

  ASSERT_EQ(ZE_RESULT_SUCCESS, zelEnableTracingLayer());
  CallbackState state;
  EXPECT_EQ(ZE_RESULT_SUCCESS,
            zelDriverSetExtensionFunctionCallback(hDriver, "zeSampleExtFunc",
                                                  &state, prologueCb, epilogueCb));

  auto fn = getSampleExtFunc(hDriver);
  ASSERT_NE(nullptr, fn);
  uint32_t out = 0;
  EXPECT_EQ(ZE_RESULT_SUCCESS, fn(hDriver, 5, &out));

  EXPECT_EQ(10u, out);
  EXPECT_EQ(1, state.prologCount);
  EXPECT_EQ(1, state.epilogCount);

  unregister(hDriver, "zeSampleExtFunc");
  EXPECT_EQ(ZE_RESULT_SUCCESS, zelDisableTracingLayer());
}

TEST(ExtFnCallback, UnregisterStopsCallbacks) {
  auto hDriver = getFirstDriver();
  auto fn = getSampleExtFunc(hDriver);
  ASSERT_NE(nullptr, fn);

  ASSERT_EQ(ZE_RESULT_SUCCESS, zelEnableTracingLayer());
  CallbackState state;
  EXPECT_EQ(ZE_RESULT_SUCCESS,
            zelDriverSetExtensionFunctionCallback(hDriver, "zeSampleExtFunc",
                                                  &state, prologueCb, epilogueCb));
  EXPECT_EQ(ZE_RESULT_SUCCESS,
            zelDriverSetExtensionFunctionCallback(hDriver, "zeSampleExtFunc",
                                                  &state, nullptr, nullptr));

  uint32_t out = 0;
  EXPECT_EQ(ZE_RESULT_SUCCESS, fn(hDriver, 7, &out));

  EXPECT_EQ(14u, out);
  EXPECT_EQ(0, state.prologCount);
  EXPECT_EQ(0, state.epilogCount);

  EXPECT_EQ(ZE_RESULT_SUCCESS, zelDisableTracingLayer());
}

TEST(ExtFnCallback, UnknownFunctionNameNeverFires) {
  auto hDriver = getFirstDriver();

  ASSERT_EQ(ZE_RESULT_SUCCESS, zelEnableTracingLayer());
  CallbackState state;
  EXPECT_EQ(ZE_RESULT_SUCCESS,
            zelDriverSetExtensionFunctionCallback(
                hDriver, "zeNeverImplementedExtFunc", &state, prologueCb, epilogueCb));

  auto fn = getSampleExtFunc(hDriver);
  ASSERT_NE(nullptr, fn);
  uint32_t out = 0;
  EXPECT_EQ(ZE_RESULT_SUCCESS, fn(hDriver, 3, &out));

  EXPECT_EQ(6u, out);
  EXPECT_EQ(0, state.prologCount);
  EXPECT_EQ(0, state.epilogCount);

  unregister(hDriver, "zeNeverImplementedExtFunc");
  EXPECT_EQ(ZE_RESULT_SUCCESS, zelDisableTracingLayer());
}

TEST(ExtFnCallback, NullArgumentsReturnErrors) {
  auto hDriver = getFirstDriver();

  EXPECT_EQ(ZE_RESULT_ERROR_INVALID_NULL_HANDLE,
            zelDriverSetExtensionFunctionCallback(nullptr, "zeSampleExtFunc",
                                                  nullptr, prologueCb, epilogueCb));
  EXPECT_EQ(ZE_RESULT_ERROR_INVALID_NULL_POINTER,
            zelDriverSetExtensionFunctionCallback(hDriver, nullptr, nullptr,
                                                  prologueCb, epilogueCb));
}

// Two-level gate: registered but tracing layer NOT enabled -> must not fire.
TEST(ExtFnCallback, NotEnabledDoesNotFire) {
  auto hDriver = getFirstDriver();
  auto fn = getSampleExtFunc(hDriver);
  ASSERT_NE(nullptr, fn);

  CallbackState state;
  EXPECT_EQ(ZE_RESULT_SUCCESS,
            zelDriverSetExtensionFunctionCallback(hDriver, "zeSampleExtFunc",
                                                  &state, prologueCb, epilogueCb));

  uint32_t out = 0;
  EXPECT_EQ(ZE_RESULT_SUCCESS, fn(hDriver, 9, &out));

  EXPECT_EQ(18u, out);              // body runs
  EXPECT_EQ(0, state.prologCount);  // gate closed -> no callbacks
  EXPECT_EQ(0, state.epilogCount);

  unregister(hDriver, "zeSampleExtFunc");
}

// Disabling the tracing layer stops callbacks even while still registered.
TEST(ExtFnCallback, DisableStopsCallbacks) {
  auto hDriver = getFirstDriver();
  auto fn = getSampleExtFunc(hDriver);
  ASSERT_NE(nullptr, fn);

  ASSERT_EQ(ZE_RESULT_SUCCESS, zelEnableTracingLayer());
  CallbackState state;
  EXPECT_EQ(ZE_RESULT_SUCCESS,
            zelDriverSetExtensionFunctionCallback(hDriver, "zeSampleExtFunc",
                                                  &state, prologueCb, epilogueCb));

  uint32_t out = 0;
  EXPECT_EQ(ZE_RESULT_SUCCESS, fn(hDriver, 1, &out));
  EXPECT_EQ(1, state.prologCount);  // fires while enabled

  EXPECT_EQ(ZE_RESULT_SUCCESS, zelDisableTracingLayer());
  EXPECT_EQ(ZE_RESULT_SUCCESS, fn(hDriver, 1, &out));
  EXPECT_EQ(1, state.prologCount);  // no additional fire after disable
  EXPECT_EQ(1, state.epilogCount);

  unregister(hDriver, "zeSampleExtFunc");
}

// Underflow guard (fix #1): a disable with no matching enable must be a safe
// no-op. If the unsigned counter had underflowed, the subsequent enable would
// not detect the 0->1 edge, the driver would never be enabled, and the callback
// would not fire - so a passing "fires" assertion proves no corruption occurred.
TEST(ExtFnCallback, DisableWithoutEnableIsSafe) {
  auto hDriver = getFirstDriver();
  auto fn = getSampleExtFunc(hDriver);
  ASSERT_NE(nullptr, fn);

  // Counter is 0 here (all prior tests balanced). Extra disables must no-op.
  EXPECT_EQ(ZE_RESULT_SUCCESS, zelDisableTracingLayer());
  EXPECT_EQ(ZE_RESULT_SUCCESS, zelDisableTracingLayer());
  EXPECT_EQ(ZE_RESULT_SUCCESS, zelDisableTracingLayer());

  ASSERT_EQ(ZE_RESULT_SUCCESS, zelEnableTracingLayer());
  CallbackState state;
  EXPECT_EQ(ZE_RESULT_SUCCESS,
            zelDriverSetExtensionFunctionCallback(hDriver, "zeSampleExtFunc",
                                                  &state, prologueCb, epilogueCb));

  uint32_t out = 0;
  EXPECT_EQ(ZE_RESULT_SUCCESS, fn(hDriver, 2, &out));
  EXPECT_EQ(1, state.prologCount);  // enable's 0->1 edge still worked
  EXPECT_EQ(1, state.epilogCount);

  unregister(hDriver, "zeSampleExtFunc");
  EXPECT_EQ(ZE_RESULT_SUCCESS, zelDisableTracingLayer());
}

// ---------------------------------------------------------------------------
// Environment suite: run with ZE_ENABLE_TRACING_LAYER=1 (separate ctest entry).
// Tracing is enabled statically at init; the app never calls
// zelEnableTracingLayer, and per documented behavior it stays enabled for the
// whole process.
// ---------------------------------------------------------------------------

TEST(ExtFnCallbackEnviron, EnvKeepsTracingEnabled) {
  auto hDriver = getFirstDriver();
  auto fn = getSampleExtFunc(hDriver);
  ASSERT_NE(nullptr, fn);

  // No zelEnableTracingLayer call: the driver was enabled at init (Site A).
  CallbackState state;
  EXPECT_EQ(ZE_RESULT_SUCCESS,
            zelDriverSetExtensionFunctionCallback(hDriver, "zeSampleExtFunc",
                                                  &state, prologueCb, epilogueCb));

  uint32_t out = 0;
  EXPECT_EQ(ZE_RESULT_SUCCESS, fn(hDriver, 4, &out));

  EXPECT_EQ(8u, out);
  EXPECT_EQ(1, state.prologCount);
  EXPECT_EQ(1, state.epilogCount);

  unregister(hDriver, "zeSampleExtFunc");
}

// A spurious disable under static enablement must not turn tracing off (sticky
// env) and must not corrupt state (underflow guard).
TEST(ExtFnCallbackEnviron, EnvDisableIsNoOp) {
  auto hDriver = getFirstDriver();
  auto fn = getSampleExtFunc(hDriver);
  ASSERT_NE(nullptr, fn);

  EXPECT_EQ(ZE_RESULT_SUCCESS, zelDisableTracingLayer());  // counter 0 -> no-op

  CallbackState state;
  EXPECT_EQ(ZE_RESULT_SUCCESS,
            zelDriverSetExtensionFunctionCallback(hDriver, "zeSampleExtFunc",
                                                  &state, prologueCb, epilogueCb));

  uint32_t out = 0;
  EXPECT_EQ(ZE_RESULT_SUCCESS, fn(hDriver, 6, &out));

  EXPECT_EQ(12u, out);
  EXPECT_EQ(1, state.prologCount);  // still fires: env-enabled tracing is sticky
  EXPECT_EQ(1, state.epilogCount);

  unregister(hDriver, "zeSampleExtFunc");
}

}  // namespace
