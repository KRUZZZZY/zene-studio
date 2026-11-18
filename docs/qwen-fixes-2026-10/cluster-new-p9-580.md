# Cluster new-p9-580

Verdict: **FIXED**

`include/base64.h:44-53` now rejects decoded byte counts that are not divisible by `sizeof(T)`, returning a null pointer and size zero before allocation or copying. The regression at `tests/src/plugins/PluginPortsMigrationTest.cpp:221` decodes `AQID` (three bytes) as `uint32_t` and checks that refusal. The inherited header change is registered at `tests/upstream-modifications.txt:33`.

The original implementation failed this case: fortified `memcpy` aborted on the three-byte copy into a zero-element allocation. The configured tree had no `WaveShaperControls` CTest match, so the regression uses the existing `PluginPortsMigrationTest` CTest family, which already includes `base64.h` through its harness.

## Evidence

The CTest family contains one registered test. It failed 1/1 before the fix and passed 1/1 after the fix. The proof run with the production patch removed reproduced the same failure, and reapplying the patch restored the pass.

| Phase | Command | Exit |
|---|---|---:|
| RED, original code | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` from `build/tests`; output in `test-1.log` | 8 |
| GREEN, fixed code | `cmake --build build --target zene PluginPortsMigrationTest PluginPortsMigrationReference waveshaper dynamicsprocessor multitapecho bitinvader freeboy watsyn vibedstrings xpressive partc_ref_waveshaper partc_ref_dynamicsprocessor partc_ref_multitapecho partc_ref_freeboy partc_ref_bitinvader partc_ref_watsyn partc_ref_xpressive partc_ref_vibedstrings`; then the same CTest command; outputs in `build-2.log` and `test-2.log` | 0, 0 |
| Reverse-patch proof | `git diff -- include/base64.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/fix.patch` | 0 |
| RED, patch removed | Same full build target command; then the same CTest command; outputs in `build-3.log` and `test-3.log` | 0, 8 |
| Restore proof patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/fix.patch` | 0 |
| GREEN, patch restored | Same full build target command; then the same CTest command; outputs in `build-4.log` and `test-4.log` | 0, 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check` (logged in `complexity-gate.log`) | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check` (logged in `file-length-gate.log`) | 0 |

## Exact command ledger

Commands were run from the worktree except CTest, which ran from `build/tests`. Each command below includes the actual output redirection and exit-code capture.

```sh
cmake --build build --target PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/build-1.log; exit "$rc"
ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/test-1.log; exit "$rc"

cmake --build build --target zene PluginPortsMigrationTest PluginPortsMigrationReference waveshaper dynamicsprocessor multitapecho bitinvader freeboy watsyn vibedstrings xpressive partc_ref_waveshaper partc_ref_dynamicsprocessor partc_ref_multitapecho partc_ref_freeboy partc_ref_bitinvader partc_ref_watsyn partc_ref_xpressive partc_ref_vibedstrings > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/build-2.log; exit "$rc"
ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/test-2.log; exit "$rc"

git diff -- include/base64.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/fix.patch
cmake --build build --target zene PluginPortsMigrationTest PluginPortsMigrationReference waveshaper dynamicsprocessor multitapecho bitinvader freeboy watsyn vibedstrings xpressive partc_ref_waveshaper partc_ref_dynamicsprocessor partc_ref_multitapecho partc_ref_freeboy partc_ref_bitinvader partc_ref_watsyn partc_ref_xpressive partc_ref_vibedstrings > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/build-3.log; exit "$rc"
ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/test-3.log; exit "$rc"
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/fix.patch

cmake --build build --target zene PluginPortsMigrationTest PluginPortsMigrationReference waveshaper dynamicsprocessor multitapecho bitinvader freeboy watsyn vibedstrings xpressive partc_ref_waveshaper partc_ref_dynamicsprocessor partc_ref_multitapecho partc_ref_freeboy partc_ref_bitinvader partc_ref_watsyn partc_ref_xpressive partc_ref_vibedstrings > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/build-4.log; exit "$rc"
ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/test-4.log; exit "$rc"

bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/complexity-gate.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/complexity-gate.log; exit "$rc"
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/file-length-gate.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs/file-length-gate.log; exit "$rc"
```

All build and CTest output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-580.r1/logs`. The `fff grep` tool was not available in this session; I searched the log folder with `rg -n -i` for `error`, `FAILED`, and `Assertion`.

Files committed:

- `include/base64.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-580.md`
