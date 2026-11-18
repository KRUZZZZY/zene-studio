# Cluster new-p10-159

**Verdict: FIXED**

`plugins/Gate/Gate.cpp:121` allowed a nonfinite key peak into the envelope follower. The persistent state failure is real: with the original code, sustained finite input after a NaN remains attenuated after the hold expires. The in-place mixer path sanitizes the program block containing the NaN, so the patrol's specific claim that later rendered samples are NaN was not reproduced; the measured later output was finite but stuck at `0.0256194` per channel.

`plugins/Gate/Gate.cpp:122-127` now resets the envelope, gain, and hold state when the peak is nonfinite. Normal finite peaks continue through the existing follower and gain path at `:130-138`. `tests/src/plugins/AudioPluginTest.cpp:445-480` loads the real Gate module, sends one NaN sample, then renders finite 64-frame blocks for half a second and checks that the Gate has reopened.

## Verification evidence

| Check | Exact command | Result |
|---|---|---|
| RED, final patch removed | `cd build/tests && ctest -R '^AudioPluginTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-159.r1/logs/test-11.log 2>&1; echo EXIT=$?` | `EXIT=8`; regression failed at `0.0256194` L/R; 11 other Qt cases passed. |
| GREEN, final patch reapplied | `cd build/tests && ctest -R '^AudioPluginTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-159.r1/logs/test-12.log 2>&1; echo EXIT=$?` | `EXIT=0`; `1/1` CTest passed. |
| Affected family count | `ctest -R '^AudioPluginTest$'` | One CTest executable; 12 Qt cases total (the RED run reported 11 passed and the regression failed). |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-159.r1/logs/gate-3.log 2>&1; echo EXIT=$?` | `EXIT=0`. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-159.r1/logs/gate-4.log 2>&1; echo EXIT=$?` | `EXIT=0`. |

The reversible proof used these exact commands and results:

```text
git diff -- plugins/Gate/Gate.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-159.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-159.r1/logs/fix.patch; echo EXIT=$?
EXIT=0
cmake --build build --target zene gate AudioPluginTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-159.r1/logs/build-11.log 2>&1; echo EXIT=$?
EXIT=0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-159.r1/logs/fix.patch; echo EXIT=$?
EXIT=0
cmake --build build --target zene gate AudioPluginTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-159.r1/logs/build-12.log 2>&1; echo EXIT=$?
EXIT=0
```

`test-11.log` and `test-12.log` above are the matching RED and GREEN runs after those builds. Both builds were searched for `error`, `FAILED`, and `Assertion`; none matched.

Earlier harness probes were not used as proof: the first `AudioBuffer` probe segfaulted (`test-1.log`, exit 8), a 64-frame probe passed before the fix because it ended within the default hold (`test-2.log`, exit 0), and a single oversized bus block was cleared by router sanitization (`test-3.log` through `test-5.log`). The final regression uses ordinary 64-frame blocks so the state is observed across later finite input.

## Files in the cluster commit

- `plugins/Gate/Gate.cpp`
- `tests/src/plugins/AudioPluginTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-159.md`
