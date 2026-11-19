FALSE-POSITIVE

# Cluster new-p9-199: false-positive report

## Verdict

The reported doubled root does not occur on this code. A note with children is a master note, and the track does not render master notes, so the root sounds once, through its interval-0 sub-note. No regression test is committed with this report. The evidence is the code references below and a scratch probe run, recorded in the commands section.

## Why not real

1. `include/NotePlayHandle.h:279-282`: `isMasterNote()` returns true when the note has sub-notes or `m_hadChildren` is set.
2. `src/core/NotePlayHandle.cpp:99-100`: constructing a child appends it to `parent->m_subNotes` and sets `parent->m_hadChildren = true`. This happens synchronously, in the constructor.
3. `src/core/InstrumentFunctions.cpp:263-269`: note stacking copies the base note for each chord interval and passes a child handle, with the base note as parent, into `addPlayHandle` (line 267). The child is constructed inside that call's argument list, so the parent has children once the call completes, and `isMasterNote()` is true for it.
4. `src/tracks/InstrumentTrack.cpp:780`: a note is played only when `n->isMasterNote() == false` (plus the instrument and layer checks). The parent with children is therefore skipped.
5. All 95 chord table entries (`src/core/InstrumentFunctions.cpp:44-145`) start with interval 0. A check with rg found 95 of 95 entries beginning `{ 0,`. The clone at interval 0 is therefore the only sounding root.
6. The arpeggio path calls `_n->setMasterNote()` at `src/core/InstrumentFunctions.cpp:351` for the same purpose. Its comment says that without it the note "will play as an ordinary note". The body of `setMasterNote()` was not read in this check.

## Commands and results

Log directory: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-199.r1/logs`

Exit codes. These are the exit codes the probe runs returned, as shown in the command output of the run. The log files themselves do not contain `EXIT` lines (`rg -c EXIT` on the log directory returned no matches).

- build-1: EXIT=0
- probe-green: EXIT=0
- build-2: EXIT=0
- probe-red: EXIT=8
- build-4: EXIT=0

Log contents, rechecked in this check with rg:

- `probe-green.log` (ChordRootProbeTest, CTest #328): `1/1 Test #328: ChordRootProbeTest ... Passed 1.46 sec`; `100% tests passed out of 1`. The green log does not print the root counts (no `rootCount` line); the passing QCOMPARE checks imply the count was 1.
- `probe-red.log` (same test): `1/1 Test #328: ChordRootProbeTest ... ***Failed 1.30 sec`; `FAIL!  : ChordRootProbeTest::rootSoundsOnceWithMajorChordOn() Compared values are not the same`; `Actual (rootCount): 2`; `Expected (1): 1`; `Loc: [../../tests/src/core/ChordRootProbeTest.cpp(132)]`; `Totals: 2 passed, 1 failed, 0 skipped`; `0% tests passed, 1 tests failed out of 1`; `Errors while running CTest`.
- `build-1.log` and `build-2.log`: last line `[100%] Built target ChordRootProbeTest`. Build-2 relinked the target.
- `build-4.log`: full `zene` build; last line `[100%] Built target zene`.

### Control mutation

The probe source, `tests/src/core/ChordRootProbeTest.cpp`, counted root sounds through a spy instrument on `InstrumentTrack::playNote`. It was scratch and is not kept.

The control mutation replaced the term `n->isMasterNote() == false` at `src/tracks/InstrumentTrack.cpp:780` with `true`. That disables the master skip. The mutant was built and run and gave RED (rootCount 2, expected 1; see `probe-red.log`). It was then reverted with `git checkout`. The logs do not record the mutation; this description comes from the record of the scratch run, not from a log.

Revert state, checked in this run: `git diff --quiet HEAD -- src/tracks/InstrumentTrack.cpp` returned EXIT=0, so the file matches HEAD. `tests/src/core/ChordRootProbeTest.cpp` is absent from the worktree and is not tracked (`git ls-files` returned no lines).

## Not verified

- No audio rendering through a real instrument plugin. The check is code references plus a probe that counts root sounds; no rendered audio was compared.
- No ThreadSanitizer run. This report changes no product code, so nothing new runs on the audio thread. The audio-thread status of the existing paths was not checked.
- The body of `setMasterNote()` was not read.
- The layer path below was traced only as far as the `acquire` call.

## Registration

Registration of this report in `tests/fork-sources.txt` is not decided here. It is deferred to the feature_check result.

## Other findings

- Layer copies and the parent's chord (design question). `src/tracks/InstrumentTrackNotes.cpp:297` calls `InstrumentLayers::playNote` (`src/core/InstrumentLayers.cpp:471-483`) before `addPlayHandle` at line 298. Each layer track gets a copy acquired without a parent (`InstrumentLayers.cpp:479`), so the copy is not a chord sub-note of the parent, and the parent's chord stacking is not applied to it on this path. That copy plays a different instrument by design (MX-112), so it is not a doubled root. Whether layers should inherit the parent's chord is an undocumented product decision; this report does not settle it.

## Self-check

(Pending: to be filled in by the self-check step.)
