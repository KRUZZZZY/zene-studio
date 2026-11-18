# cmds2 batch-10 — recording journal control commands

Lane: `ControlCommands[N-Z]*.cpp`. Six findings, all in the recording journal / recovery
cluster. Five share one root cause and one is a false positive.

## Shared root cause (clusters 2073, 4894, 4895, 4896)

Every journal command records its inverse as a **paired command invocation** rather than an
opaque undo step:

- `ControlCommandsRecording.cpp:132-136` — undo of `journal_begin` replays `record.journal_begin`
  with only `take`, `sample_rate`, `channels` (or `record.recovery_discard` when there was no
  journal before).
- `ControlCommandsRecording.cpp:222-223` — undo of `journal_finish` replays `record.journal_begin`
  with the same three fields.
- `ControlCommandsRecordingRecovery.cpp:128-129` — undo of `recovery_restore` replays
  `record.journal_begin` with the same three fields.

`journalArgsSchema` (`ControlRecordingSupport.cpp:146-156`) accepts exactly `take`,
`sample_rate`, `channels`, `project`, `track`, `start_ticks`. It has **no `frames_on_disk` and
no `state`**, so a replayed `record.journal_begin` cannot carry either field back. And
`journalBegin` itself hard-codes the two values that matter for recovery:

- `ControlCommandsRecording.cpp:104` — `journal.state = "in_progress"`
- `ControlCommandsRecording.cpp:107` — `journal.framesOnDisk = 0`

Recovery is not cosmetic about that number: `RecordingJournal.cpp:263` computes the offer as
`std::min(journal.framesOnDisk, framesInFile(journal.takePath))` and
`ControlRecordingSupport.cpp:75` reports it as `frames_recoverable`. A journal replayed with
`framesOnDisk = 0` therefore offers zero recoverable frames even though the WAV still holds the
audio. The audio itself is never moved or deleted (`ControlCommandsRecordingRecovery.cpp:119-123`
says so explicitly), so no recorded material is destroyed — but the recovery *offer* and its
project/track/start-tick metadata are lost.

Restoring either field through the existing paired inverse requires adding `frames_on_disk`
(and, for `journal_finish` / `recovery_restore`, a non-`in_progress` `state`) to
`journalArgsSchema`. That is a command params-schema change, which binding rule 8 assigns to the
parent lane and which would move the `ControlCommandsSnapshot` canary. All four are therefore
SKIPPED-SCOPE, and the paired inverse remains the wrong long-term shape.

Cluster 4893 is a fifth instance of the same pairing: `journalUpdate` refuses any count below the
current one at `ControlCommandsRecording.cpp:167-173` ("would move BACKWARDS"), while its own
inverse at `ControlCommandsRecording.cpp:185-190` passes the previous, smaller count back through
that same command. The backward refusal is a deliberate, documented guarantee locked by
`tests/control-recording-recovery.py:322-326`; loosening it is barred by rule 5, and the alternative
(dedicated restore command) is barred by rule 8. It does not corrupt anything: `undoByCommand`
(`ControlCommandsControl.cpp:330-346`) returns the inverse's `Refused` failure, so undo reports a
typed error and the journal keeps the larger, correct count.

---

### Cluster 2073 — Starting a journal can erase existing recovery progress

