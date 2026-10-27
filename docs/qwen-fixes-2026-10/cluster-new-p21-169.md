FIXED

# Cluster new-p21-169: FlexEnvelope divides by zero on a zero-duration sustained stage

Verdict: **FIXED**. The division by zero is real, and the fix is at its cause. Its observable effect is small:
on every shape curve the product can build, the old code reached the target level only up to one float
rounding step, and it never produced NaN. The finding's title overstates the audible impact. The defect is that
the sustained path evaluates `1 / 0` and then depends on `-ffast-math` infinity arithmetic, while the timed-stage
path it should match states the intended contract.

## Evidence

- **Defect (before the fix):** `plugins/SfzPlayer/sfizz/src/sfizz/FlexEnvelope.cpp` in `Impl::process`, the
  sustained-stage loop at original lines 242-249: `while ((time < stageEndTime || sustained) && frameIndex < maxFrameIndex)`
  with `float x = time * (1.0f / stageEndTime);`. `stageEndTime` is `stageTime_` (line 236), which is `0` for a
  zero-time point (`FlexEGPoint::getTime`, `FlexEGDescription.cpp:89-95`, returns `time` plus CC terms). For a sustained
  stage the loop runs with `time < stageEndTime` false, so only `|| sustained` keeps it going: `1.0f / 0` is `+inf`.
- **Intended contract:** `FlexEnvelope.cpp:217-219` says "if stage is of zero duration, immediate transition to
  level" and sets `currentLevel_ = stageTargetLevel_`. The non-sustained timed path never reaches the division:
  `time < 0` is false there, so it skips the inner loop. Only the sustained path lacked the zero-duration case.
- **Why no NaN in practice:** `time` has already been advanced by `samplePeriod` (line 243), so `x` is `+inf`.
  `Curve::evalNormalized` (`Curve.h:48-51`) multiplies by 127 and `Curve::evalCC7(float)` (`Curve.h:36-38`) clamps to
  `[0, 127]`, so the lookup reads `_points[127]`. Every shape curve the product builds ends at 1:
  shape 0 is the default curve, shapes > 0 are `pow(x, 1+shape)` (`FlexEGDescription.cpp:54-59`), shapes < 0 are
  `1 - pow(1-x, 1-shape)` (`FlexEGDescription.cpp:62-68`), and the predefined curves set `_points[NumValues-1] = 1.0`
  (`Curve.cpp:110-111`). The result is `source + 1 * (target - source)`, which lands on the target up to a float
  rounding step.
- **Why it is still a defect:** the build compiles sfizz with `-ffast-math` (`plugins/SfzPlayer/CMakeLists.txt:206`),
  which lets the compiler assume no infinities, so the division-by-zero path is outside the arithmetic contract.
  The rounding miss was emulated in float32 (not run in the C++ build): with source 0.25 and target 0.03 the old formula
  gives 0.030000001 instead of 0.03. The RED run below confirms the inequality in the compiled test.
- **Fix:** `FlexEnvelope.cpp:242-253`. In the sustained loop, `stageEndTime == 0.0f` writes `level = targetLevel`
  (exact); otherwise the original ramp computation runs unchanged. Non-zero stages take the old path unchanged.
- **Callers and siblings checked:** `FlexEnvelope::process` (`FlexEnvelope.cpp:154-170`) is the only caller of
  `Impl::process`; both its dynamic (per-chunk `updateCurrentTimeAndLevel`) and static branches reach the fixed loop.
  Dynamic envelopes can move `stageTime_` to zero mid-stage through CC modulation, and the guard covers that case in
  the same way. The timed loop at `FlexEnvelope.cpp:214-226` is unchanged. Its zero-time branch (217-219) already
  matches the fix's behaviour. `advanceToStage` / `updateCurrentTimeAndLevel` are unchanged.
- **Reachability:** a sustain point's `time` field (`FlexEGPoint::time`, `FlexEGDescription.h`) feeds `getTime`
  directly, so a zero there reaches the sustained loop. I did not trace the SFZ opcode parser for this step.

## Changes

- `plugins/SfzPlayer/sfizz/src/sfizz/FlexEnvelope.cpp` (fix, lines 242-253).
- `tests/src/plugins/SfzFlexEnvelopeZeroStageTest.cpp` (new regression test, 95 lines).
- `tests/CMakeLists.txt`: registers the test under the `if(TARGET sfzplayer_sfizz)` block and adds it to the
  sfizz include/link list.
