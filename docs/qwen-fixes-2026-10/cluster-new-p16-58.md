FIXED — cluster new-p16-58: ZIP64 entry-count sentinel silently dropped entries

## Finding

`src/core/DawProjectZipRead.cpp` read the end record's 16-bit entry-count field as a plain count. When that field holds
`0xFFFF`, the value is the ZIP64 sentinel (the real count lives in a ZIP64 record this reader does not decode). The reader
scanned only the first 65,535 central-directory records and returned success with no deviation for the rest.

## Decision: REAL

Evidence on the pre-fix code (`git show 639e1d23e:src/core/DawProjectZipRead.cpp`):

- `locateDirectory` took `*count = readU16(bytes, endOffset + 10)` with no sentinel check.
- `dawProjectZipReadTolerant` calls `scanDirectory(scan, centralOffset, count)`. The loop runs `count` times and returns;
  it never checks that the directory holds no further records.
- A `0xFFFF` count over a directory of more than 65,535 records therefore read successfully with no `report.deviations`
  entry. The RED run below confirms the success on the pre-fix code.

The writer side already knows about the sentinel: `src/core/DawProjectZip.cpp:145-154` (`MaxEntryCount = 0xFFFE`).

## The fix (round 2: overflow-only refusal)

Round 1 refused every `0xFFFF` count. Review (`qa/reviews/new-p16-58.r1.md`) showed that also rejects a valid classic
archive of exactly 65,535 entries, which PKWARE APPNOTE §§4.4.1.4 and 4.4.22 allow: `0xFFFF` is a real count when nothing
follows it. The fix now refuses only when a further record exists:

- `src/core/DawProjectZipRead.cpp:116` — `constexpr quint16 Zip64CountMarker = 0xFFFFu;`
- `src/core/DawProjectZipRead.cpp:216` — `locateDirectory` again reads the count unchanged (`*count = readU16(...)`).
- `src/core/DawProjectZipRead.cpp:246-260` — `holdsRecordPastCount`: walks the `count` declared records with the existing
  `decodeCentral`, then checks whether a central-directory signature sits at the next position. The directory ends where
  the end record starts (`findEndOfCentralDirectory` requires that), so a signature there is a further entry. A
  truncated directory returns false here and is reported by `scanDirectory`, as before.
- `src/core/DawProjectZipRead.cpp:552-563` — `dawProjectZipReadTolerant` refuses only when the count is the sentinel and
  `holdsRecordPastCount` is true. The error says the container is a ZIP64 container with more entries past the sentinel.
  Strict `dawProjectZipRead` goes through the same function (`src/core/DawProjectZipRead.cpp:581`).
- `src/core/DawProjectZipRead.cpp:20-25` — header comment updated to the new condition.

Callers and siblings checked:

- `rg` for end-record readers across `src` and `tests`: `locateDirectory` is the only reader of the end record. Both
  entry points go through `dawProjectZipReadTolerant`: the `.dawproject` import (`src/core/DawProjectRead.cpp:502`) and
  strict `dawProjectZipRead` (`src/core/ProjectContainer.cpp:320`, the `.mmpz` v2 path).
- The writer never emits `0xFFFF` as a count (`MaxEntryCount = 0xFFFE`).
- Only a directory with a record past its declared count changes behaviour. Every other count, including a classic
  exactly-65,535 directory, is read exactly as before.

## Regression tests

`tests/src/core/DawProjectDeflateImportTest.cpp` (next to the existing reader tests in the same class; no new file, so no
registration change). Both build the directory in memory from the file's existing `stored()` and `buildZip()` helpers.
There are no clocks, sleeps or threads, and the result depends only on the bytes.

1. `aZip64OverflowOfTheEntryCountIsRefusedNotTruncated` (`:632`): 65,536 records, count `0xFFFF`. Expects refusal with an
   error naming ZIP64 from the tolerant reader, the strict reader and `dawproject.import`, and no entries.
2. `aClassicDirectoryOfExactly65535EntriesReadsInFull` (`:654`): 65,535 records, count `0xFFFF`, expects a full read
   with no deviations from both readers. Then 65,534 records with count `0xFFFE` also read in full.

Test matrix (step 3): normal path (test 2, and the existing 11 tests in the class); overflow and failure path (test 1);
the exact boundary on both sides (65,535 and 65,536 records); audio-thread constraints (none — file reading and import
only); platform requirements (none beyond the existing test build).

## Commands and results

ccache note: the default `CCACHE_TEMPDIR` in this shell points at `dsh/home/ccache-tmp-r6`, which is outside the write
area. Every build below set `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp-new-p16-58` (build-cache area); the
shared ccache config is unchanged.

Round 2 logs are in `new-p16-58.r2/logs/`. Round 1 logs are in `new-p16-58.r1/logs/` and are cited where used.

