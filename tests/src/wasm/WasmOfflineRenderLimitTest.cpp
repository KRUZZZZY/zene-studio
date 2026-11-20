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
#include "WasmOfflineRenderLimits.h"
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
	void limitIsTheDocumentedFourMebiframes();
	void rejectsOneFrameOverTheLimit();
	void rejectsTheLargestFrameCount();
	void acceptsExactlyTheLimit();
	void keepsTheExistingRejections();
	void validatorBoundsTheFrameCount();

private:
	// A frame count that must be refused before the sandbox is reached.
	static void expectRefusedBeforeSandbox(std::uint32_t frames);
};

void WasmOfflineRenderLimitTest::init()
{
	lmms::wasm::offlineLimitStub::loadModuleFileCalls = 0;
	lmms::wasm::offlineLimitStub::functional = false;
}

void WasmOfflineRenderLimitTest::limitIsTheDocumentedFourMebiframes()
{
	QCOMPARE(lmms::wasm::maxOfflineRenderFrames, std::uint32_t{4194304});
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
	expectRefusedBeforeSandbox(lmms::wasm::maxOfflineRenderFrames + 1u);
}

void WasmOfflineRenderLimitTest::rejectsTheLargestFrameCount()
{
	expectRefusedBeforeSandbox(std::numeric_limits<std::uint32_t>::max());
}

void WasmOfflineRenderLimitTest::acceptsExactlyTheLimit()
{
	lmms::wasm::offlineLimitStub::functional = true;
	const auto outcome = lmms::wasm::renderOffline("module.wasm", lmms::wasm::maxOfflineRenderFrames,
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

void WasmOfflineRenderLimitTest::validatorBoundsTheFrameCount()
{
	QVERIFY(!lmms::wasm::offlineRenderFramesError(lmms::wasm::maxOfflineRenderFrames + 1u).empty());
	QVERIFY(!lmms::wasm::offlineRenderFramesError(std::numeric_limits<std::uint32_t>::max()).empty());
	QVERIFY(lmms::wasm::offlineRenderFramesError(1u).empty());
	QVERIFY(lmms::wasm::offlineRenderFramesError(lmms::wasm::maxOfflineRenderFrames).empty());
}

QTEST_GUILESS_MAIN(WasmOfflineRenderLimitTest)
#include "WasmOfflineRenderLimitTest.moc"
