# Cluster new-p10-464: Xpressive non-finite waveform samples

Verdict: **FIXED**

## Finding and change

`plugins/Xpressive/Xpressive.cpp:173-189` now checks each decoded sample for finiteness before copying it into its `graphModel`. NaN and either infinity are replaced with `0.0f`; the sanitized graphs are then smoothed and copied into the three `WaveSample` buffers at lines 191-196.

The regression in `tests/src/plugins/PluginPortsMigrationTest.cpp:227-271` loads the migrated Xpressive module through its plugin entry point, restores correctly sized 4096-sample payloads containing NaN, positive infinity, and negative infinity, then checks all three resulting `WaveSample` buffers. With the production patch removed, the W1 assertion fails. With the patch restored, the test passes.

There is no standalone CTest named `Xpressive` in this configured tree: `cd build/tests && ctest -N -R '^Xpressive$'` returned `Total Tests: 0`. The regression therefore runs inside the existing `PluginPortsMigrationTest` family, which exercises the actual Xpressive module. That inventory query is not counted as a test run.

## Required evidence

| Evidence | Command and result |
| --- | --- |
| RED | From `build/tests`: `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-464.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-464.r1/logs/test-4.log` — **EXIT=8**; `W1 waveform retained a saved NaN sample`. |
| GREEN | From `build/tests`: `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-464.r1/logs/test-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-464.r1/logs/test-5.log` — **EXIT=0**, 1/1 CTest tests passed. |
| Affected family count | **1** CTest test: `PluginPortsMigrationTest` (CTest test #313). The QtTest slot `xpressiveLoadedWaveSamplesAreFinite` is the regression. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-464.r1/logs/gate-1.log 2>&1` — **EXIT=0**. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-464.r1/logs/gate-2.log 2>&1` — **EXIT=0**. |
| Files committed | `plugins/Xpressive/Xpressive.cpp`; `tests/src/plugins/PluginPortsMigrationTest.cpp`; `tests/upstream-modifications.txt`; `docs/qwen-fixes-2026-10/cluster-new-p10-464.md`. |

The production source is inherited and is registered in `tests/upstream-modifications.txt:1054`. The test source is already in `tests/all-sources.txt:2662`. The upstream modifications manifest ends with a newline.

## Reversible proof

The regression was rerun against the same test source with only the production patch reversed, then with the patch restored:

```text
git diff -- plugins/Xpressive/Xpressive.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-464.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-464.r1/logs/fix.patch — EXIT=0
cmake --build build --target zene PluginPortsMigrationTest -- -j12 — build-6.log EXIT=0
ctest -R '^PluginPortsMigrationTest$' --output-on-failure — test-4.log EXIT=8 (RED)
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-464.r1/logs/fix.patch — EXIT=0
cmake --build build --target zene PluginPortsMigrationTest -- -j12 — build-7.log EXIT=0
ctest -R '^PluginPortsMigrationTest$' --output-on-failure — test-5.log EXIT=0 (GREEN)
```

All CTest commands above ran with the working directory `build/tests`. Build and test output is in the numbered files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-464.r1/logs`.

Earlier harness iterations are retained there: `build-1.log` EXIT=2 (the first draft called protected `loadSettings()`); `build-2.log` EXIT=0; `test-1.log` EXIT=8 (the first draft’s audio liveness assertion found silence and was not accepted as regression proof); `build-3.log` EXIT=2 (const accessor mismatch); `build-4.log` EXIT=0; `test-2.log` EXIT=8 (raw graph NaN detected); `build-5.log` EXIT=0; `test-3.log` EXIT=0 (raw graph check passed after the fix). The final RED/GREEN pair above checks the actual `WaveSample` buffers.

All builds used `cmake --build build --target zene PluginPortsMigrationTest -- -j12`, with each run's output redirected to its numbered `build-N.log`; exit codes were captured unpiped. The CTest command was `ctest -R '^PluginPortsMigrationTest$' --output-on-failure`, run from `build/tests`, with output redirected to the numbered `test-N.log` and exit codes captured unpiped.

| Log | Result |
| --- | --- |
| `build-1.log` | EXIT=2; first test draft called protected `loadSettings()`. |
| `build-2.log` | EXIT=0. |
| `test-1.log` | EXIT=8; first test draft's audio liveness assertion found silence. |
| `build-3.log` | EXIT=2; const accessor mismatch in the revised test. |
| `build-4.log` | EXIT=0. |
| `test-2.log` | EXIT=8; raw graph check detected the saved W1 NaN. |
| `build-5.log` | EXIT=0. |
| `test-3.log` | EXIT=0; raw graph check passed with the fix. |
| `build-6.log` | EXIT=0; rebuild after reversing the product patch. |
| `test-4.log` | EXIT=8; final WaveSample RED. |
| `build-7.log` | EXIT=0; rebuild after restoring the product patch. |
| `test-5.log` | EXIT=0; final WaveSample GREEN. |
| `gate-1.log` | EXIT=0; complexity gate. |
| `gate-2.log` | EXIT=0; file-length gate. |

The `fff grep` MCP and `fff` executable were unavailable in this session. Build, test, and gate logs were searched with `rg`, one term per query.
