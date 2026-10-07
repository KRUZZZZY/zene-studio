# Cluster new-p8-43: no-op mixer strip writes

**Verdict: FIXED**

`setMany` now compares only requested mute/solo fields and returns before undo, model writes, or `setModified()` when they already match (`src/core/ControlCommandsMixerStrip.cpp:70`, `:133`, `:140`). `StripSettings` has memberwise equality and `setStrip` returns the current strip result before recording an action when unchanged (`:164`, `:252`). Regression slots cover both commands through the control registry and verify `control.undo_depth` is unchanged (`tests/src/core/MixerStripTest.cpp:206`, `:222`).

## Evidence

The first red run used the original production code. Both new slots failed because the undo depth increased. The inverse-patch red run repeated the same failure after the final fix was removed; restoring that exact patch made the same family pass.

| Phase | Exact command | Exit | Result |
|---|---|---:|---|
| Build regression against original | `cmake --build build --target zene MixerStripTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-43.r1/logs/build-2.log' 2>&1` | 0 | Test target rebuilt |
| RED, original code | `ctest -R '^MixerStripTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-43.r1/logs/test-1.log' 2>&1` (cwd: `build/tests`) | 8 | Both no-op slots failed; QtTest totals were 11 passed, 2 failed |
| Save product patch | `git diff -- src/core/ControlCommandsMixerStrip.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-43.r1/logs/fix.patch'` | 0 | Production-only patch |
| Remove product patch | `git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-43.r1/logs/fix.patch'` | 0 | Original behavior restored |
| Build for inverse proof | `cmake --build build --target zene MixerStripTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-43.r1/logs/build-7.log' 2>&1` | 0 | Rebuilt with original production code |
| RED, inverse patch | `ctest -R '^MixerStripTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-43.r1/logs/test-6.log' 2>&1` (cwd: `build/tests`) | 8 | Both no-op slots failed again; 11 passed, 2 failed |
| Restore product patch | `git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-43.r1/logs/fix.patch'` | 0 | Final fix restored |
| Build fixed code | `cmake --build build --target zene MixerStripTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-43.r1/logs/build-8.log' 2>&1` | 0 | Rebuilt `zene` and `MixerStripTest` |
| GREEN | `ctest -R '^MixerStripTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-43.r1/logs/test-7.log' 2>&1` (cwd: `build/tests`) | 0 | CTest reports 1/1 passed |

**Affected-family count:** CTest executed one registered test, `MixerStripTest` (not zero). Its red QtTest run reported 11 passing and the two new no-op regressions failing.

## Gates

Both gates passed after the report was first written (logs `gate-5.log` and `gate-6.log`). They are repeated once more after this final report edit below.

| Command | Exit |
|---|---:|
| `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-43.r1/logs/gate-5.log' 2>&1` | 0 |
| `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-43.r1/logs/gate-6.log' 2>&1` | 0 |

The first complexity check before extracting `flagsDiffer` exited 1 because `setMany` measured CCN 11. The predicate extraction brought it under the unchanged limit; the subsequent check passed. The exact initial failed commands were `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-43.r1/logs/gate-1.log' 2>&1` (exit 1) and `cmake --build build --target zene MixerStripTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-43.r1/logs/build-1.log' 2>&1` (exit 2). The build failed because the first test draft called private `Song::setModified(bool)`; that assertion was removed and the regression uses the public undo-depth command.

No `fff grep` tool or executable was available in this session, so required log-term searches used `rg` as the available fallback. The changed production and test files were already listed in `tests/fork-sources.txt`.

## Files committed

- `src/core/ControlCommandsMixerStrip.cpp`
- `tests/src/core/MixerStripTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-43.md`

Final gate rerun after this report edit: `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-43.r1/logs/gate-7.log' 2>&1` (exit 0) and `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-43.r1/logs/gate-8.log' 2>&1` (exit 0).
