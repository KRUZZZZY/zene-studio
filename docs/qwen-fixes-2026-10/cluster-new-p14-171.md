# Cluster new-p14-171

## Verdict: ALREADY-FIXED

Finding: "MIDI export reports success after file open or write failure" (`plugins/MidiExport/MidiExport.cpp:86`).

The product code on this branch already does what the finding asks: a file-open failure returns
`NotCreated`, and a write or close failure returns `Incomplete`. The caller refuses both and
removes only a partial file that this call created. The fix came in with cluster review-354232bbf
(see `docs/qwen-fixes-2026-10/cluster-review-354232bbf.md`). This lane found no product defect.

What was missing was regression coverage for the two cases the finding names: no existing test
drove an unwritable destination or a refused write. This lane adds both, as deterministic tests in
`tests/src/core/MidiExportTest.cpp`, and proves they catch the reported bug.

## Evidence (file:line, current tree)

- `plugins/MidiExport/MidiExport.cpp:83-86`: the output is opened `WriteOnly | NewOnly`; a failed
  open returns `ExportOutcome::NotCreated`, so nothing at the path is claimed.
- `plugins/MidiExport/MidiExport.cpp:278`: `writeOk` is `midiout.status() == QDataStream::Ok &&
  f.error() == QFileDevice::NoError`, read before the close (a successful close clears the error).
- `plugins/MidiExport/MidiExport.cpp:279-283`: `f.close()`, then `exportOutcomeAfterClose(f, writeOk)`.
- `src/core/Song.cpp:3016-3024`: `exportOutcomeAfterClose` returns `Incomplete` when `file.error()`
  is set after the close, and `Written` only when `writeOk` holds.
- `src/core/Song.cpp:2994-3006`: `exportProjectMidi` returns the filter's outcome; a filter that
  cannot be instantiated returns `NotCreated`.
- `src/core/ControlCommandsProjectLifecycle.cpp:464-471`: the command refuses any outcome other
  than `Written`, and removes the temporary file only for `Incomplete`.

## Regression tests added (tests/src/core/MidiExportTest.cpp)

- `anUncreatableDestinationIsNotReportedAsWritten`: the destination directory does not exist;
  `exportProjectMidi` must return `NotCreated` and create nothing.
- `aRefusedWriteIsAFailedExportNotASuccess`: a `RLIMIT_FSIZE` of 4 bytes (POSIX; the case
  `QSKIP`s elsewhere) makes the kernel refuse the export's writes for real. The command must be
  refused, the partial temporary file removed, and the previous export left byte for byte. The
  `FileSizeLimit` guard restores the limit and the `SIGXFSZ` disposition on every exit path.

No timing, sleeps or races: the limit is a kernel rule, and the destination is absent by construction.

## Commands run (EXIT codes)

Build environment note: the first build failed because ccache could not create its temp file in
`dsh/home/ccache-tmp-r6` (a permission denied from the sandbox, not a code error). Later builds set
`CCACHE_TEMPDIR=$TMPDIR/ccache-tmp` (on disk).

| # | Command (from `build/` or `build/tests`) | EXIT | Log |
|---|---|---|---|
| 1 | `cmake --build build --target MidiExportTest -j4` | 2 (ccache temp-dir permission; no code error) | `logs/build-1.log` |
| 2 | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp cmake --build build --target MidiExportTest -j4` | 0 | `logs/build-2.log` (0 warnings) |
| 3 | `ctest -R '^MidiExportTest$' --output-on-failure` (from build/tests), current code + new tests | 0 (1/1 passed) | `logs/test-1-green-as-is.log` |
| 4 | RED: bug injected in `MidiExport.cpp` (open failure returns `Written`; close outcome ignored, `Written` returned); saved as `logs/fix.patch`; `cmake --build build --target MidiExportTest -j4` | 0 | `logs/build-3-red.log` |
| 5 | RED run: `ctest -R '^MidiExportTest$' --output-on-failure` | **8** (4 failed of 12) | `logs/test-2-red.log` |
| 6 | Restore: `git apply -R logs/fix.patch`; `git diff -- plugins/MidiExport/MidiExport.cpp` is empty | 0 / empty | - |
| 7 | GREEN rebuild: `cmake --build build --target MidiExportTest -j4` | 0 | `logs/build-4-green.log` |
| 8 | GREEN run: `ctest -R '^MidiExportTest$' --output-on-failure` | 0 (1/1 passed) | `logs/test-3-green.log` |
| 9 | GREEN run of the binary: `./MidiExportTest` (from build/tests) | 0 (12 passed, 0 failed) | `logs/test-4-green-binary.log` |

RED failures in run 5 (`logs/test-2-red.log`):

- `aRefusedWriteIsAFailedExportNotASuccess`: `'!result.ok' returned FALSE` at
  `tests/src/core/MidiExportTest.cpp:520` (the export reported success although the disk refused its write).
- `anUncreatableDestinationIsNotReportedAsWritten`: `'outcome == ExportOutcome::NotCreated' returned FALSE`
  at `tests/src/core/MidiExportTest.cpp:482`.
- The two pre-existing cases that guard the same contract also fail under the injection:
  `aCollidingTemporaryPathKeepsTheUserFileAndThePreviousExport` (line 424) and
  `aFailedCloseLeavesThePreviousExportInPlace` (line 465).

Affected family: `ctest -R '^MidiExportTest$'` runs one executable, which holds 12 QTest cases.
All 12 pass on the committed code.

## Gates

- `feature_check.py` run unchanged, with only its log folder redirected (see below):
  `python3 -I logs/fc-wrapper.py <worktree>` - EXIT **0**, `RESULT: PASS`, log
  `logs/gate-feature-check-2.log`. All six simple gates PASS; complexity and file-length
  "no new item in your files".
- The first attempt, `python3 .../feature_check.py <worktree>` (EXIT **1**, log
  `logs/gate-feature-check-1.log`), failed before running any gate: the script writes its logs to
  `<worktree>/../<name>-feature-check`, outside this lane's write-jail, and got a PermissionError.
  `logs/fc-wrapper.py` runs the same file (`exec`) with that one line swapped for a log path inside
  the run folder; gate commands, baselines and PASS/FAIL rules are unchanged. The base baseline
  (`gate-baselines/cf3618e61...json`) was already cached and was only read.

## Files committed

- `tests/src/core/MidiExportTest.cpp` (two new cases, the `FileSizeLimit` guard, and includes)
- `docs/qwen-fixes-2026-10/cluster-new-p14-171.md` (this report)

Already registered, no registration change needed: `tests/src/core/MidiExportTest.cpp` is the
test source of `tests/CMakeLists.txt:1006,2171,2256` (`MidiExportTest`), and the gate
`all-sources-reproduce` (PASS above) confirms the source lists are consistent.
No product file changed. No new control command. No header changed.

## Self-check

Filled in after the completion check below.
