# Cluster new-p10-281

Verdict: **FIXED**

## Evidence

- `plugins/Organic/Organic.cpp:288-289` writes the oscillator samples beginning at `offset`. The effect loop at `plugins/Organic/Organic.cpp:296-303` now also begins at `offset` and ends at `offset + frames`, so it leaves samples already written earlier in the period untouched.
- `tests/src/plugins/PluginPortsMigrationTest.cpp:492-538` adds `organicDistortionLeavesPreOffsetFramesUnchanged`. It enables distortion, renders a note at half-period offset into a prefilled buffer, and checks both channels before the offset.
- RED: before the fix, that assertion failed. Reversing the production patch made the same family fail again, proving the regression catches the reported behavior.
- Affected family: `PluginPortsMigrationTest`, one registered CTest test (CTest #313) per run. The RED QtTest summary was 8 passed and 1 failed; the green CTest result was 1/1 passed.
- The inherited production source is registered in `tests/upstream-modifications.txt` with the reason for the behavior change.

## Exact commands and results

Build and test output was redirected to the named new log file, with the command's exit code appended as `EXIT=<code>`.

| Phase | Command | EXIT | Log |
|---|---|---:|---|
| Initial RED build | `cmake --build build --target zene PluginPortsMigrationTest` | 0 | `build-1.log` |
| Initial RED test | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8 | `test-1.log` |
| Fixed GREEN build | `cmake --build build --target zene PluginPortsMigrationTest` | 0 | `build-2.log` |
| Fixed GREEN test | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 0 | `test-2.log` |
| Reverse production patch | `git diff -- plugins/Organic/Organic.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-281.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-281.r1/logs/fix.patch` | 0 | — |
| No-fix proof build | `cmake --build build --target zene PluginPortsMigrationTest` | 0 | `build-3.log` |
| No-fix proof test | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8 | `test-3.log` |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-281.r1/logs/fix.patch` | 0 | — |
| Restored GREEN build | `cmake --build build --target zene PluginPortsMigrationTest` | 0 | `build-4.log` |
| Restored GREEN test | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 0 | `test-4.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `complexity-gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `file-length-gate-1.log` |

## Files committed

- `plugins/Organic/Organic.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-281.md`
