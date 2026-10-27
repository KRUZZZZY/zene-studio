/*
 * AbletonLinkEsp32RunnerTest.cpp - Link's ESP32 service runner reports a failed
 *                                  FreeRTOS task creation.
 *
 * THE CLAIM: when FreeRTOS cannot create the task that runs Link's asio service
 * (out of memory for its stack or control block), the ESP32 port's Context
 * throws std::runtime_error from the first call that needs the service, instead
 * of carrying on with a runner that never runs. A failed attempt is not cached,
 * so a later attempt that succeeds works.
 *
 * HOW. Zene never builds the ESP32 port (ESP_PLATFORM is not defined by any
 * Zene target), so this test compiles the port's own Context.hpp on the host
 * against the FreeRTOS stand-in in esp32-freertos-host/. The stand-in does not
 * schedule tasks; it reports the outcome the test asks for and counts the calls.
 *
 * WHAT IT CANNOT PROVE: the behaviour on a real ESP32 or with real FreeRTOS
 * memory exhaustion. It proves the port's own error handling against the
 * documented FreeRTOS return contract (pdPASS / pdFAIL).
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <chrono>
#include <functional>
#include <stdexcept>

// The port expects the ESP-IDF build to name the core; any core works on the host.
#ifndef LINK_ESP_TASK_CORE_ID
#define LINK_ESP_TASK_CORE_ID 1
#endif

#include <ableton/platforms/esp32/Context.hpp>

#include <freertos/task.h>

namespace
{
struct NullLog
{
};

// The interface scan is never called by these tests.
struct NoInterfaces
{
};

using EspContext = ableton::platforms::esp32::Context<NoInterfaces, NullLog>;

// The callback dispatcher is the port's other FreeRTOS task owner. It is never
// started in these tests, so no task exists and nothing waits on a clock.
using EspDispatcher = ableton::platforms::esp32::
  LockFreeCallbackDispatcher<std::function<void()>, std::chrono::milliseconds>;
} // namespace

class AbletonLinkEsp32RunnerTest : public QObject
{
  Q_OBJECT

private slots:
  void init() { freertos_host::createSucceeds = true; }

  void failedTaskCreationIsReported()
  {
    EspContext context;
    const int createsBefore = freertos_host::createCalls;

    freertos_host::createSucceeds = false;
    QVERIFY_EXCEPTION_THROWN(context.async([] {}), std::runtime_error);
    QCOMPARE(freertos_host::createCalls - createsBefore, 1);
  }

  void failedTaskCreationIsNotCachedAndRecoversOnRetry()
  {
    EspContext context;
    const int createsBefore = freertos_host::createCalls;

    freertos_host::createSucceeds = false;
    QVERIFY_EXCEPTION_THROWN(context.makeTimer(), std::runtime_error);

    freertos_host::createSucceeds = true;
    bool threw = false;
    try
    {
      context.makeTimer();
    }
    catch (const std::runtime_error&)
    {
      threw = true;
    }
    QVERIFY(!threw);
    // One failed attempt, then exactly one successful creation: no stale runner.
    QCOMPARE(freertos_host::createCalls - createsBefore, 2);
  }

  void dispatcherNeverStartedDeletesNoTask()
  {
    // Destroying an unstarted dispatcher runs stop(); it must not delete a task
    // handle that was never set.
    const int deletesBefore = freertos_host::deleteCalls;
    {
      EspDispatcher dispatcher([] {}, std::chrono::milliseconds(1));
    }
    QCOMPARE(freertos_host::deleteCalls - deletesBefore, 0);
  }
};

QTEST_GUILESS_MAIN(AbletonLinkEsp32RunnerTest)
#include "AbletonLinkEsp32RunnerTest.moc"
