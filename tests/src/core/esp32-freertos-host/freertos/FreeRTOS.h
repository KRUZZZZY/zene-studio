/*
 * FreeRTOS.h - host stand-in for the FreeRTOS base header that Link's ESP32
 *              port (ableton/platforms/esp32/*.hpp) includes.
 *
 * Test-only. It lives under tests/ and is on the include path of
 * AbletonLinkEsp32RunnerTest alone; nothing in the product includes it. It
 * records what the port asked for and lets the test decide whether a task
 * creation succeeds, so the port's error handling can be exercised on a host.
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

#include <cstdint>

typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef unsigned long TickType_t;

#define pdPASS ((BaseType_t)1)
#define pdFAIL ((BaseType_t)0)
#define portPRIVILEGE_BIT ((UBaseType_t)0x80000000UL)
#define tskIDLE_PRIORITY ((UBaseType_t)0U)