| Step | Command (from the worktree unless noted) | EXIT | Log |
|---|---|---|---|
| RED on HEAD (round-1 code, unconditional refusal) | `cmake --build build --target zene DawProjectDeflateImportTest -j4`; `ctest -R DawProjectDeflateImportTest -V` (from `build/tests`) | build 0; **test 8** | `build-1-red-head.log`, `test-1-red-head.log` — `FAIL! : …aClassicDirectoryOfExactly65535EntriesReadsInFull()`, `dawProjectZipReadTolerant(exact…) returned FALSE`; `Totals: 12 passed, 1 failed` |
| GREEN (overflow-only check) | same build; same ctest | build **0**; test **0** | `build-2-green.log`, `test-2-green.log` — `Totals: 13 passed, 0 failed` (both new tests PASS) |
| Patch proof, correction reverted | `git diff -- src/core/DawProjectZipRead.cpp > fix-r2.patch`; `git apply -R fix-r2.patch`; build; ctest | apply **0**; build 0; test **8** | `test-3-revert-r2.log` — `FAIL! …aClassicDirectoryOfExactly65535EntriesReadsInFull`; `Totals: 12 passed, 1 failed` |
| Patch proof, correction restored | `git apply fix-r2.patch`; build; ctest | apply **0**; build 0; test **0** | `test-4-restore-r2.log` — `Totals: 13 passed, 0 failed` |
| Overflow test vs pre-fix base reader | `git show 639e1d23e:src/core/DawProjectZipRead.cpp` into the worktree; build; ctest | build 0; test **8** | `test-5-base-file.log` — `FAIL! …aZip64OverflowOfTheEntryCountIsRefusedNotTruncated()` at `'!dawProjectZipReadTolerant(overflow…)' returned FALSE` (silent 65,535-entry read) |
| Restore after base check | copy back `DawProjectZipRead.r2-fixed.cpp`; build; ctest | build 0; test **0** | `build-6-restore.log`, `test-6-restore.log` |
| Family build | `cmake --build build --target DawProjectInterchangeRoundTripTest ProjectContainerTest ProjectContainerEntriesTest ProjectContainerSaveTest ProjectContainerSaveAsGuiTest -j4` | **0** | `build-7-family.log` |
| Family test | `ctest -R "DawProjectInterchangeRoundTripTest\|DawProjectDeflateImportTest\|ProjectContainerTest\|ProjectContainerEntriesTest\|ProjectContainerSaveTest\|ProjectContainerSaveAsGuiTest\|MmpzV2CliRefusal" --output-on-failure` | **0** | `test-7-family.log` — 7/7 passed |
| MmpzGitDepthTest (not in the family above; pre-existing) | `ctest -R MmpzGitDepthTest --output-on-failure` | **8** | `test-9-mmpzgit-verbose.log` — `Ran 51 tests`, `FAILED (failures=41, skipped=1)`; identical to the round-1 baseline (`new-p16-58.r1/logs/test-8-baseline-mmpzgit.log`) |
| Gates (log folder redirected copy) | `python3 -I new-p16-58.r2/feature_check_redirected.py <worktree>` | **0** | `gates-1-feature-check-table.txt` — `RESULT: PASS`, 8 rows |

The redirected gate script is a copy in the run folder (`feature_check_redirected.py`, next to `logs/`). Its only changes
from the original are `ROOT` (kept at the original qwen folder so `FIX-BASE` and the cached base listings are read
unchanged) and the log folder (moved into the run folder). No limit, baseline or rule was changed.

Gate table (`gates-1-feature-check-table.txt`):

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

The original `feature_check.py` (not the copy) cannot run from this lane: it creates a log folder next to the worktree,
outside the write area. That is the cause of the `submit_check` gate row (see Self-check).

## Pre-existing failure, not this fix

`MmpzGitDepthTest` fails with and without the fix, with the same 41 failures out of 51 (1 skipped). The failing checks are
Python byte-identity tests on `zlib.compress` output (`tools/mmpz-git/tests/test_mmpz_git.py:99`,
`Container.test_recompress_byte_identical`) and an audible-diff test that expects a rendering difference and gets none
(`test_mmpz_git.py:1018`). Neither reaches `DawProjectZipRead.cpp`.

## Not run

- The full ctest suite: the change is confined to the ZIP reader and its direct callers, which are all in the family above.
- GUI or audio paths: none touched. No control command added, so the ControlCommandsSnapshot canary is unaffected.

## Self-check

- **Regression tests fail on the broken code and pass with the fix.** Exact-65,535 test: RED on HEAD (`test-1-red-head.log`,
  EXIT 8, unconditional refusal) and with the correction reverted (`test-3-revert-r2.log`, EXIT 8). Overflow test: RED on
  the pre-fix base reader (`test-5-base-file.log`, EXIT 8). GREEN with the fix: `test-2-green.log` and `test-4-restore-r2.log`,
  EXIT 0, 13 passed.
- **Deterministic.** No clock, sleep, thread or race. Inputs are fixed in-memory directories built by `stored()` and
  `buildZip()`, with fixed count overrides (`0xFFFF`, `0xFFFE`), written into the test's `QTemporaryDir`.
- **Fix at the cause, and complete.** `locateDirectory` (`src/core/DawProjectZipRead.cpp:216`) is the only reader of the
  end record. Both entry points go through `dawProjectZipReadTolerant` (`:543`), so the import (`DawProjectRead.cpp:502`)
  and the `.mmpz` v2 path (`ProjectContainer.cpp:320`) get the same behaviour. The writer never emits `0xFFFF`
  (`DawProjectZip.cpp:145-154`).
- **No collateral damage.** Every count except `0xFFFF` is read as before. A classic directory of exactly 65,535 entries
  reads in full (test 2). The family passes 7/7 (`test-7-family.log`). The only changed case is a directory with a record
  past a `0xFFFF` count, which used to import partially and silently. This program never writes that. `MmpzGitDepthTest`
  is unchanged at 41/51 failing (`test-9-mmpzgit-verbose.log`).
- **Verdict FIXED.** The ALREADY-FIXED and FALSE-POSITIVE evidence rule does not apply.

### Gate row under the pre-submit check

`submit_check.py` runs the original `feature_check.py` on the worktree. That script creates its log folder
`<worktree>-feature-check/` next to the worktree, which is outside the write area for this lane, so it exits 1 with
`PermissionError` before any gate runs. The same gate code, with only the log folder changed, passes all 8 rows with EXIT 0
(`gates-1-feature-check-table.txt`). The shared gate script and its baselines were not edited. The gate row needs a run
where the sibling log folder can be created, and this lane could not do that.
