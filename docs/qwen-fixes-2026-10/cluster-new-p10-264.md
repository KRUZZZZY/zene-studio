# Cluster new-p10-264: Monstro LFO attack

Verdict: **FIXED**

## Evidence

- `plugins/Monstro/Monstro.cpp:790` now multiplies by `tfp + f`, so each sample in an attack period uses its own elapsed-frame position.
- `tests/src/plugins/PluginPortsMigrationTest.cpp:509` renders Monstro with a square LFO and a 20 ms attack, then compares the first period with modulation disabled. The original formula produced a zero difference; the corrected formula produces a nonzero difference.
- The migration parity check at `tests/src/plugins/PluginPortsMigrationTest.cpp:472` expects the intentional difference only inside each configured LFO attack window and still requires exact equality elsewhere.
- `tests/upstream-modifications.txt:640` records the inherited product source change and its reason.

## Evidence table

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED on original product code | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-264.r1/logs/test-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-264.r1/logs/test-1.log; exit "$result"` | 8 | `monstroLfoAttackRampsWithinPeriod` failed with `max|delta|=0`; the QTest run reported 8 passed and 1 failed. |
| RED with fix reversed | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-264.r1/logs/test-8.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-264.r1/logs/test-8.log; exit "$result"` | 8 | The rendered-buffer regression again failed with `max|delta|=0`. |
| GREEN after restoring fix | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-264.r1/logs/test-10.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-264.r1/logs/test-10.log; exit "$result"` | 0 | `PluginPortsMigrationTest` passed, 1/1 registered CTest tests. The earlier RED run reported 9 QTest outcomes. |
| Affected-family test count | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` from `build/tests` | 0 | 1/1 CTest tests executed; no zero-test run. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-264.r1/logs/gate-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-264.r1/logs/gate-3.log; exit "$result"` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-264.r1/logs/gate-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-264.r1/logs/gate-4.log; exit "$result"` | 0 | Passed. |

## Build and reverse-patch proof

Final affected-target build:

```sh
cmake --build build --target zene monstro PluginPortsMigrationTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-264.r1/logs/build-11.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-264.r1/logs/build-11.log; exit "$result"
```

Exit: 0. All numbered build logs `build-1.log` through `build-11.log` recorded exit 0.

The prescribed product-only patch check was run and restored:

```sh
git diff -- plugins/Monstro/Monstro.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-264.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-264.r1/logs/fix.patch
```

Exit: 0 (`verify-1.log`). The rebuild and RED run are recorded above. The restore command was `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-264.r1/logs/fix.patch`, exit 0 (`verify-2.log`), followed by the GREEN run above.

Intermediate comparison-window experiments are retained in `test-2.log` through `test-7.log`; the first five exited 8 while the expected attack window was being refined, and `test-7.log` exited 0. Final verification is `test-8.log` (reversed RED) and `test-10.log` (restored GREEN).

## Files committed

- `plugins/Monstro/Monstro.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-264.md`
