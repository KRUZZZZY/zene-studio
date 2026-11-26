BLOCKED

Underlying defect: REAL (established by reading the code this session, not by running it).
The regression test for it cannot be built or run in this lane: `plugins/WasmEffect/WasmEffect.cpp`
is compiled only under `WANT_WASM=ON`, this lane's build tree is configured `WANT_WASM:BOOL=OFF`
by design, and the lane guard refuses any `cmake` configure. No product code was changed, because
a change that cannot be compiled or run here would be an unverified guess in the train.

## Missing capability

A build tree configured `-DWANT_WASM=ON` with the wasmtime C API 48.0.1 on the find path
(`<tree>/third_party/wasmtime` or `WASMTIME_ROOT`). Without it, neither the `wasm_effect` plugin
target nor `WasmSandboxTest` exists:

* `plugins/WasmEffect/CMakeLists.txt:5-7` — `IF(NOT WANT_WASM) RETURN() ENDIF()`: the plugin is not built.
* `tests/CMakeLists.txt:5460-5475` — `WasmSandboxTest`, the only test binary that compiles
  `plugins/WasmEffect/WasmEffect.cpp` (`:5468`), is registered inside `if(WANT_WASM)`.
* `tests/CMakeLists.txt:5512-5514` — "On a build without the wasmtime C API (WANT_WASM degrades to
  OFF ...) neither this target nor WasmSandboxTest exists, which is the case on the reference box."

This lane was prepared that way on purpose:

* `prepare.log` line 1, the configure the parent ran: `-DWANT_WASM:BOOL=OFF`.
* `~/.local/share/zene-bunny-fix/qwen/FIX-CMAKE-ARGS:1` — "WASM off in fix trees"; `:48` — `-DWANT_WASM:BOOL=OFF`.
* `~/.local/share/zene-bunny-fix/qwen/qwenfix.py:1139` — `if m and m.group(1) != "WANT_WASM":  # WASM needs a per-tree Wasmtime bootstrap`.
* `build/CMakeCache.txt` — `WANT_WASM:BOOL=OFF`.

The tree cannot be switched on from a lane; the guard refuses the configure:

```
$ cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5439
$ cmake -S . -B build -DWANT_WASM=ON
bunny-guard: configuring is the parent's job; the lane build tree is already configured. Use 'cmake --build build --target <t>'.
EXIT=97
```

and the target does not exist:

```
$ cmake --build build --target WasmSandboxTest
make: *** No rule to make target 'WasmSandboxTest'.  Stop.
EXIT=2
```

ctest confirms there is nothing to run (zero tests is an error by the rules):

```
$ cd build/tests && ctest -R WasmSandboxTest --output-on-failure
No tests were found!!!
EXIT=0
```

The train tree has the capability this lane lacks: `zene-cachyos/build/CMakeCache.txt` contains
`WANT_WASM:BOOL=ON`, `zene-cachyos/third_party/wasmtime/include/wasmtime.h` declares
`WASMTIME_VERSION "48.0.1"`, and `zene-cachyos/build/tests/WasmSandboxTest` is built. I copied that
gitignored `third_party/wasmtime` into this worktree while testing the route; the configure that
would consume it is refused, so the copy was removed and `git status` is clean.

## The defect (REAL), with evidence

`latencyFrames()` still returns early for a module that declares zero latency, so the host's
collect-after-submit pipeline period is left out of the PDC report:

