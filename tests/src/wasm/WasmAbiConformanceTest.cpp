/*
 * WasmAbiConformanceTest.cpp - conformance suite for docs/WASM-EFFECT-ABI.md
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*
 * What this file is. One assertion per documented property in docs/WASM-EFFECT-ABI.md,
 * against modules assembled at run time from .wat source with the runtime's own parser
 * (WasmSandbox::assembleWat). It is deliberately independent of the built-in fixtures
 * under modules/wasm/ so that it tests the DOCUMENT, not the fixtures: the only
 * pre-existing file it reads is the #614 example,
 * tests/data/wasm-effect-abi/softclip.wat, which was itself written from that document.
 *
 * It is built and registered only inside `if(WANT_WASM)` in tests/CMakeLists.txt, the
 * same guard as WasmSandboxTest, because it needs the wasmtime C API to link.
 *
 * IT HAS NEVER BEEN EXECUTED. WANT_WASM degrades to OFF when the wasmtime C API is not
 * on the find path (CMakeLists.txt:957-963), which is the case on the box this was
 * written on; no build has compiled or run this file. It is source a build with wasmtime
 * can run, and docs/KNOWN-LIMITATIONS.md says so plainly.
 *
 * ABI doc section references in the slot names refer to docs/WASM-EFFECT-ABI.md.
 */

#include "WasmSandbox.h"

#include <QDebug>
#include <QFileInfo>
#include <QTest>

#include <wasmtime/trap.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace lmms::wasm
{

namespace
{

// The example effect, written from docs/WASM-EFFECT-ABI.md and nothing else.
const QString kExampleWat = QString(WASM_ABI_EXAMPLE_WAT);

// Above the planes' maximum extent: 2 * channels * maxBlockFrames * 4 = 131072 bytes
// (ABI doc s5). Every probe below keeps its own state above that line, exactly as the
// document requires of a module.
constexpr std::uint32_t kScratch = 131072;

std::string readWat(const QString& path)
{
	std::ifstream in(path.toStdString(), std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in),
		std::istreambuf_iterator<char>());
}

// Assemble \p wat and load it. Returns the load outcome.
bool loadWat(WasmSandbox& sandbox, const std::string& wat, std::string& error)
{
	std::vector<std::uint8_t> bytes;
	if (!WasmSandbox::assembleWat(wat, bytes, error))
	{
		return false;
	}
	return sandbox.loadModuleBytes(bytes.data(), bytes.size(), error);
}

// A module with linear memory, an optional globals block, and one process() whose body
// is \p body. Signature per ABI doc s3.
std::string moduleWat(const std::string& imports, const std::string& globals,
	const std::string& body)
{
	return std::string("(module") + imports
		+ " (memory (export \"memory\") 3)" + globals
		+ " (func (export \"process\") (param i32 i32 i32 f32) (result i32) "
		+ body + "))";
}

std::string channelsGlobal(int value)
{
	return std::string(" (global (export \"channels\") i32 (i32.const ")
		+ std::to_string(value) + "))";
}

std::string latencyGlobal(int value)
{
	return std::string(" (global (export \"latency\") i32 (i32.const ")
		+ std::to_string(value) + "))";
}

const char* const kImportGetParam =
	" (import \"env\" \"host_get_param\" (func $host_get_param (param i32) (result f32)))";
const char* const kImportLog =
	" (import \"env\" \"host_log\" (func $host_log (param i32 i32)))";
const char* const kImportTransport =
	" (import \"env\" \"host_get_transport_state\" (func $host_get_transport_state (result i32)))";

// The declared channel count the host will actually use, or -1 when the module failed to
// load. Factored out so each clamp case is one call rather than a copy of the whole
// load-and-read sequence.
int effectiveChannels(const std::string& wat, std::string& error)
{
	WasmSandbox sandbox;
	if (!loadWat(sandbox, wat, error))
	{
		return -1;
	}
	return sandbox.declaredChannels();
}

int effectiveLatency(const std::string& wat, std::string& error)
{
	WasmSandbox sandbox;
	if (!loadWat(sandbox, wat, error))
	{
		return -1;
	}
	return sandbox.declaredLatency();
}

bool readFloat(WasmSandbox& sandbox, std::uint32_t offset, float& value)
{
	return sandbox.readMemory(offset, &value, sizeof(value));
}

bool readI32(WasmSandbox& sandbox, std::uint32_t offset, std::int32_t& value)
{
	return sandbox.readMemory(offset, &value, sizeof(value));
}

// out[i] = clamp((base + i) * gain, -1, 1) - the example's documented arithmetic.
bool isClippedRamp(const std::vector<float>& out, float base, float gain)
{
	for (std::size_t i = 0; i < out.size(); ++i)
	{
		const float expected = std::min(std::max((base + static_cast<float>(i)) * gain, -1.0f), 1.0f);
		if (std::fabs(out[static_cast<std::size_t>(i)] - expected) > 1e-6f)
		{
			return false;
		}
	}
	return true;
}

// Run the example once for one channel plane and read the output plane back.
bool runExamplePlane(WasmSandbox& sandbox, std::uint32_t frames, std::uint32_t inOffset,
	std::uint32_t outOffset, float gain, std::vector<float>& out)
{
	out.assign(frames, 0.0f);
	std::vector<float> input(frames);
	for (std::uint32_t i = 0; i < frames; ++i)
	{
		input[i] = 1.0f + static_cast<float>(i);
	}
	if (!sandbox.writeMemory(inOffset, input.data(), input.size() * sizeof(float)))
	{
		return false;
	}
	sandbox.setParam(0, gain);
	const CallResult result = sandbox.callProcess(inOffset, outOffset, frames, 48000.0f);
	if (!result.ok())
	{
		return false;
	}
	return sandbox.readMemory(outOffset, out.data(), out.size() * sizeof(float));
}

} // namespace

