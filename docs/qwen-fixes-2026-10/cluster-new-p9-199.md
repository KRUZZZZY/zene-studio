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

The fork-sources gate passed with this doc unregistered, so no entry was added to `tests/fork-sources.txt`.

## Other findings

- Layer copies and the parent's chord (design question). `src/tracks/InstrumentTrackNotes.cpp:297` calls `InstrumentLayers::playNote` (`src/core/InstrumentLayers.cpp:471-483`) before `addPlayHandle` at line 298. Each layer track gets a copy acquired without a parent (`InstrumentLayers.cpp:479`), so the copy is not a chord sub-note of the parent, and the parent's chord stacking is not applied to it on this path. That copy plays a different instrument by design (MX-112), so it is not a doubled root. Whether layers should inherit the parent's chord is an undocumented product decision; this report does not settle it.

## Gates

Run 1: `feature_check.py` on `kads-new-p9-199`, log `gates-1.log`. The log has no exit-code line. The exit code `EXIT=0` comes from the command output of that run; it was not re-run for this report.

```
feature-check kads-new-p9-199 (base fa4a5f2e7, 1 changed files; logs in /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-199/build/feature-check)
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

Run 2: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-199 > .../logs/gates-2.log 2>&1; echo EXIT=$?` gave `EXIT=0`. The table below is from `gates-2.log`.

```
feature-check kads-new-p9-199 (base fa4a5f2e7, 2 changed files; logs in /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-199/build/feature-check)
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

Run 4: gates-4.log, EXIT=0, RESULT: PASS (`feature_check.py` run unpiped with `echo EXIT=$?` before the self-check citation commit; the RESULT line is `logs/gates-4.log:10`).

## Self-check

No product code changed. The branch diff against the base is this report only: `git diff --stat fa4a5f2e7 HEAD` lists `docs/qwen-fixes-2026-10/cluster-new-p9-199.md` alone. Answers are checked against the worktree in the self-check run.

1. **Regression test fails with the change reverted and passes with it: N/A.** There is no product change to revert, and no regression test is committed (Verdict). The only test run was a scratch probe, ChordRootProbeTest (CTest #328). With the unmodified code it passed, EXIT=0 (`logs/probe-green.log:3`). With a control mutation at `src/tracks/InstrumentTrack.cpp:780` (`== false` changed to `true`) it failed, EXIT=8, with `Actual (rootCount): 2`, `Expected (1)` (`logs/probe-red.log:9,10`). The exit codes come from the command output recorded in "Commands and results", not from the log files. The mutation was reverted: `git diff --quiet HEAD -- src/tracks/InstrumentTrack.cpp` returned EXIT=0 in the self-check run.

2. **Deterministic: not verified.** The probe is not kept (`git ls-files tests/src/core/ChordRootProbeTest.cpp` returned 0 lines), so no committed test exists to check for timing or races. The scratch probe injected its seam as a spy on `InstrumentTrack::playNote` (Commands and results, Control mutation). The logs show runtime only (`Passed 1.46 sec`), not an assertion on time.

3. **Fix at the cause and complete: N/A as a fix; sibling coverage is partial.** Nothing was changed. The verdict rests on the chord stacking path and the master-note skip (item 5 below). The arpeggio path calls `setMasterNote()` at `src/core/InstrumentFunctions.cpp:351`, but its body was not read (Not verified). The layer path (`src/tracks/InstrumentTrackNotes.cpp:297`, `src/core/InstrumentLayers.cpp:471-483`) was traced only as far as `acquire` (Not verified, Other findings).

4. **No collateral damage: N/A.** No product code has changed since the base `fa4a5f2e7`, so old projects and saved data are unaffected. No limit or check was added.

5. **FALSE-POSITIVE evidence (file:line, checked in the self-check run):**
   - `include/NotePlayHandle.h:279-282`: `isMasterNote()` returns `m_subNotes.size() > 0 || m_hadChildren`.
   - `src/core/NotePlayHandle.cpp:99-100`: building a child appends it to `parent->m_subNotes` and sets `m_hadChildren = true`.
   - `src/core/InstrumentFunctions.cpp:263-269`: the chord copy is acquired with `_n` as parent (line 269) and passed to `addPlayHandle` (line 267).
   - `src/tracks/InstrumentTrack.cpp:780`: `n->isMasterNote() == false` gates playing, so the parent with children is skipped.
   - `src/core/InstrumentFunctions.cpp:44-145`: all 95 chord table entries start with `{ 0,`, so the interval-0 clone is the only sounding root.
