<!--
  In-repo copy of the specification this repository's code and tests cite (DOC-5, 2026-09-15).
  This is a PINNED SNAPSHOT, not a live document:
    source   : the program workspace, WASM-SANDBOX.md
    sha256   : 1d233d09442ce2a14dc1df9aaa67c3805a7272eb2161246ff476d6324254b3e4
    bytes    : 34079
    why this file: the WASM sandbox design; cited by plugins/WasmEffect, modules/wasm/*.wat and tests/src/wasm/WasmSandboxTest.cpp
  The workspace copy remains the working copy and is where the document is edited; refreshing
  this snapshot is a deliberate act, recorded in the commit that does it. If a citation in this
  repository and this file ever disagree, reconcile them in one commit - the citations name the
  file by bare name (e.g. `SPEC-zene-studio.md`), and this directory is where that name resolves.
  See docs/specs/README.md for the convention and for the cited documents NOT yet committed.
-->
# WASM DSP sandbox — task #582 implementation report

Branch: `feat/wasm-sandbox` (worktree `lmms-wasm/`), base `4e677cb6c`
("Don't compress man page in during build (#8494)"), local commits only — **no push**.

| Gate | What it proves | Verdict |
|---|---|---|
| G1 | runtime embedded; hello module instantiated and called; output observed | **PASS** |
| G2 | `WasmEffect` runs a demo DSP module over a block; 48-frame block produces expected output | **PASS** |
| G3 | fuel metering traps an infinite loop; abort/OOB modules do not crash the host | **PASS** |
| G4 | C demo module compiled to wasm, run through the sandbox; runtime size measured | **PASS** |

Commits:

```
f399a0d75a3f41efb7ef31d23ca4b89e1971fe16 Make fetch-wasmtime.sh executable
62a799cef64376dd45aa54f669d45888915196fa Add WASM DSP sandbox gate tests
9084767404919a2539c6fbab8582311a8ded67db Add WASM DSP sandbox with fuel metering and crash isolation
2c47df4a3d60034e38a33dedafbecf5083fe185d Add optional wasmtime C API dependency discovery
```

(The top commit only flips the fetch script's mode to `+x`; the audit below still scans the
full `4e677cb6c..HEAD` range.)

---

## 1. Runtime: obtained and pinned

**wasmtime v48.0.1, prebuilt C API, x86_64-linux.**
URL: `https://github.com/bytecodealliance/wasmtime/releases/download/v48.0.1/wasmtime-v48.0.1-x86_64-linux-c-api.tar.xz`

Chosen because the acceptance criteria require first-class **fuel metering**; wasmtime's C API
provides `wasmtime_config_consume_fuel_set`, `wasmtime_context_set_fuel` and
`wasmtime_context_get_fuel` directly, and the prebuilt archive needs no Rust toolchain.
(wasm3/MIT was the documented fallback and would have required a hand-rolled runaway guard.)

`scripts/fetch-wasmtime.sh` downloads, **verifies** and extracts the archive to
`third_party/wasmtime/` (gitignored). Measured facts:

```
$ ls -l third_party/wasmtime-v48.0.1-x86_64-linux-c-api.tar.xz
-rw-rw-r-- 1 kruzzzzy kruzzzzy 16041804 Sep  9 00:09 third_party/wasmtime-v48.0.1-x86_64-linux-c-api.tar.xz
$ sha256sum third_party/wasmtime-v48.0.1-x86_64-linux-c-api.tar.xz
67683d04b416a8b91f0e607e7b4c22bd32f18f947c10b5372eb8c277ae3b883a  third_party/wasmtime-v48.0.1-x86_64-linux-c-api.tar.xz
$ du -sh third_party/wasmtime
104M	third_party/wasmtime
$ du -sh third_party/wasmtime/include third_party/wasmtime/lib
960K	third_party/wasmtime/include
98M	third_party/wasmtime/lib
$ ls -l third_party/wasmtime/lib/
total 100320
-rw-r--r-- 1 kruzzzzy kruzzzzy 71193834 Aug 24 20:45 libwasmtime.a
-rw-r--r-- 1 kruzzzzy kruzzzzy 31528984 Aug 24 20:45 libwasmtime.so
$ grep -rn "WASMTIME_VERSION" third_party/wasmtime/include/wasmtime.h | head -3
232:#define WASMTIME_VERSION "48.0.1"
236:#define WASMTIME_VERSION_MAJOR 48
240:#define WASMTIME_VERSION_MINOR 0
```

So: **16,041,804 bytes** on disk for the pinned archive, **104 MB** extracted,
**31,528,984 bytes** for the shared library and **71,193,834 bytes** for the static one.

### How CMake finds it

`cmake/modules/FindWasmtime.cmake` (a standard find module, no `FindPkgConfig` dependency):

* `-DWASMTIME_ROOT=<prefix>` (cache variable) and `$ENV{WASMTIME_ROOT}` are used as hints;
* otherwise the usual system paths are searched for `include/wasmtime.h` and
  `lib/libwasmtime.so` / `libwasmtime.a`;
* on success it sets `WASMTIME_INCLUDE_DIRS` and `WASMTIME_LIBRARIES`.

Top-level `CMakeLists.txt`:

```cmake
option(WANT_WASM "Include the WASM DSP sandbox (requires the wasmtime C API)" ON)
...
FIND_PACKAGE(Wasmtime)
```

The dependency is **optional**: if the runtime is absent, `WANT_WASM` is forced off and the
sandbox sources are simply not built.

### Configure with / without the runtime (both exit 0)

```
$ cmake -B build -DWANT_QT6=ON          # wasmtime present
exit=0
* WASM DSP sandbox                  : Enabled

$ mv third_party/wasmtime third_party/.wasmtime-hidden
$ rm -rf build-nowasm && cmake -B build-nowasm -DWANT_QT6=ON   # wasmtime absent
exit=0
-- Could NOT find Wasmtime (missing: Wasmtime_LIBRARY Wasmtime_INCLUDE_DIR) 
* WASM DSP sandbox                  : Disabled (wasmtime C API not found; set WASMTIME_ROOT)
$ mv third_party/.wasmtime-hidden third_party/wasmtime
$ ls -d third_party/wasmtime
third_party/wasmtime
```

The full LMMS build with the runtime present then completed cleanly (`tail -2` of the build log
plus the exit status file):

```
$ tail -2 /tmp/build-wasm4.log ; cat /tmp/build-wasm4.exit
[100%] Linking CXX shared module ../libxpressive.so
[100%] Built target xpressive
BUILD_EXIT=0
```

---

## 2. The audio ABI as implemented (frozen v0)

`src/wasm/WasmAbi.h` is the single source of truth for module authors. The host passes
**offsets into the module's linear memory, never host pointers**:

```c
//! Required export: the DSP entry point.
//! process(in_ptr, out_ptr, frames, sample_rate) -> i32 (0 = ok)
constexpr const char* processExport = "process";

//! Required export: linear memory holding the planar sample planes.
constexpr const char* memoryExport = "memory";

//! Optional export: number of process() calls the host must make per block,
//! one per channel plane (default 1, clamped to [1, maxChannels]).
constexpr const char* channelsExport = "channels";

//! Optional export: declared latency in frames (default 0).
constexpr const char* latencyExport = "latency";

//! Optional export: ABI version (default 1).
constexpr const char* abiVersionExport = "abi";

//! Host functions importable from "env".
constexpr const char* hostGetParam = "host_get_param";
constexpr const char* hostLog = "host_log";
constexpr const char* hostGetTransportState = "host_get_transport_state";
```

Threading model (spec §4):

* the **audio thread** calls `WasmEffect::processImpl` only;
* it copies the block into a pre-allocated slot and pushes the slot index onto a lock-free
  SPSC queue (`WasmSpscRingBuffer.h`), then pops the previous result if one is ready;
* a dedicated **worker thread** owns the `WasmSandbox` (wasmtime engine/store/instance) and
  runs the module — the audio thread never enters the runtime;
* each `process()` call runs under a **fuel budget** (`defaultFuelBudget = 1000000`), with
  store limits of 16 MiB linear memory, 1 instance, 1 memory, 1 table.

A module that traps (fuel, `unreachable`, OOB) is caught at the call boundary; the
worker records the error and goes to `Corrupted`, and the effect keeps passing audio through
dry. LMMS never crashes.

---

## 3. Gate evidence

All of the following is the unedited output of the gate suite on the pinned runtime; the
standalone-driver block is explicitly labelled as an excerpt.

### G1 — hello module instantiated and called

```
QINFO  : lmms::wasm::WasmSandboxTest::g1_helloFromWatSource() G1 hello.wat: hello() -> 42, fuelConsumed=2
PASS   : lmms::wasm::WasmSandboxTest::g1_helloFromWatSource()
QINFO  : lmms::wasm::WasmSandboxTest::g1_helloFromBuildTimeModule() G1 hello.wasm (assembled at build time): hello() -> 42
PASS   : lmms::wasm::WasmSandboxTest::g1_helloFromBuildTimeModule()
QINFO  : lmms::wasm::WasmSandboxTest::g1_hostImportsAreWired() G1 host imports: host_log -> 'hello from wasm', host_get_transport_state -> 1
PASS   : lmms::wasm::WasmSandboxTest::g1_hostImportsAreWired()
```

### G2 — 48-frame block through the sandbox and through `WasmEffect`

```
QINFO  : lmms::wasm::WasmSandboxTest::g2_sandboxProcesses48FrameBlock() G2 direct: 48-frame block through process(in=0, out=192, frames=48, rate=44100) scaled by 0.5; out[0]=0.500 out[47]=24.000
PASS   : lmms::wasm::WasmSandboxTest::g2_sandboxProcesses48FrameBlock()
QINFO  : lmms::wasm::WasmSandboxTest::g2_wasmEffectProcesses48FrameBlock() G2 WasmEffect: 48-frame block (l=1.0 r=0.5) -> out[0]=(0.500, 0.250) out[47]=(0.500, 0.250), submitted=2 processed=1 dropped=0
PASS   : lmms::wasm::WasmSandboxTest::g2_wasmEffectProcesses48FrameBlock()
```

### G3 — fuel exhaustion, abort isolation, OOB isolation, effect quarantine

```
QINFO  : lmms::wasm::WasmSandboxTest::g3_infiniteLoopTrapsOnFuel() G3 fuel: spin.wasm trapped after 100000/100000 fuel, code=11 (error while executing at wasm backtrace:
    0:     0x4a - <unknown>!<wasm function 0>

Caused by:
    wasm trap: all fuel consumed by WebAssembly
)
PASS   : lmms::wasm::WasmSandboxTest::g3_infiniteLoopTrapsOnFuel()
QINFO  : lmms::wasm::WasmSandboxTest::g3_abortModuleTrapsAndDoesNotCrashHost() G3 crash isolation: abort.wasm trapped, code=9 (error while executing at wasm backtrace:
    0:     0x5b - <unknown>!abort
    1:     0x5f - <unknown>!<wasm function 1>

Caused by:
    wasm trap: wasm `unreachable` instruction executed
); host still alive
PASS   : lmms::wasm::WasmSandboxTest::g3_abortModuleTrapsAndDoesNotCrashHost()
QINFO  : lmms::wasm::WasmSandboxTest::g3_outOfBoundsMemoryTraps() G3 memory isolation: oob.wasm trapped, code=1 (error while executing at wasm backtrace:
    0:     0x4c - <unknown>!<wasm function 0>

Caused by:
    0: memory fault at wasm address 0x100000 in linear memory of size 0x10000
    1: wasm trap: out of bounds memory access
)
PASS   : lmms::wasm::WasmSandboxTest::g3_outOfBoundsMemoryTraps()
QINFO  : lmms::wasm::WasmSandboxTest::g3_effectQuarantinesTrappingModule() G3 quarantine: abort.wasm corrupted the worker (error while executing at wasm backtrace:
    0:     0x5b - <unknown>!abort
    1:     0x5f - <unknown>!<wasm function 1>

Caused by:
    wasm trap: wasm `unreachable` instruction executed
); effect passes audio through dry, process alive
PASS   : lmms::wasm::WasmSandboxTest::g3_effectQuarantinesTrappingModule()
```

The same gates were also run through a standalone driver against the built modules
(excerpt of that driver's output; the full run reported `SMOKE PASS (0 failures)`):

```
[ok]   G1 hello() returned 42, got 42
[ok]   G1 fuel metered on hello call: 2 units
[ok]   G2 declared channels == 1, got 1
[ok]   G2 process() returned 0, got 0
[ok]   G2 all 48 frames scaled by gain 0.5
     in[0]=1.0 -> out[0]=0.5, in[47]=48.0 -> out[47]=24.0, fuel 1113
[ok]   G3 spin.wasm trapped instead of hanging the host
[ok]   G3 trap code == 11 (out of fuel), got 11
[ok]   G3 all 100000 fuel consumed, got 100000
[ok]   G3 abort.wasm trapped instead of crashing the host
[ok]   G3 trap code == 9 (unreachable), got 9
[ok]   G3 oob.wasm trapped instead of crashing the host
[ok]   G3 trap code == 1 (memory out of bounds), got 1
[ok]   probe log_it() returned ok
[ok]   host_log() delivered text, got 'hello from wasm'
[ok]   host_get_transport_state() == 1, got 1

SMOKE PASS (0 failures)
```

### G4 — reference module written in C, compiled to wasm, measured

`modules/wasm/demo/gain_clip.c` (gain + soft clip) was compiled with Zig 0.14.1:

```
zig cc -target wasm32-freestanding -O2 -nostdlib -Wl,--no-entry \
    -o build/wasm-modules/gain_clip.wasm modules/wasm/demo/gain_clip.c
```

Run through `WasmSandbox`:

```
loaded: build/wasm-modules/gain_clip.wasm
declaredChannels=1 declaredLatency=0 hasProcess=1
process ok=1 trapCode=-1 message= fuelConsumed=1313
out[0]=0.333333 (expect 0.333333) out[47]=0.200000 (expect 0.200000)
host log: 'gain_clip: first block processed'
```

and through the QtTest suite:

```
QINFO  : lmms::wasm::WasmSandboxTest::g4_compiledCDemoModule() G4 C demo: gain_clip.wasm 48 frames, in=1.0 -> 0.333333, in=0.5 -> 0.200000, fuelConsumed=1313
PASS   : lmms::wasm::WasmSandboxTest::g4_compiledCDemoModule()
```

Built module sizes:

```
$ ls -l build/wasm-modules/
total 28
-rw-rw-r-- 1 kruzzzzy kruzzzzy  115 Sep  9 00:19 abort.wasm
-rwxrwxr-x 1 kruzzzzy kruzzzzy 2315 Sep  9 00:22 gain_clip.wasm
-rw-rw-r-- 1 kruzzzzy kruzzzzy  266 Sep  9 00:32 gain.wasm
-rw-rw-r-- 1 kruzzzzy kruzzzzy   38 Sep  9 00:19 hello.wasm
-rw-rw-r-- 1 kruzzzzy kruzzzzy  127 Sep  9 00:19 oob.wasm
-rw-rw-r-- 1 kruzzzzy kruzzzzy  201 Sep  9 00:19 probe.wasm
-rw-rw-r-- 1 kruzzzzy kruzzzzy  103 Sep  9 00:19 spin.wasm
```

### Full suite totals

```
$ cd build/tests && ctest --output-on-failure
Test project /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-wasm/build/tests
    Start 1: ArrayVectorTest
1/9 Test #1: ArrayVectorTest ..................   Passed    0.02 sec
    Start 2: AudioBufferTest
2/9 Test #2: AudioBufferTest ..................   Passed    0.02 sec
    Start 3: AutomatableModelTest
3/9 Test #3: AutomatableModelTest .............   Passed    1.23 sec
    Start 4: MathTest
4/9 Test #4: MathTest .........................   Passed    0.02 sec
    Start 5: ProjectVersionTest
5/9 Test #5: ProjectVersionTest ...............   Passed    0.02 sec
    Start 6: RelativePathsTest
6/9 Test #6: RelativePathsTest ................   Passed    0.02 sec
    Start 7: TimelineTest
7/9 Test #7: TimelineTest .....................   Passed    1.23 sec
    Start 8: AutomationTrackTest
8/9 Test #8: AutomationTrackTest ..............   Passed    1.24 sec
    Start 9: WasmSandboxTest
9/9 Test #9: WasmSandboxTest ..................   Passed    1.29 sec

100% tests passed, 0 tests failed out of 9

Total Test time (real) =   5.10 sec
```

`WasmSandboxTest` itself: **16 passed, 0 failed, 0 skipped** (1232 ms). The eight
pre-existing tests pass unchanged.

---

## 4. Audio-thread audit

Command (over the whole diff):

```
git diff -U0 4e677cb6c..HEAD -- src/wasm | grep -nE "^\+.*(new|delete|malloc|calloc|realloc|free|strdup|push_back|emplace_back|resize|reserve|make_unique|make_shared|unique_ptr|shared_ptr|std::mutex|std::lock_guard|std::unique_lock|std::condition_variable|\.lock\(|try_lock|std::function|std::vector|QString)"
```

Findings, classified:

```
$ git diff -U0 4e677cb6c..HEAD -- src/wasm | grep -nE "^\+.*PATTERN"
49:+		COMMAND "${ZIG_EXECUTABLE}" cc -target wasm32-freestanding -O2 -nostdlib
76:+ * This program is free software; you can redistribute it and/or
166:+ * This program is free software; you can redistribute it and/or
234:+bool WasmEffect::loadModule(const QString& path, QString* error)
241:+			*error = QString::fromStdString(message);
274:+	// worker rendered last time around. Both calls are allocation-free,
275:+	// lock-free and syscall-free; the module itself runs on the worker thread.
295:+ * This program is free software; you can redistribute it and/or
318:+#include <QString>
338:+	QString nodeName() const override { return "WasmEffectControls"; }
358:+	bool loadModule(const QString& path, QString* error = nullptr);
361:+	QString lastError() const { return QString::fromStdString(m_worker.lastError()); }
395:+ * This program is free software; you can redistribute it and/or
680:+	m_impl(std::make_unique<Impl>())
686:+bool WasmSandbox::assembleWat(const std::string& wat, std::vector<std::uint8_t>& out,
788:+	std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
969:+ * This program is free software; you can redistribute it and/or
1021:+//! it; it communicates through WasmWorker's lock-free queues.
1028:+	WasmSandbox(const WasmSandbox&) = delete;
1029:+	WasmSandbox& operator=(const WasmSandbox&) = delete;
1033:+	static bool assembleWat(const std::string& wat, std::vector<std::uint8_t>& out, std::string& error);
1074:+	std::unique_ptr<Impl> m_impl;
1087:+ * WasmSpscRingBuffer.h - lock-free single-producer/single-consumer ring buffer
1093:+ * This program is free software; you can redistribute it and/or
1119:+//! Fixed-capacity lock-free SPSC queue.
1201:+ * This program is free software; you can redistribute it and/or
1266:+	// Drain queues and free every slot so a restart starts clean.
1281:+	m_sandbox = std::make_unique<WasmSandbox>();
1545:+ * This program is free software; you can redistribute it and/or
1583:+//! touch pre-allocated slot memory and lock-free SPSC queues - no allocation,
1608:+	WasmWorker(const WasmWorker&) = delete;
1609:+	WasmWorker& operator=(const WasmWorker&) = delete;
1623:+	//! Copy one interleaved stereo block into a free slot and queue it.
1624:+	//! Returns false (and counts a drop) when no slot or queue entry is free.
1673:+	std::unique_ptr<WasmSandbox> m_sandbox;
grep_exit=0 (1 = no hits)
```

Every hit is one of: a GPL licence header ("free software"), a comment containing the words
"lock-free"/"allocation-free", a deleted copy constructor, or a construction/load-path use of
`std::make_unique`/`std::vector`/`QString` (`WasmSandbox` constructor, `assembleWat`,
`loadModuleFile`, `WasmEffect::loadModule`). **None is in a function the audio thread runs.**

The same scan restricted to the exact bodies the audio thread executes
(`WasmEffect::processImpl`, `WasmWorker::submit`, `WasmWorker::collect`,
`WasmSpscRingBuffer::push/pop`) finds only the two comment words:

```
26:	// worker rendered last time around. Both calls are allocation-free,
27:	// lock-free and syscall-free; the module itself runs on the worker thread.
```

The bodies themselves, for the record:

```
== 5. bodies: the functions the audio thread actually calls ==
--- Effect::ProcessStatus WasmEffect::processImpl ---
92: Effect::ProcessStatus WasmEffect::processImpl(SampleFrame* buf, const f_cnt_t frames)
93: {
94: 	if (!m_worker.isReady())
95: 	{
96: 		// Not loaded yet, still loading, failed to load or quarantined after a
97: 		// trap: pass audio through untouched.
98: 		return ProcessStatus::Continue;
99: 	}
100: 	const auto frameCount = static_cast<std::uint32_t>(frames);
101: 	float sampleRate = 44100.0f;
102: 	if (Engine::audioEngine() != nullptr)
103: 	{
104: 		sampleRate = static_cast<float>(Engine::audioEngine()->outputSampleRate());
105: 	}
106: 	if (Engine::getSong() != nullptr && Engine::getSong()->isPlaying())
107: 	{
108: 		m_worker.setTransportState(abi::transportPlaying);
109: 	}
110: 	else
111: 	{
112: 		m_worker.setTransportState(abi::transportStopped);
113: 	}
114: 
115: 	// Submit the dry input first, then overwrite the buffer with the block the
116: 	// worker rendered last time around. Both calls are allocation-free,
117: 	// lock-free and syscall-free; the module itself runs on the worker thread.
118: 	m_worker.submit(buf, frameCount, sampleRate);
119: 	m_worker.collect(buf, frameCount);
120: 	return ProcessStatus::Continue;
121: }
--- bool WasmWorker::submit ---
350: bool WasmWorker::submit(const SampleFrame* interleaved, std::uint32_t frames,
351: 	float sampleRate)
352: {
353: 	if (frames == 0 || frames > maxBlockFrames ||
354: 		m_state.load(std::memory_order_acquire) != State::Ready)
355: 	{
356: 		m_dropped.fetch_add(1, std::memory_order_relaxed);
357: 		return false;
358: 	}
359: 	Slot* slot = nullptr;
360: 	for (Slot& candidate : m_slots)
361: 	{
362: 		if (candidate.state.load(std::memory_order_acquire) == SlotState::Free)
363: 		{
364: 			slot = &candidate;
365: 			break;
366: 		}
367: 	}
368: 	if (slot == nullptr)
369: 	{
370: 		m_dropped.fetch_add(1, std::memory_order_relaxed);
371: 		return false;
372: 	}
373: 	std::memcpy(slot->in.data(), interleaved, frames * sizeof(SampleFrame));
374: 	slot->frames = frames;
375: 	m_sampleRate = sampleRate;
376: 	slot->state.store(SlotState::Filled, std::memory_order_release);
377: 
378: 	const std::uint32_t index = static_cast<std::uint32_t>(slot - m_slots.data());
379: 	if (!m_commands.push(index))
380: 	{
381: 		slot->state.store(SlotState::Free, std::memory_order_release);
382: 		m_dropped.fetch_add(1, std::memory_order_relaxed);
383: 		return false;
384: 	}
385: 	m_submitted.fetch_add(1, std::memory_order_release);
386: 	return true;
387: }
--- bool WasmWorker::collect ---
389: bool WasmWorker::collect(SampleFrame* interleaved, std::uint32_t frames)
390: {
391: 	std::uint32_t index = 0;
392: 	if (!m_results.pop(index))
393: 	{
394: 		return false;
395: 	}
396: 	Slot& slot = m_slots[index];
397: 	const std::uint32_t count = std::min(frames, slot.frames);
398: 	// SampleFrame is trivially copyable (a plain std::array<float, 2>); the
399: 	// explicit void* cast documents the deliberate raw byte copy and keeps
400: 	// -Wclass-memaccess quiet about the user-provided default constructor.
401: 	std::memcpy(static_cast<void*>(interleaved), slot.out.data(),
402: 		count * sizeof(SampleFrame));
403: 	if (count < frames)
404: 	{
405: 		std::memset(static_cast<void*>(interleaved + count), 0,
406: 			(frames - count) * sizeof(SampleFrame));
407: 	}
408: 	slot.state.store(SlotState::Free, std::memory_order_release);
409: 	m_collected.fetch_add(1, std::memory_order_relaxed);
410: 	return true;
411: }
--- void WasmWorker::setParam ---
413: void WasmWorker::setParam(std::uint32_t index, float value)
414: {
415: 	if (index < m_params.size())
416: 	{
417: 		m_params[index].store(value, std::memory_order_relaxed);
418: 	}
419: }
--- 	bool push( ---
666: 	bool push(const T& value) noexcept
667: 	{
668: 		const std::size_t write = m_write.load(std::memory_order_relaxed);
669: 		const std::size_t next = (write + 1) % storageSize;
670: 		if (next == m_read.load(std::memory_order_acquire))
671: 		{
672: 			return false;
673: 		}
674: 		m_data[write] = value;
675: 		m_write.store(next, std::memory_order_release);
676: 		return true;
677: 	}
678: 
679: 	//! Consumer side. Returns false when the queue is empty.
680: 	bool pop(T& value) noexcept
681: 	{
682: 		const std::size_t read = m_read.load(std::memory_order_relaxed);
683: 		if (read == m_write.load(std::memory_order_acquire))
684: 		{
685: 			return false;
686: 		}
687: 		value = m_data[read];
688: 		m_read.store((read + 1) % storageSize, std::memory_order_release);
689: 		return true;
690: 	}
691: 
692: 	bool empty() const noexcept
693: 	{
694: 		return m_read.load(std::memory_order_acquire) == m_write.load(std::memory_order_acquire);
695: 	}
696: 
697: 	std::size_t size() const noexcept
698: 	{
699: 		const std::size_t write = m_write.load(std::memory_order_acquire);
700: 		const std::size_t read = m_read.load(std::memory_order_acquire);
701: 		return (write + storageSize - read) % storageSize;
702: 	}
703: 
704: 	static constexpr std::size_t capacity() noexcept { return Capacity - 1; }
705: 
706: private:
707: 	static constexpr std::size_t storageSize = Capacity + 1;
708: 
709: 	std::array<T, storageSize> m_data{};
710: 	std::atomic<std::size_t> m_read{0};
711: 	std::atomic<std::size_t> m_write{0};
712: };
--- 	bool pop( ---
680: 	bool pop(T& value) noexcept
681: 	{
682: 		const std::size_t read = m_read.load(std::memory_order_relaxed);
683: 		if (read == m_write.load(std::memory_order_acquire))
684: 		{
685: 			return false;
686: 		}
687: 		value = m_data[read];
688: 		m_read.store((read + 1) % storageSize, std::memory_order_release);
689: 		return true;
690: 	}
691: 
692: 	bool empty() const noexcept
693: 	{
694: 		return m_read.load(std::memory_order_acquire) == m_write.load(std::memory_order_acquire);
695: 	}
696: 
697: 	std::size_t size() const noexcept
698: 	{
699: 		const std::size_t write = m_write.load(std::memory_order_acquire);
700: 		const std::size_t read = m_read.load(std::memory_order_acquire);
701: 		return (write + storageSize - read) % storageSize;
702: 	}
703: 
704: 	static constexpr std::size_t capacity() noexcept { return Capacity - 1; }
705: 
706: private:
707: 	static constexpr std::size_t storageSize = Capacity + 1;
708: 
709: 	std::array<T, storageSize> m_data{};
710: 	std::atomic<std::size_t> m_read{0};
711: 	std::atomic<std::size_t> m_write{0};
712: };
```

`submit()` and `collect()` only `memcpy` into pre-allocated slots and push/pop indices on the
SPSC queue; `WasmSpscRingBuffer::push/pop` are `noexcept` array writes plus acquire/release
atomics. The suite proves this at runtime by overriding global `operator new` and counting
allocations around the calls:

```
QINFO  : lmms::wasm::WasmSandboxTest::rt_spscRingBufferIsAllocationFree() RT: 100000 SPSC push/pop pairs, allocations=0
PASS   : lmms::wasm::WasmSandboxTest::rt_spscRingBufferIsAllocationFree()
QINFO  : lmms::wasm::WasmSandboxTest::rt_audioThreadCallsDoNotAllocate() RT: submit()/collect() on the audio-thread path, allocations=0
PASS   : lmms::wasm::WasmSandboxTest::rt_audioThreadCallsDoNotAllocate()
```

No mutex, no condition variable, no lock guard, no `std::function` copy and no allocation
appears on the audio-thread path.

---

## 5. Plugin registration (task #591)

`WasmEffect` is a first-class LMMS plugin now: it builds as `libwasm_effect.so`,
is discovered by `PluginFactory` and instantiated through `Plugin::instantiate`
in a headless run, survives a project save/load round-trip, reports its latency
to the host, and the tree still configures without the wasmtime runtime.

Commits on `feat/wasm-sandbox` (local only, no push):

```
6fbf5fae420a889b1ab6f5e9e5b29192c36ce3c1 wasm: make WasmEffect a first-class plugin (green the WIP build)
82763ed8c836856ebedef4406575c11cbf93f877 wasm: drop the previous store before reinstantiating a module
```

### The WIP tree was red

A fresh `cmake --build build -j4` on the committed WIP failed. The first errors
were the plugin tests calling an API the plugin did not have yet:

```
tests/src/wasm/WasmSandboxTest.cpp:614:25: error: 'class lmms::WasmEffect' has no member named 'modulePath'; did you mean 'moduleLatency'?
tests/src/wasm/WasmSandboxTest.cpp:655:27: error: 'class lmms::WasmEffect' has no member named 'modulePath'; did you mean 'moduleLatency'?
tests/src/wasm/WasmSandboxTest.cpp:659:47: error: 'class lmms::EffectControls' has no member named 'paramModel'; did you mean 'parentModel'?
tests/src/wasm/WasmSandboxTest.cpp:660:43: error: 'class lmms::EffectControls' has no member named 'paramModel'; did you mean 'parentModel'?
```

The fix added a covariant `WasmEffect::controls()` returning `WasmEffectControls*`,
`WasmEffect::modulePath()`, the module path persisted on the controls, and marked
`WasmSandbox`/`WasmWorker` as `LMMS_EXPORT`. That last part matters: LMMS compiles
with `-fvisibility=hidden`, so a plugin DSO cannot resolve those symbols from the
host unless they are exported - the difference between "the test binary links the
sources" and "the shipped plugin loads".

### Build, with the module produced

Forced rebuild of every touched source (all timestamps bumped first), then:

```
BUILD_EXIT=0
[ 96%] Built target WasmSandboxTest
[100%] Linking CXX shared module ../libwasm_effect.so
[100%] Built target wasm_effect
```

```
-rwxrwxr-x 1 kruzzzzy kruzzzzy 98488 Sep  9 01:34 build/plugins/libwasm_effect.so
build/plugins/libwasm_effect.so: ELF 64-bit LSB shared object, x86-64, version 1 (SYSV), dynamically linked, BuildID[sha1]=ae886ce2f5c96847be92a94bd6241123bea2c29a, not stripped
```

### Discovery in a headless run

`p5_pluginFactoryDiscoversPlugin` loads the built `libwasm_effect.so` through the
production path - `PluginFactory::discoverPlugins()` then `Plugin::instantiate()`
- inside a `QTEST_GUILESS_MAIN` process (no GUI, no display):

```
QINFO  : lmms::wasm::WasmSandboxTest::p5_pluginFactoryDiscoversPlugin() P5 discovery: PluginFactory found "wasm_effect" in /tmp/WasmSandboxTest-DzCiVe/libwasm_effect.so (library loaded=1), descriptor "wasm_effect" type=Effect; Plugin::instantiate -> effect with 8 params
PASS   : lmms::wasm::WasmSandboxTest::p5_pluginFactoryDiscoversPlugin()
```

The test binary exports its own symbols (`ENABLE_EXPORTS ON`, mirroring the real
`lmms` binary in `src/CMakeLists.txt`), so the plugin DSO resolves the host API
exactly as it would in a shipped LMMS.

### Project save -> reload round-trip

`p2_effectSavesAndReloadsModuleAndParameters` writes the effect with
`Effect::saveSettings` (the same call `EffectChain` makes), then builds a fresh
instance and feeds that XML back through `loadSettings`:

```
QINFO  : lmms::wasm::WasmSandboxTest::p2_effectSavesAndReloadsModuleAndParameters() P2 save: project fragment written by saveSettings:
<effect on="1" name="wasm_effect" autoquit_denominator="4" autoquit_numerator="4" wet="1" autoquit_syncmode="0" autoquit="1">
 <wasmeffectcontrols module="/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-wasm/build/wasm-modules/gain.wasm">
  <param value="0.125" index="0"/>
  <param value="0.5" index="1"/>
  <param value="0.5" index="2"/>
  <param value="0.75" index="3"/>
  <param value="0.5" index="4"/>
  <param value="0.5" index="5"/>
  <param value="0.5" index="6"/>
  <param value="0.25" index="7"/>
  <journallingObject metadata="1" id="8394237"/>
 </wasmeffectcontrols>
</effect>
QINFO  : lmms::wasm::WasmSandboxTest::p2_effectSavesAndReloadsModuleAndParameters() P2 round trip: module=/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-wasm/build/wasm-modules/gain.wasm param0=0.125 param3=0.750 param7=0.250, rendered block2 out[0]=0.1250
QINFO  : lmms::wasm::WasmSandboxTest::p2_effectSavesAndReloadsModuleAndParameters() P2 backward compatibility: empty element -> module "", partial element -> param0=0.750 param1=0.500 (default)
PASS   : lmms::wasm::WasmSandboxTest::p2_effectSavesAndReloadsModuleAndParameters()
```

Module path and all eight parameter values survive the round-trip, and the
reloaded instance really renders with the restored parameter (block 2 is the
previous block scaled by 0.125). Projects that predate the plugin, and elements
without a `module` attribute, keep neutral defaults.

### Latency is applied

```
QINFO  : lmms::wasm::WasmSandboxTest::p3_moduleLatencyIsApplied() P3 latency: module declares 64 frames; impulse at input frame 0 appears at output frame 112 (expected 112 = latency + 1 block of 48)
QINFO  : lmms::wasm::WasmSandboxTest::p3_moduleLatencyIsApplied() P3 latency after trap: module impulse at 112 (expected 112), dry-path impulse at 352 (expected 352), corrupted=1 trapped=1
PASS   : lmms::wasm::WasmSandboxTest::p3_moduleLatencyIsApplied()
```

A sample-rate change reinstantiates the module exactly once and clears the ring
(p4); fixing this required dropping the previous wasmtime store, because the C
API has no per-instance destructor and the store limiter counts live instances:

```
QINFO  : lmms::wasm::WasmSandboxTest::p4_sampleRateChangeReinstantiatesModule() P4 sample rate: 44100 -> 44100 (0 reloads) -> 48000 (1 reload, ring cleared) -> 48000 (still 1 reload); processed=4
PASS   : lmms::wasm::WasmSandboxTest::p4_sampleRateChangeReinstantiatesModule()
```

### Full suite

```
$ cd build/tests && ctest --output-on-failure
1/9 Test #1: ArrayVectorTest ..................   Passed    0.02 sec
2/9 Test #2: AudioBufferTest ..................   Passed    0.02 sec
3/9 Test #3: AutomatableModelTest .............   Passed    1.23 sec
4/9 Test #4: MathTest .........................   Passed    0.02 sec
5/9 Test #5: ProjectVersionTest ...............   Passed    0.02 sec
6/9 Test #6: RelativePathsTest ................   Passed    0.02 sec
7/9 Test #7: TimelineTest .....................   Passed    1.23 sec
8/9 Test #8: AutomationTrackTest ..............   Passed    1.23 sec
9/9 Test #9: WasmSandboxTest ..................   Passed    1.29 sec

100% tests passed, 0 tests failed out of 9
CTEST_EXIT=0
```

`WasmSandboxTest` alone: `Totals: 21 passed, 0 failed, 0 skipped, 0 blacklisted`.
The audio-thread allocation counters are still zero.

### Configure without the runtime still exits 0

Runtime moved aside, fresh build directory, feature still requested:

```
$ mv third_party/wasmtime /tmp/wasmtime-aside-verify
$ cmake -S . -B /tmp/build-nowasm-verify -DWANT_QT6=ON -DWANT_WASM=ON
-- Could NOT find Wasmtime (missing: Wasmtime_LIBRARY Wasmtime_INCLUDE_DIR) 
* WASM DSP sandbox                  : Disabled (wasmtime C API not found; set WASMTIME_ROOT)
-- Configuring done (6.0s)
-- Generating done (0.4s)
-- Build files have been written to: /tmp/build-nowasm-verify
CONFIGURE_EXIT=0
```

---

## 6. Not verified

* **No real-time audio was played.** All gates run on synthetic `AudioBuffer`s and a
  standalone driver; no sound card output, no xrun/underrun measurement under load.
* **No stress/latency measurement** of the worker round-trip (how many blocks the module can
  be behind, worst-case worker wakeup latency).
* **No GUI verification.** The plugin was never launched in a visible LMMS session and its
  control dialog (`WasmEffectControlDialog`) has no widget-level test; discovery is proven
  through `PluginFactory` in a headless process, not by a user browsing the plugin list.
* **No end-to-end `lmms` host run.** The real `lmms` binary was not driven with a project
  file containing the effect; the round-trip is proven through the same
  `saveSettings`/`loadSettings` code path the host uses, in the test binary.
* **Only one sample-rate transition is covered** (44100 -> 48000, once) and the switch is
  abrupt: the ring is cleared, so in-flight audio is dropped rather than ramped.
* **No multiple-instance test** (several sandboxes at once) and no long-run stability test of
  the worker thread across many reloads.
* **No fuzzing of the runtime boundary.** The only malformed-input case tested is a garbage
  header, which is rejected at compile time.
* **No ASan/Valgrind/UBSan run.**
* **x86_64-linux only.** The pin is a prebuilt linux archive; macOS/Windows would need their
  own archive and hash, and the sandbox has not been built there.
* **The runtime is not bundled or installed.** `third_party/wasmtime` is gitignored and there
  is no install rule / RPATH for a deployed LMMS; a packager must fetch it.
* **Zig is not pinned in the repo.** G4 used `/tmp/zig-x86_64-linux-0.14.1/zig`
  (sha256 of the tarball `24aeeec8af16c381934a6cd7d95c807a8cb2cf7df9fa40d359aa884195c4716c`);
  the CMake rule skips the C demo when `zig` is not on `PATH`.
* **No upstream review.** Commits are local; nothing was pushed or proposed.

---

## 7. Reproduce

```sh
cd lmms-wasm
./scripts/fetch-wasmtime.sh                 # fetch + verify the pinned runtime
cmake -B build -DWANT_QT6=ON                # exits 0 with or without the runtime
cmake --build build -j4
cmake --build build --target wasm-modules   # assembles the .wat gate modules
cd build/tests && ctest --output-on-failure # 9/9
```

To re-run only the WASM gates:

```sh
cd build/tests && ./WasmSandboxTest
```

To regenerate the audio-thread audit:

```sh
git diff -U0 4e677cb6c..HEAD -- src/wasm | grep -nE "^\+.*(new|delete|malloc|make_unique|std::vector|std::mutex|lock_guard|\.lock\(|try_lock)"
```
