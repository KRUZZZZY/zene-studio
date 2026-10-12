# Cluster new-p10-179 — FIXED

## Finding and evidence

**Verdict: FIXED.** The old state at `src/core/InstrumentLayers.cpp:315` stored one skip bit per key, so a later same-key note-on replaced the earlier routing result. The new state in `include/InstrumentLayers.h:163-164` counts accepted and skipped note-ons per key. `src/core/InstrumentLayers.cpp:324-330` consumes an accepted count first and suppresses note-offs while skipped counts remain. This uses atomics and fixed storage on the event path.

The regression at `tests/src/core/InstrumentLayersCommandsTest.cpp:128-169` routes the same key through base-zone velocities that are accepted and skipped, in both note-on orders. It verifies the base receives exactly one of the two note-offs.

## RED/GREEN evidence

The first valid RED run used the original production code. It failed at `acceptedOff` because the skipped note-on overwrote the accepted note's state.

| Check | Exact command | Exit |
|---|---|---:|
| RED, original code (`test-5.log`) | `ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r1/logs/test-5.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 8 |
| RED, production patch removed (`test-10.log`) | `ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r1/logs/test-10.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 8 |
| GREEN, patch restored (`test-11.log`) | `ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r1/logs/test-11.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |

The final run executed 1/1 CTest family test. QtTest reported 7 passed, 0 failed, 1 skipped; the skipped slot requires the unavailable CLAP multi-output fixture. The regression slot itself passed.

## Build, proof, and gates

| Purpose | Exact command | Exit |
|---|---|---:|
| Build the test instrument fixture (`build-3.log`) | `cmake --build build --target tripleoscillator > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r1/logs/build-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Build the regression before the original-code RED (`build-4.log`) | `cmake --build build --target InstrumentLayersCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r1/logs/build-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Save the final production patch | `git diff -- include/InstrumentLayers.h src/core/InstrumentLayers.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Remove the production patch for RED | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Rebuild `zene` and the test with the patch removed (`build-9.log`) | `cmake --build build --target zene InstrumentLayersCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r1/logs/build-9.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Restore the production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Rebuild `zene` and the test after restoration (`build-10.log`) | `cmake --build build --target zene InstrumentLayersCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r1/logs/build-10.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r1/logs/gate-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r1/logs/gate-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"` | 0 |

The unrestricted `cmake --build build` attempt exited 97 because the worktree build guard permits named targets only. The named `zene` and `InstrumentLayersCommandsTest` targets both rebuilt successfully with the production patch removed and restored. The first test attempts also exposed a skipped plugin fixture and an inactive-rack test setup; those runs were not counted as RED evidence. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-179.r1/logs`.

The `fff` grep tool was not available in this session; build, test, and gate logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `include/InstrumentLayers.h`
- `src/core/InstrumentLayers.cpp`
- `tests/src/core/InstrumentLayersCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-179.md`

The three C++ files were already registered in `tests/fork-sources.txt`; no source-list change was needed.
