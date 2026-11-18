# Cluster new-p10-155 — cue names for invalid indices

**Verdict: FIXED**

`cueName()` clamped the cue index indirectly for the master lookup, but used the caller's index to form its fallback label. It now clamps once and uses that index for both operations. The regression at `tests/src/core/HardwareIoTest.cpp:207-222` checks negative and high indices while restoring the cue master state. Production evidence is at `src/core/HardwareIo.cpp:196-200`.

## Evidence

| Requirement | Exact command or result | Exit / count |
|---|---|---:|
| RED, before the fix (run from `build/tests`) | `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/test-1.log 2>&1; rc=$?; printf '\nEXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/test-1.log; echo EXIT=$rc; exit "$rc"` | 8; expected `Cue A`, actual `Cue @` |
| GREEN, after the fix (run from `build/tests`) | `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/test-2.log 2>&1; rc=$?; printf '\nEXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/test-2.log; echo EXIT=$rc; exit "$rc"` | 0 |
| Affected family | `HardwareIoTest` (CTest #138) | 1/1 CTest tests executed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 |
| Explicit committed files | `src/core/HardwareIo.cpp`; `tests/src/core/HardwareIoTest.cpp`; `docs/qwen-fixes-2026-10/cluster-new-p10-155.md` | — |

The RED assertion is `HardwareIoTest::invalidCueNumbersUseClampedDefaultNames()`. Reversing only the production patch reproduced the same failure; restoring it passed. The proof logs are `test-3.log` (exit 8) and `test-4.log` (exit 0). All build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/`.

## Command ledger

Each command below was run from the repository root unless a working directory is stated. Build commands rebuilt `zene` and `HardwareIoTest` together for the fixed and reverse-patch proof runs.

| Step | Exact command | Exit |
|---|---|---:|
| Initial regression build | `cmake --build build --target HardwareIoTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/build-1.log 2>&1; rc=$?; printf '\nEXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/build-1.log; echo EXIT=$rc; exit "$rc"` | 0 |
| Fixed build | `cmake --build build --target zene HardwareIoTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/build-2.log 2>&1; rc=$?; printf '\nEXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/build-2.log; echo EXIT=$rc; exit "$rc"` | 0 |
| Reverse production patch | `git diff -- src/core/HardwareIo.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/fix.patch` | 0 |
| Reverse-patch build | `cmake --build build --target zene HardwareIoTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/build-3.log 2>&1; rc=$?; printf '\nEXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/build-3.log; echo EXIT=$rc; exit "$rc"` | 0 |
| Reverse-patch test, from `build/tests` | `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/test-3.log 2>&1; rc=$?; printf '\nEXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/test-3.log; echo EXIT=$rc; exit "$rc"` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/fix.patch` | 0 |
| Restored build | `cmake --build build --target zene HardwareIoTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/build-4.log 2>&1; rc=$?; printf '\nEXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/build-4.log; echo EXIT=$rc; exit "$rc"` | 0 |
| Restored test, from `build/tests` | `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/test-4.log 2>&1; rc=$?; printf '\nEXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/test-4.log; echo EXIT=$rc; exit "$rc"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/gate-1.log 2>&1; rc=$?; printf '\nEXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/gate-1.log; echo EXIT=$rc; exit "$rc"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/gate-2.log 2>&1; rc=$?; printf '\nEXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-155.r1/logs/gate-2.log; echo EXIT=$rc; exit "$rc"` | 0 |
