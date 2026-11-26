FIXED

## The defect

`WasmEffect::latencyFrames()` returned 0 before it added the host pipeline period whenever the
module declared no latency:

* `plugins/WasmEffect/WasmEffect.cpp:199-217` (before this fix) — `const int moduleFrames =
  compensatedModuleLatency(); if (moduleFrames == 0) { return 0; }` at `:202-205`, immediately
  above the function's own statement of the invariant at `:206-207`: "The audio thread submits
  block N and collects block N-1, so the host adds exactly one block of pipeline delay on top of
  whatever the module reports."
* The delay is real: `processImpl()` takes the pipeline path for every ready worker
  (`plugins/WasmEffect/WasmEffect.cpp:248`), and `advancePipeline()` collects the result submitted in
  the previous call (`:305`) before submitting the current block (`:337`). A ready module that
  declares latency 0 therefore delivers block N in call N+1 — exactly one engine period.
* The same contract is stated in `plugins/WasmEffect/WasmEffect.h:87-95` and
  `plugins/WasmEffect/WasmEffect.cpp:235` ("a block submitted in call N is due in call N+1, which is
  the fixed one-block pipeline delay latencyFrames() reports").
* PDC consumes this figure: `include/Effect.h:310-313` (`compensatedLatencyFrames()` returns the
  user override or `latencyFrames()`) and `src/core/EffectChain.cpp:115-145` sums it (`:125`). A
  zero-latency module made the chain report 0 for a path that is one period late.
* `git blame -L 202,205` attributes the early return to `8cdd74a557` (2026-09-09), never modified
  since: the finding was not already fixed.
* No test covered it: the report was asserted only for a module declaring 64
  (`tests/src/wasm/WasmSandboxTest.cpp:1065-1072`, `:1262-1283`), for no module (`:903`, `:1255`)
  and after an unload (`:1484`). `p7` pinned the bug for a zero-latency replacement at
  `:1370-1374` (`effect->latencyFrames() == 0`).

## The fix

```diff
 int WasmEffect::latencyFrames() const
 {
 	const int moduleFrames = compensatedModuleLatency();
-	if (moduleFrames == 0)
+	// A ready module always goes through the collect-after-submit pipeline
+	// below, so the host adds its period even when the module declares no
+	// latency of its own. With no module - or while one loads and none is
+	// being replaced - the dry signal passes through undelayed and the effect
+	// adds nothing.
+	const bool pipelineActive = m_worker.isReady() || moduleFrames > 0;
+	if (!pipelineActive)
 	{
 		return 0;
 	}
```

`plugins/WasmEffect/WasmEffect.h:87-95` gains the matching sentence in the accessor's contract
("The pipeline period is counted whenever a module is ready, including one that declares no
latency; with nothing loaded the signal passes through undelayed and this is 0"). `m_worker.isReady()`
is `state() == State::Ready` (`src/wasm/WasmWorker.h:85-86`), one atomic acquire load, so the
control-thread report stays allocation- and lock-free.

`moduleFrames > 0` stays in the condition: when the worker is not ready but a declared latency is
still compensated (a quarantine, or a reload whose `m_preservedLatency` is in force),
`processImpl()` delays the dry signal by `moduleFrames + frames` (`:254-260`, `:344-351`), so the
period belongs in the report there too.

The one existing assertion that pinned the bug is corrected, not weakened — `p7` now expects the
period for the zero-latency replacement it loads:

```diff
-	// The replacement resolved: its own latency (none) is in force now, and
-	// the chain's cache follows it.
+	// The replacement resolved: it declares no latency of its own, but a ready
+	// module still goes through the host pipeline, so the figure is the period -
+	// and the chain's cache follows it.
 	QCOMPARE(effect->worker().declaredLatency(), 0);
-	QCOMPARE(effect->latencyFrames(), 0);
-	QCOMPARE(chain.latencyFrames(), 0);
+	QCOMPARE(effect->latencyFrames(), periodFrames);
+	QCOMPARE(chain.latencyFrames(), periodFrames);
```

## Regression test

New slot `p10_zeroLatencyModuleReportsTheHostPipelinePeriod` in
`tests/src/wasm/WasmSandboxTest.cpp` (declared at `:360`, defined at `:1615`). It loads
`modules/wasm/gain.wat` (declares `latency 0`, `out = param(0) * in`), sets the gain to 0.5, and
asserts:

1. nothing loaded: effect and chain report 0 (the fix may not over-report an empty effect);
2. ready module declaring 0: `effect->latencyFrames() == periodFrames` (256 here) and
   `chain.latencyFrames() == periodFrames` — the assertion that fails without the fix;
3. the audio agrees with the report: one distinct marker per block, `waitForIdle()` after each call
   (the lockstep seam `p3` already uses at `:1050`); call 0 carries its own dry block, every later
   call carries `0.5f * marker(block - 1)`, i.e. a one-call = one-period pipeline;
4. steady state does not move the figure.

No wall-clock timing, no sleeps, no polling: block markers plus the worker's condition-variable
`waitForIdle()` seam; the only timeouts are hang safety nets.

## How the test was built and run here (and why not through ctest)

This lane's build tree is configured `WANT_WASM:BOOL=OFF` (the parent's `prepare.log` line 1,
`FIX-CMAKE-ARGS:48` "WASM off in fix trees", `qwenfix.py:1139`), `tests/CMakeLists.txt:5460-5475`
registers `WasmSandboxTest` only `if(WANT_WASM)`, and the lane guard refuses the configure that
would turn it on (`configure-1.log`, exit 97). Rather than report an untested fix again, the
registered test binary was compiled and linked by hand from the same sources, flags and objects the
CMake target uses (`tests/CMakeLists.txt:5466-5503`):

* sources: `tests/src/wasm/WasmSandboxTest.cpp`, `plugins/WasmEffect/WasmEffect.cpp`,
  `WasmEffectControls.cpp`, `WasmEffectControlDialog.cpp`, `src/wasm/{WasmWorker,WasmWorkerPool,
  WasmSandbox,WasmSandboxHost,WasmSandboxModules,WasmOfflineRender}.cpp`,
  `src/core/ControlWasmSupport.cpp`; moc output for the three plugin headers and the test's
  `Q_OBJECT`;
* defines and includes copied from an existing test target's `flags.make`
  (`build/tests/CMakeFiles/AudioPluginTest.dir/flags.make`) with `PLUGIN_NAME=wasm_effect`,
  `WASM_MODULES_DIR=<the built modules>`, `WASM_WAT_SOURCE_DIR=<worktree>/modules/wasm`,
  `LMMS_TESTING`, `PLUGIN_STATIC_DEFINE`;
* link: the `lmmsobjs` and third-party objects plus the libraries an existing test links
  (`objects1.rsp` / `link.txt` of `AudioPluginTest`), adding `libwasmtime.so` from
  `third_party/wasmtime` (gitignored; wasmtime 48.0.1, the pinned version);
* the script is `logs/handbuild/build.py` (beside this run's logs).

The test's own `initTestCase()` passed (`Engine::init`, LV2 scan, `hello.wasm` present), so the
binary is the real test class, not a copy. On a build with `WANT_WASM=ON` the same test file is
compiled by the registered `WasmSandboxTest` target and run by `ctest -R WasmSandboxTest`; that
target does not exist in this lane, so `ctest` was not used (see `## Other findings`).

## Evidence: red and green

| step | command | exit | log |
| --- | --- | --- | --- |
| RED before any fix | `QT_QPA_PLATFORM=offscreen ./WasmSandboxTest p10_...` | 1 | `test-red-1.log` |
| diff the product files | `git diff -- plugins/WasmEffect/WasmEffect.cpp plugins/WasmEffect/WasmEffect.h > logs/fix.patch` | 0 | `fix.patch` (35 lines) |
| revert the fix | `git apply -R logs/fix.patch` | 0 | — |
| rebuild harness | `python3 logs/handbuild/build.py` | 0 | `handbuild/build-red.out` |
| RED with the fix reverted | `QT_QPA_PLATFORM=offscreen ./WasmSandboxTest p10_...` | 1 | `test-red-2.log` |
| reapply the fix | `git apply logs/fix.patch` | 0 | — |
| rebuild harness | `python3 logs/handbuild/build.py` | 0 | `handbuild/build-green.out` |
| GREEN with the fix | `QT_QPA_PLATFORM=offscreen ./WasmSandboxTest p10_...` | 0 | `test-green-2.log`, `test-green-3.log` |
| family, minus p5 | `./WasmSandboxTest <27 other slots>` | 0 | `test-family-1.log`, `test-family-2.log` (28 passed, 0 failed, 1 skipped) |
| configured tree | `cmake --build build --target zene` | 0 | `build-zene-1.log` |
| gates | `python3 .../feature_check.py <worktree>` | 0 | `gate-1.log` |

Red run: `P10 zero latency: module declares 0, host period 256 -> effect reports 0, chain 0` then
`FAIL! ... Actual (reported): 0  Expected (periodFrames): 256`. Green run: same line with
`effect reports 256, chain 256`, `Totals: 3 passed, 0 failed`. `p7` passes with the corrected
expectation (`P7 reload: ... after it resolved: effect=256 chain=256`).

`p5_pluginFactoryDiscoversPlugin` is excluded: it loads `libwasm_effect.so` from the plugin target,
which cannot exist in this tree. Every other slot in the file ran.

## Test matrix

| path | expectation | result |
| --- | --- | --- |
| normal, zero-latency ready module | report and chain cache = period; audio one call late | new `p10`, passes |
| normal, module declaring 64 | report and cache = 64 + period, unchanged | `p3`, `p6`, `p7` pass |
| no module / unloaded | report 0, dry passthrough, unchanged | `p1`, `p6`, `p8_emptyPathUnload...` pass |
| failure (refused load) | previous figure kept, status text says why | `p8_failedModuleLoad...` passes |
| exhaustion (worker pool full) | all-or-nothing load, running module and compensation kept | `loadModule` path; `p7` reload passes |
| reconfiguration (reload, sample-rate change) | compensation follows the new module, no gap while it loads | `p7`, `p4` pass |
| audio-thread constraints | no change to `processImpl`/`advancePipeline`; the report is control-thread and reads one atomic | diff reviewed; no allocation, lock, wait or log added |
| platforms | no new platform API; header declaration unchanged (comment only); the plugin and test targets that include the header rebuild | `WasmEffect.cpp` recompiled in the harness; configured `zene` target unaffected (`build-zene-1.log`) |

## Callers and siblings checked

* `EffectChain::refreshLatency()` (`src/core/EffectChain.cpp:115-145`) sums
  `Effect::compensatedLatencyFrames()`; `loadModule()` refreshes it on every exit path, so the
  chain cache follows the corrected figure (`p6`, `p7`).
* The user latency override (`include/Effect.h:310-313`) still replaces the figure wholesale; the
  override path is untouched.
* `WasmEffect::moduleLatency()` (`WasmEffect.h:81-85`) and the `wasm` state JSON
  (`src/core/ControlWasmSupport.cpp:93`) report the module's *declared* value, a different surface;
  unchanged and still correct.
* `src/core/ControlDeviceSupport.cpp:483` (`latency_frames` of a device) now reports the pipeline
  period for such a device — the effect's real latency, which is what the field documents.
* `src/core/ControlCommandsLowLatency.cpp:136` (`low_latency` bypasses effects above a threshold):
  a ready zero-latency WASM module is now counted as one period. That is the point of the mode —
  it bypasses effects that really delay the signal — and it is a consequence of the corrected
  figure, not a new rule. No threshold changed.
* `src/core/ControlCommandsPdc.cpp:123` and `src/core/ControlCommandsRouting.cpp:153` report chain
  handles; they read the same corrected cache.

## Gates

`feature-check kads-5439 (base 3436a808b, 4 changed files)`: fork-sources, all-sources-reproduce,
no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all
PASS; `RESULT: PASS`, exit 0 (`gate-1.log`).

## Self-check

1. **Regression test fails with the product change reverted and passes with it.** Yes, seen:
   `git apply -R fix.patch` (exit 0) → rebuild (exit 0) → `p10` exit 1 with
   `Actual (reported): 0 / Expected: 256` (`test-red-2.log`); `git apply fix.patch` (exit 0) →
   rebuild (exit 0) → `p10` exit 0 with `effect reports 256, chain 256` (`test-green-2.log`,
   `test-green-3.log`). The earlier pre-fix RED is `test-red-1.log` (exit 1).
2. **The test is deterministic.** Per-block markers and `WasmWorker::waitForIdle()` (a condition
   variable in the worker) replace any wall clock; no sleeps, no polling, no thread races. The
   only timeouts are the existing 5 s hang nets in `waitForIdle`.
3. **Fix at the cause and complete.** The early return at `WasmEffect.cpp:202-205` discarded the
   period the same function computes and that `processImpl()` applies. The condition now names
   exactly when the pipeline is active; the sibling assertions (`p3`, `p6`, `p8`), the chain cache,
   the user override, the module-only accessors and the control reports were checked above. `p7`'s
   post-reload expectation was corrected because it pinned the bug.
4. **No collateral damage.** Nothing loaded: 0 (p1, p6, p8 unchanged). Declares 64: 64 + period
   (p3, p6 unchanged). Reload in flight: preserved latency keeps 64 + period (p7 unchanged while
   loading). Unload/failed load: 0 (p8 unchanged). Saved projects carry the module path and
   parameters, not a latency figure, so save/load is unaffected; no new limit or check was added.
5. **Not ALREADY-FIXED / FALSE-POSITIVE.** The evidence is above: the early return existed at
   `WasmEffect.cpp:202-205`, blamed to `8cdd74a557` and never modified, and the red run measured
   the unreported period (0 vs 256).

Realtime: the changed function is the control-thread report; `processImpl()` and
`advancePipeline()` are untouched, and the new code is one atomic load plus the existing engine
period read — no allocation, free, lock, wait, I/O or log. Undo/commands: no command changes, so
the ControlCommandsSnapshot canary is untouched.

## Other findings

* A ready zero-latency module passes its first block through dry (`advancePipeline` leaves `buf`
  alone when nothing is in flight, `WasmEffect.cpp:314-327`) and then delivers the same block again
  through the module one call later, so that block is heard twice at module start. Pre-existing,
  outside this finding, not changed here; `p10` documents the measured behaviour.
* Fix lanes are configured without WASM (`FIX-CMAKE-ARGS:48`), so this plugin's registered ctest
  cannot be built in a lane; the hand-built harness above is the workaround used here. The train
  tree has `WANT_WASM:BOOL=ON` and builds `WasmSandboxTest` normally.
