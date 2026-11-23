FIXED

# Cluster new-p8-249 - a worker backlog made the effect's actual latency vary

The finding is REAL and is fixed on this branch.

## The bug, in the code of this tree

`WasmEffect::processImpl()` (before the fix) popped the oldest completed block
every call and submitted a new one unconditionally, while `latencyFrames()`
reports a FIXED one-block pipeline delay on top of the module's declared latency
(`plugins/WasmEffect/WasmEffect.cpp:198`, "the audio thread submits block N and
collects block N-1"; the report was corrected to the engine's period by
0.5.0/#605 audit C1b and is deliberately constant):

* pre-fix `plugins/WasmEffect/WasmEffect.cpp:270` - `const bool haveOutput =
  m_worker.collect(buf, frameCount)` pops the OLDEST result in the worker's
  result queue, without asking which call it belongs to;
* pre-fix `plugins/WasmEffect/WasmEffect.cpp:281` - `m_worker.submit(...)` ran
  every call regardless, so a module slower than one period queued a backlog
  (up to 4 slots / 8 queue entries, `src/wasm/WasmWorker.h:61-63`);
* the dry fallback (`pullDelayed`) holds the position the effect owes its
  listener, so the sequence the finding describes is exactly what happened:
  dry audio first, then the stale block's result inserted several calls late -
  audio the dry path had already delivered, replayed, with the wet path's lag
  grown past the one block the effect reports.

`WasmWorker::collect()` itself (`src/wasm/WasmWorker.cpp:473`) is a dumb
SPSC pop and cannot know lateness; the call cadence is known only to the
effect, so the policy belongs there.

## The fix

At most the expected block is in flight, and a result that missed its call is
dropped, never inserted at a later one - the dry signal holds the position
instead, so the effect's actual latency stays the fixed figure
`latencyFrames()` reports:

* `plugins/WasmEffect/WasmEffect.cpp:291` - a new private
  `advancePipeline(buf, frameCount, moduleLatency, sampleRate)` is the whole
  worker handoff of one call:
  - the block submitted in the PREVIOUS call is due now and is delivered
    (`collect(buf, ...)`, `m_pipelineAge <= 1`);
  - one older than that is late - popped into `m_discardScratch` and dropped,
    never into `buf`;
  - with nothing in flight, any queued result is a leftover of an interrupted
    episode (a quarantine, a re-instantiation) and is dropped too, so the
    pipeline starts clean;
  - the dry fallback (`pullDelayed`) still runs whenever no result was
    delivered and the module declared latency;
  - `submit()` runs only when the previous block's result was taken above
    (`plugins/WasmEffect/WasmEffect.cpp:337`), keeping at most one block in
    flight - the backlog the finding describes can no longer form;
* `plugins/WasmEffect/WasmEffect.cpp:227-241` - pipeline bookkeeping at the top
  of `processImpl()`: the epoch check and the aging. The block in flight is one
  call older on EVERY call, including the `!m_worker.isReady()` ones, so a
  re-instantiation episode (whose lane-passthrough result lands late) is
  handled by the same late-drop rule;
* `plugins/WasmEffect/WasmEffect.cpp:233` / `src/wasm/WasmWorker.h:113` - the
  worker exposes a `queueEpoch()`, bumped by `WasmWorker::stop()`
  (`src/wasm/WasmWorker.cpp:141`) after it drains both queues and frees every
  slot. `stopWorker()` runs under the engine's model-change guard, but a
  stop+start can still resolve entirely between two audio periods (the compile
  runs with the guard released, `plugins/WasmEffect/WasmEffect.cpp:156-166`), so
  the epoch - not a hoped-for `Loading` observation - is what tells the audio
  thread that a block it still counts as in flight is gone. Without it the
  pipeline would wait forever for a result that was drained, and the wet path
  would stall permanently after a fast reload;
* `plugins/WasmEffect/WasmEffect.h:148-172` - the audio-thread-only members
  (`m_pipelineEpoch`, `m_pipelineInFlight`, `m_pipelineAge`) and
  `m_discardScratch`, allocated in the constructor like `m_dryScratch`
  (`plugins/WasmEffect/WasmEffect.cpp:69`).

`WasmWorker`'s `submit()`/`collect()` signatures are unchanged; the offline
render's lockstep loop (`src/wasm/WasmOfflineRender.cpp:177` and `:191`,
"the block is collected before the next is submitted") and the pool tests
drive the worker exactly as before.

