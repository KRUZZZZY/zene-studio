# Cluster new-p9-51: arpeggiate output limit

**Verdict: FIXED**

`note.arpeggiate` now preflights the sum of the generated step count across all chord groups. It returns `InvalidArgs` above 10,000 output notes before creating a journal checkpoint or changing the clip. The existing step-generation behavior is unchanged at or below the limit.

## Evidence

| Location | Evidence |
| --- | --- |
| `src/core/ControlCommandsNoteTools.cpp:104-138` | `exceedsArpeggioOutputLimit` counts `ceil(max chord-note length / rate)` per chord and refuses aggregate counts over `MaxArpeggioNotes` before `addJournalCheckPoint()`. |
| `tests/src/core/NoteToolsTest.cpp:117-127` | Two separate 5,001-tick chords at rate 1 would create 10,002 notes total; the command must return `InvalidArgs` and preserve both original notes. |

The test is registered in the existing `NoteToolsTest` target. The affected family contains 23 QtTest cases and one registered CTest test. The final verbose run reports 23 passed, 0 failed; CTest reports 1/1 passed.

## Regression evidence

| Run | Command | Exit | Result |
| --- | --- | ---: | --- |
| Initial RED | `ctest -R NoteToolsTest --output-on-failure` from `build/tests` | 8 | Original long-note regression failed: actual error kind 0, expected `InvalidArgs` (22 passed, 1 failed). |
| Final aggregate RED | Same CTest command from `build/tests`, after reversing only the production patch and rebuilding | 8 | Aggregate regression failed: actual error kind 0, expected `InvalidArgs` (22 passed, 1 failed). |
| Final GREEN | Same CTest command from `build/tests`, after restoring the production patch and rebuilding | 0 | 1/1 CTest passed; final verbose confirmation: 23 passed, 0 failed. |
| Final complexity gate | `bash tests/complexity-gate.sh --check` | 0 | Pass; reports 68 functions over CCN 10 in the fork scope. |
| Final file-length gate | `bash tests/file-length-gate.sh --check` | 0 | Pass. |

## Command transcript

The following build and test commands were run with output redirected to the named new log files. Each `EXIT=` value is the child command's exit code printed by the wrapper. `ctest` commands ran from `build/tests`; the other commands ran from the worktree root.

| Command | Observed exit |
| --- | ---: |
| `cmake --build build --target NoteToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/build-1.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `ctest -R NoteToolsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/test-1.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 8 |
| `cmake --build build --target zene NoteToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/build-2.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `ctest -R NoteToolsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/test-2.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `git diff -- src/core/ControlCommandsNoteTools.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/fix.patch` | 0 |
| `cmake --build build --target zene NoteToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/build-3.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `ctest -R NoteToolsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/test-3.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 8 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/fix.patch` | 0 |
| `cmake --build build --target zene NoteToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/build-4.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `ctest -R NoteToolsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/test-4.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/gate-1.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 1 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/gate-2.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `cmake --build build --target zene NoteToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/build-5.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `ctest -R NoteToolsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/test-5.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `git diff -- src/core/ControlCommandsNoteTools.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/fix.patch` | 0 |
| `cmake --build build --target zene NoteToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/build-6.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `ctest -R NoteToolsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/test-6.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 8 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/fix.patch` | 0 |
| `cmake --build build --target zene NoteToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/build-7.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `ctest -R NoteToolsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/test-7.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/gate-3.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/gate-4.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `ctest -R NoteToolsTest --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/test-8.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/fix.patch` | 0 |
| `cmake --build build --target zene NoteToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/build-8.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `ctest -R NoteToolsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/test-9.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 8 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/fix.patch` | 0 |
| `cmake --build build --target zene NoteToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/build-9.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `ctest -R NoteToolsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/test-10.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `ctest -R NoteToolsTest --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/test-11.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/gate-5.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-51.r1/logs/gate-6.log 2>&1; code=$?; echo EXIT=$code; exit 0` | 0 |
| `git diff --check` | 0 |

The first complexity-gate run returned 1 because the inline preflight made `arpeggiate` CCN 11. The count was moved into `exceedsArpeggioOutputLimit`; the final gate run returned 0 without changing any gate or baseline.
