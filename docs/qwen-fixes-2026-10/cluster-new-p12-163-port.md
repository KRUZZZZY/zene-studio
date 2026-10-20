# Cluster new-p12-163-port

Verdict: **ALREADY-FIXED**.

The reported bug ("NAM loader accepts negative dilation", historical `NamModelLoader.cpp:270`) is already fixed on this branch, with a regression test for the exact finding scenario already registered in CTest. The patrol's line numbers pre-date commit `95dff2a47`; no code change is needed in this round.

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
- Regression test for the exact scenario: `plugins/NeuralAmp/tests/nam_harness.cpp:325` — `{"dilation-negative", 1, 1, 1, -1, 3, 1, true}` (kernel size 3, dilation -1, expects rejection). Registered as CTest `NamLoaderTest_dilation-negative` at `plugins/NeuralAmp/tests/CMakeLists.txt:59,68-70` and mirrored into the main tree at `tests/CMakeLists.txt:5689-5695` (present in `build/tests/CTestTestfile.cmake:1179`).
- Sibling/caller audit: the only downstream consumer of dilation is `plugins/NeuralAmp/nam/NamModel.cpp:176` (`const int D = layer.dilation;`), fed exclusively from `LayerSpec.dilation` assigned in the validated loop (`NamModelLoader.cpp:291`); `rg dilation plugins/NeuralAmp --glob '!tests/**'` shows no other `.nam` parsing path. Nothing else in the plugin parses `dilations` from JSON.

## RED / GREEN proof

The check is already committed, so the "revert" step (procedure 4b) was done by temporarily deleting the 4-line guard at `NamModelLoader.cpp:270-273` (an edit equivalent to reversing the fix hunk), rebuilding `nam_harness`, running the test, then restoring the file and verifying `git diff` was clean.

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-163-port.r2/logs`.

| Step | Command (cwd = worktree root unless noted) | EXIT |
|---|---|---|
| Build (baseline) | `cmake --build build --target nam_harness > LOGS/build-1.log 2>&1` | 0 |
| GREEN baseline | `ctest -R NamLoaderTest --output-on-failure > LOGS/test-green-1.log 2>&1` (cwd `build/tests`) | 0 — 8/8 passed |
| Revert fix (temp) | delete `if (dilation <= 0) { throw ParseError("invalid dilation"); }` from `NamModelLoader.cpp` | — |
| Build (reverted) | `cmake --build build --target nam_harness > LOGS/build-2-red.log 2>&1` | 0 |
| RED | `ctest -R NamLoaderTest_dilation-negative --output-on-failure > LOGS/test-red-1.log 2>&1` (cwd `build/tests`) | 8 — FAIL: "unexpected loader result for config dilation-negative" (loader accepted dilation -1) |
| Restore fix | re-insert the guard; `git diff --stat` → empty (tree identical to HEAD) | 0 |
| Build (restored) | `cmake --build build --target nam_harness > LOGS/build-3-green.log 2>&1` | 0 |
| GREEN restored | `ctest -R NamLoaderTest --output-on-failure > LOGS/test-green-2.log 2>&1` (cwd `build/tests`) | 0 — 8/8 passed |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gates-1.log 2>&1` | 0 — RESULT: PASS (all 8 gates PASS) |

Affected-family test count: **8 CTest tests** (`NamLoaderTest_valid`, `dilation-zero`, `dilation-negative`, `dilation-overflow`, `receptive-total-overflow`, `channels-zero`, `bottleneck-zero`, `head-size-zero`), all passing with the fix in place.

Honesty note: the first RED attempt ran `cmake --build build` with cwd `build/tests`, so the build failed silently and ctest ran the stale (fixed) binary and passed; it was redone from the worktree root, rebuilt, and only then did the test fail as recorded above. The final logs reflect the corrected runs.

Log search: `fff grep` (path = LOG FOLDER) for `error`, `FAILED`, `Assertion` — `test-red-1.log` contains the expected `FAIL: unexpected loader result` and `tests FAILED` summary; the build and green-test logs contain no `error`/`Assertion` matches.

## Collateral damage

None: no product file was changed in this round (the guard and test were already committed in `95dff2a47`). The guard only rejects `dilation <= 0`, which cannot occur in a valid `.nam` model (the format's dilations are positive receptive-field strides); `NamLoaderTest_valid` and the full 8-test family still pass. Old projects and saved data load exactly as before.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p12-163-port.md` (this report; no product or test changes needed)

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).** Yes: with the 4-line dilation guard temporarily removed and `nam_harness` rebuilt, `ctest -R NamLoaderTest_dilation-negative` exited 8 with "FAIL: unexpected loader result for config dilation-negative" (LOGS/test-red-1.log); with the guard restored and rebuilt, `ctest -R NamLoaderTest` exited 0 with 8/8 passed (LOGS/test-green-2.log).
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).** Yes: the harness writes a fixture `.nam` JSON with fixed injected values (channels 1, bottleneck 1, head 1, kernel 3, dilation -1, 1 layer, weights 0.1 — `nam_harness.cpp:325,351-373`) and asserts only on `NamModel::loadFromFile` accept/reject. The sole `steady_clock` use (`nam_harness.cpp:383-386`) generates a unique temp-file suffix, not any assertion.
- **The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.** No change was needed: the guard sits at the single parse point (`NamModelLoader.cpp:269-273`), before `LayerSpec` construction. The only downstream consumer, `NamModel.cpp:176`, receives dilation solely via the validated `LayerSpec` (`NamModelLoader.cpp:291`); a repo search over `plugins/NeuralAmp` shows no second JSON path that parses dilations.
- **No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.** No check was added in this round. The existing guard rejects only non-positive dilation; all positive dilations load unchanged (`NamLoaderTest_valid` and the overflow family pass; 8/8 green). Parsing runs at model-load time, not on the audio thread, so realtime behaviour is untouched.
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.** ALREADY-FIXED at `plugins/NeuralAmp/nam/NamModelLoader.cpp:269-273` (rejects `dilation <= 0` with `ParseError("invalid dilation")` before the layer is built), committed in `95dff2a47` (cluster new-p9-311), with the matching regression case `dilation-negative` at `plugins/NeuralAmp/tests/nam_harness.cpp:325` registered as CTest `NamLoaderTest_dilation-negative` — proven both ways above (FAIL without the guard, PASS with it).
