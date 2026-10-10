# Cluster new-p9-462

**Verdict: FIXED**

`TapTempo::tap()` truncated elapsed time to whole milliseconds and divided by the resulting count. A same-millisecond interval therefore set BPM to infinity. The guard at [TapTempo.cpp:74](../../plugins/TapTempo/TapTempo.cpp) rejects a zero-millisecond interval before it is stored or used for BPM; the last valid tap timestamp and tempo remain unchanged.

The regression at [AudioPluginTest.cpp:447](../../tests/src/plugins/AudioPluginTest.cpp) sets up a sub-millisecond interval and calls `TapTempo::tap(false)`. The existing plugin test target compiles the TapTempo implementation directly, configured at [tests/CMakeLists.txt:2412](../../tests/CMakeLists.txt).

The fff grep tool was not exposed in this session, so I searched the log folder with `rg` for `error`, `FAILED`, and `Assertion` as separate single-term searches.

## Regression and build evidence

| Check | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| RED on original behavior | `ctest -R AudioPluginTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 | `std::isfinite(tapTempo.m_bpm)` failed. |
| GREEN with fix | `ctest -R AudioPluginTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r1/logs/test-5.log 2>&1; echo EXIT=$?` | 0 | The affected family ran 1/1 registered CTest tests. |
| Reverse-patch RED | `ctest -R AudioPluginTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r1/logs/test-6.log 2>&1; echo EXIT=$?` | 8 | With the production guard removed and rebuilt, the same finite-BPM assertion failed. |
| Restore-patch GREEN | `ctest -R AudioPluginTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r1/logs/test-7.log 2>&1; echo EXIT=$?` | 0 | Reapplying the guard and rebuilding returned the family to 1/1 passing. |

The reversal used `git diff -- plugins/TapTempo/TapTempo.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r1/logs/fix.patch; echo EXIT=$?` (exit 0). Restoring it used `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r1/logs/fix.patch; echo EXIT=$?` (exit 0).

The rebuilt targets passed in both proof directions:

- `cmake --build build --target zene AudioPluginTest taptempo -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r1/logs/build-11.log 2>&1; echo EXIT=$?` — exit 0 before the reverse-patch RED run.
- `cmake --build build --target zene AudioPluginTest taptempo -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r1/logs/build-12.log 2>&1; echo EXIT=$?` — exit 0 after restoring the fix.

## Required gates

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r1/logs/gate-5.log 2>&1; echo EXIT=$?` — exit 0.
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-462.r1/logs/gate-6.log 2>&1; echo EXIT=$?` — exit 0.

## Files committed

- `plugins/TapTempo/TapTempo.cpp`
- `tests/CMakeLists.txt`
- `tests/src/plugins/AudioPluginTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-462.md`
