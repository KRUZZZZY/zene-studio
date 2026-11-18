/*
 * task.h - host stand-in for the FreeRTOS task API used by Link's ESP32 port.
 *
 * Test-only (see FreeRTOS.h beside this file). Task creation does not run the
 * task function: the host has no FreeRTOS scheduler. Instead it reports the
 * outcome the test asked for and counts the calls, so the test can check that
 * the port handles a failed creation and only deletes a task it created.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#pragma once

#include <freertos/FreeRTOS.h>

namespace freertos_host
{
// Set by the test before each creation: false makes the next creations fail the
// way FreeRTOS does when it cannot allocate the stack or the control block.
inline bool createSucceeds = true;
inline int createCalls = 0;
inline int deleteCalls = 0;
inline TaskHandle_t lastCreatedHandle = nullptr;
inline TaskHandle_t lastDeletedHandle = nullptr;
} // namespace freertos_host

// FreeRTOS leaves *handle untouched when creation fails; the port must not rely
// on it being set.
inline BaseType_t xTaskCreatePinnedToCore(
  TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t* handle, BaseType_t)
{
  ++freertos_host::createCalls;
  if (!freertos_host::createSucceeds)
  {
    return pdFAIL;
  }
  // Any non-null token stands for the created task.
  static char taskToken;
  freertos_host::lastCreatedHandle = &taskToken;
  *handle = freertos_host::lastCreatedHandle;
  return pdPASS;
}

inline BaseType_t xTaskCreate(
  TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t* handle)
{
  ++freertos_host::createCalls;
  if (!freertos_host::createSucceeds)
  {
    return pdFAIL;
  }
  static char taskToken;
  freertos_host::lastCreatedHandle = &taskToken;
  *handle = freertos_host::lastCreatedHandle;
  return pdPASS;
}

// Records the call and the handle it was given. A null handle is what FreeRTOS
// treats as "the current task", so the test checks the handle, not just the count.
inline void vTaskDelete(TaskHandle_t handle)
{
  ++freertos_host::deleteCalls;
  freertos_host::lastDeletedHandle = handle;
}

inline void vTaskDelay(TickType_t) {}