* `plugins/WasmEffect/WasmEffect.cpp:199-217` — `latencyFrames()`; the early return is
  `:202-205` (`if (moduleFrames == 0) { return 0; }`), followed by the code's own statement of the
  invariant at `:206-207` ("The audio thread submits block N and collects block N-1, so the host
  adds exactly one block of pipeline delay on top of whatever the module reports").
* `git blame -L 202,205` attributes those four lines to `8cdd74a557` (2026-09-09), the commit that
  created the file; no later commit changed them. The finding is **not** already fixed on this
  branch.
* The delay is real, not merely documented: `processImpl()` (`:219-289`) always calls
  `advancePipeline()` for a ready worker, and `advancePipeline()` (`:291-342`) collects the result
  submitted in the previous call (`:305`) *before* submitting the current block (`:337`). A ready
  module that declares latency 0 therefore delivers block N in call N+1: exactly one engine period.
  The same file says so twice more: `:227-229` ("a block submitted in call N is due in call N+1,
  which is the fixed one-block pipeline delay latencyFrames() reports") and
  `plugins/WasmEffect/WasmEffect.h:87-91` ("Total frames this effect adds to the signal path: the
  module's declared latency ... plus the host's one-block collect-after-submit pipeline").
* PDC consumes exactly this figure: `include/Effect.h:310-313` (`compensatedLatencyFrames()` returns
  the user override or `latencyFrames()`), and `src/core/EffectChain.cpp:115-145` sums it into the
  chain's cached figure (`:125`) that the mixer aligns parallel paths against.
* A module that declares zero latency exists and is used by the suite: `modules/wasm/gain.wat:19`
  (`(global (export "latency") i32 (i32.const 0))`). With it, the effect is one period late and
  reports 0, so everything else in the project is aligned to a path that is not.
* No existing test measures this case. `WasmSandboxTest` asserts the report only for a module that
  declares 64 (`tests/src/wasm/WasmSandboxTest.cpp:1065-1069`, `:1262-1280`), for the no-module case
  (`:900`, `:1252`) and after an unload (`:1480`). Worse, `p7` currently *pins the bug* for a
  zero-latency replacement: `:1370-1374` asserts `effect->latencyFrames() == 0` and
  `chain.latencyFrames() == 0` after reloading `gain`, with the comment `:1324-1325` "The
  replacement declares no latency, so once it is loaded the effect reports none."

## Proposed fix (not applied — nothing in this lane compiles the file)

Add the host period whenever the effect actually delays the signal through the worker pipeline, and
keep the passthrough case at 0 so an empty effect does not over-report:

```diff
 int WasmEffect::latencyFrames() const
 {
 	const int moduleFrames = compensatedModuleLatency();
-	if (moduleFrames == 0)
+	// A ready module always goes through the collect-after-submit pipeline, so the
+	// host adds its period even when the module declares no latency of its own.
+	// Nothing loaded (or loading with nothing being replaced) passes the dry
+	// signal through undelayed, and adds nothing.
+	const bool pipelineActive = m_worker.isReady() || moduleFrames > 0;
+	if (!pipelineActive)
 	{
 		return 0;
 	}
```

Why this shape:

* `m_worker.isReady()` is `state() == State::Ready` (`src/wasm/WasmWorker.h:85-86`), one atomic
  acquire load: safe on the control thread, and it is the exact gate `processImpl()` uses to take
  the pipeline path (`WasmEffect.cpp:248`).
* `moduleFrames > 0` must stay: when the worker is not ready but a declared latency is still
  compensated (a quarantine, or a reload whose `m_preservedLatency` is in force), `processImpl()`
  delays the dry signal by `moduleFrames + frames` (`:254-260`, `:344-351`), so the period belongs
  in the report there too. Today only that case survives the early return.
* Not ready and zero latency (nothing loaded, loading with nothing replaced, a failed load) stays
  0: `processImpl()` returns the block untouched (`:248-257`), which is what `p1:900`, `p6:1252`
  and `p8:1480` already assert.

Complete set of places the fix must touch to stay consistent:

1. `plugins/WasmEffect/WasmEffect.cpp:202-205` — the condition above.
2. `plugins/WasmEffect/WasmEffect.h:87-91` — the doc comment gains "even when the module declares
   none" (declaration unchanged, but the header is rebuilt for the plugin and the test binary).
3. `tests/src/wasm/WasmSandboxTest.cpp:1370-1374` (p7) — a zero-latency replacement must now report
   and cache `periodFrames`, not 0; the comment at `:1324-1325` follows. This is a correction of an
   assertion that pins the bug, not a weakening: the value it must equal becomes the contract.
4. `moduleLatency()` (`WasmEffect.h:81-85`) and the `wasm` state JSON
   (`src/core/ControlWasmSupport.cpp:93`) deliberately report the *module's* declared value, not the
   effect's PDC figure; they are a different surface and stay unchanged.

## Test matrix

| path | expectation | coverage |
| --- | --- | --- |
| normal, zero-latency ready module | report and chain cache = one engine period; measured delay = one call | new `p10` (below) |
| normal, module declaring 64 | report and cache = 64 + period, unchanged | `p3:1065-1069`, `p6:1262-1280` |
| no module / unloaded | report 0, dry passthrough, unchanged | `p1:900`, `p6:1252`, `p8:1480` |
| failure (refused load, unload) | previous figure kept, then 0 after unload | `p8_failedModuleLoadIsReportedInTheStatusText`, `p8_emptyPathUnloadReportsSuccess` |
| exhaustion (worker pool full) | load refused, running module and its compensation kept | `loadModule`'s all-or-nothing path; `p7` for the reload |
| reconfiguration (reload, sample-rate change) | compensation follows the new module, no gap while it loads | `p7:1328-1374`, `p4` |
| audio thread | `processImpl`/`advancePipeline` untouched; the fix runs on the control thread and reads one atomic | review of the diff |
| platform | no new platform API, no Qt-6-only call; header comment only, but every target including it must rebuild (`wasm_effect`, `WasmSandboxTest`) | build |

## Proposed regression test (written here, not run)

New slot `p10_zeroLatencyModuleReportsTheHostPipelinePeriod` in `tests/src/wasm/WasmSandboxTest.cpp`
(declared next to `:357`), using `gain.wat` (declares latency 0, gain = parameter 0, set to 0.5 as
`p9` does, so a processed block is distinguishable from dry):

1. `EffectChain chain; auto* effect = new WasmEffect(&chain, nullptr); chain.appendEffect(effect);`
   `QCOMPARE(effect->latencyFrames(), 0); QCOMPARE(chain.latencyFrames(), 0);` — nothing loaded
   still reports nothing (the over-report guard).
2. `loadModule(modulePath("gain"))`, `QVERIFY(effect->worker().isReady())`,
   `QCOMPARE(effect->worker().declaredLatency(), 0)`, `setModuleParam(0, 0.5f)`.
3. `QCOMPARE(effect->latencyFrames(), periodFrames);` and
   `QCOMPARE(chain.latencyFrames(), periodFrames);` with
   `periodFrames = Engine::audioEngine()->framesPerPeriod()` — the assertion that fails before the
   fix (0 != period).
4. Drive `periodFrames`-sized blocks with one distinct marker value per block, calling
   `QVERIFY(effect->worker().waitForIdle(5000))` after each `processAudioBuffer` (the lockstep seam
   `p3:1050` already uses): call 0 outputs its own dry marker, every call N >= 1 outputs
   `0.5f * marker(N-1)`. So the measured pipeline delay is one call = one period, equal to the
   reported figure — the report and the audio agree.
5. After `loadModule(QString())`, `QCOMPARE(effect->latencyFrames(), 0)` again.

Determinism: block markers and `waitForIdle()` (a condition variable in the worker, not a sleep);
no wall-clock timing, no polling loop, no race. The bounded timeouts are hang safety nets only.

## Commands run, with exit codes

All logs in `/home/kruzzzzy/.config/kads/zene/state/runs/5439.r1/logs`.

| # | command | exit | log |
| --- | --- | --- | --- |
| 1 | `cmake -S . -B build -DWANT_WASM=ON` | 97 (guard refusal) | `configure-1.log`, `configure-2.log` |
| 2 | `cmake --build build --target WasmSandboxTest` | 2 (`No rule to make target`) | `build-1.log` |
| 3 | `cd build/tests && ctest -R WasmSandboxTest --output-on-failure` | 0, `No tests were found!!!` (zero tests) | `test-1.log` |
| 4 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0, all eight rows PASS | `gate-1.log` |

Reads that establish the evidence: `nl -ba plugins/WasmEffect/WasmEffect.cpp`, `git blame -L 198,214`,
`nl -ba plugins/WasmEffect/WasmEffect.h`, `nl -ba include/Effect.h`, `nl -ba src/core/EffectChain.cpp`,
`nl -ba tests/CMakeLists.txt`, `tests/src/wasm/WasmSandboxTest.cpp`, `prepare.log`,
`FIX-CMAKE-ARGS`, `qwenfix.py:1139`, and the `CMakeCache.txt` of both trees.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
   Not done, and no exit codes are claimed: there is no product change and no test run. The test
   target does not exist in this build tree (`build-1.log`, exit 2), and the configure that would
   create it is refused (`configure-1.log`, exit 97). The test is specified above for a
   WASM-enabled tree.
2. **The test is deterministic (seam or injected values).** The proposed test uses one distinct
   marker per block and `WasmWorker::waitForIdle()` after each `processAudioBuffer` — the same
   lockstep seam `p3` uses — so the pipeline state is observed, not timed. It is not run here.
3. **The fix is at the cause and complete.** Cause: the early return at `WasmEffect.cpp:202-205`
   discards the period the rest of the function computes and `processImpl` really applies. Callers
   and siblings checked: `compensatedLatencyFrames()`/`EffectChain::refreshLatency()`
   (`Effect.h:310-313`, `EffectChain.cpp:125`) consume the figure; the user latency override path
   bypasses it and is untouched; `moduleLatency()` and the `wasm` state JSON report the module's raw
   declaration and are a different surface; the existing tests that pin 0 for "nothing loaded" and
   "declare 64" keep their expectations, while `p7:1370-1374` must be corrected because it pins the
   bug for a zero-latency replacement.
4. **No collateral damage.** The proposed condition changes exactly one case — a ready module whose
   declared (and preserved) latency is 0 — from 0 to one period. Nothing is loaded: 0. Declares 64:
   64 + period, unchanged. Reload in flight: the preserved latency keeps 64 + period, unchanged.
   Unload/failed load: 0. Saved projects carry no latency figure, so loading and saving are
   unaffected; no limit or check is added that could reject another caller. All of this is
   reasoned from the code, not executed — the lane cannot execute it.
5. **Verdict basis.** Not ALREADY-FIXED and not FALSE-POSITIVE: the early return is present at
   `plugins/WasmEffect/WasmEffect.cpp:202-205`, blamed to `8cdd74a557` (2026-09-09) and never
   modified since, while the same function's comment (`:206-207`), `advancePipeline()`
   (`:291-342`) and `WasmEffect.h:87-91` all state the one-block pipeline that the early return
   drops.

## Other findings

* A ready zero-latency module passes its first block through dry (`advancePipeline` leaves `buf`
  alone when nothing is in flight, `WasmEffect.cpp:314-327`) and then delivers the same block again
  through the module one call later, so that block is heard twice at module start. Pre-existing,
  outside this finding, not changed here.
* `tests/CMakeLists.txt:5512-5514` records that `WasmAbiConformanceTest` "HAS NOT BEEN EXECUTED"
  because builds on the reference box lack wasmtime. Same root cause as this block: WASM code has
  no executable lane in fix trees.

6. **Pre-submit check (`submit_check.py <worktree> 5439`).** One FAIL, and it cannot be satisfied
   in this lane: "tests added or changed". The checker's only alternative branch is a claim-only
   ALREADY-FIXED/FALSE-POSITIVE report, and this finding is neither — the early return is present
   in the code (`WasmEffect.cpp:202-205`). A test change would have to be an unrun test for a target
   this tree cannot build, which is exactly what the evidence above rules out. The check's five ASK
   items are answered at length in items 1-5 above; the run itself:

   ```
   PRE-SUBMIT CHECK for 5439
     PASS  commits on the branch
     PASS  working tree clean
     PASS  report docs/qwen-fixes-2026-10/cluster-5439.md
     FAIL  tests added or changed
     PASS  gates (feature_check.py)
   RESULT: FAIL - 1 item(s) to fix before submitting
   EXIT=1
   ```

   Nothing was faked to clear it: the work tree has no product change and no unrun test.