The two functions stay under the complexity target by construction:
`processImpl` counts 8 decision tokens (CCN 9) and `advancePipeline` 6
(CCN 7), target <= 10 (counted with the gate's own rules - `if`/`&&`/`||`
each +1; `lizard`, the gate's tool, is absent on this box, see Build note).

## The regression test

`WasmSandboxTest::p9_lateWorkerResultIsDroppedNotDisplaced`
(`tests/src/wasm/WasmSandboxTest.cpp:1514`, declared at `:357`).

It loads the `gain.wasm` fixture (out = param(0) * in, declares no latency, so
the dry fallback is plain passthrough and a wet result is exactly half its
block's marker), feeds each call a distinct marker (odd multiples of 0.25, so
a halved marker can never be mistaken for another call's dry marker), and
drives `processAudioBuffer()` by hand:

* call 0 submits block 0; the lane is held INSIDE that block by the new
  per-block test seam, so block 0 deterministically misses the call it was due
  in - a module slower than one audio period, without racing anything;
* calls 1 and 2 fall due with nothing ready: the dry signal holds each call's
  own position (0.750, 1.250);
* the held block finishes only then; call 3 must NOT play it - it is dropped,
  and call 3's own position (1.750) comes out dry;
* calls 4 and 5 prove the pipeline resynchronised: block 3's result on time
  (0.875 = 0.5 * 1.75), then block 4's (1.125);
* `droppedBlocks()` stays 0 (the fix orders, it does not refuse) and the
  worker stays Ready and un-quarantined.

RED (product fix not yet applied, seam and test only):
`Actual 0.125` - the stale block-0 result - `Expected 1.75` - the dry
position. GREEN: all six calls hold the contract; the measured line reads
"calls 1-3 held their positions dry (0.750, 1.250, 1.750); the late block was
dropped, not played at call 3; call 4 delivered block 3 on time (0.875) and
call 5 block 4 (1.125); submitted=4 collected=3 dropped=0"
(`test-13-full-binary-final.log`).

Deterministic: the seam is `WasmWorker::setProcessBlockTestHook` ->
`notifyProcessBlockForTest()` fired on the pool lane right before a queued
block runs (`src/wasm/WasmWorker.cpp:273`, declared `src/wasm/WasmWorker.h:139`),
the same pattern as the worker's existing module-load and audio-endpoint
seams, compiled only under `LMMS_TESTING` and inert without a test. The test's
`BlockProcessGate` (`tests/src/wasm/WasmSandboxTest.cpp:243`) stops the lane on
a condition variable the test releases explicitly; `waitUntilHeld(10000)` is
only the safety net that turns a seam that never fires into a failed
assertion; `waitForIdle()` waits on the worker's own counters. No sleep, no
wall-clock value the outcome depends on; every asserted value (markers, gain
0.5, the fixture's 0 latency) is injected by the test or fixed by the
build-time fixture.

## Evidence

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-249.r1/logs`.

| Check | Command | Exit | Log |
| --- | --- | --- | --- |
| Guard refusal (recorded) | `cmake -S . -B build-wasm -DWASMTIME_ROOT=...` | 97 | `configure-1.log` |
| WASM bootstrap configure | `/usr/bin/cmake -S . -B build -DWANT_WASM=ON` | 0 | `configure-2.log` |
| Baseline build | `cmake --build build --target WasmSandboxTest WasmWorkerPoolTest` | 0 | `build-1-baseline.log` |
| RED build (seam + test, no fix) | `cmake --build build --target WasmSandboxTest WasmWorkerPoolTest` | 0 | `build-2-red.log` |
| RED test | `ctest -R '^WasmSandboxTest$' --output-on-failure` (from `build/tests`) | **8** | `test-1-red.log` |
| GREEN build | `cmake --build build --target WasmSandboxTest WasmWorkerPoolTest zene wasm_effect` | 0 | `build-3-green.log` |
| GREEN test | `ctest -R '^WasmSandboxTest$' --output-on-failure` | **0** | `test-2-green.log` |
| 4b - product diff saved | `git diff -- plugins/WasmEffect/WasmEffect.cpp plugins/WasmEffect/WasmEffect.h > .../logs/fix.patch` | 0 | `fix.patch` |
| 4b - reversed | `git apply -R .../logs/fix.patch` | 0 | `apply-reverse.log` |
| 4b - rebuild reverted | `cmake --build build --target WasmSandboxTest zene wasm_effect` | 0 | `build-8-reverted.log` |
| 4b - test FAILS without the fix | `ctest -R '^WasmSandboxTest$' --output-on-failure` | **8** | `test-10-reverted.log` |
| 4b - restored | `git apply .../logs/fix.patch` | 0 | `apply-forward.log` |
| 4b - rebuild fixed | `cmake --build build --target WasmSandboxTest zene wasm_effect` | 0 | `build-9-reapplied.log` |
| 4b - test PASSES with the fix | `ctest -R '^WasmSandboxTest$' --output-on-failure` | **0** | `test-11-reapplied.log` |
| Pre-existing failure, fix reverted | `ctest -R '^ControlWasmDevice$' --output-on-failure` | 8 | `test-6-reverted-controlwasmdevice.log` |
| Affected family (whole binary) | `./WasmSandboxTest` (from `build/tests`) | 0 | `test-13-full-binary-final.log` |
| Wasm family | `ctest -R 'Wasm' --output-on-failure` | 8 | `test-12-wasm-family-final.log` |
| Namespace gate on the touched file | `tests/scripted/check-namespace src/wasm/WasmWorker.cpp` | 0 | - |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `gate-feature-check-2.log` |
| Final rebuild (post #endif comment) | `cmake --build build --target WasmSandboxTest WasmWorkerPoolTest zene wasm_effect` | 0 | `build-10-final.log` |
| Final test | `ctest -R '^WasmSandboxTest$' --output-on-failure` | 0 | `test-14-final.log` |

* Whole binary: **28 passed, 0 failed, 1 skipped** - the skip is
  `g4_compiledCDemoModule`, which needs the optional `zig` module (same skip as
  the accepted cluster new-p8-250-port and 5444 reports).
* Wasm family: 6 of 7 pass. `ControlWasmDevice` fails PRE-EXISTING, identically
  with the fix applied (`test-3-wasm-family.log`, `test-4-wasm-family.log`,
  `test-12-wasm-family-final.log`) and with it reversed
  (`test-6-reverted-controlwasmdevice.log`): the same two checks the accepted
  new-p8-250-port report recorded ("a non-wasm device to refuse exists in this
  build (no other built-in effect)" and the `control.undo` check) - this build
  tree carries no second built-in effect plugin. This change touches no plugin
  registration, no command and no undo step.
* An earlier 4b cycle on the pre-refactor shape produced the same verdicts
  (`build-5-reverted.log` EXIT=0 build / `test-5-reverted.log` EXIT=8,
  `build-6-reapplied.log` EXIT=0 / `test-7-reapplied.log` EXIT=0); it was
  repeated on the final shape above after `advancePipeline` was extracted.

## Build note (the sandbox has to be built for this test to exist)

`WasmSandboxTest` and the wasm plugin targets only exist when the tree is
configured with the WASM sandbox enabled; this lane's build tree came
configured `WANT_WASM=OFF`. The lane guard's `cmake` shim refuses every
configure ("configuring is the parent's job", exit 97, `configure-1.log`), and
the parent's own replay deliberately skips the flag
(`qwenfix.py:1100`, `if m and m.group(1) != "WANT_WASM":  # WASM needs a
per-tree Wasmtime bootstrap`) - so, exactly as the accepted
cluster new-p8-250-port report did before this one:

* the pinned prebuilt wasmtime C API (v48.0.1, the archive
  `scripts/fetch-wasmtime.sh` installs, taken byte-identical from the sibling
  `zene-cachyos` checkout) was copied into `<worktree>/third_party/wasmtime`
  (`/third_party/` is gitignored, nothing of it is committed);
* the LANE build tree was reconfigured once with the real binary,
  `/usr/bin/cmake -S . -B build -DWANT_WASM=ON` (EXIT=0,
  "WASM DSP sandbox : Enabled", `configure-2.log`). No other tree and no other
  target was touched; every later build went through the guard's allowed
  `cmake --build build --target <t>` form.

The complexity gate's `lizard` is not installed on this box
(`tests/complexity-gate.sh --check` exits 2 with "lizard is not installed",
`gate-complexity.log`), so that row is locally vacuous; the two touched
functions were kept under the CCN<=10 target by an explicit decision-token
count (processImpl 9, advancePipeline 7) using the gate's own counting rules.

## Files committed

* `plugins/WasmEffect/WasmEffect.cpp` - the fix (epoch check, aging, the
  `advancePipeline` handoff) and the `m_discardScratch` constructor entry.
* `plugins/WasmEffect/WasmEffect.h` - the fix's declarations and members.
* `src/wasm/WasmWorker.cpp` - `queueEpoch` bumped by `stop()`, and the
  `LMMS_TESTING` per-block seam the regression test holds the lane with
  (inert without a test); the long `#endif // LMMS_TESTING` comment its growth
  past the checker's 30-line exemption now requires.
* `src/wasm/WasmWorker.h` - `queueEpoch()` and the seam's declaration.
* `tests/src/wasm/WasmSandboxTest.cpp` - the regression test
  `p9_lateWorkerResultIsDroppedNotDisplaced` and its `BlockProcessGate`.
* `docs/qwen-fixes-2026-10/cluster-new-p8-249.md` - this report.

No new file: every product file above is already registered in
`tests/fork-sources.txt` (`plugins/WasmEffect/WasmEffect.cpp/.h` at lines
1618-1619, `src/wasm/WasmWorker.cpp/.h` at 2524-2525). No product file is
upstream-inherited, so `tests/upstream-modifications.txt` is untouched.

## Self-check

* **The regression test FAILS with the product change reverted and PASSES
  with it.** RED (fix not yet applied): `ctest -R '^WasmSandboxTest$'` from
  `build/tests` -> EXIT=8, `Actual 0.125` (the stale block-0 result) vs
  `Expected 1.75` (`test-1-red.log`). 4b on the final shape: `git apply -R
  fix.patch` -> rebuild EXIT=0 -> the same test EXIT=8 with the same assertion
  (`test-10-reverted.log`); `git apply fix.patch` -> rebuild EXIT=0 -> EXIT=0
  (`test-11-reapplied.log`). GREEN: EXIT=0 (`test-2-green.log`,
  `test-14-final.log`).
* **The test is deterministic.** The lateness is injected, not timed: the
  per-block seam `WasmWorker::setProcessBlockTestHook` ->
  `notifyProcessBlockForTest()` (`src/wasm/WasmWorker.cpp:273`) stops the pool
  lane inside block 0 on a condition variable (`BlockProcessGate`,
  `tests/src/wasm/WasmSandboxTest.cpp:243`) that the test releases explicitly
  after calls 1 and 2 have collected; `waitUntilHeld(10000)` is only the
  safety net, and `waitForIdle()` waits on the worker's own counters. No
  sleep, no wall-clock assertion, no raced state. All asserted values are
  injected: the markers (odd multiples of 0.25), the gain (0.5), the fixture
  (`gain.wasm`, built from `modules/wasm/gain.wat`, declares 0 latency), the
  48-frame blocks.
* **The fix is at the cause and complete.** The cause is the effect's
  hand-off policy (pop the oldest, submit unconditionally), and both halves
  are now conditional in one place, `advancePipeline`. Checked callers and
  siblings: `WasmOfflineRender.cpp:177/:191` drives the same worker in
  lockstep (collect before the next submit) and needed no change;
  `WasmWorkerPoolTest` drives `submit()`/`collect()` directly with unchanged
  signatures; the `!m_worker.isReady()` path keeps its dry compensation and
  now also ages the in-flight block, so a re-instantiation episode's late
  passthrough result is dropped rather than displaced (the reload case is the
  epoch); a trap (Corrupted) never resumes the wet path, and the only resume
  is a reload, which drains and bumps the epoch; the
  `frameCount > maxBlockFrames` early-out (unreachable in production - the
  engine caps the period at 256, `include/AudioEngine.h:71-73` and the
  FALSE-POSITIVE verdict for new-p14-376 in `cluster-5444.md` - now ages the
  in-flight block instead of losing a call; the first call after a load is
  dry and submits, exactly as before, so every lockstep test (g2, p3, p6, p7,
  p8) passes unchanged; `submit()`'s sample-rate-change notice is preserved
  verbatim. Realtime: the new audio-thread work is one acquire atomic load
  (`queueEpoch`), integer arithmetic and memcpys into buffers allocated in the
  constructor - no allocation, no free, no lock, no wait, no logging; the
  existing `rt_audioThreadCallsDoNotAllocate` allocation counter passes
  (`test-13-full-binary-final.log`). Undo and commands are untouched.
* **No collateral damage.** Nothing persisted changes: the module path and
  parameter save/load are untouched, the pipeline bookkeeping is
  audio-thread runtime state, no project format or default moves; old projects
  load exactly as before (p2's save/reload test passes). No limit was added
  that a legitimate caller could exceed: the worker's own `submit()`/`collect()`
  contracts are unchanged - the one-block discipline is the effect's policy
  over its own worker, and the offline render (the other `submit()` caller)
  already ran lockstep. A module slower than one period now degrades to the
  aligned dry signal (its results arrive late and are dropped) rather than to
  variable latency - which is the finding's own suggested handling ("handle
  late output without inserting it at the wrong time").
* **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code that shows
  it.** Not applicable - the verdict is FIXED; the pre-fix code is quoted
  above (pre-fix `WasmEffect.cpp:270` and `:281`, visible in `fix.patch` and
  in the 4b reverted runs).
* **Sibling commands.** No command file, registration, schema or snapshot was
  touched by this change, so the `app.*` / `arrangement.*` set is unaffected:
  the diff is `plugins/WasmEffect/*`, `src/wasm/WasmWorker*` and one test
  source, none of which a command handler reads.

## Other findings

* Pre-existing, not fixed here: `ControlWasmDevice` fails in this build tree
  because no second built-in effect plugin is built (same two checks with the
  fix applied and reversed; also recorded by the new-p8-250-port report).
* Pre-existing design tension, not touched: a module that declares ZERO
  latency still reports `latencyFrames() == 0` while its delivered wet result
  carries the one-block pipeline delay (`WasmEffect::latencyFrames()` returns
  early at `plugins/WasmEffect/WasmEffect.cpp:201`); the dry passthrough for
  such modules is 0-delay, so wet and dry differ by one block. With this fix
  the timing no longer varies, but that one-block difference between the wet
  and dry paths of a zero-latency module remains as designed.
* Environment: `lizard` (the complexity gate's tool) is not installed on
  this box, so `tests/complexity-gate.sh --check` cannot measure locally
  (exit 2, `gate-complexity.log`).
