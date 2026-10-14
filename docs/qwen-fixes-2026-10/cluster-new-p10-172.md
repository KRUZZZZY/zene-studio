# Cluster new-p10-172

Verdict: **FIXED**

The finding is real. `InstrumentFunctionArpeggio::processNote()` computes `range` with repeat slots included (`src/core/InstrumentFunctions.cpp:378`). For the one-pitch `octave` chord, range 1, and repeats 2, this gives `range == 2`; the up/down cycle divisor at line 447 is then `range * 2 - 2 * repeats == 0`.

At `src/core/InstrumentFunctions.cpp:440-442`, the up/down branch now runs only when `range` exceeds the repeat count. When the repeats fill the range, the initialized index remains zero, so the note stays on the single pitch and no modulo is evaluated. The regression at `tests/src/core/NotePlaybackTest.cpp:141` configures the same chord/range/repeat combination, renders a track note, and checks that the produced handles stay on the base pitch. The production file is registered at `tests/upstream-modifications.txt:1054`; the test source was already registered in `tests/fork-sources.txt`.

## Evidence

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED, final regression with the fix reverted | From `build/tests`: `ctest -R NotePlaybackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/test-4.log 2>&1` | 8 | `NotePlaybackTest` terminated with `NUMERICAL`; this is the same final test source used for GREEN. |
| GREEN, restored fix | From `build/tests`: `ctest -R NotePlaybackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/test-5.log 2>&1` | 0 | CTest reports 1/1 target passed. |
| Affected-family count | From `build/tests`: `ctest -R NotePlaybackTest --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/test-6.log 2>&1` | 0 | 11 QtTest cases passed, 0 failed; 1/1 CTest target passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/gate-1-complexity.log 2>&1` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/gate-2-file-length.log 2>&1` | 0 | Passed. |

## Build and test command trace

All build commands below ran from the worktree root. CTest commands ran from `build/tests`.

| Command | Exit | Note |
|---|---:|---|
| `cmake --build build --target zene NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/build-1.log 2>&1` | 0 | Built the initial regression. |
| `ctest -R NotePlaybackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/test-1.log 2>&1` | 8 | Initial RED; numerical exception in the arpeggio case. |
| `cmake --build build --target zene NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/build-2.log 2>&1` | 0 | Rebuilt with the first guard edit. |
| `ctest -R NotePlaybackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/test-2.log 2>&1` | 8 | The first post-fix assertion observed only one period; extended the observation window. |
| `cmake --build build --target zene NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/build-3.log 2>&1` | 0 | Rebuilt the corrected regression. |
| `ctest -R NotePlaybackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/test-3.log 2>&1` | 0 | Initial GREEN. |
| `git diff -- src/core/InstrumentFunctions.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/fix.patch` | 0 | Reverted only the production fix for the required proof. |
| `cmake --build build --target zene NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/build-4.log 2>&1` | 0 | Rebuilt the unfixed production code. |
| `ctest -R NotePlaybackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/test-4.log 2>&1` | 8 | Required final-source RED: numerical exception. |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/fix.patch` | 0 | Restored the production fix. |
| `cmake --build build --target zene NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/build-5.log 2>&1` | 0 | Rebuilt final production code and test. |
| `ctest -R NotePlaybackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/test-5.log 2>&1` | 0 | Required final-source GREEN. |
| `ctest -R NotePlaybackTest --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/test-6.log 2>&1` | 0 | Confirmed 11/11 QtTest cases. |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/gate-1-complexity.log 2>&1` | 0 | Complexity gate. |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-172.r1/logs/gate-2-file-length.log 2>&1` | 0 | File-length gate. |

The report and fix are committed together as `fix: handle repeated single-pitch up/down arpeggiation (cluster new-p10-172)`.

Files in the commit:

- `src/core/InstrumentFunctions.cpp`
- `tests/src/core/NotePlaybackTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-172.md`
