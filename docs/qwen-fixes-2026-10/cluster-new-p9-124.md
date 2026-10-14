# Cluster new-p9-124: one-frame EQ progress

**Verdict: FIXED**

`plugins/Eq/EqEffect.cpp:58-68` defines progress for zero or one frame without dividing; a single frame uses end-of-block progress (`1.0f`) so the filter transition completes. `processImpl` uses the helper at `plugins/Eq/EqEffect.cpp:187`.

`tests/src/plugins/AudioPluginTest.cpp:320-341` loads the EQ module, enables its high-pass filter, and routes one frame through `Effect::processAudioBuffer()`. It checks that output is finite and nonzero. The audio bus clears a channel containing NaN, so the unmodified code silences this one-frame signal and fails the nonzero assertion.

## Evidence

| Check | Exact command | Exit |
|---|---|---:|
| RED before fix (`test-6.log`) | `ctest -R '^AudioPluginTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/test-6.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/test-6.log; exit $rc` | 8 |
| GREEN with fix (`test-7.log`) | `ctest -R '^AudioPluginTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/test-7.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/test-7.log; exit $rc` | 0 |
| RED after reversing only production patch (`test-8.log`) | `ctest -R '^AudioPluginTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/test-8.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/test-8.log; exit $rc` | 8 |
| GREEN after reapplying patch (`test-9.log`) | `ctest -R '^AudioPluginTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/test-9.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/test-9.log; exit $rc` | 0 |
| Final verbose family run (`test-10.log`) | `ctest -R '^AudioPluginTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/test-10.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/test-10.log; exit $rc` | 0 |
| Affected test family | One CTest target, 12 QtTest cases; all passed in the final run | — |
| Complexity gate (`gate-3.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/gate-3.log; exit $rc` | 0 |
| File-length gate (`gate-4.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/gate-4.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/gate-4.log; exit $rc` | 0 |

The proof cycle used these commands, each with exit 0:

```sh
git diff -- plugins/Eq/EqEffect.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/fix.patch
cmake --build build --target zene eq AudioPluginTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/build-7.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/build-7.log; exit $rc
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/fix.patch
cmake --build build --target zene eq AudioPluginTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/build-8.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-124.r1/logs/build-8.log; exit $rc
```

The build command for both proof builds exited 0. The requested `EqEffect` and `EqControls` CTest names are not registered in this configured tree: `ctest -N -R '^(EqEffect|EqControls)$'` reported `Total Tests: 0`. I used the existing `AudioPluginTest` family instead. Earlier builds `build-1.log` and `build-2.log` exited 2 while correcting the test harness compile errors. The first three test runs (`test-3.log` through `test-5.log`) exited 0 because the initial assertion was masked by audio-bus sanitization; the final regression was tightened and then recorded RED/GREEN above.

The session did not expose an `fff grep` tool or CLI, so I searched the log folder with `rg` for `error`, `FAILED`, and `Assertion` one term at a time.

## Files committed

- `plugins/Eq/EqEffect.cpp`
- `tests/src/plugins/AudioPluginTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-124.md`
