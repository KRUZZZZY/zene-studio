# Cluster new-p9-251

Verdict: **FIXED**

## Finding and change

The finding is real. `src/gui/instrument/LfoGraph.cpp:167-180` set the painter to a pixel-sized font, but built `QFontMetrics` from the original font. `drawInfoText()` now assigns the adjusted font to `f`, sets that font on the painter, and measures `f` for the backing rectangle.

The render regression in `tests/src/gui/LfoGraphTest.cpp:45-92` compares the rendered graph with the expected backing fill and asserts that the fill extends beyond the original-font metrics. It failed on the original implementation at line 91 and passed with the fix. The test is registered at `tests/CMakeLists.txt:764-765` and in `tests/fork-sources.txt:2093`. The inherited source change is recorded in `tests/upstream-modifications.txt:1054`.

## Verification evidence

The affected ctest family contains one test; `ctest -R LfoGraphTest` ran **1/1**. The behavioral regression has one test slot.

| Check | Exact command | Exit | Log |
|---|---|---:|---|
| RED on original code | `ctest -R LfoGraphTest --output-on-failure` (cwd: `build/tests`) | 8 | `test-1.log` |
| GREEN with fix | `ctest -R LfoGraphTest --output-on-failure` (cwd: `build/tests`) | 0 | `test-2.log` |
| Reverse production patch | `git diff -- src/gui/instrument/LfoGraph.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-251.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-251.r1/logs/fix.patch` | 0 | `reversal-1.log` |
| RED after reversal | `ctest -R LfoGraphTest --output-on-failure` (cwd: `build/tests`) | 8 | `test-3.log` |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-251.r1/logs/fix.patch` | 0 | `reversal-2.log` |
| GREEN after reapply | `ctest -R LfoGraphTest --output-on-failure` (cwd: `build/tests`) | 0 | `test-4.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |

Build commands and exits:

| Exact command | Exit | Log / note |
|---|---:|---|
| `cmake --build build --target LfoGraphTest -j2` | 2 | `build-1.log`; target had not yet been generated from the newly added CMake entry |
| `cmake --build build --target KnobKeyboardTest -j2` | 0 | `build-2.log`; refreshed the generated test registry |
| `cmake --build build --target LfoGraphTest -j2` | 2 | `build-3.log`; initial test draft attempted to set a const speed model; corrected before the RED run |
| `cmake --build build --target LfoGraphTest -j2` | 0 | `build-4.log` |
| `cmake --build build --target zene LfoGraphTest -j2` | 0 | `build-5.log`; built fixed production code before GREEN |
| `cmake --build build --target zene LfoGraphTest -j2` | 0 | `build-6.log`; rebuilt original production code before reversal RED |
| `cmake --build build --target zene LfoGraphTest -j2` | 0 | `build-7.log`; rebuilt fixed production code before final GREEN |

All build, ctest, and gate output is under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-251.r1/logs`. The `fff grep` tool was not exposed in this session, so I searched targeted log terms with `rg -n` instead.

## Files committed

- `src/gui/instrument/LfoGraph.cpp`
- `tests/src/gui/LfoGraphTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-251.md`
