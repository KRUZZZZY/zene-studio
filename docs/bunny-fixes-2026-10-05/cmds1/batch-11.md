# Batch 11 — Space Bunny lane `cmds1`

Six findings triaged against the current tree: four fixed, one real but untestable
headlessly, one out of scope. No command was added, removed or renamed and no
command's params or result schema changed.

Environment note: as in earlier batches the build tree has `LIST_PLUGINS:BOOL=OFF`,
so no audible instrument exists and the freeze transcript
(`ControlFreezeCommandsTranscript`, ctest 336) reports
`PASS: no audible instrument in this build: Skipped, never Passed` and fails. That
is why the one freeze-region finding below is REAL-UNTESTED rather than fixed.

## Cluster 3878 — Feedback state says compensation is suspended when feedback is off

Verdict: FIXED

Evidence: `src/core/ControlCommandsFeedback.cpp:164` (old `:158`) inserted the
`SUSPENSION` sentence into `feedbackState()` unconditionally, so
`feedback.get_state` answered "compensation is SUSPENDED" while the same reply
reported `pdc_suspended: false` (`:162`). The sentence is now chosen from the
same `feedbackMode()` flag, with a new `NO_SUSPENSION` constant (`:97-101`).
`feedback.enable`'s own `suspension` field (`:203`) is unchanged — it runs only
after the mode is on.

Commit: 13cced654

Test: `FeedbackModeActionTest::theSuspensionTextFollowsTheSubmode`

Red/green: RED `cmake --build build --target FeedbackModeActionTest zene` EXIT=0,
`cd build/tests && ctest -R '^FeedbackModeActionTest$' --output-on-failure` EXIT=8
(`!off.value("suspension").toString().contains("SUSPENDED")` returned FALSE).
GREEN build EXIT=0, same ctest EXIT=0. Gates: complexity EXIT=0,
file-length EXIT=0.

## Cluster 1092 — Feedback-disable's transaction inverse does not restore removed sends

Verdict: FIXED

Evidence: `src/core/ControlCommandsFeedback.cpp:253` (old `:244`) built the
transaction with `transactionPayload(before, "feedback.enable", {}, true, ...)`.
`feedback.enable` only calls `Mixer::setFeedbackMode(true)`
(`ControlCommandsFeedback.cpp:200-215`), so replaying the advertised inverse
returned the mode with every removed route still missing. The inverse is now
`control.undo` — the one command the recorded action checkpoint (`:237-239`,
`restoreEdges` re-creating each send through `Mixer::createChannelSend`) makes
exact. Same precedent as `ControlCommandsDeviceCopy.cpp:153`. The registry
strips the private `__transaction` key from the wire result
(`ControlRegistry.cpp:389`), so the regression reads the recorded inverse back
out of `control.transactions`.

Commit: 64f72e334

Test: `FeedbackModeActionTest::theDisableInverseIsTheCommandThatRestoresTheSends`

Red/green: RED build EXIT=0, ctest EXIT=8 (recorded inverse op `"feedback.enable"`,
expected `"control.undo"`). The first RED run failed for the wrong reason — the
test read the stripped `__transaction` key and saw an empty op — so the test was
corrected to read `control.transactions` and RED was re-confirmed against the
unfixed source before committing. GREEN build EXIT=0, ctest EXIT=0. Gates:
complexity EXIT=0, file-length EXIT=0.

## Cluster 5664 — Unfreeze advertises the wrong inverse for a regional take

Verdict: FIXED

Evidence: `src/core/ControlCommandsFreeze.cpp:260-263` built the transaction with
`transactionPayload(before, "freeze.track", {track}, true, ...)`. `freeze.track`
freezes the WHOLE track and renders a NEW take, so it cannot recreate a regional
take or the audio that came off — the code's own comment above it said so. The
checkpoint the call takes at `:253` carries the exact take, so the inverse is now
`control.undo`.

The regression installs a real take through the public `Track::freezeTo`
(`include/Track.h:245`, a control-thread load, not a render) using a real WAV
written by the shared `taketest::writeToneWav` fixture, so the proof needs no
child render process.

Commit: 55e708642

Test: `ControlVerbInverseTest::theUnfreezeInverseRestoresTheTakeItTookOff`

Red/green: RED `cmake --build build --target ControlVerbInverseTest zene` EXIT=0,
`cd build/tests && ctest -R '^ControlVerbInverseTest$' --output-on-failure` EXIT=8
(recorded inverse op `"freeze.track"`, expected `"control.undo"`). GREEN build
EXIT=0, ctest EXIT=0 (all 9 cases). Gates: complexity EXIT=0, file-length EXIT=0.

