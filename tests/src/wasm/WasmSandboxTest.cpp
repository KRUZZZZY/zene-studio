/*
 * WasmSandboxTest.cpp - QtTest gates for the WASM DSP sandbox
 *
 * Copyright (c) 2026 LMMS WASM DSP sandbox contributors
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

#include "WasmEffect.h"
#include "WasmEffectControls.h"
#include "WasmSandbox.h"
#include "WasmSpscRingBuffer.h"
#include "WasmWorker.h"

#include "AudioBuffer.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Plugin.h"
#include "PluginFactory.h"

#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QTemporaryDir>
#include <QTest>

// The gate tests assert on the runtime's own trap-code constants so the
// sandbox's int codes cannot silently drift from wasmtime's enum.
#include <wasmtime/trap.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <new>
#include <string>
#include <utility>
#include <vector>

// ---- global allocation detector -------------------------------------------
// The audio-thread claim is "no allocation". Prove it instead of asserting it:
// while g_countAllocations is set, every global operator new bumps a counter.
// Counting is only enabled around code paths under test, never around Qt.
namespace
{
std::atomic<bool> g_countAllocations{false};
std::atomic<std::uint64_t> g_allocationCount{0};

void noteAllocation() noexcept
{
	if (g_countAllocations.load(std::memory_order_relaxed))
	{
		g_allocationCount.fetch_add(1, std::memory_order_relaxed);
	}
}
} // namespace

void* operator new(std::size_t size)
{
	noteAllocation();
	if (void* ptr = std::malloc(size)) { return ptr; }
	throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
	return ::operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
	noteAllocation();
	return std::malloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept
{
	return ::operator new(size, tag);
}

void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete(void* ptr, const std::nothrow_t&) noexcept { std::free(ptr); }
void operator delete[](void* ptr, const std::nothrow_t&) noexcept { std::free(ptr); }

// ---------------------------------------------------------------------------

namespace lmms
{
// Defined in plugins/WasmEffect/WasmEffect.cpp, which is compiled into this
// test binary. The plugin loader resolves exactly this symbol name from
// libwasm_effect.so (PluginFactory strips the "lib" prefix from the file name).
extern "C" Plugin::Descriptor wasm_effect_plugin_descriptor;
} // namespace lmms

namespace lmms::wasm
{

namespace
{
const QString kModuleDir = QString(WASM_MODULES_DIR);
const QString kWatSourceDir = QString(WASM_WAT_SOURCE_DIR);

QString modulePath(const QString& name)
{
	return kModuleDir + "/" + name + ".wasm";
}

QString watPath(const QString& name)
{
	return kWatSourceDir + "/" + name + ".wat";
}

std::string readFile(const QString& path)
{
	std::ifstream in(path.toStdString(), std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in),
		std::istreambuf_iterator<char>());
}
} // namespace

class WasmSandboxTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();

	// ---- G1: runtime embedded, hello module instantiated and called ----
	void g1_helloFromWatSource();
	void g1_helloFromBuildTimeModule();
	void g1_hostImportsAreWired();

	// ---- ABI / loader robustness ----
	void loaderRejectsGarbageWithoutCrashing();
	void dspModuleMustExportLinearMemory();

	// ---- G2: 48-frame block through the frozen audio ABI ----
	void g2_sandboxProcesses48FrameBlock();
	void g2_wasmEffectProcesses48FrameBlock();

	// ---- G3: fuel metering + crash isolation ----
	void g3_infiniteLoopTrapsOnFuel();
	void g3_abortModuleTrapsAndDoesNotCrashHost();
	void g3_outOfBoundsMemoryTraps();
	void g3_effectQuarantinesTrappingModule();

	// ---- G4: reference module compiled from C (zig cc), optional ----
	void g4_compiledCDemoModule();

	// ---- real-time thread discipline ----
	void rt_spscRingBufferIsAllocationFree();
	void rt_audioThreadCallsDoNotAllocate();

	// ---- task #591: WasmEffect as a first-class LMMS plugin ----
	void p1_pluginDescriptorIsWellFormed();
	void p2_effectSavesAndReloadsModuleAndParameters();
	void p3_moduleLatencyIsApplied();
	void p4_sampleRateChangeReinstantiatesModule();
	void p5_pluginFactoryDiscoversPlugin();

	// ---- mixer audit C1b: the PDC chain cache follows a module load ----
	void p6_moduleLoadRefreshesTheChainLatencyCache();

private:
};

void WasmSandboxTest::initTestCase()
{
	Engine::init(true);
	QVERIFY2(QFileInfo::exists(modulePath("hello")),
		qPrintable("missing " + modulePath("hello")));
}

void WasmSandboxTest::cleanupTestCase()
{
	Engine::destroy();
}

// ---------------------------------------------------------------------------
// G1
// ---------------------------------------------------------------------------

void WasmSandboxTest::g1_helloFromWatSource()
{
	WasmSandbox sandbox;
	const std::string wat = readFile(watPath("hello"));
	QVERIFY2(!wat.empty(), qPrintable("could not read " + watPath("hello")));

	std::string error;
	std::vector<std::uint8_t> bytes;
	QVERIFY2(WasmSandbox::assembleWat(wat, bytes, error),
		qPrintable(QString::fromStdString("assembleWat: " + error)));
	QVERIFY2(sandbox.loadModuleBytes(bytes.data(), bytes.size(), error),
		qPrintable(QString::fromStdString("loadModuleBytes: " + error)));

	auto result = sandbox.callI32("hello");
	QVERIFY2(result.ok(),
		qPrintable(QString::fromStdString("hello() trapped: " + result.message)));
	QCOMPARE(result.returnValue, 42);
	QVERIFY(result.fuelConsumed > 0);
	qInfo("G1 hello.wat: hello() -> %d, fuelConsumed=%llu",
		result.returnValue,
		static_cast<unsigned long long>(result.fuelConsumed));
}

void WasmSandboxTest::g1_helloFromBuildTimeModule()
{
	WasmSandbox sandbox;
	std::string error;
	QVERIFY2(sandbox.loadModuleFile(modulePath("hello").toStdString(), error),
		qPrintable(QString::fromStdString("loadModuleFile: " + error)));
	QVERIFY(!sandbox.hasProcess());

	auto result = sandbox.callI32("hello");
	QVERIFY2(result.ok(),
		qPrintable(QString::fromStdString("hello() trapped: " + result.message)));
	QCOMPARE(result.returnValue, 42);
	qInfo("G1 hello.wasm (assembled at build time): hello() -> %d",
		result.returnValue);
}

void WasmSandboxTest::g1_hostImportsAreWired()
{
	WasmSandbox sandbox;
	std::string error;
	QVERIFY2(sandbox.loadModuleFile(modulePath("probe").toStdString(), error),
		qPrintable(QString::fromStdString("loadModuleFile: " + error)));

	auto logResult = sandbox.callVoid("log_it");
	QVERIFY2(logResult.ok(),
		qPrintable(QString::fromStdString("log_it trapped: " + logResult.message)));
	QCOMPARE(QString::fromStdString(sandbox.takeLog()), QStringLiteral("hello from wasm"));

	sandbox.setTransportState(1);
	auto transport = sandbox.callI32("transport");
	QVERIFY2(transport.ok(),
		qPrintable(QString::fromStdString("transport trapped: " + transport.message)));
	QCOMPARE(transport.returnValue, 1);
	qInfo("G1 host imports: host_log -> 'hello from wasm', "
		"host_get_transport_state -> %d", transport.returnValue);
}

// ---------------------------------------------------------------------------
// loader robustness
// ---------------------------------------------------------------------------

void WasmSandboxTest::loaderRejectsGarbageWithoutCrashing()
{
	WasmSandbox sandbox;
	std::string error;
	const char garbage[] = "this is not a wasm module";
	QVERIFY(!sandbox.loadModuleBytes(
		reinterpret_cast<const std::uint8_t*>(garbage), sizeof(garbage) - 1,
		error));
	QVERIFY(!error.empty());
	qInfo("loader rejected garbage: %s", error.c_str());

	// The host survives and a fresh sandbox still works.
	WasmSandbox second;
	QVERIFY2(second.loadModuleFile(modulePath("hello").toStdString(), error),
		qPrintable(QString::fromStdString(error)));
	QCOMPARE(second.callI32("hello").returnValue, 42);
}

void WasmSandboxTest::dspModuleMustExportLinearMemory()
{
	// Exports process() but no memory: the DSP ABI requires linear memory.
	const std::string wat =
		"(module (func (export \"process\") (param i32 i32 i32 f32) (result i32) "
		"(i32.const 0)))";
	WasmSandbox sandbox;
	std::string error;
	std::vector<std::uint8_t> bytes;
	QVERIFY2(WasmSandbox::assembleWat(wat, bytes, error),
		qPrintable(QString::fromStdString("assembleWat: " + error)));
	QVERIFY(!sandbox.loadModuleBytes(bytes.data(), bytes.size(), error));
	QVERIFY(error.find("memory") != std::string::npos);
	qInfo("loader rejected process() without memory: %s", error.c_str());
}

// ---------------------------------------------------------------------------
// G2
// ---------------------------------------------------------------------------

void WasmSandboxTest::g2_sandboxProcesses48FrameBlock()
{
	constexpr std::uint32_t frames = 48;
	WasmSandbox sandbox;
	std::string error;
	QVERIFY2(sandbox.loadModuleFile(modulePath("gain").toStdString(), error),
		qPrintable(QString::fromStdString("loadModuleFile: " + error)));
	QVERIFY(sandbox.hasProcess());
	QCOMPARE(sandbox.declaredChannels(), 2);
	QVERIFY(sandbox.memorySize() >= 2 * frames * sizeof(float));

	sandbox.setParam(0, 0.5f);
	const std::uint32_t planeBytes = frames * sizeof(float);
	std::vector<float> input(frames);
	std::vector<float> output(frames, 0.0f);
	for (std::uint32_t i = 0; i < frames; ++i)
	{
		input[i] = 1.0f + static_cast<float>(i);
	}
	QVERIFY(sandbox.writeMemory(0, input.data(), planeBytes));
	QVERIFY(sandbox.writeMemory(planeBytes, output.data(), planeBytes));

	auto result = sandbox.callProcess(0, planeBytes, frames, 44100.0f);
	QVERIFY2(result.ok(),
		qPrintable(QString::fromStdString("process trapped: " + result.message)));
	QVERIFY(sandbox.readMemory(planeBytes, output.data(), planeBytes));

	for (std::uint32_t i = 0; i < frames; ++i)
	{
		const float expected = (1.0f + static_cast<float>(i)) * 0.5f;
		QCOMPARE(output[i], expected);
	}
	qInfo("G2 direct: 48-frame block through process(in=0, out=%u, frames=48, "
		"rate=44100) scaled by 0.5; out[0]=%.3f out[47]=%.3f",
		frames * static_cast<std::uint32_t>(sizeof(float)),
		static_cast<double>(output[0]), static_cast<double>(output[47]));
}

void WasmSandboxTest::g2_wasmEffectProcesses48FrameBlock()
{
	constexpr f_cnt_t frames = 48;
	WasmEffect effect(nullptr, nullptr);
	QString error;
	QVERIFY2(effect.loadModule(modulePath("gain"), &error), qPrintable(error));
	effect.setModuleParam(0, 0.5f);

	AudioBuffer buffer{frames, DEFAULT_CHANNELS};
	buffer.allocateInterleavedBuffer();
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		buffer.interleavedBuffer()[f][0] = 1.0f;
		buffer.interleavedBuffer()[f][1] = 0.5f;
	}
	// Writing raw samples does not update AudioBuffer's silence bookkeeping;
	// without this the effect would treat the block as silent and stay asleep.
	buffer.assumeNonSilent(0);
	buffer.assumeNonSilent(1);

	// Block 1: submit only - the audio thread must not run the module, so the
	// buffer is passed through dry.
	QVERIFY(effect.processAudioBuffer(buffer));
	QCOMPARE(buffer.interleavedBuffer()[0][0], 1.0f);
	QVERIFY(effect.worker().waitForIdle(5000));
	QCOMPARE(effect.worker().processedBlocks(), std::uint64_t{1});

	// Block 2: the previous block's result is collected.
	QVERIFY(effect.processAudioBuffer(buffer));
	QCOMPARE(buffer.interleavedBuffer()[0][0], 0.5f);
	QCOMPARE(buffer.interleavedBuffer()[frames - 1][0], 0.5f);
	QCOMPARE(buffer.interleavedBuffer()[frames - 1][1], 0.25f);
	QCOMPARE(effect.worker().droppedBlocks(), std::uint64_t{0});
	qInfo("G2 WasmEffect: 48-frame block (l=1.0 r=0.5) -> out[0]=(%.3f, %.3f) "
		"out[47]=(%.3f, %.3f), submitted=%llu processed=%llu dropped=%llu",
		static_cast<double>(buffer.interleavedBuffer()[0][0]),
		static_cast<double>(buffer.interleavedBuffer()[0][1]),
		static_cast<double>(buffer.interleavedBuffer()[frames - 1][0]),
		static_cast<double>(buffer.interleavedBuffer()[frames - 1][1]),
		static_cast<unsigned long long>(effect.worker().submittedBlocks()),
		static_cast<unsigned long long>(effect.worker().processedBlocks()),
		static_cast<unsigned long long>(effect.worker().droppedBlocks()));
}

// ---------------------------------------------------------------------------
// G3
// ---------------------------------------------------------------------------

void WasmSandboxTest::g3_infiniteLoopTrapsOnFuel()
{
	WasmSandbox sandbox;
	std::string error;
	QVERIFY2(sandbox.loadModuleFile(modulePath("spin").toStdString(), error),
		qPrintable(QString::fromStdString("loadModuleFile: " + error)));

	constexpr std::uint64_t budget = 100000;
	sandbox.setFuelBudget(budget);
	auto result = sandbox.callProcess(0, 4096, 1024, 44100.0f);
	QVERIFY(result.trapped());
	QCOMPARE(static_cast<int>(result.trapCode),
		static_cast<int>(WASMTIME_TRAP_CODE_OUT_OF_FUEL));
	QCOMPARE(result.fuelConsumed, budget);
	QVERIFY(!result.message.empty());
	qInfo("G3 fuel: spin.wasm trapped after %llu/%llu fuel, code=%d (%s)",
		static_cast<unsigned long long>(result.fuelConsumed),
		static_cast<unsigned long long>(budget),
		static_cast<int>(result.trapCode), result.message.c_str());
}

void WasmSandboxTest::g3_abortModuleTrapsAndDoesNotCrashHost()
{
	WasmSandbox sandbox;
	std::string error;
	QVERIFY2(sandbox.loadModuleFile(modulePath("abort").toStdString(), error),
		qPrintable(QString::fromStdString("loadModuleFile: " + error)));
	auto result = sandbox.callProcess(0, 4096, 1024, 44100.0f);
	QVERIFY(result.trapped());
	QCOMPARE(static_cast<int>(result.trapCode),
		static_cast<int>(WASMTIME_TRAP_CODE_UNREACHABLE_CODE_REACHED));
	qInfo("G3 crash isolation: abort.wasm trapped, code=%d (%s); host still alive",
		static_cast<int>(result.trapCode), result.message.c_str());

	// The host process is demonstrably still functional.
	WasmSandbox second;
	QVERIFY2(second.loadModuleFile(modulePath("hello").toStdString(), error),
		qPrintable(QString::fromStdString(error)));
	QCOMPARE(second.callI32("hello").returnValue, 42);
}

void WasmSandboxTest::g3_outOfBoundsMemoryTraps()
{
	WasmSandbox sandbox;
	std::string error;
	QVERIFY2(sandbox.loadModuleFile(modulePath("oob").toStdString(), error),
		qPrintable(QString::fromStdString("loadModuleFile: " + error)));
	auto result = sandbox.callProcess(0, 4096, 1024, 44100.0f);
	QVERIFY(result.trapped());
	QCOMPARE(static_cast<int>(result.trapCode),
		static_cast<int>(WASMTIME_TRAP_CODE_MEMORY_OUT_OF_BOUNDS));
	qInfo("G3 memory isolation: oob.wasm trapped, code=%d (%s)",
		static_cast<int>(result.trapCode), result.message.c_str());
}

void WasmSandboxTest::g3_effectQuarantinesTrappingModule()
{
	constexpr f_cnt_t frames = 48;
	WasmEffect effect(nullptr, nullptr);
	QString error;
	QVERIFY2(effect.loadModule(modulePath("abort"), &error), qPrintable(error));

	AudioBuffer buffer{frames, DEFAULT_CHANNELS};
	buffer.allocateInterleavedBuffer();
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		buffer.interleavedBuffer()[f][0] = 0.25f;
		buffer.interleavedBuffer()[f][1] = -0.25f;
	}
	buffer.assumeNonSilent(0);
	buffer.assumeNonSilent(1);

	// The module traps on the worker thread; the audio thread must survive.
	QVERIFY(effect.processAudioBuffer(buffer));
	QVERIFY(effect.worker().waitForIdle(5000));
	QVERIFY(effect.isModuleCorrupted());
	QVERIFY(!effect.worker().isReady());
	QCOMPARE(effect.worker().trappedBlocks(), std::uint64_t{1});
	QVERIFY(!effect.lastError().isEmpty());

	// Subsequent blocks are passed through dry and the host is fine.
	QVERIFY(effect.processAudioBuffer(buffer));
	QCOMPARE(buffer.interleavedBuffer()[0][0], 0.25f);
	QCOMPARE(buffer.interleavedBuffer()[frames - 1][1], -0.25f);
	qInfo("G3 quarantine: abort.wasm corrupted the worker (%s); effect passes "
		"audio through dry, process alive",
		effect.lastError().toStdString().c_str());
}

// ---------------------------------------------------------------------------
// G4
// ---------------------------------------------------------------------------

void WasmSandboxTest::g4_compiledCDemoModule()
{
	const QString path = modulePath("gain_clip");
	if (!QFileInfo::exists(path))
	{
		QSKIP("gain_clip.wasm not built (no zig toolchain); see WASM-SANDBOX.md");
	}

	WasmSandbox sandbox;
	std::string error;
	QVERIFY2(sandbox.loadModuleFile(path.toStdString(), error),
		qPrintable(QString::fromStdString("loadModuleFile: " + error)));
	QVERIFY(sandbox.hasProcess());
	QCOMPARE(sandbox.declaredChannels(), 1);

	// gain_clip.c: out = softclip(in * param(0)), softclip(x) = x / (1 + |x|).
	// gain 0.5, in 1.0 -> 0.5 / 1.5 = 0.333333; in 0.5 -> 0.25 / 1.25 = 0.2.
	sandbox.setParam(0, 0.5f);
	constexpr std::uint32_t frames = 48;
	const std::uint32_t planeBytes = frames * sizeof(float);
	std::vector<float> input(frames, 1.0f);
	std::vector<float> output(frames, 0.0f);
	input[47] = 0.5f;
	QVERIFY(sandbox.writeMemory(0, input.data(), planeBytes));
	QVERIFY(sandbox.writeMemory(planeBytes, output.data(), planeBytes));

	auto result = sandbox.callProcess(0, planeBytes, frames, 48000.0f);
	QVERIFY2(result.ok(),
		qPrintable(QString::fromStdString("gain_clip trapped: " + result.message)));
	QVERIFY(sandbox.readMemory(planeBytes, output.data(), planeBytes));
	QVERIFY2(std::fabs(output[0] - 0.333333f) < 1e-5f,
		qPrintable(QString("out[0]=%1").arg(output[0], 0, 'f', 6)));
	QVERIFY2(std::fabs(output[47] - 0.2f) < 1e-5f,
		qPrintable(QString("out[47]=%1").arg(output[47], 0, 'f', 6)));
	QCOMPARE(QString::fromStdString(sandbox.takeLog()),
		QStringLiteral("gain_clip: first block processed"));
	qInfo("G4 C demo: gain_clip.wasm 48 frames, in=1.0 -> %.6f, in=0.5 -> %.6f, "
		"fuelConsumed=%llu",
		static_cast<double>(output[0]), static_cast<double>(output[47]),
		static_cast<unsigned long long>(result.fuelConsumed));
}

// ---------------------------------------------------------------------------
// real-time thread discipline
// ---------------------------------------------------------------------------

void WasmSandboxTest::rt_spscRingBufferIsAllocationFree()
{
	SpscRingBuffer<std::uint32_t, 8> queue;
	static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
		"the queue must not take locks");

	QCOMPARE(queue.capacity(), std::size_t{7});

	// Warm up (first-touch) before measuring.
	QVERIFY(queue.push(0));
	std::uint32_t value = 0;
	QVERIFY(queue.pop(value));

	g_allocationCount.store(0);
	g_countAllocations.store(true);
	std::uint32_t pushed = 0;
	std::uint32_t popped = 0;
	for (std::uint32_t i = 0; i < 100000; ++i)
	{
		if (queue.push(i)) { ++pushed; }
		if (queue.pop(value) && value == popped) { ++popped; }
	}
	g_countAllocations.store(false);

	QCOMPARE(pushed, std::uint32_t{100000});
	QCOMPARE(popped, std::uint32_t{100000});
	QCOMPARE(g_allocationCount.load(), std::uint64_t{0});
	qInfo("RT: 100000 SPSC push/pop pairs, allocations=%llu",
		static_cast<unsigned long long>(g_allocationCount.load()));
}

void WasmSandboxTest::rt_audioThreadCallsDoNotAllocate()
{
	constexpr std::uint32_t frames = 48;
	WasmWorker worker;
	std::string error;
	QVERIFY2(worker.start(modulePath("gain").toStdString(), error),
		qPrintable(QString::fromStdString("start: " + error)));
	worker.setParam(0, 0.5f);

	std::vector<SampleFrame> block(frames, SampleFrame{1.0f, 0.5f});

	// Warm up the queue/slot paths while the worker is idle, then measure the
	// audio-thread entry points with the worker drained.
	QVERIFY(worker.submit(block.data(), frames, 44100.0f));
	QVERIFY(worker.waitForIdle(5000));

	g_allocationCount.store(0);
	g_countAllocations.store(true);
	const bool collected = worker.collect(block.data(), frames);
	g_countAllocations.store(false);

	QVERIFY(collected);
	QCOMPARE(block[0][0], 0.5f);
	QCOMPARE(block[frames - 1][1], 0.25f);
	QCOMPARE(g_allocationCount.load(), std::uint64_t{0});

	// And the submit path with the worker idle: no allocation on the caller.
	QVERIFY(worker.waitForIdle(5000));
	g_allocationCount.store(0);
	g_countAllocations.store(true);
	const bool submitted = worker.submit(block.data(), frames, 44100.0f);
	g_countAllocations.store(false);
	QVERIFY(submitted);
	QCOMPARE(g_allocationCount.load(), std::uint64_t{0});
	QVERIFY(worker.waitForIdle(5000));

	qInfo("RT: submit()/collect() on the audio-thread path, allocations=%llu",
		static_cast<unsigned long long>(g_allocationCount.load()));
}

// ---------------------------------------------------------------------------
// Task #591: WasmEffect as a first-class LMMS plugin
// ---------------------------------------------------------------------------

void WasmSandboxTest::p1_pluginDescriptorIsWellFormed()
{
	// The plugin browser and the loader both go through this descriptor. The
	// name must equal the library base name (libwasm_effect.so -> "wasm_effect")
	// because PluginFactory looks up <baseName>_plugin_descriptor.
	QCOMPARE(QString(wasm_effect_plugin_descriptor.name), QString("wasm_effect"));
	QVERIFY(wasm_effect_plugin_descriptor.type == Plugin::Type::Effect);
	QVERIFY(wasm_effect_plugin_descriptor.displayName != nullptr);
	QVERIFY(wasm_effect_plugin_descriptor.description != nullptr);
	QVERIFY(wasm_effect_plugin_descriptor.author != nullptr);
	QVERIFY(wasm_effect_plugin_descriptor.logo != nullptr);

	WasmEffect effect(nullptr, nullptr);
	QCOMPARE(effect.nodeName(), QString("effect"));
	QVERIFY(effect.controls() != nullptr);
	QCOMPARE(effect.controls()->nodeName(), QString("wasmeffectcontrols"));
	QCOMPARE(effect.controls()->controlCount(), 8);
	QCOMPARE(effect.modulePath(), QString());
	QCOMPARE(effect.latencyFrames(), 0);
	QVERIFY(!effect.worker().isReady());
	QVERIFY(effect.worker().state() == WasmWorker::State::Idle);
	QVERIFY(!effect.isModuleCorrupted());

	qInfo("P1 registration: descriptor name=%s type=Effect, nodeName=%s, "
		"controls=%s (%d params)",
		wasm_effect_plugin_descriptor.name,
		qPrintable(effect.nodeName()),
		qPrintable(effect.controls()->nodeName()),
		effect.controls()->controlCount());
}

void WasmSandboxTest::p2_effectSavesAndReloadsModuleAndParameters()
{
	const QString module = modulePath("gain");
	WasmEffect saved(nullptr, nullptr);
	QString error;
	QVERIFY2(saved.loadModule(module, &error), qPrintable(error));
	QVERIFY(saved.worker().isReady());
	// Drive the parameters the way the UI does: through the model, which is
	// what saveSettings() serialises (setModuleParam() only pushes to the DSP).
	saved.controls()->paramModel(0)->setValue(0.125f);
	saved.controls()->paramModel(3)->setValue(0.75f);
	saved.controls()->paramModel(7)->setValue(0.25f);

	// Save exactly like EffectChain::saveSettings does: Effect::saveSettings
	// writes the effect's own attributes and appends
	// <wasmeffectcontrols module=...> with one <param index= value=/> per knob.
	QDomDocument doc;
	QDomElement element = doc.createElement("effect");
	element.setAttribute("name", "wasm_effect");
	doc.appendChild(element);
	saved.saveSettings(doc, element);
	const QString xml = doc.toString();
	qInfo().noquote() << "P2 save: project fragment written by saveSettings:\n" + xml;

	// Reopen: a fresh instance loads the element, as when a project is opened.
	WasmEffect reloaded(nullptr, nullptr);
	QDomElement savedElement = doc.documentElement();
	reloaded.loadSettings(savedElement);

	QCOMPARE(reloaded.modulePath(), module);
	QVERIFY(reloaded.worker().isReady());
	for (int i = 0; i < reloaded.controls()->controlCount(); ++i)
	{
		QCOMPARE(reloaded.controls()->paramModel(i)->value(),
			saved.controls()->paramModel(i)->value());
	}

	// The reloaded instance really runs the restored module with the restored
	// parameter: block 2 carries the previous block scaled by 0.125.
	AudioBuffer buffer(48, DEFAULT_CHANNELS);
	buffer.allocateInterleavedBuffer();
	for (f_cnt_t f = 0; f < 48; ++f)
	{
		buffer.interleavedBuffer()[f][0] = 1.0f;
		buffer.interleavedBuffer()[f][1] = 1.0f;
	}
	buffer.assumeNonSilent(0);
	buffer.assumeNonSilent(1);
	QVERIFY(reloaded.processAudioBuffer(buffer));
	QCOMPARE(buffer.interleavedBuffer()[0][0], 1.0f);
	QVERIFY(reloaded.worker().waitForIdle(5000));
	QVERIFY(reloaded.processAudioBuffer(buffer));
	QCOMPARE(buffer.interleavedBuffer()[0][0], 0.125f);
	qInfo("P2 round trip: module=%s param0=%.3f param3=%.3f param7=%.3f, "
		"rendered block2 out[0]=%.4f",
		qPrintable(reloaded.modulePath()),
		static_cast<double>(reloaded.controls()->paramModel(0)->value()),
		static_cast<double>(reloaded.controls()->paramModel(3)->value()),
		static_cast<double>(reloaded.controls()->paramModel(7)->value()),
		static_cast<double>(buffer.interleavedBuffer()[0][0]));

	// Backward compatible: a project saved before WasmEffect existed has no
	// <wasmeffectcontrols> child at all; defaults must survive untouched.
	QDomDocument legacyDoc;
	QDomElement legacyElement = legacyDoc.createElement("effect");
	legacyDoc.appendChild(legacyElement);
	WasmEffect legacy(nullptr, nullptr);
	legacy.loadSettings(legacyElement);
	QCOMPARE(legacy.modulePath(), QString());
	QVERIFY(!legacy.worker().isReady());
	QCOMPARE(legacy.controls()->paramModel(0)->value(), 0.5f);

	// Backward compatible: an older element with only a subset of parameters
	// keeps defaults for the missing ones.
	QDomDocument partialDoc;
	QDomElement partialElement = partialDoc.createElement("effect");
	partialDoc.appendChild(partialElement);
	QDomElement controlsElement = partialDoc.createElement("wasmeffectcontrols");
	controlsElement.setAttribute("module", module);
	QDomElement paramElement = partialDoc.createElement("param");
	paramElement.setAttribute("index", 0);
	paramElement.setAttribute("value", QString::number(0.75));
	controlsElement.appendChild(paramElement);
	partialElement.appendChild(controlsElement);
	WasmEffect partial(nullptr, nullptr);
	partial.loadSettings(partialElement);
	QCOMPARE(partial.modulePath(), module);
	QCOMPARE(partial.controls()->paramModel(0)->value(), 0.75f);
	QCOMPARE(partial.controls()->paramModel(1)->value(), 0.5f);
	QVERIFY(partial.worker().isReady());
	qInfo("P2 backward compatibility: empty element -> module \"%s\", "
		"partial element -> param0=%.3f param1=%.3f (default)",
		qPrintable(legacy.modulePath()),
		static_cast<double>(partial.controls()->paramModel(0)->value()),
		static_cast<double>(partial.controls()->paramModel(1)->value()));
}

void WasmSandboxTest::p3_moduleLatencyIsApplied()
{
	constexpr std::uint32_t kFrames = 48;
	constexpr int kModuleLatency = 64;
	constexpr int kTotalLatency = kModuleLatency + static_cast<int>(kFrames);
	constexpr int kBlocks = 8;

	// The effect's *reported* latency is expressed in host periods - the engine
	// hands effects exactly framesPerPeriod() frames - while this test drives
	// 48-frame blocks by hand so the impulse arithmetic stays exact. The two
	// figures therefore differ here; in the engine they are the same number.
	const int kReportedLatency =
		kModuleLatency + static_cast<int>(Engine::audioEngine()->framesPerPeriod());

	// Phase 1: the module keeps running. An impulse fed in block 0 must come
	// out exactly kModuleLatency + one pipeline block later.
	{
		WasmEffect effect(nullptr, nullptr);
		QString error;
		QVERIFY2(effect.loadModule(modulePath("latency"), &error), qPrintable(error));
		QVERIFY(effect.worker().isReady());
		QCOMPARE(effect.worker().declaredLatency(), kModuleLatency);

		std::vector<float> out;
		out.reserve(kBlocks * kFrames);
		AudioBuffer buffer(kFrames, DEFAULT_CHANNELS);
		buffer.allocateInterleavedBuffer();
		for (int b = 0; b < kBlocks; ++b)
		{
			for (std::uint32_t f = 0; f < kFrames; ++f)
			{
				const float in = (b == 0 && f == 0) ? 1.0f : 0.0f;
				buffer.interleavedBuffer()[f][0] = in;
				buffer.interleavedBuffer()[f][1] = in;
			}
			buffer.assumeNonSilent(0);
			buffer.assumeNonSilent(1);
			QVERIFY(effect.processAudioBuffer(buffer));
			// Lockstep: collect() runs before the next submit(), so the
			// pipeline delay is exactly one block (deterministic by design).
			QVERIFY(effect.worker().waitForIdle(5000));
			for (std::uint32_t f = 0; f < kFrames; ++f)
			{
				out.push_back(buffer.interleavedBuffer()[f][0]);
			}
		}
		int impulseAt = -1;
		for (std::size_t i = 0; i < out.size(); ++i)
		{
			if (std::fabs(out[i]) > 0.5f) { impulseAt = static_cast<int>(i); break; }
		}
		qInfo("P3 latency: module declares %d frames; impulse at input frame 0 "
			"appears at output frame %d (expected %d = latency + 1 block of %u); "
			"reported effect latency %d frames (%d + one host period of %d)",
			kModuleLatency, impulseAt, kTotalLatency, kFrames,
			effect.latencyFrames(), kModuleLatency,
			static_cast<int>(Engine::audioEngine()->framesPerPeriod()));
		QCOMPARE(impulseAt, kTotalLatency);
		for (int i = 0; i < impulseAt; ++i) { QCOMPARE(out[i], 0.0f); }
		QCOMPARE(effect.latencyFrames(), kReportedLatency);
		QVERIFY(!effect.isModuleCorrupted());
	}

	// Phase 2: the module traps on its fourth call. The dry path must keep the
	// same latency, so an impulse before and an impulse after the quarantine
	// land at the same offset.
	{
		WasmEffect effect(nullptr, nullptr);
		QString error;
		QVERIFY2(effect.loadModule(modulePath("latency_trap"), &error), qPrintable(error));
		QVERIFY(effect.worker().isReady());
		QCOMPARE(effect.worker().declaredLatency(), kModuleLatency);

		std::vector<float> out;
		out.reserve(kBlocks * kFrames);
		AudioBuffer buffer(kFrames, DEFAULT_CHANNELS);
		buffer.allocateInterleavedBuffer();
		for (int b = 0; b < kBlocks; ++b)
		{
			for (std::uint32_t f = 0; f < kFrames; ++f)
			{
				// impulse in block 0 (module path) and block 5 (dry path)
				const float in = ((b == 0 || b == 5) && f == 0) ? 1.0f : 0.0f;
				buffer.interleavedBuffer()[f][0] = in;
				buffer.interleavedBuffer()[f][1] = in;
			}
			buffer.assumeNonSilent(0);
			buffer.assumeNonSilent(1);
			QVERIFY(effect.processAudioBuffer(buffer));
			QVERIFY(effect.worker().waitForIdle(5000));
			for (std::uint32_t f = 0; f < kFrames; ++f)
			{
				out.push_back(buffer.interleavedBuffer()[f][0]);
			}
		}
		QVERIFY(effect.isModuleCorrupted());
		QVERIFY(effect.worker().trappedBlocks() >= std::uint64_t{1});

		int firstImpulse = -1;
		int secondImpulse = -1;
		for (std::size_t i = 0; i < out.size(); ++i)
		{
			if (std::fabs(out[i]) > 0.5f)
			{
				if (firstImpulse < 0) { firstImpulse = static_cast<int>(i); }
				else { secondImpulse = static_cast<int>(i); }
			}
		}
		qInfo("P3 latency after trap: module impulse at %d (expected %d), "
			"dry-path impulse at %d (expected %d), corrupted=%d trapped=%llu",
			firstImpulse, kTotalLatency, secondImpulse,
			static_cast<int>(5 * kFrames) + kTotalLatency,
			effect.isModuleCorrupted() ? 1 : 0,
			static_cast<unsigned long long>(effect.worker().trappedBlocks()));
		QCOMPARE(firstImpulse, kTotalLatency);
		QCOMPARE(secondImpulse, static_cast<int>(5 * kFrames) + kTotalLatency);
		QCOMPARE(effect.latencyFrames(), kReportedLatency);
	}
}

void WasmSandboxTest::p4_sampleRateChangeReinstantiatesModule()
{
	constexpr std::uint32_t kFrames = 48;
	WasmWorker worker;
	std::string error;
	QVERIFY2(worker.start(modulePath("latency").toStdString(), error),
		qPrintable(QString::fromStdString("start: " + error)));
	QVERIFY(worker.isReady());
	QCOMPARE(worker.declaredLatency(), 64);
	QCOMPARE(worker.reinstantiatedModules(), std::uint64_t{0});

	std::vector<SampleFrame> block(kFrames, SampleFrame{0.0f, 0.0f});
	std::vector<SampleFrame> out(kFrames, SampleFrame{0.0f, 0.0f});

	// Block 1 at 44100: impulse goes into the module's 64-slot ring buffer.
	block[0] = SampleFrame{1.0f, 1.0f};
	QVERIFY(worker.submit(block.data(), kFrames, 44100.0f));
	QVERIFY(worker.waitForIdle(5000));
	QVERIFY(worker.collect(out.data(), kFrames));
	QCOMPARE(out[0][0], 0.0f);
	QCOMPARE(worker.reinstantiatedModules(), std::uint64_t{0});

	// Block 2 at the same rate: the impulse is still in the ring and must
	// emerge 64 frames after it was written (64 - 48 = frame 16).
	QVERIFY(worker.submit(block.data(), kFrames, 44100.0f));
	QVERIFY(worker.waitForIdle(5000));
	QVERIFY(worker.collect(out.data(), kFrames));
	QCOMPARE(out[16][0], 1.0f);
	QCOMPARE(worker.reinstantiatedModules(), std::uint64_t{0});

	// Now the host switches to 48000 Hz. The module must be re-instantiated:
	// the stale ring content (the impulse would otherwise surface again) must
	// be gone, and the counter must record exactly one reload.
	QVERIFY(worker.submit(block.data(), kFrames, 48000.0f));
	QVERIFY(worker.waitForIdle(5000));
	QVERIFY(worker.collect(out.data(), kFrames));
	QCOMPARE(worker.reinstantiatedModules(), std::uint64_t{1});
	QCOMPARE(worker.declaredLatency(), 64);
	QCOMPARE(worker.declaredChannels(), 1);
	for (std::uint32_t f = 0; f < kFrames; ++f)
	{
		QCOMPARE(out[f][0], 0.0f);
	}

	// A stable rate must not re-instantiate on every block.
	QVERIFY(worker.submit(block.data(), kFrames, 48000.0f));
	QVERIFY(worker.waitForIdle(5000));
	QVERIFY(worker.collect(out.data(), kFrames));
	QCOMPARE(worker.reinstantiatedModules(), std::uint64_t{1});
	QCOMPARE(worker.processedBlocks(), std::uint64_t{4});
	qInfo("P4 sample rate: 44100 -> 44100 (0 reloads) -> 48000 (1 reload, "
		"ring cleared) -> 48000 (still 1 reload); processed=%llu",
		static_cast<unsigned long long>(worker.processedBlocks()));
}

// ---------------------------------------------------------------------------
// Task #591: headless plugin discovery - the plugin must be found and loaded
// through PluginFactory/Plugin::instantiate, exactly as a user's LMMS does at
// startup, not merely by directly constructing the C++ class.
// ---------------------------------------------------------------------------

void WasmSandboxTest::p5_pluginFactoryDiscoversPlugin()
{
	// libwasm_effect.so is the real plugin module the build produces. This test
	// binary does not link it (the plugin sources are compiled in directly), so
	// discovery here exercises the loader path a shipped LMMS would take.
	const QDir pluginDir(QCoreApplication::applicationDirPath() + "/../plugins");
	const QString moduleFile = pluginDir.absoluteFilePath("libwasm_effect.so");
	QVERIFY2(QFileInfo::exists(moduleFile), qPrintable(moduleFile));

	// Search a directory that holds only our module: the build tree contains
	// every other LMMS plugin too, which discovery would otherwise dlopen.
	QTemporaryDir isolatedDir;
	QVERIFY(isolatedDir.isValid());
	QVERIFY(QFile::link(moduleFile, isolatedDir.filePath("libwasm_effect.so")));

	PluginFactory* factory = getPluginFactory();
	QDir::setSearchPaths("plugins", QStringList{isolatedDir.path()});
	factory->discoverPlugins();

	const PluginFactory::PluginInfo info = factory->pluginInfo("wasm_effect");
	QVERIFY2(!info.isNull(), qPrintable(factory->errorString("wasm_effect")));
	QVERIFY(info.library->isLoaded());
	QVERIFY(info.descriptor != nullptr);
	QCOMPARE(QString::fromUtf8(info.descriptor->name), QStringLiteral("wasm_effect"));
	QVERIFY(info.descriptor->type == Plugin::Type::Effect);

	// Plugin::instantiate() is what the engine calls when a project references
	// the plugin: it resolves lmms_plugin_main from the loaded .so.
	Plugin* plugin = Plugin::instantiate("wasm_effect", nullptr, nullptr);
	auto* effect = dynamic_cast<WasmEffect*>(plugin);
	QVERIFY2(effect != nullptr,
		"Plugin::instantiate() did not return a WasmEffect - no plugin was loaded");
	QCOMPARE(effect->controls()->nodeName(), QStringLiteral("wasmeffectcontrols"));
	QCOMPARE(effect->controls()->controlCount(), 8);
	qInfo("P5 discovery: PluginFactory found \"%s\" in %s (library loaded=%d), "
		"descriptor \"%s\" type=Effect; Plugin::instantiate -> %s with %d params",
		qPrintable(info.name()), qPrintable(info.file.absoluteFilePath()),
		info.library->isLoaded() ? 1 : 0, info.descriptor->name,
		qPrintable(effect->nodeName()), effect->controls()->controlCount());
	delete plugin;
}

// ---------------------------------------------------------------------------
// Mixer audit C1b (#605): the PDC graph reads EffectChain::latencyFrames(),
// the cache refreshed by EffectChain::refreshLatency(). WasmEffect reported
// its pipeline delay as "module latency + the last block processImpl() saw",
// which is 0 until the audio thread has run a block, and nothing refreshed
// the cache when a module was loaded. A graph aligned against a Wasm effect
// was therefore off by exactly one host period until some unrelated change
// refreshed the cache - a comb whose first notch sits near fs / (2 * fpp).
// ---------------------------------------------------------------------------

void WasmSandboxTest::p6_moduleLoadRefreshesTheChainLatencyCache()
{
	constexpr int kModuleLatency = 64;
	const int periodFrames = static_cast<int>(Engine::audioEngine()->framesPerPeriod());
	const int kTrueLatency = kModuleLatency + periodFrames;

	EffectChain chain{nullptr};
	auto* effect = new WasmEffect(&chain, nullptr);
	chain.appendEffect(effect);
	QCOMPARE(chain.latencyFrames(), 0); // no module loaded yet

	QString error;
	QVERIFY2(effect->loadModule(modulePath("latency"), &error), qPrintable(error));
	QVERIFY(effect->worker().isReady());
	QCOMPARE(effect->worker().declaredLatency(), kModuleLatency);

	// The PDC graph reads the cache before any audio has run; the load must
	// have refreshed it already, and the effect's own report must not depend
	// on whether processImpl() has seen a block yet.
	const int reportedAfterLoad = effect->latencyFrames();
	const int cachedAfterLoad = chain.latencyFrames();

	// One processed block: a host period is a property of the engine, not of
	// how many blocks the audio thread has rendered, so neither figure may
	// move.
	AudioBuffer buffer(periodFrames, DEFAULT_CHANNELS);
	buffer.allocateInterleavedBuffer();
	for (f_cnt_t f = 0; f < static_cast<f_cnt_t>(periodFrames); ++f)
	{
		buffer.interleavedBuffer()[f][0] = 1.0f;
		buffer.interleavedBuffer()[f][1] = 1.0f;
	}
	buffer.assumeNonSilent(0);
	buffer.assumeNonSilent(1);
	QVERIFY(effect->processAudioBuffer(buffer));

	const int reportedAfterBlock = effect->latencyFrames();
	const int cachedAfterBlock = chain.latencyFrames();

	qInfo("C1b latency: module=%d host_period=%d true=%d | after load: effect=%d chain=%d | "
		"after one processed block: effect=%d chain=%d",
		kModuleLatency, periodFrames, kTrueLatency,
		reportedAfterLoad, cachedAfterLoad, reportedAfterBlock, cachedAfterBlock);

	QCOMPARE(reportedAfterLoad, kTrueLatency);
	QCOMPARE(cachedAfterLoad, kTrueLatency);
	QCOMPARE(reportedAfterBlock, kTrueLatency);
	QCOMPARE(cachedAfterBlock, kTrueLatency);
}

} // namespace lmms::wasm

QTEST_GUILESS_MAIN(lmms::wasm::WasmSandboxTest)

#include "WasmSandboxTest.moc"