//! Conformance suite for the documented v0 WASM effect ABI.
class WasmAbiConformanceTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	// s1-s3: the surface the example declares
	void abiExampleDeclaresItsSurface();
	// s3: process() is called once per channel plane, at channels * planeBytes
	void abiPlanesAreInputThenOutput();
	// s2: process() without linear memory is refused
	void abiMemoryExportIsRequired();
	// s4.1: channels is an i32 global, defaulted and clamped
	void abiChannelsIsAGlobalAndIsClamped();
	// s4.2: latency is an i32 global, defaulted and clamped
	void abiLatencyIsAGlobalAndIsClamped();
	// s6: parameter slots are 16 anonymous floats; out of range reads 0
	void abiParamIndexOutOfRangeIsZero();
	// s10: host_log truncates and never traps on a bad range
	void abiHostLogIsBoundedAndSafe();
	// s10: host_get_transport_state reports the host's state
	void abiTransportStateIsExposed();
	// s8: the i32 return value is recorded, not acted on
	void abiProcessReturnValueIsRecordedNotActedOn();
	// s9: an out-of-bounds access traps and the host survives
	void abiOutOfBoundsTrapsAndHostSurvives();
	// s9: fuel exhaustion traps rather than hanging
	void abiFuelExhaustionTraps();
	// s7: frames and sample rate arrive as per-call arguments
	void abiFramesAndSampleRateReachTheModule();
};

void WasmAbiConformanceTest::initTestCase()
{
	QVERIFY2(QFileInfo::exists(kExampleWat), qPrintable("missing " + kExampleWat));
}

// ---------------------------------------------------------------------------
// s1-s3: the surface
// ---------------------------------------------------------------------------

void WasmAbiConformanceTest::abiExampleDeclaresItsSurface()
{
	WasmSandbox sandbox;
	std::string error;
	const std::string wat = readWat(kExampleWat);
	QVERIFY2(!wat.empty(), qPrintable("could not read " + kExampleWat));
	QVERIFY2(loadWat(sandbox, wat, error), qPrintable(QString::fromStdString(error)));

	// process() present, and the ABI requires linear memory beside it (s2).
	QVERIFY(sandbox.hasProcess());
	// s4.1: the example declares stereo through the channels GLOBAL.
	QCOMPARE(sandbox.declaredChannels(), 2);
	// s4.2: it declares no delay.
	QCOMPARE(sandbox.declaredLatency(), 0);
	// s5: three pages, enough for a maximum stereo block (131072 bytes) plus scratch.
	QVERIFY(sandbox.memorySize() >= 3u * 65536u);
	qInfo("ABI conformance: example surface ok (channels=%d latency=%d memory=%zu)",
		sandbox.declaredChannels(), sandbox.declaredLatency(), sandbox.memorySize());
}