- `tests/upstream-modifications.txt`: the `FlexEnvelope.cpp` entry now records cluster new-p21-169 and its regression.
- `tests/all-sources.txt`: regenerated with its own recipe (`git ls-files` pipeline); the only change is the new test.
- `tests/fork-sources.txt`: one path added in the position the gate derives (after the commit that adds the test).

## Regression test (`tests/src/plugins/SfzFlexEnvelopeZeroStageTest.cpp`)

Test matrix (all deterministic: fixed sample rate 48000, fixed descriptors, no clock, sleep or thread):

| Path | Test | Expectation |
|---|---|---|
| sustained zero-time stage | `zeroDurationSustainHoldsTargetLevelExactly` | every frame equals target 0.03f exactly (RED before fix) |
| sustained zero-time stage, 0 -> 1 | `zeroDurationSustainStaysFinite` | every frame finite and within 1e-6 of 1 |
| sustained stage with real duration | `nonZeroDurationSustainStillRamps` | starts above start level, never reaches target within 64 frames of a 2 ms ramp, non-decreasing |

The descriptor has two points: stage 0 is a zero-time point at the start level (the timed loop passes it at once and
sets the level), and stage 1 is the sustain point (`desc.sustain = 1`). `FlexEnvelope::start(0)` and `process()` then
run for 64 frames. The first test uses source 0.25f and target 0.03f: in float, `0.25f + (0.03f - 0.25f)` is not
`0.03f` (checked with a float32 emulation before writing the test), so it fails exactly when the division-based
formula is used.

Declared seams: the sample rate (`setSampleRate(48000)`) and the descriptor values are the only inputs. The test
calls `Resources` (default constructor) as the other sfizz tests do.

## Commands and results

All builds used `CCACHE_DISABLE=1`: the first targeted build failed at ccache's temp-file step
(`ccache: error: failed to create temporary file ... dsh/home/ccache-tmp-r6 ... Permission denied`,
`build-red-1.log`, EXIT=2). No product code was involved; the rebuilds are plain compiles.

| # | Command (run from the worktree / `build/tests`) | Log | EXIT |
|---|---|---|---|
| RED build | `CCACHE_DISABLE=1 cmake --build build --target zene SfzFlexEnvelopeZeroStageTest -j4` (fix not applied) | `build-red-2.log` | 0 |
| RED test | `ctest -R SfzFlexEnvelopeZeroStageTest --output-on-failure` (in `build/tests`) | `test-red-1.log` | **8** (`FAIL! : ...zeroDurationSustainHoldsTargetLevelExactly() 'std::all_of(...)' returned FALSE`; Totals 4 passed, 1 failed) |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene SfzFlexEnvelopeZeroStageTest -j4` (fix applied) | `build-green-1.log` | 0 |
| GREEN test | `ctest -R SfzFlexEnvelopeZeroStageTest --output-on-failure` | `test-green-1.log` | **0** (`100% tests passed out of 1`) |
| 4b diff | `git diff -- plugins/SfzPlayer/sfizz/src/sfizz/FlexEnvelope.cpp > fix.patch` | `fix.patch` | 0 |
| 4b unapply | `git apply -R fix.patch` | | 0 |
| 4b RED build | `CCACHE_DISABLE=1 cmake --build build --target zene SfzFlexEnvelopeZeroStageTest -j4` | `build-4b-unfixed.log` | 0 |
| 4b RED test | `ctest -R SfzFlexEnvelopeZeroStageTest --output-on-failure` | `test-4b-unfixed.log` | **8** (same assertion) |
| 4b reapply | `git apply fix.patch` | | 0 |
| 4b GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene SfzFlexEnvelopeZeroStageTest -j4` | `build-4b-fixed.log` | 0 |
| 4b GREEN test | `ctest -R SfzFlexEnvelopeZeroStageTest --output-on-failure` | `test-4b-fixed.log` | **0** |
| family build | `CCACHE_DISABLE=1 cmake --build build --target SfzPlayerTest SfzPlayerRealtimeTest SfizzLeakDetectorTest SfzFilePoolTest SfzEffectBusViewTest SfzSimdHelperAlignmentTest SfzEQPoolExhaustionTest SfzSimdGainAVXTest -j4` | `build-family-1.log` | 0 |
| family test | `ctest -R "Sfz\|Sfizz\|FlexEnv" -j4 --timeout 300` (in `build/tests`) | `test-family-2.log` | **0**, 10/10 passed (the earlier run, `test-family-1.log`, exited 8 with 8 "Not Run" because those binaries were not yet built; it is not a result of the fix) |