Verdict: SKIPPED-SCOPE
Evidence: `src/core/ControlCommandsRecording.cpp:99-107` — `journalBegin` reads any existing
journal into `previous` (line 100) and then unconditionally writes a fresh one with
`framesOnDisk = 0` and `state = "in_progress"` (lines 104, 107). Recovery reports
`min(framesOnDisk, framesInFile)` (`src/core/RecordingJournal.cpp:263`), so restarting a capture
over an interrupted take drops the recovery offer to zero. Fixing it either means refusing a
legitimate re-capture of the same take (a design change to the command's contract) or threading
the previous `frames_on_disk` into `journal_begin` (`journalArgsSchema`,
`src/core/ControlRecordingSupport.cpp:146-156`, has no such argument) — a params-schema change
reserved to the parent lane by rule 8.
Commit: none
Test: none
Red/green: none

### Cluster 4894 — Undoing `record.journal_begin` over an existing journal loses its contents

Verdict: SKIPPED-SCOPE
Evidence: `src/core/ControlCommandsRecording.cpp:128-131` builds the inverse args with only
`take`, `sample_rate`, `channels`, so the replay at `journalBegin` (lines 102-112) rewrites the
journal as `in_progress` / `framesOnDisk = 0` with empty `project`, `track` and `start_ticks`.
Same root cause and same blocker as cluster 2073: the frame count cannot be expressed in
`journalArgsSchema` (`src/core/ControlRecordingSupport.cpp:146-156`). The `project` / `track` /
`start_ticks` half is expressible today, but fixing it alone would leave the reported frame-count
loss in place, so it is left to the parent lane as a paired change.
Commit: none
Test: none
Red/green: none

### Cluster 4893 — `record.journal_update` cannot undo an update that advances the frame count

Verdict: SKIPPED-SCOPE
Evidence: `src/core/ControlCommandsRecording.cpp:167-173` refuses a smaller `frames_on_disk`
(`Refused`, "would move BACKWARDS"), and the inverse recorded at lines 185-190 replays
`record.journal_update` with exactly that smaller previous count. The refusal is an intended
monotonicity guarantee asserted by `tests/control-recording-recovery.py:322-326`, so rule 5
forbids weakening it; a dedicated restore command is forbidden by rule 8. Current behaviour is
fail-safe, not corrupting: `undoByCommand` (`src/core/ControlCommandsControl.cpp:330-346`)
surfaces the inverse's failure and the journal keeps the larger count.
Commit: none
Test: none
Red/green: none

### Cluster 4895 — Undoing `record.journal_finish` creates an empty recovery journal

Verdict: SKIPPED-SCOPE
Evidence: `src/core/ControlCommandsRecording.cpp:211-214` — the inverse replays
`record.journal_begin` with `take`, `sample_rate`, `channels` only, so the reappearing journal has
`framesOnDisk = 0` (`line 107`) and zero recoverable frames by
`src/core/RecordingJournal.cpp:263`. Restoring the removed journal's count and its finished
metadata needs `frames_on_disk` (and a non-`in_progress` `state`) in
`journalArgsSchema` (`src/core/ControlRecordingSupport.cpp:146-156`) — a params-schema change
under rule 8.
Commit: none
Test: none
Red/green: none

### Cluster 2921 — `record_options` can persist an invalid mode and out-of-range bar counts

Verdict: FALSE-POSITIVE
Evidence: the handler at `src/core/ControlCommandsRecordingPass.cpp:54-63` does no validation, but
it never sees invalid args. The command's `argsSchema`
(`src/core/ControlCommandsRecordingPass.cpp:156-157`) declares `count_in_bars` as
`integerProperty(0, 4)`, `pre_roll_bars` as `integerProperty(0, 8)` and `mode` as
`enumProperty(recordoptions::modeNames())` — the enum being exactly `{"overdub", "replace"}`
(`src/core/RecordOptions.cpp:60-63`). `ControlRegistry::invoke` runs `validateArgs` before the
handler (`src/core/ControlRegistry.cpp:251`), and `validateValue` applies
`checkType` → `checkRange` → `checkEnum` (`src/core/ControlSchema.cpp:152-159`); `checkRange`
rejects `-1` and `9` (lines 84-100) and `checkEnum` rejects any string outside the mode set
(lines 102-110), so `indexOf` can never return `-1` in the handler. `count_in_bars: 9` returning
`InvalidArgs` is already locked by `tests/src/core/RecordingPassTest.cpp:96-97`.
Commit: none
Test: none (existing `RecordingPassTest::theOptionsAreSetAndUndone` covers the range case)
Red/green: none

### Cluster 4896 — Undoing `record.recovery_restore` discards the recovered frame count

Verdict: SKIPPED-SCOPE
Evidence: `src/core/ControlCommandsRecordingRecovery.cpp:112-115` builds the inverse with only
`take`, `sample_rate`, `channels`, so undo replays `record.journal_begin` and the offer comes back
as `in_progress` with `framesOnDisk = 0` (`src/core/ControlCommandsRecording.cpp:107`) — reported
as zero journalled frames via `src/core/ControlRecordingSupport.cpp:70,75`. Same blocker as
clusters 2073/4894/4895: the frame count cannot be carried by `journalArgsSchema`
(`src/core/ControlRecordingSupport.cpp:146-156`) without a params-schema change under rule 8. The
`project` / `track` / `start_ticks` fields, which this inverse also drops and which *are* in that
schema, should be restored by the parent lane alongside the frame count.
Commit: none
Test: none
Red/green: none

Leftovers: five findings (2073, 4893, 4894, 4895, 4896) are unfixed and all hinge on one
parent-lane decision — whether `record.journal_begin`'s params schema grows `frames_on_disk` and
`state` (or a dedicated journal-restore command is added) so the paired inverses at
`src/core/ControlCommandsRecording.cpp:128-131,185-190,211-214` and
`src/core/ControlCommandsRecordingRecovery.cpp:112-115` can carry a whole journal back. Until that
lands, the in-scope residue is the `project` / `track` / `start_ticks` metadata those same four
inverses drop. No product, test or gate file was modified in this batch; no commit was made other
than this report.