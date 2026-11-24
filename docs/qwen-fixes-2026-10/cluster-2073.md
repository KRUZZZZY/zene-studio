FIXED

# Cluster 2073 — Starting a journal can erase existing recovery progress

`record.journal_begin` (`src/core/ControlCommandsRecording.cpp`) read a take's existing journal
and then replaced it unconditionally with a fresh one at `framesOnDisk = 0`. Recovery reports
`min(frames_on_disk, frames in the take's file)` (`src/core/RecordingJournal.cpp:358-361`), so
beginning a capture over a take whose journal still recorded material dropped that offer to zero
recoverable frames.

The command now computes the side-file path once and refuses, typed, when a journal is already
there: unreadable (truncated/damaged) it is left untouched; an interrupted capture is named with
its state and its recoverable frames plus the two verbs that retire it
(`record.journal_finish` / `record.recovery_discard`). Only a genuinely absent side file — and the
`restored` tombstone that `record.recovery_restore`/`record.recovery_place` leave behind, whose own
undo INVERSE this command is — proceeds to write. The recorded inverse is always the paired
`record.recovery_discard`.

## Verdict evidence (REAL)

- Before the fix, `journalBegin` read the side file into `previous` and then wrote a fresh
  `TakeJournal` with `journal.framesOnDisk = 0` and `state = in_progress`, discarding what it had
  just read. The removed hunks are in `logs/fix.patch` (`git diff` of the product files, 127 lines,
  `DIFF_EXIT=0`); the runtime proof is the RED run below, which fails on exactly those two cases.
- The bound that makes it a loss: `recoverableFrames()` is
  `std::min(journal.framesOnDisk, framesInFile(journal.takePath))`
  (`src/core/RecordingJournal.cpp:358-361`), and `isRecoverable()` is
  `state == "in_progress" && take exists` (`src/core/RecordingJournal.cpp:283-288`).
- RED (product code unmodified, new test built): `logs/test-1-red.log`, `EXIT=8` —
  `FAIL! anInterruptedJournalIsRefusedAndLeftByteIdentical '!again.ok' returned FALSE` and
  `FAIL! anUnreadableSidecarIsRefusedAndLeftUntouched '!begun.ok' returned FALSE`.

## The fix

- `src/core/ControlCommandsRecording.cpp:99-134` — the guard. `journalPathFor(take)` is computed
  once (`:110`) and used for the guard, the write (`:147`) and the error text. `:114-121` refuses a
  side file that exists but does not read; `:122-133` refuses one whose `isRecoverable()` is true,
  quoting its `state` and `recoverableFrames(previous)` and naming
  `record.journal_finish` / `record.recovery_discard`. The `hadJournal` branch and its
  `record.journal_begin` inverse are gone; `:160-162` always records the paired
  `record.recovery_discard`, so one `control.undo` removes the journal the call created.
- `src/core/ControlCommandsRecording.cpp:262-272` — `cmd.description` states the refusal and the
  two retiring verbs, and names `record.recovery_discard` as the inverse.
- `src/core/ControlReversibilityTableSnapshot.cpp:288-299` — the A16 row is the truth about the
  same behaviour, so it was rewritten with it: inverse = `record.recovery_discard`, a side file
  that protects an interrupted capture is refused rather than replaced, and the one side file it
  still writes over is the `restored` tombstone whose undo it is.
- Not touched, as instructed: `journalArgsSchema`, `tools/mcp-zene-control/zene_control/commands_snapshot.json`,
  `tests/control-recording-recovery.py`, `src/core/RecordingJournal.cpp`, `src/core/audio/TrackRecorder.cpp`.

### Why the guard is `isRecoverable(previous)` and not "any existing side file"

The recorded inverse of `record.recovery_restore` is `record.journal_begin` with the take's rate
and channels (`src/core/ControlCommandsRecordingRecovery.cpp:116-132`), and `record.journal_finish`
records the same inverse (`src/core/ControlCommandsRecording.cpp:237-249`). One `control.undo` of a
restore therefore dispatches `record.journal_begin` while the side file is still present in state
`restored`. A guard that refused every existing side file would break that undo:
`tests/src/gui/RecordingRecoveryMenuTest.cpp:203-212` asserts the journal is back to `in_progress`
after one undo of a placement, and `tests/control-recording-recovery.py` (`check_place_undo_redo`,
which this job may not modify) asserts it over the socket. `isRecoverable(previous)` is the scan's
own predicate for "this journal is an offer", so the guard refuses exactly the side files recovery
would otherwise advertise, and nothing else. Case 5 of the test pins that path down.

## Regression test

