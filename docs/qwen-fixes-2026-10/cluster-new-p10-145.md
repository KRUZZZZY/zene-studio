# Cluster new-p10-145: DualFilter cache initialization

**Verdict: BLOCKED.** The defect is fixed and the regression passes with the fix. The required ratchet gates still fail on unrelated files already present on this branch; their baselines were not changed.

## Evidence

`DualFilterEffect::processImpl` compares each cached cutoff and resonance before checking `m_filter1changed` / `m_filter2changed` at `plugins/DualFilter/DualFilter.cpp:151-152` and `175-176`. The constructor initializes those flags to `true`, but that does not short-circuit the preceding comparisons. The four cache members now start at `0.0f` in `plugins/DualFilter/DualFilter.h:63-66`.

The regression at `tests/src/plugins/PluginPortsMigrationTest.cpp:1012` poisons the newly allocated effect storage, then checks the object representation of all four cache fields. With the product patch reversed, the targeted test fails at the first cache assertion; with the patch restored, it passes.

The affected CTest family is `PluginPortsMigrationTest`: the final `ctest` run executed 1 registered test and passed. The targeted QtTest invocation reported 3 passed (test initialization, the regression slot, and cleanup).

## Commands and results

All build and test output was redirected to new files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-145.r1/logs`.

| Phase | Command | Exit |
|---|---|---:|
| Original-code build | `cmake --build build --target zene dualfilter PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-145.r1/logs/build-1-red.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| Initial RED family run | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-145.r1/logs/test-1-red.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 8 |
| Fixed build | `cmake --build build --target zene dualfilter PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-145.r1/logs/build-2-green.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| Build missing LADSPA test fixture | `cmake --build build --target decimator_1202 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-145.r1/logs/build-3-fixture.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| Targeted GREEN run | `./PluginPortsMigrationTest dualFilterCutoffCachesStartInitialized > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-145.r1/logs/test-2-green.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| Family GREEN run | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-145.r1/logs/test-3-family-green.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| Save product-only patch | `git diff -- plugins/DualFilter/DualFilter.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-145.r1/logs/fix.patch` | 0 |
| Reverse patch for proof | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-145.r1/logs/fix.patch` | 0 |
| Reverted-code build | `cmake --build build --target zene dualfilter PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-145.r1/logs/build-4-proof-red.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| Required RED proof, patch reversed | `./PluginPortsMigrationTest dualFilterCutoffCachesStartInitialized > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-145.r1/logs/test-4-proof-red.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 1 |
| Restore patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-145.r1/logs/fix.patch` | 0 |
| Restored-code build | `cmake --build build --target zene dualfilter PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-145.r1/logs/build-5-proof-green.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| Required GREEN proof, patch restored | `./PluginPortsMigrationTest dualFilterCutoffCachesStartInitialized > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-145.r1/logs/test-5-proof-green.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| Final family run | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-145.r1/logs/test-6-family-final.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-145.r1/logs/gate-1-complexity.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-145.r1/logs/gate-2-file-length.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 1 |

The initial RED family run reported the intended cache assertion failure and also lacked the SWH Decimator fixture. After building the existing `decimator_1202` target, the full family passed. The ratchets report six unrelated over-limit functions and fourteen unrelated new files over 500 lines; neither report names a changed DualFilter or test file. No gate baseline or unrelated code was changed.

## Files in this change

- `plugins/DualFilter/DualFilter.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-145.md`
