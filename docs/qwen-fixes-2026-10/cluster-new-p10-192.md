# Cluster new-p10-192 — client NaN frequency-to-note

**Verdict: BLOCKED**

The bug is fixed and the regression proves the client returned note 0 before the fix and note 60 after it. The required complexity and file-length gates still fail on unchanged files elsewhere in the branch, so the full gate requirement remains blocked.

## File evidence

- `thirdparty/mts-esp/libMTSClient.cpp:435-438`: the client scalar helper returns note 60 for NaN, matching the no-client helper at `:892-894`.
- `thirdparty/mts-esp/libMTSClient.cpp:508-514`: the note-plus-channel overload returns note 60 and sets channel 0 for NaN.
- `tests/src/core/SessionTuningTest.cpp:1258-1272`: the regression compares the registered-client scalar result with the no-client fallback and checks the channel result.

## Verification record

All CTest commands below ran from `build/tests`. The affected family contains **1 CTest test** (`SessionTuningTest`). The red runs both report the new assertion failing with `MTS_FrequencyToNote(client.get(), nan, -1)` returning `'\0'`; the green runs report 1/1 passed.

| Stage | Command | EXIT | Result |
| --- | --- | ---: | --- |
| Pre-fix build | `cmake --build build --target zene SessionTuningTest` (log `build-2.log`) | 0 | Rebuilt `zene` and `SessionTuningTest` before the baseline run. |
| RED | `ctest -R '^SessionTuningTest$' --output-on-failure` (log `test-1.log`) | 8 | 1/1 failed at the new NaN assertion; QtTest totals were 23 passed, 1 failed. |
| Initial fixed build | `cmake --build build --target zene SessionTuningTest` (log `build-3.log`) | 0 | Rebuilt both targets with the fix. |
| GREEN | `ctest -R '^SessionTuningTest$' --output-on-failure` (log `test-2.log`) | 0 | 1/1 passed. |
| Remove fix for proof | `git diff -- thirdparty/mts-esp/libMTSClient.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-192.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-192.r1/logs/fix.patch` | 0 | Reversed only the production patch. |
| Proof RED build | `cmake --build build --target zene SessionTuningTest` (log `build-4.log`) | 0 | Rebuilt both targets without the fix. |
| Proof RED | `ctest -R '^SessionTuningTest$' --output-on-failure` (log `test-3.log`) | 8 | The same assertion failed; 1/1 failed. |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-192.r1/logs/fix.patch` | 0 | Reapplied the saved production patch. |
| Proof GREEN build | `cmake --build build --target zene SessionTuningTest` (log `build-5.log`) | 0 | Rebuilt both targets with the fix restored. |
| Proof GREEN | `ctest -R '^SessionTuningTest$' --output-on-failure` (log `test-4.log`) | 0 | 1/1 passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` (log `gate-3.log`) | 1 | Reports six over-target functions in unchanged files, including `tests/src/gui/XyPadPairTest.cpp` and several `src/core`/`src/gui` files. |
| File-length gate | `bash tests/file-length-gate.sh --check` (log `gate-4.log`) | 1 | Reports fourteen existing files over 500 lines, including `src/core/SmfInterchangeReader.cpp` and `tests/src/core/NotePlaybackTest.cpp`. |

The initial test-only build attempt in `build-1.log` completed target linking, but its exec response did not expose an exit status. It is not used as verification; the later logged builds above include explicit exit markers. The `fff` grep tool was not exposed in this session, so log searches used `rg -n` against the required log folder.

## Files committed

- `thirdparty/mts-esp/libMTSClient.cpp`
- `tests/src/core/SessionTuningTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-192.md`
