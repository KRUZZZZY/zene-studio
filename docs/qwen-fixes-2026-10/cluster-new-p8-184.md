# Cluster new-p8-184 — FIXED

The finding is real. `findStartTagEnd()` in `src/core/ProjectContainerEntries.cpp:213` now scans the content start tag while tracking single- and double-quoted attribute values. `walkToContentStart()` uses that boundary at line 273; the existing self-closing check at line 274 now detects `/>` even when an attribute contains `>`, and refuses to splice section bodies outside the content element.

The regression at `tests/src/core/ProjectContainerEntriesTest.cpp:330` passes a self-closing `<song marker="left>right"/>` skeleton with an index row and section body to `reassembleContainerDocument()`. It checks refusal, the empty-element error, and that the destination remains untouched. Before the fix, reassembly incorrectly returned success.

Affected family: ctest executed 1/1 target, `ProjectContainerEntriesTest` (CTest #59). The test source has 12 test slots. In the RED run, QtTest reported 13 passed and this new slot failed; the restored-fix run passed the complete ctest target.

## Evidence

Commands below include their actual log destinations. Build and gate commands ran from the worktree root; ctest commands ran from `build/tests`.

| Evidence | Exact command | EXIT |
|---|---|---:|
| Initial regression build | `cmake --build build --target ProjectContainerEntriesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-184.r1/logs/build-1-red.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 0 |
| Initial RED | `ctest -R '^ProjectContainerEntriesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-184.r1/logs/test-1-red.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 8 |
| Reverse final product patch for proof | `git diff -- src/core/ProjectContainerEntries.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-184.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-184.r1/logs/fix.patch` | 0 |
| Rebuild without fix | `cmake --build build --target zene ProjectContainerEntriesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-184.r1/logs/build-6-final-proof-red.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 0 |
| Proof RED | `ctest -R '^ProjectContainerEntriesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-184.r1/logs/test-6-final-proof-red.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 8 |
| Restore final product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-184.r1/logs/fix.patch` | 0 |
| Rebuild with fix | `cmake --build build --target zene ProjectContainerEntriesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-184.r1/logs/build-7-final-proof-green.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 0 |
| Proof GREEN | `ctest -R '^ProjectContainerEntriesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-184.r1/logs/test-7-final-proof-green.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 0 |
| Complexity gate, final | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-184.r1/logs/gate-3-final-complexity.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 0 |
| File-length gate, final | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-184.r1/logs/gate-4-final-file-length.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 0 |

The first complexity-gate attempt reported `walkToContentStart()` at CCN 13 (exit 1). Extracting the scan into `findStartTagEnd()` brought the final complexity gate to exit 0. The final file-length gate also exited 0.

## Files committed

- `src/core/ProjectContainerEntries.cpp`
- `tests/src/core/ProjectContainerEntriesTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-184.md`
