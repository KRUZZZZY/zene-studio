# Cluster new-p9-210

Verdict: **FIXED**

`Ladspa2LMMS` now places a transfer plugin in `m_validEffects` only when its input and output channel counts match a supported layout and its LADSPA descriptor reports hard real-time capability (`src/core/Ladspa2LMMS.cpp:48-53`). The complementary invalid branch already checks the real-time flag (`:58-63`).

The regression uses SWH Decimator, a deterministic mono transfer descriptor without `HARD_RT_CAPABLE`. `PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid` requires it to be absent from the valid list and present in the invalid list (`tests/src/plugins/PluginPortsMigrationTest.cpp:221-237`). Before the fix and with the fix temporarily removed, that assertion failed because Decimator appeared in the valid list. The same test passed after the fix was applied.

## Validation evidence

The affected CTest family is `PluginPortsMigrationTest`. Each ctest invocation below selected and executed **1/1 CTest targets**.

| Phase | Exact command | Exit | Evidence |
|---|---|---:|---|
| Original-code build | `cmake --build build --target zene PluginPortsMigrationTest decimator_1202` | 0 | `build-2.log` |
| RED, original code | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (cwd `build/tests`) | 8 | `test-1.log`; the regression reported `non-real-time LADSPA plugin was classified as a valid effect` |
| Fixed-code build | `cmake --build build --target zene PluginPortsMigrationTest decimator_1202` | 0 | `build-4.log` |
| GREEN, fixed code | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (cwd `build/tests`) | 0 | `test-2.log`; 1/1 passed |
| Save and reverse only the product patch | `git diff -- src/core/Ladspa2LMMS.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-210.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-210.r1/logs/fix.patch` | 0 | `fix.patch` contains only the production predicate change |
| Reversed-code build | `cmake --build build --target zene PluginPortsMigrationTest decimator_1202` | 0 | `build-5.log` |
| RED, reversed patch | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (cwd `build/tests`) | 8 | `test-3.log`; the same regression assertion failed |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-210.r1/logs/fix.patch` | 0 | — |
| Restored-code build | `cmake --build build --target zene PluginPortsMigrationTest decimator_1202` | 0 | `build-6.log` |
| GREEN, restored patch | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (cwd `build/tests`) | 0 | `test-4.log`; 1/1 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `complexity-gate-2.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `file-length-gate-2.log` |

Build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-210.r1/logs`.

## Command notes

One intermediate build (`build-3.log`, same build command as above) exited 2 because the first edit missed a closing parenthesis. That syntax error was corrected before the fixed-code build and test recorded above. The initial build invocation logged as `build-1.log` outlived the tool's 30-second wait and its final shell status was not captured; the identical build command was rerun as `build-2.log` and exited 0. Neither invocation was used as RED/GREEN test evidence.