void WasmAbiConformanceTest::abiPlanesAreInputThenOutput()
{
	constexpr std::uint32_t frames = 48;
	const std::uint32_t planeBytes = frames * sizeof(float);
	const std::uint32_t outBase = 2u * planeBytes; // channels * planeBytes (s5)

	WasmSandbox sandbox;
	std::string error;
	QVERIFY2(loadWat(sandbox, readWat(kExampleWat), error),
		qPrintable(QString::fromStdString(error)));

	// The host enters process() once per channel, with the same frames and a different
	// offset pair (s3). Run both planes the way WasmWorker does.
	std::vector<float> plane0;
	std::vector<float> plane1;
	QVERIFY(runExamplePlane(sandbox, frames, 0, outBase, 0.5f, plane0));
	QVERIFY(runExamplePlane(sandbox, frames, planeBytes, outBase + planeBytes, 0.5f, plane1));

	// out[i] = clamp(in[i] * 0.5) for both planes: the layout above is the document's.
	QVERIFY(isClippedRamp(plane0, 1.0f, 0.5f));
	QVERIFY(isClippedRamp(plane1, 1.0f, 0.5f));
	// Clipping actually happened, so the assertion above is not vacuous.
	QVERIFY(plane0[frames - 1] == 1.0f);
	qInfo("ABI conformance: planar layout ok (planeBytes=%u outBase=%u)", planeBytes, outBase);
}

// ---------------------------------------------------------------------------
// s2 / s4: the exports
// ---------------------------------------------------------------------------

void WasmAbiConformanceTest::abiMemoryExportIsRequired()
{
	// process() exported, memory NOT exported: the load must fail and say why (s2).
	const std::string wat =
		"(module (func (export \"process\") (param i32 i32 i32 f32) (result i32) "
		"(i32.const 0)))";
	WasmSandbox sandbox;
	std::string error;
	QVERIFY(!loadWat(sandbox, wat, error));
	QVERIFY(!sandbox.hasProcess());
	QVERIFY2(error.find("memory") != std::string::npos, error.c_str());
	qInfo("ABI conformance: memory-less process() refused: %s", error.c_str());
}

void WasmAbiConformanceTest::abiChannelsIsAGlobalAndIsClamped()
{
	std::string error;
	// Declared 2 -> 2.
	QCOMPARE(effectiveChannels(moduleWat("", channelsGlobal(2), "(i32.const 0)"), error), 2);
	// Declared above maxChannels (2) -> clamped silently (s4.1).
	QCOMPARE(effectiveChannels(moduleWat("", channelsGlobal(6), "(i32.const 0)"), error), 2);
	// Declared 0 or negative -> 1.
	QCOMPARE(effectiveChannels(moduleWat("", channelsGlobal(0), "(i32.const 0)"), error), 1);
	QCOMPARE(effectiveChannels(moduleWat("", channelsGlobal(-3), "(i32.const 0)"), error), 1);
	// Absent -> 1.
	QCOMPARE(effectiveChannels(moduleWat("", "", "(i32.const 0)"), error), 1);
	// A FUNCTION named channels is ignored, so the default applies (s4.1).
	QCOMPARE(effectiveChannels(
		"(module (memory (export \"memory\") 3) (func (export \"channels\") (result i32) "
		"(i32.const 2)) (func (export \"process\") (param i32 i32 i32 f32) (result i32) "
		"(i32.const 0)))", error), 1);
}

