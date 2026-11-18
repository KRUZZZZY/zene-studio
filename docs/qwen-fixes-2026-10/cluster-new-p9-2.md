# Cluster new-p9-2

Verdict: **FIXED**

## Evidence

- `src/core/ChordProgression.cpp:169-172`: `layOutArpeggio` now computes each voice offset in `qint64` and skips voices at or beyond the chord span before adding the offset to `pos`.
- `tests/src/core/ChordProgressionTest.cpp:237-246`: the one-tick arpeggio regression uses a valid request starting at `INT32_MAX - 1` and asserts that only the first tone is emitted inside the step.
- On the original code, this assertion failed because `notes.size()` was 2 instead of 1. Reversing only the production patch reproduced the same failure; restoring it passed.

## Commands and results

Commands below ran in the worktree unless a working directory is stated. The CTest commands ran from `build/tests`.

| Purpose | Exact command | Exit |
| --- | --- | ---: |
| Original regression build (test added, production unchanged) | `cmake --build build --target ChordProgressionTest` | 0 |
| Original RED regression | `ctest -R '^ChordProgressionTest$' --output-on-failure` | 8 |
| Initial fixed GREEN build | `cmake --build build --target ChordProgressionTest` | 0 |
| Initial fixed GREEN test | `ctest -R '^ChordProgressionTest$' --output-on-failure` | 0 |
| Create and reverse the production patch for proof | `git diff -- src/core/ChordProgression.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-2.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-2.r1/logs/fix.patch` | 0 |
| Reapply the inverse for the fresh `zene` build proof | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-2.r1/logs/fix.patch` | 0 |
| RED build, including `zene` | `cmake --build build --target zene ChordProgressionTest` | 0 |
| RED test | `ctest -R '^ChordProgressionTest$' --output-on-failure` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-2.r1/logs/fix.patch` | 0 |
| GREEN build, including `zene` | `cmake --build build --target zene ChordProgressionTest` | 0 |
| GREEN test | `ctest -R '^ChordProgressionTest$' --output-on-failure` | 0 |
| GREEN verbose count | `ctest -R '^ChordProgressionTest$' --verbose` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 |

The affected CTest family executed **1/1 target** (`ChordProgressionTest`). The verbose green run reports **14 passed, 0 failed** Qt test cases. The two gate logs report no regressions.

The initial regression run and the inverse-patch proof both failed as expected (CTest exit 8); the restored patch passed (CTest exit 0). Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-2.r1/logs/`, including `red-test-1.log`, `green-test-1.log`, `final-proof-red-test-1.log`, `final-proof-green-test-1.log`, `green-verbose-test-1.log`, `complexity-gate-1.log`, `file-length-gate-1.log`, `complexity-gate-2.log`, `file-length-gate-2.log`, `complexity-gate-3.log`, and `file-length-gate-3.log`.

The requested `fff` grep tool was not available in this session, so log checks used targeted `rg -n` searches.

## Files committed

- `src/core/ChordProgression.cpp`
- `tests/src/core/ChordProgressionTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-2.md`