`tests/src/core/JournalBeginGuardTest.cpp` (new; registered in `tests/CMakeLists.txt` with its
offscreen property, and in `tests/fork-sources.txt` and `tests/all-sources.txt`). Everything goes
through `ControlRegistry::invoke`, the entry point the socket, the menu and an agent use, with the
engine up and a `QTemporaryDir` world: no timing, no sleeps, no threads — "untouched" is a sha256 of
the side file before and after the refused call.

| path | case | asserted |
| --- | --- | --- |
| normal | `anAbsentSidecarStartsTheCapture` | success, `frames_journalled` 0, `state` in_progress, side file written |
| failure (the bug) | `anInterruptedJournalIsRefusedAndLeftByteIdentical` | typed `Refused`; message names the side file and both retiring verbs; sha256 unchanged; the offer still reports 4096 journalled and 4096 recoverable frames; no new transaction record (a refusal is not an edit) |
| failure (damaged) | `anUnreadableSidecarIsRefusedAndLeftUntouched` | side file with no `take=` line: typed `Refused`, sha256 unchanged |
| reconfiguration | `aRetiredJournalCanBeStartedAgain` | `record.journal_finish` then begin: success, frames 0, in_progress |
| undo inverse | `undoOfARestoreStillPutsTheOfferBack` | restore → `control.undo` → `restored_by == record.journal_begin`, journal `in_progress`, take offered again |
| undo of the new transaction | `oneUndoRemovesTheJournalThisCallCreated` | `restored_by == record.recovery_discard`, side file gone |
| exhaustion | — | this verb writes one small side file and holds no pool or ring; `journal_update`/`journal_finish` on a take with no journal are `not_found`, which `ControlRecordingRecovery` (run below) covers |
| audio thread | — | `journalBegin` is a control handler, off the render path; nothing added runs per period. The file I/O it does (`QFileInfo`, `read`, `recoverableFrames`) is why it must stay off the audio thread, and it does: the only other journal writer is the recorder's `arm()`, whose own comment (`src/core/audio/TrackRecorder.cpp:197-202`) places it off the audio thread |
| platforms | — | Qt Core only (`QFileInfo`, `QString`, `QJsonObject`, `QCryptographicHash`), no POSIX-only call, no Qt-6-only API; paths come from `QDir`/`QTemporaryDir`, never a bare `'/'` test |

## Commands and exit codes

All logs in `/home/kruzzzzy/.config/kads/zene/state/runs/2073.r1/logs/`. Builds with
`cmake --build build --target …` (guard caps parallelism), tests from `build/tests`.

| # | command | log | EXIT |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target JournalBeginGuardTest` (before the build system knew the target) | `build-1.log` | 2 |
| 2 | `cmake --build build --target zene` — this run re-ran the tree's own check-build-system, so the new test target appeared | `build-2.log` | 0 |
| 3 | `cmake --build build --target JournalBeginGuardTest` | `build-3.log` | 0 |
| 4 | `ctest -R '^JournalBeginGuardTest$' --output-on-failure` — RED, product code unmodified | `test-1-red.log` | 8 |
| 5 | `cmake --build build --target JournalBeginGuardTest` — with the fix | `build-4.log` | 0 |
| 6 | `ctest -R '^JournalBeginGuardTest$' --output-on-failure` — GREEN | `test-2-green.log` | 0 |
| 7 | `ctest -R '^JournalBeginGuardTest$' -V` — 8 passed, 0 failed | `test-3-green-verbose.log` | 0 |
| 8 | `cmake --build build --target RecordingRecoveryMenuTest ControlRecordingRoutesTest` | `build-5.log` | 0 |
| 9 | `ctest -R '^(RecordingRecoveryMenuTest|ControlRecordingRoutesTest)$'` — the undo-of-place path and the recorder route | `test-4-siblings.log` | 0 |
| 10 | `cmake --build build --target zene` | `build-6-zene.log` | 0 |
| 11 | `ctest -R '^ControlRecordingRecovery$'` — the `.py` transcript (SIGKILL crash then real recovery, undo of a begin, place/undo/redo) | `test-5-transcript.log` | 0 |
| 12 | `ctest -R '^ControlCommandsSnapshot$'` — the canary: no command added, ids and snapshot agree | `test-10-canary.log` | 0 |
| 13 | `cmake --build build --target ReversibilityContractTest ReversibilityUndoTest` | `build-9-rev.log` | 0 |
| 14 | `cmake --build build --target JournallingObjectRestoreTest` | `build-10-journalobj.log` | 0 |
| 15 | `ctest -R '^(ReversibilityContractTest|ReversibilityUndoTest|JournallingObjectRestoreTest)$'` | `test-9-reversibility.log` | 0 |
| 16 | `python3 tests/agent-surface-gate.py` — needs a binary and a fixture, so it printed usage; it does not read command descriptions (`multi_grep 'description' tests/agent_surface_lib.py` → 0 matches) | `gates-2-agent-surface.log` | 2 |