void WasmAbiConformanceTest::abiLatencyIsAGlobalAndIsClamped()
{
	std::string error;
	QCOMPARE(effectiveLatency(moduleWat("", latencyGlobal(64), "(i32.const 0)"), error), 64);
	// Negative latency is clamped to 0 (s4.2).
	QCOMPARE(effectiveLatency(moduleWat("", latencyGlobal(-5), "(i32.const 0)"), error), 0);
	// Absent -> 0.
	QCOMPARE(effectiveLatency(moduleWat("", "", "(i32.const 0)"), error), 0);
}

// ---------------------------------------------------------------------------
// s6 / s10: parameters, logging, transport
// ---------------------------------------------------------------------------

void WasmAbiConformanceTest::abiParamIndexOutOfRangeIsZero()
{
	// Store host_get_param(0) and host_get_param(99) above the planes, then read them back.
	const std::string body = std::string("(f32.store (i32.const ")
		+ std::to_string(kScratch) + ") (call $host_get_param (i32.const 0)))"
		" (f32.store (i32.const " + std::to_string(kScratch + 4)
		+ ") (call $host_get_param (i32.const 99))) (i32.const 0)";

	WasmSandbox sandbox;
	std::string error;
	QVERIFY2(loadWat(sandbox, moduleWat(kImportGetParam, "", body), error),
		qPrintable(QString::fromStdString(error)));
	sandbox.setParam(0, 0.25f);

	const CallResult result = sandbox.callProcess(0, 0, 1, 48000.0f);
	QVERIFY2(result.ok(), qPrintable(QString::fromStdString(result.message)));

	float inRange = -1.0f;
	float outOfRange = -1.0f;
	QVERIFY(readFloat(sandbox, kScratch, inRange));
	QVERIFY(readFloat(sandbox, kScratch + 4, outOfRange));
	QCOMPARE(inRange, 0.25f);
	// The 16th slot is 15; 99 is out of range and reads 0.0f without trapping (s6).
	QCOMPARE(outOfRange, 0.0f);
}

void WasmAbiConformanceTest::abiHostLogIsBoundedAndSafe()
{
	// A length above maxLogBytes-1 must be truncated to 4095, not refused.
	const std::string longLog = std::string("(call $host_log (i32.const ")
		+ std::to_string(kScratch) + ") (i32.const 20000)) (i32.const 0)";
	WasmSandbox sandbox;
	std::string error;
	QVERIFY2(loadWat(sandbox, moduleWat(kImportLog, "", longLog), error),
		qPrintable(QString::fromStdString(error)));
	QVERIFY(sandbox.callProcess(0, 0, 1, 48000.0f).ok());
	QCOMPARE(sandbox.takeLog().size(), static_cast<std::size_t>(4095));

	// A negative length is a no-op, not a trap (s10).
	const std::string badRange =
		"(call $host_log (i32.const 0) (i32.const -1)) (i32.const 0)";
	WasmSandbox second;
	QVERIFY2(loadWat(second, moduleWat(kImportLog, "", badRange), error),
		qPrintable(QString::fromStdString(error)));
	QVERIFY(second.callProcess(0, 0, 1, 48000.0f).ok());
	QVERIFY(second.takeLog().empty());
}

void WasmAbiConformanceTest::abiTransportStateIsExposed()
{
	const std::string body = std::string("(i32.store (i32.const ")
		+ std::to_string(kScratch) + ") (call $host_get_transport_state)) (i32.const 0)";

	WasmSandbox sandbox;
	std::string error;
	QVERIFY2(loadWat(sandbox, moduleWat(kImportTransport, "", body), error),
		qPrintable(QString::fromStdString(error)));

	std::int32_t state = -1;
	sandbox.setTransportState(0);
	QVERIFY(sandbox.callProcess(0, 0, 1, 48000.0f).ok());
	QVERIFY(readI32(sandbox, kScratch, state));
	QCOMPARE(state, 0);

	sandbox.setTransportState(1);
	QVERIFY(sandbox.callProcess(0, 0, 1, 48000.0f).ok());
	QVERIFY(readI32(sandbox, kScratch, state));
	QCOMPARE(state, 1);
}

// ---------------------------------------------------------------------------
// s7-s9: arguments, the return value, traps
// ---------------------------------------------------------------------------

