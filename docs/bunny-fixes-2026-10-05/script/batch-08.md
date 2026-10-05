# Bunny fixes — `script` lane — batch 08

Worktree `zene-bunny-script`, branch `bugs/bunny-script-2026-10-05`, base `b5d1cd4b6`.
Scope reviewed: WASM worker / sandbox / offline render.

## Blocker that shaped this whole batch

This build tree cannot compile the WASM lane at all. `WANT_WASM:BOOL=ON` but
`WASMTIME_ROOT` is empty and no wasmtime C API exists anywhere on the machine
(`cmake/modules/FindWasmtime.cmake` degrades `WANT_WASM` to `OFF` without it).
`cmake --build build --target WasmWorkerPoolTest` fails with
`gmake: *** No rule to make target 'WasmWorkerPoolTest'`, `ctest -N` in
`build/tests` lists 402 tests and **zero** wasm tests, and
`nm -C build/bin/zene` has no `WasmWorker::processSlot` symbol at all.

So for every finding below that is REAL, the rule-2 gate could not be met:
a test written against `tests/src/wasm/WasmWorkerPoolTest.cpp` cannot be
compiled, let alone run, in this tree. Per procedure step 2 that means **no
product code was changed** — all such findings are recorded as REAL-UNTESTED
with their evidence rather than fixed blind. `zene` and all 402 registered
tests were left untouched and green.

## Findings

### Cluster 5476 — Modules declaring more than two channels access beyond stereo slots

Verdict: FALSE-POSITIVE

Evidence: `src/wasm/WasmWorker.cpp:207-208` clamps the declared channel count
before any indexing exists:
`const int channels = std::clamp(m_sandbox->declaredChannels(), 1, static_cast<int>(abi::maxChannels));`
with `abi::maxChannels == 2` at `src/wasm/WasmAbi.h:62`. The only loops that
index the slot are `src/wasm/WasmWorker.cpp:236-244` (in) and
`:270-278` (out), both bounded by that clamped `channels`, and both indexed
`slot.in[frame * 2 + channel]` / `slot.out[frame * 2 + channel]` against the
stereo-sized `std::array<float, 2 * maxBlockFrames>` at
`src/wasm/WasmWorker.h:141-142`. A module exporting `channels = 3` is clamped
to 2 and its third channel is simply never processed — there is no reachable
out-of-bounds write. The same clamp exists on the offline-render path
(`src/wasm/WasmOfflineRender.cpp:101` uses `render.channels` fed from the
clamped worker value).

Commit: none

Test: none

Red/green: none (no code change; the clamp is present in the current tree).

### Cluster 4628 — Stopping a worker can race a lane into use-after-free

Verdict: REAL-UNTESTED

Evidence: `src/wasm/WasmWorkerPool.cpp:170` loads the raw pointer
(`m_workers[slot].load(...)`) *before* the claim at
`src/wasm/WasmWorkerPool.cpp:172` (`claimForLane()`). `WasmWorker::stop()`
waits only on `m_laneClaimed` (`src/wasm/WasmWorker.cpp:80-85`), so a lane that
loaded the pointer and has not yet CAS'd the claim is invisible to it, and
`removeWorker()` plus `m_sandbox.reset()` (`src/wasm/WasmWorker.cpp:99`) can run
under a reader. The window is real; the fix the report proposes (a pool-side
lifetime guard) changes the pool's publication protocol. Not attempted: the
defect is a race whose only honest proof is a stress harness, and the wasm
worker does not compile in this tree, so it is untestable headlessly here.

Commit: none

Test: none

Red/green: none — `cmake --build build --target WasmWorkerPoolTest > build/bunny-b.log 2>&1`
gave EXIT=2 (`No rule to make target`), so no test could be built or run.

### Cluster 2658 — Worker shutdown breaks the SPSC queue ownership

Verdict: REAL-UNTESTED

Evidence: `src/wasm/WasmWorker.cpp:92-97` drains `m_commands` and
`src/wasm/WasmWorker.cpp:97-100` drains `m_results` on the control thread
inside `stop()`. Those queues are SPSC (`src/wasm/WasmWorker.h:185-186`): the
audio thread is the producer of `m_commands` via `submit()`
(`:329`) and the consumer of `m_results` via `collect()` (`:346`), while the
lane is the other half. Draining from a third thread while audio is live
converts both into multi-consumer/multi-producer use. Confirmed as the report
states. The proposed fix ("quiesce the audio callback before draining") is a
lifecycle/ownership redesign touching `stop()`, `submit()` and `collect()` —
well past a minimal fix and touching the audio-thread contract, which rule 6
and rule 7 put out of scope. Recorded, not changed.

Commit: none

Test: none

Red/green: none — same unbuildable target as Cluster 4628.

### Cluster 5477 — Queued blocks can be processed at the wrong sample rate

Verdict: REAL-UNTESTED

