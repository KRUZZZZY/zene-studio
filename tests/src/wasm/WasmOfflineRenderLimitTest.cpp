/*
 * WasmOfflineRenderLimitTest.cpp - the offline-render frame cap, exercised through
 *                                  the real WasmOfflineRender.cpp
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

#include "WasmOfflineRender.h"
#include "WasmWorker.h"

#include <QObject>
#include <QTest>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// Defined in WasmOfflineRenderLimitStubs.cpp.
namespace lmms::wasm::offlineLimitStub
{
extern int loadModuleFileCalls;
extern bool functional;
}

namespace
{
//! The cap the control schema already publishes (ControlCommandsWasmRender.cpp).
//! Kept as a literal here so the regression compiles with the product fix fully
//! reverted, which is what the RED step reverses.
constexpr std::uint32_t kFrameLimit = 4194304;

bool namesTheLimit(const std::string& error)
{
	return error.find("4194304") != std::string::npos || error.find("frame limit") != std::string::npos;
}
}

class WasmOfflineRenderLimitTest : public QObject
{
	Q_OBJECT

private slots:
	void init();
	void rejectsOneFrameOverTheLimit();
	void rejectsTheLargestFrameCount();
	void acceptsExactlyTheLimit();
	void keepsTheExistingRejections();
	void normalRenderStillReachesTheSandbox();

private:
	// A frame count that must be refused before the sandbox is reached.
	static void expectRefusedBeforeSandbox(std::uint32_t frames);
};

void WasmOfflineRenderLimitTest::init()
{
	lmms::wasm::offlineLimitStub::loadModuleFileCalls = 0;
	lmms::wasm::offlineLimitStub::functional = false;
}

void WasmOfflineRenderLimitTest::expectRefusedBeforeSandbox(std::uint32_t frames)
{
	const auto outcome = lmms::wasm::renderOffline("module.wasm", frames, 256u, 2u, 48000.0f, std::vector<float>());
	QVERIFY(!outcome.ok);
	// Checked first, so a refusal that reached the sandbox cannot be hidden by a later assertion.
	QCOMPARE(lmms::wasm::offlineLimitStub::loadModuleFileCalls, 0);
	QVERIFY2(namesTheLimit(outcome.error), outcome.error.c_str());
	QVERIFY(outcome.runs.empty());
	QVERIFY(outcome.control.empty());
}

void WasmOfflineRenderLimitTest::rejectsOneFrameOverTheLimit()
{
	expectRefusedBeforeSandbox(kFrameLimit + 1u);
}

void WasmOfflineRenderLimitTest::rejectsTheLargestFrameCount()
{
	expectRefusedBeforeSandbox(std::numeric_limits<std::uint32_t>::max());
}

void WasmOfflineRenderLimitTest::acceptsExactlyTheLimit()
{
	lmms::wasm::offlineLimitStub::functional = true;
	const auto outcome = lmms::wasm::renderOffline("module.wasm", kFrameLimit,
		lmms::wasm::WasmWorker::maxBlockFrames, 2u, 48000.0f, std::vector<float>());
	QVERIFY2(!namesTheLimit(outcome.error), outcome.error.c_str());
	QVERIFY(lmms::wasm::offlineLimitStub::loadModuleFileCalls >= 1);
}

void WasmOfflineRenderLimitTest::keepsTheExistingRejections()
{
	const auto noFrames = lmms::wasm::renderOffline("module.wasm", 0u, 256u, 2u, 48000.0f, std::vector<float>());
	QVERIFY(!noFrames.ok);
	QVERIFY(!noFrames.error.empty());

	const auto wideBlock = lmms::wasm::renderOffline("module.wasm", 4096u,
		lmms::wasm::WasmWorker::maxBlockFrames + 1u, 2u, 48000.0f, std::vector<float>());
	QVERIFY(!wideBlock.ok);
	QVERIFY(!wideBlock.error.empty());

	QCOMPARE(lmms::wasm::offlineLimitStub::loadModuleFileCalls, 0);
}

void WasmOfflineRenderLimitTest::normalRenderStillReachesTheSandbox()
{
	lmms::wasm::offlineLimitStub::functional = true;
	const auto outcome = lmms::wasm::renderOffline("module.wasm", 64u, 16u, 2u, 48000.0f, std::vector<float>());
	QVERIFY2(!namesTheLimit(outcome.error), outcome.error.c_str());
	QVERIFY(lmms::wasm::offlineLimitStub::loadModuleFileCalls >= 1);
}

QTEST_GUILESS_MAIN(WasmOfflineRenderLimitTest)
#include "WasmOfflineRenderLimitTest.moc"
