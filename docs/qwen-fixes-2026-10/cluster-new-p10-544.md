# Cluster new-p10-544

Verdict: **FIXED**

`src/core/ChordProgression.cpp:431-432` previously converted a zero room into a one-tick span. `layOutChord` now returns before layout when `room <= 0`. The regression at `tests/src/core/ChordProgressionTest.cpp:457-459` calls the block layout with a zero room and asserts that it emits no notes.

## Evidence

| Check | Command | Exit |
|---|---|---:|
| Original-code RED build | `cmake --build build --target ChordProgressionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/build-1.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/build-1.log; exit $status` | 0 |
| Original-code RED | `ctest -R '^ChordProgressionTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/test-1.log; exit $status` | 8 |
| Fixed GREEN build | `cmake --build build --target ChordProgressionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/build-2.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/build-2.log; exit $status` | 0 |
| Fixed GREEN | `ctest -R '^ChordProgressionTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/test-2.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/test-2.log; exit $status` | 0 |
| Remove only production fix | `git diff -- src/core/ChordProgression.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/fix.patch` | 0 |
| Reversed-fix build | `cmake --build build --target zene ChordProgressionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/build-3.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/build-3.log; exit $status` | 0 |
| Reversed-fix RED | `ctest -R '^ChordProgressionTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/test-3.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/test-3.log; exit $status` | 8 |
| Restore production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/fix.patch` | 0 |
| Restored-fix build | `cmake --build build --target zene ChordProgressionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/build-4.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/build-4.log; exit $status` | 0 |
| Restored-fix GREEN | `ctest -R '^ChordProgressionTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/test-4.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/test-4.log; exit $status` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/gate-1.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/gate-1.log; exit $status` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/gate-2.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-544.r1/logs/gate-2.log; exit $status` | 0 |

The RED assertion was `notes.empty()` returning false with the message “a zero-room chord produced notes”; the same assertion failed after reversing the product patch and passed after restoring it. The affected CTest family executed **1 test** (`ChordProgressionTest`): 1/1 passed after the fix.

## Files in the commit

- `src/core/ChordProgression.cpp`
- `tests/src/core/ChordProgressionTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-544.md`