Evidence: `submit()` writes one global
`m_sampleRate.store(sampleRate, ...)` at `src/wasm/WasmWorker.cpp:325`, and
the lane reads that single value per block at `src/wasm/WasmWorker.cpp:148`
(`const float sampleRate = m_sampleRate.load(...)`) before
`processSlot(slot, sampleRate)` at `:161`. `Slot`
(`src/wasm/WasmWorker.h:137-143`) has no rate field, so a block queued at
44.1 kHz that is drained after the engine moved to 48 kHz is processed — and
re-instantiated, via the `sampleRate != m_loadedSampleRate` branch at
`src/wasm/WasmWorker.cpp:149-153` — at the newer rate. REAL as reported. The
cheap fix (a `float rate` in `Slot`, written under the existing slot store) is
lock-free and allocation-free, so rule 6 does not block it, but it still needs
a test that submits at two rates and observes the module's rate argument; no
such test can run without a compiled wasm sandbox, so rule 2 forbids the
change here.

Commit: none

Test: none

Red/green: none — `cmake --build build --target WasmWorkerPoolTest > build/bunny-b.log 2>&1` gave EXIT=2.

### Cluster 3644 — WASM memory growth can invalidate cached host pointers

Verdict: REAL-UNTESTED

Evidence: `src/wasm/WasmWorker.cpp:213` caches `std::uint8_t* memory =
m_sandbox->memoryData();` and uses it to write the input planes at `:238-239`;
after `callProcess()` returns at `:249-252` the SAME cached pointer is
reused to read the output planes at `src/wasm/WasmWorker.cpp:272-273`, with no
re-fetch and no re-check of `memorySize()`. `memoryData()` is a thin
`wasmtime_memory_data*` wrapper (`src/wasm/WasmSandbox.cpp:505-516`) and
nothing in `src/wasm/` sets a memory limit or prevents `memory.grow`, so a
module that grows its linear memory inside `process()` can move the base
pointer and leave the host reading through a dangling one. The offline-render
path has the identical pattern: cache at `src/wasm/WasmOfflineRender.cpp:100`,
reuse after `callProcess()` at `:121-140`. The fix (re-acquire and re-verify
bounds after each call) is correct and cheap, but proving it RED needs a
memory-growing fixture module, and no wasm fixture or test can even be built in
this tree.

Commit: none

Test: none

Red/green: none — `cmake --build build --target WasmWorkerPoolTest > build/bunny-b.log 2>&1` gave EXIT=2.

### Cluster 2663 — Insufficient WASM memory bypasses declared latency compensation

Verdict: REAL-UNTESTED

Evidence: the insufficient-memory branch at `src/wasm/WasmWorker.cpp:216-221`
does `passthrough(slot)`, `m_dropped.fetch_add(1, ...)` and `return` — it
leaves `m_state` at `State::Ready`. `plugins/WasmEffect/WasmEffect.cpp:202`
takes the latency-compensated dry path only when `!m_worker.isReady()`, so a
module that declares latency but exports too little memory is treated by the
effect as healthy: its output is dry passthrough with one block of delay while
the effect's reported latency is `declaredLatency + one engine period`
(`plugins/WasmEffect/WasmEffect.cpp:186`), i.e. the chain under-compensates by
exactly the declared latency. REAL as reported, and the reachable case is
concrete: `modules/wasm/gain.wat:16` declares `(memory 1)` = 65536 B and
`channels = 2`, while a full `maxBlockFrames` (8192) stereo block needs
`2 * 2 * 8192 * 4 = 131072` B, so the branch is hit by the default block size.
A deterministic test was drafted (submit a full 8192-frame block to
`gain.wasm` and assert the worker stops reporting `Ready`), but it lives in
`tests/src/wasm/WasmWorkerPoolTest.cpp`, which does not compile in this tree,
so no RED could be observed. Not changed.

Commit: none

Test: none (drafted case `WasmWorkerPoolTest::testAModuleTooSmallForTheBlockStopsReportingReady`
was reverted rather than committed unverifiable)

Red/green: none — `cmake --build build --target WasmWorkerPoolTest > build/bunny-b.log 2>&1` gave EXIT=2.

## Summary

| Cluster | Verdict | Commit |
|---|---|---|
| 5476 | FALSE-POSITIVE | none |
| 4628 | REAL-UNTESTED | none |
| 2658 | REAL-UNTESTED | none |
| 5477 | REAL-UNTESTED | none |
| 3644 | REAL-UNTESTED | none |
| 2663 | REAL-UNTESTED | none |

1 FALSE-POSITIVE, 5 REAL-UNTESTED, 0 FIXED. No product code was modified.

Leftovers: all five REAL findings (4628, 2658, 5477, 3644, 2663) still need a
wasmtime-enabled build tree (`scripts/fetch-wasmtime.sh`, then re-configure
once) before their regression tests can be written and run; 5477, 3644 and
2663 have minimal fixes already identified and should be taken first in that
tree. 2658 additionally needs a scope decision on an audio-thread quiesce
protocol, and 4628 needs a pool-side lifetime-guard design. The drafted
regression case for 2663 (submit a full `maxBlockFrames` block to `gain.wasm`,
assert the worker no longer reports `Ready`) is described above and can be
re-added to `tests/src/wasm/WasmWorkerPoolTest.cpp` unchanged when the target
builds.
