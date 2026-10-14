# Cluster new-p9-462

**Verdict: FIXED**

`TapTempo::tap()` now passes its steady-clock timestamp to `TapTempo::updateTempo()`. The helper keeps the zero-millisecond guard at [TapTempo.cpp:78](../../plugins/TapTempo/TapTempo.cpp), before interval storage, tap-count updates, or BPM calculation. A sub-millisecond tap therefore leaves the tempo and interval state unchanged.

The deterministic regression at [AudioPluginTest.cpp:447](../../tests/src/plugins/AudioPluginTest.cpp) supplies two explicit timestamps 999 μs apart and checks the BPM, tap count, stored intervals, and last-tap time. The helper declaration is at [TapTempo.h:55](../../plugins/TapTempo/TapTempo.h).

## Evidence

| Check | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Reverse only the production guard | `git diff -- plugins/TapTempo/TapTempo.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r2/logs/fix.patch && git add plugins/TapTempo/TapTempo.cpp && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r2/logs/fix.patch && rg -n -C 2 "delta == 0ms\|void TapTempo::updateTempo" plugins/TapTempo/TapTempo.cpp` | 0 | The saved diff contains only the zero-millisecond guard. |
| Rebuild with guard reversed | `cmake --build build --target zene AudioPluginTest taptempo -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r2/logs/build-4.log 2>&1; build_status=$?; echo EXIT=$build_status \| tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r2/logs/build-4.log; exit "$build_status"` | 0 | Rebuilt before RED. |
| RED with guard reversed | `cd build/tests && ctest -R '^AudioPluginTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r2/logs/test-5.log 2>&1; test_status=$?; echo EXIT=$test_status \| tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r2/logs/test-5.log; exit "$test_status"` | 8 | The tempo comparison failed; 11 Qt slots passed and 1 failed. |
| Restore guarded source | `git checkout -- plugins/TapTempo/TapTempo.cpp` | 0 | Restored the staged guarded implementation. |
| Final rebuild | `cmake --build build --target zene AudioPluginTest taptempo -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r2/logs/build-5.log 2>&1; build_status=$?; echo EXIT=$build_status \| tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r2/logs/build-5.log; exit "$build_status"` | 0 | Required targets rebuilt after checkout. |
| GREEN with guard restored | `cd build/tests && ctest -V -R '^AudioPluginTest$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r2/logs/test-6.log 2>&1; test_status=$?; echo EXIT=$test_status \| tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r2/logs/test-6.log; exit "$test_status"` | 0 | Regression slot passed; 12 Qt slots passed, with 1/1 registered CTest tests passing. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r2/logs/gate-3.log 2>&1; gate_status=$?; echo EXIT=$gate_status \| tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r2/logs/gate-3.log; exit "$gate_status"` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r2/logs/gate-4.log 2>&1; gate_status=$?; echo EXIT=$gate_status \| tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r2/logs/gate-4.log; exit "$gate_status"` | 0 | Passed. |

The initial attempt to reverse the already-committed round-1 patch used its old inline-clock context and failed to apply after extracting the helper. I then generated the guard-only `fix.patch` from the current `TapTempo.cpp` diff, successfully reversed it, and restored the staged guarded source with `git checkout`. The deterministic RED result was reproduced with that guard-only patch.

The fff grep tool was unavailable in this session. I searched the build, test, and gate logs with `rg`, one term per search (`error`, `FAILED`, and `Assertion`); the only failures were from the intentional RED run.

## Files committed

- `plugins/TapTempo/TapTempo.cpp`
- `plugins/TapTempo/TapTempo.h`
- `tests/src/plugins/AudioPluginTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-462.md`
