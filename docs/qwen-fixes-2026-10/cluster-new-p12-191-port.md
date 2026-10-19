# Cluster new-p12-191-port — FIXED

Port of the fix accepted on `bugs/qwen-new-p12-191` (commit `157230c24`, "fix(recording):
escape newline paths in journal") onto the newest line (`050/m0-train-5`, CachyOS toolchain).
The original fix could not be merged because it conflicted in `src/core/RecordingJournal.cpp`;
the change was therefore written by hand against the current code, not merged or cherry-picked.

## Verdict

**REAL — fixed here.** A take path containing a newline broke its recovery journal: the
journal is a line-oriented `key=value` file, the writer stored the path raw, and the reader
reconstructed one line at a time. The take was then looked for at a truncated path, so a
capture that really happened was offered for recovery as nothing.

## The finding, and why it is real on this line

The finding points at `src/core/RecordingJournal.cpp:162` on the line it was filed
against. On this tree the same defect is at `src/core/RecordingJournal.cpp:229` (the
`take=` line of `write()`), with the reader at `src/core/RecordingJournal.cpp:245-272`.

Before the fix:

* `write()` wrote `out << "take=" << journal.takePath << "\n";` — the raw path, so a
  newline inside it started a second line.
* `read()` read the file one `QTextStream::readLine()` at a time and took the value of
  the first `key=value` line, so `take=/tmp/x/take\npart.wav` was read back as
  `/tmp/x/take`.
* `isRecoverable()` (`src/core/RecordingJournal.cpp:283`) then asked the filesystem
  whether that truncated path exists, found nothing, and `scan()` omitted the take from
  the recovery offer.

The RED run shows exactly that truncation (see the table below): the actual path read
back was `"/tmp/qt_temp-TKyexC/take"` where the take was
`"/tmp/qt_temp-TKyexC/take\npart.wav"`.

## The fix (at the cause)

Both the write and the read of a journal go through exactly one function each
(`recordingjournal::write()` and `recordingjournal::read()`), so the format is fixed
there, once, for every caller.

| Location | Evidence |
| --- | --- |
| `src/core/RecordingJournal.cpp:81` | `CurrentJournalVersion = 2` — the version this build writes. |
| `src/core/RecordingJournal.cpp:88-102` | `escapeFieldValue()`: a backslash, an LF and a CR become `\\`, `\n` and `\r`. |
| `src/core/RecordingJournal.cpp:105-124` | `decodeFieldValue()`: the inverse; an unknown escape is kept as written. |
| `src/core/RecordingJournal.cpp:127-131` | `journalVersion()`: the version a `version=` line names (0 when it names none). |
| `src/core/RecordingJournal.cpp:192-205` | `applyLine()` decodes a string value only when the journal is a version 2 one. |
| `src/core/RecordingJournal.cpp:224-240` | `write()` emits `version=2` and escapes every string field (state, take, project, track, updated_utc). |
| `src/core/RecordingJournal.cpp:253-272` | `read()` reads the `version=` line and decodes the string fields from version 2 on. |
| `src/core/RecordingJournal.cpp:19-26` | The file header documents the new format and why a version 1 file is still read literally. |
| `tests/src/core/MultiTrackRecorderTest.cpp:188-218` | The regression: a take whose name holds a newline is recovered with its exact path. |
| `tests/src/core/MultiTrackRecorderTest.cpp:220-250` | The compatibility guard: a version 1 journal is read LITERALLY. |
| `tests/fork-sources.txt:2323` | Registers the fork-NEW test source in the fork scope (the original fix did the same). |

### Old data still loads exactly as before

* A **version 1** journal (written by an older build) holds literal values. The reader
  keeps it literal: decoding a value that was never escaped would rewrite a path that
  merely contains a backslash. `JournalReadsAVersion1FileLiterally()` holds that line.
* A **version 2** journal with an ordinary path is byte-for-byte the old file apart from
  the `version=` line: `escapeFieldValue()` returns the value unchanged when it holds
  none of the three escaped characters, so `state=in_progress`, `take=/home/me/take.wav`
  and friends are written exactly as before.
* The two readers of the journal's raw text outside the product
  (`tests/src/gui/RecordingRecoveryMenuTest.cpp:94` and `tests/control-recording-recovery.py:342`)
  look for a `state=` prefix only. `state=in_progress` and `state=restored` contain no
  escaped character, so they are unchanged.

### Callers and siblings checked

* `recordingjournal::write()` is called from `src/core/audio/TrackRecorder.cpp:231`
  (`writeJournal()`) and `src/core/ControlCommandsRecordingRecovery.cpp:105`
  (`record.recovery_restore`). Both go through the one writer, so both are fixed.
* `recordingjournal::read()` is called from `src/core/ControlRecordingSupport.cpp:122`
  (`readJournal()`, used by the `record.recovery_*` and `record.journal_*` commands) and
  from `scan()` (`src/core/RecordingJournal.cpp:290`). Both go through the one reader.
* The number fields are written as plain integers and are read from the raw value, so
  they are untouched by the escaping.
* The autosave recovery sidecar (`src/core/ProjectRecovery.cpp`) is a DIFFERENT file
  with a different payload format and a different owner; it is outside this finding and
  was not touched.

### Realtime, undo and commands

`recordingjournal::write()` runs on the disk-writer thread (`TrackRecorder::writerLoop`
→ `commitTake()` → `writeJournal()`, `src/core/audio/TrackRecorder.cpp:368/213/231`) and
on the main/control thread, never on the audio thread: the audio thread only pushes
frames into the ring. The escaping adds no lock, no wait and no logging, and it
allocates a QString on the disk-writer thread, which already allocates a `QSaveFile` and
a `QTextStream` per commit. No new control command was added, and no undo step changed.

## Verification evidence

All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-191-port.r1/logs`.

| Check | Exact command | Exit / result |
| --- | --- | --- |
| Baseline build | `cmake --build build --target zene MultiTrackRecorderTest > .../logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| Baseline test (before the test was added) | `cd build/tests && ctest -R '^MultiTrackRecorderTest$' --output-on-failure > .../logs/test-0-baseline.log 2>&1; echo EXIT=$?` | 0; 1/1 |
| RED: test added, product code untouched | `cmake --build build --target MultiTrackRecorderTest > .../logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| RED run | `cd build/tests && ctest -R '^MultiTrackRecorderTest$' --output-on-failure > .../logs/test-1-red.log 2>&1; echo EXIT=$?` | **8**; `FAIL! MultiTrackRecorderTest::JournalRecoversTakePathContainingNewline()` — actual `"/tmp/qt_temp-TKyexC/take"`, expected `"/tmp/qt_temp-TKyexC/take\npart.wav"` (`tests/src/core/MultiTrackRecorderTest.cpp(209)`) |
| GREEN build | `cmake --build build --target zene MultiTrackRecorderTest > .../logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| GREEN run | `cd build/tests && ctest -R '^MultiTrackRecorderTest$' --output-on-failure > .../logs/test-2-green.log 2>&1; echo EXIT=$?` | **0**; 1/1 |
| Affected family (11 tests) | `cd build/tests && ctest -R '^(MultiTrackRecorderTest\|RecordingCrashLossTest\|RecordingRecoveryMenuTest\|ControlEditCommandsTest\|RecordingRealtimeTest\|RecordRingBufferTest\|TwoTrackRecordingHarness\|RecordingInputPathTest\|RecordingPassTest\|RecordingLatencyTest\|ControlRecordingRecovery)$' --output-on-failure > .../logs/test-6-family-final.log 2>&1; echo EXIT=$?` | **0**; 11/11 passed (the family includes the end-to-end `ControlRecordingRecovery` driver, which runs the real `zene` binary and reads the journal's `state=` line back) |
| 4b: reverse the product patch | `git diff -- src/core/RecordingJournal.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch; echo EXIT=$?` | 0 |
| 4b: rebuild without the fix | `cmake --build build --target zene MultiTrackRecorderTest > .../logs/build-5-nofix.log 2>&1; echo EXIT=$?` | 0 |
| 4b: RED again | `cd build/tests && ctest -R '^MultiTrackRecorderTest$' --output-on-failure > .../logs/test-4-red-nofix.log 2>&1; echo EXIT=$?` | **8**; the same assertion fails (actual `"/tmp/qt_temp-faLCJA/take"`) |
| 4b: restore the product patch | `git apply .../logs/fix.patch; echo EXIT=$?` | 0 |
| 4b: rebuild with the fix restored | `cmake --build build --target zene MultiTrackRecorderTest > .../logs/build-6-restored.log 2>&1; echo EXIT=$?` | 0 |
| 4b: GREEN again | `cd build/tests && ctest -R '^MultiTrackRecorderTest$' --output-on-failure > .../logs/test-5-green-restored.log 2>&1; echo EXIT=$?` | **0**; 1/1 |
| Gate (the table of record) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-191-port > .../logs/gate-feature-check-2.log 2>&1; echo EXIT=$?` | **0**; `RESULT: PASS` (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS) |
| Complexity ratchet (raw) | `bash tests/complexity-gate.sh --check > .../logs/gate-complexity.log 2>&1; echo EXIT=$?` | 1 — red on the train on purpose: 6 pre-existing `REGRESSION:` items, **none** in `src/core/RecordingJournal.cpp` or `tests/src/core/MultiTrackRecorderTest.cpp`. The new functions are `escapeFieldValue` CCN 5, `decodeFieldValue` CCN 7, `journalVersion` CCN 2; `read()` went 7 → 8, `write()` stayed 6. |
| File-length ratchet (raw) | `bash tests/file-length-gate.sh --check > .../logs/gate-file-length.log 2>&1; echo EXIT=$?` | 1 — red on the train on purpose: pre-existing over-500-line files, **none** of them mine (`src/core/RecordingJournal.cpp` is 393 lines, `tests/src/core/MultiTrackRecorderTest.cpp` is 251). |
| Registration gate | `bash tests/fork-sources-gate.sh > .../logs/gate-fork-sources.log 2>&1; echo EXIT=$?` | 0; 1791 fork-NEW, 1211 all-sources, 48 tools, 0 stale |
| Duplication ratchet | `bash tests/duplication-gate.sh --check > .../logs/gate-duplication.log 2>&1; echo EXIT=$?` | 0; duplicated lines 0.33% (budget 5%) |

The raw complexity and file-length ratchets are red on the train before this change as
well (the same 6 complexity items are listed in `.../logs/gate-complexity.log` and none
of them is in a file this change touches). `feature_check.py` compares the listing with
the branch's base and reports `no new item in your files` for both.

## Self-check

* **Is the bug real?** Yes — see the RED run above: the path read back was truncated at
  the newline, and `isRecoverable()` therefore never found the take.
* **Does the test fail without the fix and pass with it?** Yes, twice: the RED run
  (test written first, product code untouched) and the 4b reverse-patch run.
* **Is the test deterministic?** Yes — a `QTemporaryDir`, a file written and closed, and
  a journal read back. No wall clock, no sleep, no race, no thread.
* **Is the fix at the cause and complete?** Yes — the format is changed in the one
  writer and the one reader that every caller goes through; the string fields, the
  version line and the literal version 1 path are all covered.
* **Collateral damage?** None: version 1 journals are still read literally, a version 2
  journal with an ordinary path is byte-identical to the old one apart from the version
  line, no limit was added, and no control command was added.
* **Realtime?** `recordingjournal::write()` is not on the audio thread; no new
  allocation, lock, wait or log was added to any audio-thread path.
* **New files?** None. The test source already existed and is registered in
  `tests/all-sources.txt`; this change additionally registers it in
  `tests/fork-sources.txt`, exactly as the original fix did, so the fork-NEW test code
  is ratcheted as new code.

## Files committed

* `src/core/RecordingJournal.cpp` — the fix.
* `tests/src/core/MultiTrackRecorderTest.cpp` — the regression and the version 1 guard.
* `tests/fork-sources.txt` — registers the fork-NEW test source.
* `docs/qwen-fixes-2026-10/cluster-new-p12-191-port.md` — this report.
