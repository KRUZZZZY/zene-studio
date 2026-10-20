ALREADY-FIXED

# Cluster new-p12-163-port

Verdict: **ALREADY-FIXED**.

The reported bug ("NAM loader accepts negative dilation", historical `NamModelLoader.cpp:270`) is already fixed on this branch since commit `95dff2a47` (the patrol's line numbers pre-date it). This round adds a self-contained regression test for the exact finding scenario in the main test tree, proven both ways below.

## Evidence (file:line)

- `plugins/NeuralAmp/nam/NamModelLoader.cpp:269-273` — the loader reads each layer's dilation and immediately rejects non-positive values before any further use:
  ```cpp
  const int dilation = dilations[i].get<int>();
  if (dilation <= 0)
  {
      throw ParseError("invalid dilation");
  }
  ```
  The check runs before the receptive-field arithmetic (`:274-286`) and before the `LayerSpec` is built (`:287-293`), so a dilation of -1 (the finding's repro: kernel 3, dilation -1) never reaches `layer.dilation` and never reaches convolution window offsets or prewarm.
- Introduced by commit `95dff2a47` "Fix NAM loader dimensions (cluster new-p9-311)", which is in this branch's history: `git log -S "invalid dilation" -- plugins/NeuralAmp/nam/NamModelLoader.cpp` lists exactly that commit.
- Pre-existing regression for the exact scenario: `plugins/NeuralAmp/tests/nam_harness.cpp:325` — `{"dilation-negative", 1, 1, 1, -1, 3, 1, true}` (kernel size 3, dilation -1, expects rejection), registered as CTest `NamLoaderTest_dilation-negative` at `plugins/NeuralAmp/tests/CMakeLists.txt:59,68-70` and mirrored into the main tree at `tests/CMakeLists.txt:5689-5695` (`build/tests/CTestTestfile.cmake:1179`).
- New regression added this round: `tests/src/plugins/NeuralAmpModelLoadTest.cpp:290-320` — `nonPositiveDilationIsRejectedByTheLoader` writes the patrol's repro fixture (kernel 3, dilation -1, weight-complete) plus a positive-dilation control of the same shape, and asserts `NamModel::loadFromFile` accepts the control and rejects the -1 and 0 dilations. It is part of the already-registered `NeuralAmpModelLoadTest` ctest (tests/CMakeLists.txt:814,1653-1665), so no CMake change was needed.
- Sibling/caller audit: the only downstream consumer of dilation is `plugins/NeuralAmp/nam/NamModel.cpp:176` (`const int D = layer.dilation;`), fed exclusively from `LayerSpec.dilation` assigned in the validated loop (`NamModelLoader.cpp:291`); a search over `plugins/NeuralAmp` (excluding tests) shows no other `.nam` JSON path that parses `dilations`.

## RED / GREEN proof

The guard is already committed, so the "revert" step (procedure 4b) was done by temporarily deleting the 4-line guard at `NamModelLoader.cpp:270-273` (working-tree diff captured in `LOGS/fix-remove-check.patch`), rebuilding, running the tests, then restoring the file and verifying `git diff -- plugins/NeuralAmp/nam/NamModelLoader.cpp` was empty (tree identical to HEAD).

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-163-port.r2/logs`.

| Step | Command (cwd = worktree root unless noted) | EXIT |
|---|---|---|
| GREEN baseline, harness family | `cmake --build build --target nam_harness > LOGS/build-1.log 2>&1` then `ctest -R NamLoaderTest --output-on-failure > LOGS/test-green-1.log 2>&1` (cwd `build/tests`) | 0 — 8/8 passed |
| RED via harness (guard removed, rebuilt `LOGS/build-2-red.log`) | `ctest -R NamLoaderTest_dilation-negative --output-on-failure > LOGS/test-red-1.log 2>&1` (cwd `build/tests`) | 8 — FAIL: "unexpected loader result for config dilation-negative" (loader accepted dilation -1) |
| GREEN restored, harness family (`LOGS/build-3-green.log`) | `ctest -R NamLoaderTest --output-on-failure > LOGS/test-green-2.log 2>&1` (cwd `build/tests`) | 0 — 8/8 passed |
| New main-tree test, GREEN (`LOGS/build-4.log`) | `ctest -R NeuralAmpModelLoad --output-on-failure > LOGS/test-green-3.log 2>&1` (cwd `build/tests`); single slot: `QT_QPA_PLATFORM=offscreen ./tests/NeuralAmpModelLoadTest nonPositiveDilationIsRejectedByTheLoader > LOGS/test-green-4-single.log 2>&1` (cwd `build`) | 0; single slot "Totals: 3 passed, 0 failed" |
| RED via new test (guard removed again, `LOGS/fix-remove-check.patch`, rebuilt `LOGS/build-5-red.log`) | `QT_QPA_PLATFORM=offscreen ./tests/NeuralAmpModelLoadTest nonPositiveDilationIsRejectedByTheLoader > LOGS/test-red-2-single.log 2>&1` (cwd `build`) | 1 — "FAIL! ... 'negModel == nullptr' returned FALSE" at NeuralAmpModelLoadTest.cpp:312; the positive-dilation control in the same test still passed, so the failure is specifically the missing dilation rejection |
| GREEN restored (guard re-inserted; loader diff vs HEAD empty; `LOGS/build-6-green.log`) | single slot `> LOGS/test-green-5-single.log` (EXIT 0) and `ctest -R "NamLoaderTest\|NeuralAmpModelLoad" --output-on-failure > LOGS/test-green-6.log 2>&1` (cwd `build/tests`) | 0 — 9/9 passed |
| Gates (baseline) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gates-1.log 2>&1` | 0 — RESULT: PASS (all 8) |
| Gates (after test change) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gates-2.log 2>&1` | 0 — RESULT: PASS (all 8) |

Affected-family test count: **9 CTest tests** (`NamLoaderTest_valid`, `dilation-zero`, `dilation-negative`, `dilation-overflow`, `receptive-total-overflow`, `channels-zero`, `bottleneck-zero`, `head-size-zero`, plus `NeuralAmpModelLoadTest`), all passing with the fix in place.

Honesty notes: (1) the first RED attempt ran `cmake --build build` with cwd `build/tests`, so the build failed silently and ctest ran the stale (fixed) binary; it was redone from the worktree root, rebuilt, and only then did the test fail as recorded. (2) `git apply`/reverse-apply of a patch against HEAD was not usable here because HEAD already contains the fix; the equivalent proof (working-tree removal of exactly the guard hunk, captured in `LOGS/fix-remove-check.patch`, then restore) was used and is recorded above.

Log search with the fff grep tool (path = LOG FOLDER): `test-red-1.log` contains the expected `FAIL: unexpected loader result` and `tests FAILED` summary; `test-red-2-single.log` contains `FAIL!` with `'negModel == nullptr' returned FALSE`; the build and green logs contain no `error`/`Assertion` matches.

## Collateral damage

None. The only production-path behaviour touched was the temporary removal used for proof, which was restored byte-identical (`git diff -- plugins/NeuralAmp/nam/NamModelLoader.cpp` empty). The guard only rejects `dilation <= 0`, which cannot occur in a valid `.nam` model (dilations are positive receptive-field strides); the positive-dilation control in the new test and `NamLoaderTest_valid` prove well-formed models still load. The new test writes fixtures under a `QTemporaryDir` and calls the loader directly on the test thread — no audio-thread path, no timing dependency, no command/undo surface.

## Files committed

- `tests/src/plugins/NeuralAmpModelLoadTest.cpp` (new regression `nonPositiveDilationIsRejectedByTheLoader`; fork-new file already registered in tests/fork-sources.txt:2552, so Gate 6 needs no ledger entry)
- `docs/qwen-fixes-2026-10/cluster-new-p12-163-port.md` (this report)

## Round 2 rework (report-first-line fix, rerun evidence)

Round-2 review (qa/reviews/new-p12-163-port.r2.md) required the verdict on line 1; the heading now follows it (`f10e1b955`, report-only — loader and regression test untouched). Rerun after the correction, logs in the same LOG FOLDER:

| Step | Command | EXIT |
|---|---|---|
| Rebuild | `cmake --build build --target zene NeuralAmpModelLoadTest nam_harness > LOGS/build-7.log 2>&1` | 0 |
| Targeted tests | `ctest -R "NeuralAmpModelLoadTest\|NamLoaderTest" --output-on-failure > LOGS/test-green-7.log 2>&1` (cwd `build/tests`) | 0 — 9/9 passed |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gates-3.log 2>&1` | 0 — RESULT: PASS (all 8) |

(A `submit_check.py` run before this section was committed exited 1 only on "working tree clean", because this report edit was then uncommitted — LOGS/submit-check-3.log; it passes once the report is committed.)

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).** Yes, proven twice. New test: with the 4-line dilation guard temporarily removed and `NeuralAmpModelLoadTest` rebuilt, the single slot exited 1 with "FAIL! ... 'negModel == nullptr' returned FALSE" (LOGS/test-red-2-single.log); with the guard restored and rebuilt, it exited 0 ("Totals: 3 passed, 0 failed", LOGS/test-green-5-single.log) and the ctest family `NamLoaderTest|NeuralAmpModelLoad` exited 0 with 9/9 passed (LOGS/test-green-6.log). The pre-existing harness case was proven the same way: with the guard removed, `ctest -R NamLoaderTest_dilation-negative` exited 8 with "FAIL: unexpected loader result for config dilation-negative" (LOGS/test-red-1.log); restored, `ctest -R NamLoaderTest` exited 0 with 8/8 (LOGS/test-green-2.log).
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).** Yes: `nonPositiveDilationIsRejectedByTheLoader` writes fixture `.nam` JSON with fixed injected values (channels 1, head 1, kernel 3, dilations -1 / 0 / 1, 10 weights of 0.x — NeuralAmpModelLoadTest.cpp:292-302) into a `QTemporaryDir`, and asserts only on `NamModel::loadFromFile` accept/reject. No clock, sleep, thread or gate is involved; the harness case similarly depends only on the fixture values (its sole `steady_clock` use, nam_harness.cpp:383-386, is a temp-file name suffix).
- **The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.** The guard sits at the single parse point (`NamModelLoader.cpp:269-273`), before `LayerSpec` construction; no change was needed. The only downstream consumer, `NamModel.cpp:176`, receives dilation solely via the validated `LayerSpec` (`NamModelLoader.cpp:291`); a search over `plugins/NeuralAmp` (excluding tests) shows no second JSON path that parses dilations. The new test drives the same public entry point (`NamModel::loadFromFile`) the effect's worker uses.
- **No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.** No product check was added in this round. The existing guard rejects only non-positive dilation; the positive-dilation control in the new test (same fixture, dilation 1) loads successfully, and the full 9-test family passes. Parsing runs at model-load time, not on the audio thread; the new test touches no realtime, undo or command code.
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.** ALREADY-FIXED at `plugins/NeuralAmp/nam/NamModelLoader.cpp:269-273` (rejects `dilation <= 0` with `ParseError("invalid dilation")` before the layer is built), committed in `95dff2a47` (cluster new-p9-311), with the matching regression case `dilation-negative` at `plugins/NeuralAmp/tests/nam_harness.cpp:325` and the new in-tree regression at `tests/src/plugins/NeuralAmpModelLoadTest.cpp:290-320` — both proven FAIL without the guard and PASS with it (exit codes above).
