# Cluster new-p10-100-port

**Verdict: FIXED** (port of `bugs/qwen-new-p10-100` commit `7cc822c2f` onto `050/m0-train-5`). The bug was present on this line: `meterRefusal()` checked denominators only. Both ratchet gates still exit 1, but the failures are pre-existing on HEAD and this change adds none (see Gates).

## Finding

`DawProjectWrite.cpp` accepted a time-signature numerator of 0 or 33, wrote the file, and the reader then refused it. The reader accepts numerators 1–32 (`TempoMapMinNumerator`/`TempoMapMaxNumerator`, `include/TempoMap.h:48-49`).

## Change

- **`src/core/DawProjectWriteRefusal.cpp`** (new, 68 code lines): the tempo and metre refusals moved out of `DawProjectWrite.cpp` with their two predicates. `meterRefusal()` now checks the global numerator (line 85) and each metre point's numerator (line 97) against the reader's 1–32 range, before the denominator checks.
- **`src/core/DawProjectWriteRefusal.h`** (new, internal header, same convention as `DawProjectReadShared.h`): declares `tempoRefusal` and `meterRefusal`.
- **`src/core/DawProjectWrite.cpp`**: the moved blocks are removed and the header is included. It is now 457 code lines. HEAD was exactly 500, the gate cap, so the numerator checks could not sit inline without going over. This split is the seam the gate's own documentation describes, not a re-anchored ratchet.
- **`include/DawProjectInterchange.h`**: the `dawProjectXmlFromModel` doc comment names the numerator range (line 518).
- **`tests/src/core/DawProjectInterchangeRoundTripTest.cpp`**: new slot `unsupportedTimeSignatureNumeratorsAreRefusedByTheWriter` (line 1952). It checks numerators 0 and 33 are refused for the global signature and for a metre point, that the error names "numerator", and that 1 and 32 still write.
- **Registration**: `src/core/CMakeLists.txt` (`core/DawProjectWriteRefusal.cpp` after `DawProjectWrite.cpp`), `tests/fork-sources.txt` and `tests/all-sources.txt` (both new files), and a `tests/upstream-modifications.txt` row for the inherited `src/core/CMakeLists.txt`.

No control command was added.

## Evidence: RED / GREEN

RED is the test on the unfixed writer. Both proof passes were done on the final layout (the split), and again on the first inline-fix layout.

| Step | Command (run from the worktree, or `build/tests` for ctest) | Exit | Result |
|---|---|---:|---|
| Build test, first try | `cmake --build build --target DawProjectInterchangeRoundTripTest -j2 > …/build-1-red.log 2>&1; echo EXIT=$?` | 2 | Not a code error: ccache could not write its temp files under `/run/user/1000` (write-jail). |
| Build test, ccache temp in /tmp | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p10-100 cmake --build build --target DawProjectInterchangeRoundTripTest -j2 > …/build-2-red.log 2>&1; echo EXIT=$?` | 0 | Built. |
| **RED** (unfixed) | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > …/test-1-red.log 2>&1; echo EXIT=$?` | **8** | `FAIL! : …unsupportedTimeSignatureNumeratorsAreRefusedByTheWriter() '!dawProjectXmlFromModel(global, &xml, &error)' returned FALSE. (global time-signature numerator 0 was written)`; 54 passed, 1 failed. |
| Fix applied (inline), build | `CCACHE_TEMPDIR=… cmake --build build --target zene DawProjectInterchangeRoundTripTest -j2 > …/build-3-green.log 2>&1; echo EXIT=$?` | 0 | Built. |
| **GREEN** (inline fix) | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > …/test-2-green.log 2>&1; echo EXIT=$?` | **0** | 100% passed, 1/1. |
| Proof, revert (inline fix) | `git diff -- <product files> > …/fix.patch && git apply -R …/fix.patch`; build; ctest | build 0; **8** | Same numerator assertion as RED. |
| Proof, re-apply (inline fix) | `git apply …/fix.patch`; build; ctest | build 0; **0** | Passed. |
| Split: build | `CCACHE_TEMPDIR=… cmake --build build --target zene DawProjectInterchangeRoundTripTest DawProjectDeflateImportTest -j2 > …/build-7-split.log 2>&1; echo EXIT=$?` | 0 | Built (CMake re-ran automatically for the new source). |
| Split: **GREEN** | `ctest -R DawProject --output-on-failure > …/test-6-split-green.log 2>&1; echo EXIT=$?` | **0** | 2/2 passed. |
| Split: proof, revert | `git add -N` of the two new files; `git diff -- include/DawProjectInterchange.h src/core/CMakeLists.txt src/core/DawProjectWrite.cpp src/core/DawProjectWriteRefusal.cpp src/core/DawProjectWriteRefusal.h > …/fix.patch && git apply -R …/fix.patch`; build; ctest | build 0; **8** | `FAIL! … global time-signature numerator 0 was written`; 54 passed, 1 failed. |
| Split: proof, re-apply | `git apply …/fix.patch`; build; `ctest -R DawProject` | build 0; **0** | 2/2 passed. |

**Affected family**: `ctest -R DawProject` lists 2 tests, `DawProjectInterchangeRoundTripTest` (#294) and `DawProjectDeflateImportTest` (#295). Both were executed and passed (2/2). The round-trip binary runs 55 QtTest slots (54 + the new one).

## Gates

| Gate | Command | Exit on this tree | Exit on HEAD (`git archive HEAD` copy) |
|---|---|---:|---:|
| Complexity | `bash tests/complexity-gate.sh --check > …/gate-complexity-2.log 2>&1; echo EXIT=$?` | **1** | 1 |
| File length | `bash tests/file-length-gate.sh --check > …/gate-filelength-2.log 2>&1; echo EXIT=$?` | **1** | 1 |

- **Complexity**: the six `REGRESSION` lines are byte-identical to HEAD's. They are in untouched files (`tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp` `parseNotes`, `src/core/ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`). The new functions are not flagged. `dawProjectXmlFromModel` is grandfathered in `tests/complexity-baseline.tsv`.
- **File length**: the 14 `REGRESSION` lines are identical to HEAD's and are in untouched files. My own regression (`DawProjectWrite.cpp` at 512 code lines with the inline fix) is gone: it is now 457, and the new file is 68.

The two gates are red on HEAD for reasons outside this bug. I did not fix those files or re-anchor the baselines. The ratchets would need an owner decision about the stale baseline (`tests/complexity-baseline.tsv`, `tests/file-length-baseline.tsv`).

## Noted, not changed (outside this finding)

- The writer's denominator check is `isPowerOfTwo` only (`DawProjectWriteRefusal.cpp`, `meterRefusal`). The reader uses `isSupportedTempoMapDenominator` (`include/TempoMap.h:55`, which allows 1–32). So a denominator such as 64 is still written and then refused by the reader. This is by reading the code. I did not write a test for it.

## Files in this commit

- `include/DawProjectInterchange.h`
- `src/core/CMakeLists.txt`
- `src/core/DawProjectWrite.cpp`
- `src/core/DawProjectWriteRefusal.cpp` (new)
- `src/core/DawProjectWriteRefusal.h` (new)
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-100-port.md`

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-100-port.r1/logs`.