### Step 4b — prove the test catches the bug

```
git diff -- src/core/ControlCommandsRecording.cpp src/core/ControlReversibilityTableSnapshot.cpp \
  > /home/kruzzzzy/.config/kads/zene/state/runs/2073.r1/logs/fix.patch     # EXIT 0
git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/2073.r1/logs/fix.patch   # EXIT 0
cmake --build build --target JournalBeginGuardTest          # build-7-reverted.log, EXIT 0
(cd build/tests && ctest -R '^JournalBeginGuardTest$' --output-on-failure)
                                                            # test-6-reverted.log, EXIT 8
                                                            #   6 passed, 2 failed
git apply /home/kruzzzzy/.config/kads/zene/state/runs/2073.r1/logs/fix.patch      # EXIT 0
cmake --build build --target JournalBeginGuardTest zene     # build-8-restored.log, EXIT 0
(cd build/tests && ctest -R '^JournalBeginGuardTest$' --output-on-failure)
                                                            # test-7-restored.log, EXIT 0
```

### Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`, unpiped:

```
feature-check kads-2073 (base fa4a5f2e7, 655 changed files)
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

`logs/gates-3.log`, EXIT=0. The earlier run (`logs/gates-1.log`, EXIT=1) failed
`fork-sources` and `all-sources-reproduce` only because both recipes derive their entry lists from
`git diff … 4e677cb6c6ab HEAD`, and the test file was still untracked at that point (the diff it
printed was `-tests/src/core/JournalBeginGuardTest.cpp`, i.e. "in the manifest, not derivable").
After the code commit (`e90a0ec9b2`) both gates pass with the same entries, unchanged.

## Commit

`e90a0ec9b2` — `fix(record): refuse record.journal_begin over an interrupted capture's journal
(cluster 2073)`: the two product files, the new test, and the three registration lists.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.** Yes:
   reverted `EXIT=8` (`test-6-reverted.log`, 2 failed), restored `EXIT=0` (`test-7-restored.log`).
   Commands and codes in step 4b above.
2. **The test is deterministic — name the seam or injected values.** The seam is the registered
   command (`ControlRegistry::invoke`), the same one the socket and the GUI use; the injected values
   are the take WAV's frame count (4410 frames written by the test) and the journal's
   `frames_on_disk` (4410, set through `record.journal_update`). No clock, sleep, thread or timer is
   involved; "the side file was not touched" is a sha256 comparison.
3. **The fix is at the cause and complete: every caller and sibling checked.**
   - `src/core/ControlCommandsRecording.cpp` only: `journalBegin` is the one function changed;
     `journalUpdate`, `journalFinish` and the recovery verbs already read the journal first and are
     unaffected.
   - Its inverse callers: `record.recovery_restore` (`ControlCommandsRecordingRecovery.cpp:131-132`)
     and `record.journal_finish` (`ControlCommandsRecording.cpp:248-249`) dispatch
     `record.journal_begin` as their undo; both now land on the `restored`/absent path, which the
     guard allows. Proven by case 5 of the new test and by `RecordingRecoveryMenuTest` +
     `ControlRecordingRecovery` passing (rows 9 and 11).
   - `record.recovery_place` records an action checkpoint instead
     (`ControlCommandsRecordingPlace.cpp:120-142`), so it never reaches `journalBegin`; it passes in
     the same runs.
   - The A16 row (`ControlReversibilityTableSnapshot.cpp:288-299`) and the command description say
     the same thing as the code; `ReversibilityContractTest` and `ReversibilityUndoTest` pass.
   - The recorder's own `arm()` path writes the journal directly and is deliberately NOT widened
     here — see Other findings 1.
4. **No collateral damage.** The `.py` crash/recovery transcript (`ControlRecordingRecovery`, EXIT 0),
   `RecordingRecoveryMenuTest` (EXIT 0) and `ControlRecordingRoutesTest` (EXIT 0) are unchanged and
   green; the canary `ControlCommandsSnapshot` is green, so no command id moved. The check is narrow
   by construction: it rejects only side files `isRecoverable()` reports as offers, which is the
   predicate the scan itself uses, so no caller that legitimately writes over a `restored` tombstone
   is rejected.
5. **ALREADY-FIXED / FALSE-POSITIVE.** Not claimed; the verdict is FIXED and the RED run is the
   proof the defect existed on this branch.
6. **Sibling commands in the changed command files.**
   - `record.journal_update` — unchanged; it edits the frames of a journal it read, and the guard
     does not intercept it.
   - `record.journal_finish` — unchanged; it removes a journal, which is one of the two actions the
     refusal now names, and its own inverse (a begin on an absent journal) is allowed.
   - `record.recovery_get_state` / `record.recovery_restore` / `record.recovery_place` /
     `record.recovery_discard` (the same group, `ControlCommandsRecordingRecovery*.cpp`) —
     unchanged; they read the journal and either resolve or remove it, and none of them writes a
     frames-0 journal over one that exists. Their undo (which goes through `record.journal_begin`)
     is covered by case 5.
   - `record.arm_track` and the recorder's own journal writes are a different code path (Other
     findings 1).
7. **Realtime.** Nothing added runs on the audio thread: the change is inside a `ControlRegistry`
   handler. `journalBegin` already did file I/O before this change and still does; no lock,
   allocation or log line was added to any render path.
8. **Undo stays one honest step.** The recorded transaction is one paired command
   (`record.recovery_discard`), so one `control.undo` removes the journal this call created (case 6),
   and a refused call records nothing (asserted in case 2 by comparing `control.transactions` before
   and after).
9. **Old projects and saved data.** Nothing here touches project files: the journal is a side file
   beside a take, never part of a project. The refusal only changes what a *new* `record.journal_begin`
   does when a side file is present.
10. **Qt 5 / MSVC portability.** Added API: `QFileInfo::exists` (static, Qt 5 and 6),
    `QCryptographicHash::hash`, `QTemporaryDir`, `QDir`, `QJsonObject`, `QTEST_GUILESS_MAIN` — all
    Qt 5.15-safe; no `QString::midRef`, no `std::filesystem`, no POSIX header, no bare-`'/'` path
    assumption (paths come from `QDir`).

## State sequence (for the proof manifest)

`src/core/ControlCommandsRecording.cpp` matches `src/core/*Record*`, so the manifest carries a
`state-sequence` for `JournalBeginGuardTest`, whose cases drive these side-file states in order:

1. `absent` — no side file; `record.journal_begin` succeeds and reports `frames_journalled` 0,
   `state` `in_progress`.
2. `in_progress` (4410 frames journalled) — a second `record.journal_begin` is refused, the file's
   sha256 is unchanged and recovery still reports 4410/4410 recoverable frames.
3. `damaged` — a side file with no `take=` line; `record.journal_begin` is refused and the file is
   left untouched.
4. `in_progress` → `finished` (journal removed) — `record.journal_begin` succeeds again.
5. `in_progress` → `restored` — `control.undo` dispatches `record.journal_begin`
   (`restored_by: record.journal_begin`) and the state is `in_progress` again, the take offered.
6. `absent` → `in_progress` → one `control.undo` (`restored_by: record.recovery_discard`) removes
   the journal.

## Other findings (not fixed here)

1. **The recorder's own arm path still clobbers a journal.** `TrackRecorder::arm()` calls
   `writeJournal()` unconditionally (`src/core/audio/TrackRecorder.cpp:203-208`), and
   `writeJournal()` writes `state=in_progress` with `framesOnDisk = m_framesRecorded` — 0 at arm
   time. A capture armed over a take whose side file recorded progress overwrites it exactly as
   `record.journal_begin` did, and `recordingjournal::write()` has no guard
   (`src/core/RecordingJournal.cpp:218-241`). That is the capture path, not the agent surface, and
   needs its own decision (refuse to arm, or move the old side file aside), so it is a separate unit.
2. **A `restored` journal written back through `record.journal_begin` loses its frame count.**
   `record.recovery_restore` records an inverse of `record.journal_begin` carrying only `take`,
   `sample_rate` and `channels` (`src/core/ControlCommandsRecordingRecovery.cpp:116-132`), so undoing
   a restore brings the offer back at `frames_on_disk` 0 (`ControlCommandsRecording.cpp:141`) even
   though recovery had reported the frames. `record.journal_place` does not have this problem: it
   records an action checkpoint holding the whole journal
   (`src/core/ControlCommandsRecordingPlace.cpp:120-142`). Fixing it wants `frames_on_disk` in the
   inverse, i.e. a change to the restore verb and (via its schema) the arguments it accepts.
3. **`journal_finish`'s inverse likewise starts at zero frames**
   (`src/core/ControlCommandsRecording.cpp:237-249`): undoing a clean stop re-creates the journal with
   `framesOnDisk = 0`. Lower harm (the capture had stopped cleanly), same shape as 2.
