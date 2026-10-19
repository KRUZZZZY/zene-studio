FIXED — cluster new-p12-191-port (port of `bugs/qwen-new-p12-191` onto `050/m0-train-5`)

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

Answers to every item the pre-submit check asks, with the evidence.

* **The regression test FAILS with the product change reverted and PASSES with it
  (exit codes).** Yes, twice over.
  * Reverted (`git apply -R .../logs/fix.patch`, EXIT 0; rebuild EXIT 0):
    `cd build/tests && ctest -R '^MultiTrackRecorderTest$' --output-on-failure` → **EXIT 8**,
    `FAIL! MultiTrackRecorderTest::JournalRecoversTakePathContainingNewline()` — actual
    `"/tmp/qt_temp-faLCJA/take"`, expected `"/tmp/qt_temp-faLCJA/take\npart.wav"`.
  * Restored (`git apply .../logs/fix.patch`, EXIT 0; rebuild EXIT 0): the same ctest
    command → **EXIT 0**, 1/1 passed.
  * The first RED run (test written before any product change, `.../logs/test-1-red.log`)
    also exited **8** with the same assertion; the GREEN run
    (`.../logs/test-2-green.log`) exited **0**.
* **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or
  injected values it uses).** The seam is the journal FILE: the test writes a journal
  with `recordingjournal::write()` into a `QTemporaryDir` and reads it back with
  `recordingjournal::read()` / `recjournal::scan()`. Every value it uses is written by
  the test itself (`takePath`, `state`, `sampleRate`, `channels`) — no clock, no sleep,
  no thread, no signal, no polling. The `updated_utc` field the writer stamps is not
  asserted on. The same test therefore produces the same verdict on every run.
* **The fix is at the cause and complete: every caller and sibling of what you changed
  was checked and stays consistent.** The format has exactly one writer
  (`recordingjournal::write()`, `src/core/RecordingJournal.cpp:218`) and one reader
  (`recordingjournal::read()`, `:245`), and both were changed there. Callers checked:
  `write()` — `src/core/audio/TrackRecorder.cpp:231` (the disk-writer thread) and
  `src/core/ControlCommandsRecordingRecovery.cpp:105` (`record.recovery_restore`);
  `read()` — `src/core/ControlRecordingSupport.cpp:122` (`readJournal()`, behind the
  `record.recovery_*` / `record.journal_*` commands) and `scan()` (`:290`). The sibling
  fields of the same format were checked too: `state`, `take`, `project`, `track` and
  `updated_utc` are all escaped and decoded, while the number fields
  (`sample_rate`, `channels`, `frames_on_disk`, `start_ticks`) are written as plain
  integers and read from the raw value, so they are unchanged. The two readers of the
  journal's raw text outside the product (`tests/src/gui/RecordingRecoveryMenuTest.cpp:94`
  and `tests/control-recording-recovery.py:342`) look for a `state=` prefix only, which
  is unchanged. The autosave recovery sidecar (`src/core/ProjectRecovery.cpp`) is a
  different file with a different format and was deliberately not touched.
* **No collateral damage: old projects and saved data still load as before, and any
  limit or check you added does not hit other callers that legitimately exceed it.**
  A version 1 journal (written by an older build) is still read LITERALLY, so a path
  that merely contains a backslash is not rewritten — held by
  `MultiTrackRecorderTest::JournalReadsAVersion1FileLiterally()`. A version 2 journal
  with an ordinary path is byte-identical to the old file apart from the `version=`
  line, because `escapeFieldValue()` returns such a value unchanged. No limit, no
  check and no new control command was added, so no caller can hit one. The 11
  affected-family tests (including the end-to-end `ControlRecordingRecovery` driver
  against the real `zene` binary) all pass: **11/11, EXIT 0**.
* **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the
  intended contract that shows it.** Not applicable — the verdict is FIXED. The bug was
  real on this line: `src/core/RecordingJournal.cpp:229` wrote the take path raw into a
  line-oriented file, and the reader at `:245-272` reconstructs one line at a time, which
  is exactly what the RED run shows (the path came back truncated at the newline).
* **Realtime.** `recordingjournal::write()` runs on the disk-writer thread
  (`TrackRecorder::writerLoop` → `commitTake()` → `writeJournal()`), never on the audio
  thread; the audio thread only pushes frames into the ring. No new allocation, lock,
  wait or log was added to any audio-thread path.
* **New files.** None. The test source already existed and is registered in
  `tests/all-sources.txt`; this change additionally registers it in
  `tests/fork-sources.txt`, exactly as the original fix did, so the fork-NEW test code
  is ratcheted as new code.

## Files committed

* `src/core/RecordingJournal.cpp` — the fix.
* `tests/src/core/MultiTrackRecorderTest.cpp` — the regression and the version 1 guard.
* `tests/fork-sources.txt` — registers the fork-NEW test source.
* `docs/qwen-fixes-2026-10/cluster-new-p12-191-port.md` — this report.