void WasmAbiConformanceTest::abiProcessReturnValueIsRecordedNotActedOn()
{
	// A non-zero result must NOT be treated as an error (s8): the call is Ok and the
	// value is available on the CallResult.
	WasmSandbox sandbox;
	std::string error;
	QVERIFY2(loadWat(sandbox, moduleWat("", "", "(i32.const 7)"), error),
		qPrintable(QString::fromStdString(error)));
	const CallResult result = sandbox.callProcess(0, 0, 1, 48000.0f);
	QVERIFY(result.ok());
	QVERIFY(!result.trapped());
	QCOMPARE(result.returnValue, 7);
}

void WasmAbiConformanceTest::abiOutOfBoundsTrapsAndHostSurvives()
{
	const std::string body = "(drop (i32.load offset=1048576 (i32.const 0))) (i32.const 0)";
	WasmSandbox sandbox;
	std::string error;
	QVERIFY2(loadWat(sandbox, moduleWat("", "", body), error),
		qPrintable(QString::fromStdString(error)));
	const CallResult result = sandbox.callProcess(0, 0, 1, 48000.0f);
	QVERIFY(!result.ok());
	QVERIFY(result.trapped());
	QCOMPARE(result.trapCode, static_cast<int>(WASMTIME_TRAP_CODE_MEMORY_OUT_OF_BOUNDS));

	// The host process is still alive and a fresh sandbox still loads a good module.
	WasmSandbox second;
	QVERIFY2(loadWat(second, readWat(kExampleWat), error),
		qPrintable(QString::fromStdString(error)));
	QVERIFY(second.hasProcess());
}

void WasmAbiConformanceTest::abiFuelExhaustionTraps()
{
	const std::string body = "(loop $forever (br $forever)) (i32.const 0)";
	WasmSandbox sandbox;
	std::string error;
	QVERIFY2(loadWat(sandbox, moduleWat("", "", body), error),
		qPrintable(QString::fromStdString(error)));
	const CallResult result = sandbox.callProcess(0, 0, 1, 48000.0f);
	QVERIFY(result.trapped());
	QCOMPARE(result.trapCode, static_cast<int>(WASMTIME_TRAP_CODE_OUT_OF_FUEL));
}

void WasmAbiConformanceTest::abiFramesAndSampleRateReachTheModule()
{
	// process stores its own `frames` and `sample_rate` arguments above the planes, so
	// the test observes exactly what the host passed (s7).
	const std::string body = std::string("(i32.store (i32.const ")
		+ std::to_string(kScratch) + ") (local.get 2))"
		" (i32.store (i32.const " + std::to_string(kScratch + 4)
		+ ") (i32.reinterpret_f32 (local.get 3))) (i32.const 0)";

	WasmSandbox sandbox;
	std::string error;
	QVERIFY2(loadWat(sandbox, moduleWat("", "", body), error),
		qPrintable(QString::fromStdString(error)));

	std::int32_t frames = 0;
	std::int32_t rateBits = 0;
	QVERIFY(sandbox.callProcess(0, 0, 48, 44100.0f).ok());
	QVERIFY(readI32(sandbox, kScratch, frames));
	QVERIFY(readI32(sandbox, kScratch + 4, rateBits));
	QCOMPARE(frames, 48);

	float rate = 0.0f;
	std::memcpy(&rate, &rateBits, sizeof(rate));
	QCOMPARE(rate, 44100.0f);

	// A second call with different arguments proves both arrive per call (s7) - there is
	// no setter and no global for either.
	QVERIFY(sandbox.callProcess(0, 0, 13, 48000.0f).ok());
	QVERIFY(readI32(sandbox, kScratch, frames));
	QVERIFY(readI32(sandbox, kScratch + 4, rateBits));
	QCOMPARE(frames, 13);
	std::memcpy(&rate, &rateBits, sizeof(rate));
	QCOMPARE(rate, 48000.0f);
}

} // namespace lmms::wasm

QTEST_GUILESS_MAIN(lmms::wasm::WasmAbiConformanceTest)

#include "WasmAbiConformanceTest.moc"
