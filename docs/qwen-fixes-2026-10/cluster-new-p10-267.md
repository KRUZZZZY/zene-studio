# Cluster new-p10-267: malformed note recurrence mask

**Verdict: FIXED**

`NoteConditions::load` previously converted an invalid `recm` value to zero while retaining a positive recurrence length. That made `recursOnPass()` false for every pass. The loader now checks the conversion result and clears the recurrence pair when its mask is malformed, leaving the note on every pass. Valid numeric masks keep their existing clamping behavior.

## File evidence

| Location | Evidence |
| --- | --- |
| `src/core/NoteConditions.cpp:64-75` | Parses `recm` with a conversion-status flag; clears both recurrence fields if the positive-length pair is invalid. |
| `tests/src/core/NotePlaybackTest.cpp:168-180` | Loads `recl="2" recm="broken"` and checks neutral recurrence plus playback eligibility on passes 0 and 1. |

## Build and regression evidence

The affected CTest family contains one registered test, `NotePlaybackTest` (1/1 executed). The new case failed against the original production code and failed again when the fix was temporarily removed.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| Original code build | `cmake --build build --target zene NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/build-1.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/build-1.log; echo EXIT=$status` | 0 |
| Original code RED | `cd build/tests && ctest -R '^NotePlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/test-1.log; echo EXIT=$status` | 8 |
| Fixed code build | `cmake --build build --target zene NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/build-2.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/build-2.log; echo EXIT=$status` | 0 |
| Fixed code GREEN | `cd build/tests && ctest -R '^NotePlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/test-2.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/test-2.log; echo EXIT=$status` | 0 |
| Save and reverse production patch | `git diff -- src/core/NoteConditions.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/fix.patch` | 0 |
| Removed-fix build | `cmake --build build --target zene NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/build-3.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/build-3.log; echo EXIT=$status` | 0 |
| Removed-fix RED | `cd build/tests && ctest -R '^NotePlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/test-3.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/test-3.log; echo EXIT=$status` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/fix.patch` | 0 |
| Restored code build | `cmake --build build --target zene NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/build-4.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/build-4.log; echo EXIT=$status` | 0 |
| Restored code GREEN | `cd build/tests && ctest -R '^NotePlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/test-4.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/test-4.log; echo EXIT=$status` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/gate-1.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/gate-1.log; echo EXIT=$status` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/gate-2.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-267.r1/logs/gate-2.log; echo EXIT=$status` | 0 |

All build, test and gate output is saved in the numbered log files above. No fff grep tool was exposed in this session, so I searched each log with `rg`.

## Files committed

- `src/core/NoteConditions.cpp`
- `tests/src/core/NotePlaybackTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-267.md`
