# Cluster new-p9-247: LFO constructor state

**Verdict: FIXED**

`Lfo` left its frequency, phase, and increment indeterminate. `setFrequency()` and `tick()` read that state, so a first tick could be non-finite. The constructor now initializes all five members in declaration order and keeps the existing sample-rate calculation (`plugins/Delay/Lfo.cpp:34-41`). The regression poisons the allocated storage, sets a valid frequency, then asserts that the first sample is finite and exactly zero (`tests/src/plugins/PluginPortsMigrationTest.cpp:226-243`).

## Evidence

All build commands ran from the worktree root. All CTest commands ran from `build/tests`. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-247.r1/logs/`.

| Stage | Exact command | Log | Exit | Result |
|---|---|---|---:|---|
| Build regression without fix | `cmake --build build --target zene PluginPortsMigrationTest` | `build-5.log` | 0 | `zene` and the test target rebuilt |
| RED | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | `test-5.log` | 8 | 1/1 CTest test ran; `lfoStartsFromInitializedState()` failed at `std::isfinite(firstOutput)` |
| Build with fix | `cmake --build build --target zene PluginPortsMigrationTest` | `build-6.log` | 0 | Rebuilt `zene` and the test target |
| GREEN | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | `test-6.log` | 0 | 1/1 CTest test passed |
| Verbose green count | `ctest -R '^PluginPortsMigrationTest$' -V` | `test-9.log` | 0 | QtTest total: 9 passed, 0 failed, 0 skipped |
| Reverse-apply fix | `git diff -- plugins/Delay/Lfo.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-247.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-247.r1/logs/fix.patch` | `proof-1.log` | 0 | Removed only the production fix |
| Rebuild without fix | `cmake --build build --target zene PluginPortsMigrationTest` | `build-7.log` | 0 | Rebuilt `zene` and the test target |
| Reverse-proof RED | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | `test-7.log` | 8 | Same assertion failed again |
| Reapply fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-247.r1/logs/fix.patch` | `proof-2.log` | 0 | Restored the production fix |
| Rebuild with fix | `cmake --build build --target zene PluginPortsMigrationTest` | `build-8.log` | 0 | Rebuilt `zene` and the test target |
| Reapply-proof GREEN | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | `test-8.log` | 0 | Same CTest test passed |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | `gate-3.log` | 0 | Passed after this report was written |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | `gate-4.log` | 0 | Passed after this report was written |

The affected family ran one registered CTest test. The verbose run confirms all nine QtTest cases passed. The RED and reverse-proof RED runs each report eight passed and one failed.

## Superseded regression drafts

The first stack-storage draft made the compiler reject the test target with `-Werror=uninitialized` (`cmake --build build --target zene PluginPortsMigrationTest`, `build-1.log`, EXIT=2). A heap-backed draft passed because its poison write was optimized away (`ctest -R '^PluginPortsMigrationTest$' --output-on-failure`, `test-1.log`, EXIT=0; `./PluginPortsMigrationTest lfoStartsFromInitializedState`, `test-2.log` and `test-3.log`, EXIT=0). The test was revised to use volatile poison writes; the direct slot then observed NaN (`./PluginPortsMigrationTest lfoStartsFromInitializedState`, `test-4.log`, EXIT=1), before the final CTest RED run above.

The requested `fff grep` tool was not available in this session; the log folder was searched with `rg` using one term per search.

## Files in this commit

- `plugins/Delay/Lfo.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-247.md`