Gates (run individually from the worktree; see the gate table below for why the batch runner was not used):

| Gate | Command | Log | EXIT |
|---|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | `gate-fork-sources-2.log` | 0 (after the manifest commit; before it, EXIT=1 with one missing path, `gate-fork-sources.log`) |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | `gate-all-sources-2.log` | 0 (before the regeneration: EXIT=1, `gate-all-sources.log`) |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-regression-2.log` | 0 |
| check-namespace | `tests/scripted/check-namespace` | `gate-check-namespace-2.log` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | `gate-no-tautology-2.log` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | `gate-doc-figures-2.log` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | `ratchet-complexity-1.log` | 1 (see below) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | `ratchet-file-length-1.log` | 1 (see below) |

**Ratchets.** Both exit 1, and none of their REGRESSION lines is in a file this branch changed. The complexity lines
are `WasmSandbox::loadModuleBytes` (`src/wasm/WasmSandbox.cpp`), `dotCentreAlong` (`tests/src/gui/XyPadPairTest.cpp`),
`parseNotes` (`src/core/DawProjectReadTracks.cpp`), `setStrip` and `adjust` and `landPairState`. The file-length lines
name `Vst3Host.cpp`, `WasmSandbox.cpp` and several `tests/src/core` and `src/core` files. The new test is 95 lines
(under the 500-line limit) and `Impl::process` is not listed. The ratchets are red on the train on purpose.

**Feature gate runner.** The original run of
`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exited 1 with
`PermissionError: ... zene-qwen-new-p21-169-feature-check` (`gates-feature-check-1.log`); at that time, its log
directory was outside the worktree's write-jail. During independent QA, the exact requested command was rerun and
exited 0: all seven gates passed, including both ratchets reporting no new item in the changed files. This supersedes
the original feature-check and `submit_check.py` failure noted above.

## Self-check

- **The regression test FAILS with the fix reverted and PASSES with it (exit codes).** RED: `test-red-1.log`, EXIT=8,
  `zeroDurationSustainHoldsTargetLevelExactly` FAILS. GREEN: `test-green-1.log`, EXIT=0. Step 4b, with
  `git apply -R fix.patch` and then `git apply fix.patch`: `test-4b-unfixed.log` EXIT=8, `test-4b-fixed.log` EXIT=0.
  The other two tests pass both before and after the fix, as the matrix says: they guard finiteness and the non-zero
  ramp.
- **Deterministic (no wall clock, sleeps or races; injected values).** The test uses a fixed sample rate of 48000,
  fixed descriptor points, and a single-threaded envelope call. It checks sample values, not time. The reproduction
  of the defect relies on float arithmetic only.
- **The fix is at the cause and complete.** The defect is the division in the sustained loop
  (`FlexEnvelope.cpp:248` before the fix; the guard is now at 244). The guard covers the one path that reaches it:
  `process` (static and dynamic) calls only `Impl::process`, whose zero-time sustained stage is the only place with
  `stageEndTime == 0` in that loop. The sibling timed loop already matched the contract (217-219), so I left it
  unchanged. I did not read the other `FlexEnvelope.cpp` under `modulations/sources/` for this finding, so it is
  not claimed as checked.
- **No collateral damage.** Projects that load the SFZ instrument see no change in parsing; the fix only changes
  samples on the sustained, zero-duration path. On that path the old and new values differ by at most one float
  rounding step, because every shape curve ends at 1 (`FlexEGDescription.cpp:54-68`, `Curve.cpp:110-111`). The
  guard adds no limit or check that another caller could hit, and the non-zero path (ramp) is unchanged and tested.
- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE:** the defect was present on `bugs/qwen-new-p21-169` before the
  change (`FlexEnvelope.cpp:242-249` on base `5c1c6715d9`).
- **Realtime.** The change adds one comparison and an assignment inside `Impl::process`. No allocation, lock, log or
  wait is added. `FlexEnvelope::process` is the audio path (per period); the new code writes a float into the output
  span only.
- **Undo, commands, registration.** No control command was added. The new test is registered in
  `tests/CMakeLists.txt`, `tests/all-sources.txt` (regenerated), and `tests/fork-sources.txt`. The vendored file is
  recorded in `tests/upstream-modifications.txt`.
- **Open items, not fixed here:** none from the feature gate; its independent rerun passed. The ratchets are red on the
  train only for pre-existing items outside this change. The finding's "~195" line was a historical number; the code
  sits at `FlexEnvelope.cpp:242-253`.