## Cluster 3879 — Project handoff can overwrite files in the destination folder

Verdict: FIXED

Evidence: `src/core/ControlCommandsHandoff.cpp:120-150` (old `:131`) wrote the
session copy via `Song::saveProjectFile` and the manifest via `QSaveFile` after
checking only `isSessionFile()` (`:114-118`), which refuses just the currently
open session's own file. Any other `<project>.mmpz` or `HANDOFF.json` already in
the folder was replaced silently. The command now refuses first, through the new
`takenFile()` helper, naming the file already there. Refusing rather than adding an
`overwrite` param keeps the args schema untouched (rule 8). `handoff()` was split
(`takenFile`, `writeManifest`) because the new branch pushed it to CCN 12, over the
gate's limit of 10.

Commit: 0541a3936

Test: `LibraryProjectsTest::aHandOffNeverOverwritesTheDestinationFolder`

Red/green: RED `cmake --build build --target LibraryProjectsTest zene` EXIT=0,
`cd build/tests && ctest -R '^LibraryProjectsTest$' --output-on-failure` EXIT=8
(`refused.errorKind` 0, expected `ControlErrorKind::Refused` 5). GREEN build
EXIT=0, ctest EXIT=0 (all 11 cases). Gates: complexity EXIT=0, file-length
EXIT=0.

## Cluster 5662 — Region freeze silences clip tails beyond the frozen region

Verdict: REAL-UNTESTED

Evidence: `src/core/ControlCommandsFreeze.cpp:102-124` `clipsInside()` includes
any clip whose START is in `[range.start, range.end)` and records it as a whole
(`{start, clip->length().getTicks()}`, `:113`). A clip starting inside the region
but ending after `range.end` is therefore muted in full by
`Track::freezeTo` (`src/core/Track.cpp:1074-1108`, `clip->setMuted(true)`), while
the take itself stops at the region boundary — so the tail past the region is
silent. Only a clip crossing the region's START is handled, and it is reported as
overlapping instead (`:115-121`). The asymmetry in the code is real.

No product change: every path to this behaviour runs
`BounceInPlace::renderTrack` (`ControlCommandsFreeze.cpp:183`), which serialises
the session and shells out to `QCoreApplication::applicationFilePath()`
(`src/core/BounceInPlace.cpp:171-184`). In a unit test that is the test binary,
which cannot render, and the end-to-end `ControlFreezeCommandsTranscript` skips
here for want of an audible instrument. Splitting the crossing clip would be a
design change to what a regional freeze means in any case.

Commit: none

Test: none

Red/green: none.

## Cluster 5663 — Failed freeze installation leaves a journal checkpoint and rendered file

Verdict: SKIPPED-SCOPE

Evidence: `src/core/ControlCommandsFreeze.cpp:202-208` takes
`track->addJournalCheckPoint()` before `track->freezeTo(...)`, and returns the
refusal at `:207` when the installation fails, leaving both the checkpoint and the
rendered file behind. The reading is correct.

Rolling the checkpoint back needs an API `ProjectJournal` does not have —
`include/ProjectJournal.h:101-258` offers only push (`addJournalCheckPoint`,
`addJournalAction`, `addJournalStructure`) plus `clearJournal`/`clearJournal`-style
whole-stack resets, no checkpoint-removal. Adding one is an engine/journal change
rather than a command-layer fix. It is also unreachable headlessly for the same
reason as 5662: the failure needs a render that succeeds and an installation that
does not, and this build can produce no render.

Commit: none

Test: none

Red/green: none.

## Leftovers

- `ControlFreezeCommandsTranscript` (ctest 336) and `ControlFeedbackCommands`
  (ctest 351) still fail in this build tree for reasons that predate this batch and
  are unrelated to it: 336 skips for want of a loadable instrument, and 351's two
  failures are saved-project sha256 baselines. 351's two failures were confirmed
  pre-existing by reverting the batch's source change, rebuilding `zene` and
  re-running: still EXIT=8, the same two baseline checks. Nothing in this batch
  touches saved-project bytes.
- Cluster 5662's fix is still owed: clip tails past a region freeze are silenced.
  It needs a render-capable environment or a seam that does not go through
  `BounceInPlace::renderTrack`.
- Cluster 5663's checkpoint/file rollback needs a `ProjectJournal` removal API.
