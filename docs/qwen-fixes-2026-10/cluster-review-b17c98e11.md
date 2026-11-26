FIXED

# review-b17c98e11 - over-cap project markers were still lost on the next save

## Verdict: REAL
Commit b17c98e11 only added a `qWarning` in `ProjectMarkers::load` (src/core/ProjectMarkers.cpp). Markers past `MaxMarkers` (512) were still dropped, and the next `Song::saveProjectFile` wrote the 512-marker list over the only file that held the rest.

## Existing contract used (not a new one)
- `Song::saveProjectFile` (src/core/Song.cpp ~2482-2506) already treats "a session holds LESS than the file it was loaded from" as read-only: it refuses and says why through `Song::saveRefusal()` (include/Song.h:202). Every save path already surfaces that refusal (ControlCommandsProjectFiles.cpp:242, ControlCommandsProjectLifecycle.cpp:338, ControlCommandsHandoff.cpp:221, CollabSessionSync.cpp:176).
- Load problems are reported through `Song::collectError` / `Song::errors()` (include/Song.h:167-177), which `loadProject` resets with `clearErrors()` before walking sections.
- The cap stays, and the existing `aMarkerListOverTheCapIsReportedNotSilentlyTruncated` test is untouched and still passes: the over-cap file is still loaded (512 markers), not refused.

## Fix
- `include/ProjectSection.h`: new virtual `saveRefusal()` (default empty) and `static firstSaveRefusal()` - a generic extension point, no behaviour change for the other sections (SubprojectSection, AlternativesSection, ...).
- `src/core/SongSections.cpp`: `ProjectSection::firstSaveRefusal()`.
- `src/core/Song.cpp`: `saveProjectFile` returns false with that refusal as `saveRefusal()` (3 lines, after the partial-load check, before anything is written).
- `include/ProjectMarkers.h` / `src/core/ProjectMarkers.cpp`: `load()` records how many markers it left out (`droppedOnLoad()`, reset by `clear()` and each `load()`), reports them through `Song::collectError` (so project.open's `errors` carries it), and `saveRefusal()` names the count and the cap. The original file is never written by the refused save.
- A session loaded from a file that fits the cap, or cleared, saves exactly as before. No audio-thread code, no control commands, no undo changes. Undo/redo of markers (`restore`) does not touch the flag. (Corrected in round 5: an undo SNAPSHOT is affected - `captureUndoSnapshot` builds the document the restore reloads, so since round 5 the snapshot carries the count itself; see "Round 5".)

Behaviour note: a session that dropped markers cannot be saved (including autosave) until it is cleared or reopened from a file that fits; the refusal text says so.

## Regression test
`tests/src/core/ProjectMarkersTest.cpp` - `aProjectOverTheCapLoadedThenSavedDoesNotLoseMarkersInSilence`: writes a project with 515 markers, loads it via `Song::loadProject`, asserts 512 loaded, the load's `errors()` mention markers, `saveProjectFile` returns false with a refusal naming markers, no output file, original file bytes unchanged; then a cleared project with one marker saves fine. Deterministic (no timing).

## Commands (logs in /home/kruzzzzy/.config/kads/zene/state/runs/review-b17c98e11.r1/logs)
- GREEN build: `cmake --build build -j4 --target zene ProjectMarkersTest PartialLoadTest ProjectSectionTest` > build-green.log EXIT=0
- GREEN test: `cd build/tests && ctest -R '^(ProjectMarkersTest|PartialLoadTest|ProjectSectionTest)$' --output-on-failure` > test-green.log EXIT=0 (3/3 passed)
- 4b fix patch: `git diff -- include/ProjectMarkers.h include/ProjectSection.h src/core/ProjectMarkers.cpp src/core/SongSections.cpp src/core/Song.cpp > fix.patch` EXIT=0; `git apply -R fix.patch` EXIT=0
- RED build: `cmake --build build -j4 --target zene ProjectMarkersTest` > build-red.log EXIT=0
- RED test: `cd build/tests && ctest -R '^ProjectMarkersTest$' --output-on-failure` > test-red.log **EXIT=8** - `ProjectMarkersTest.cpp:381: 'reported' returned FALSE. (the load's error report says nothing about the markers it dropped)`; the only failing slot (12 passed, 1 failed)
- `git apply fix.patch` EXIT=0; GREEN rebuild > build-green2.log EXIT=0; GREEN rerun of the three tests > test-green2.log EXIT=0 (3/3 passed)
- Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` > gates.log EXIT=0, RESULT: PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS)

## Unverified
Only the three tests above were run; the wider ctest suite was not. The marker/ProjectSection header change recompiles many targets, which I did not build individually. Local TSan unavailable; the change is main-thread only (load/save).

Delegation: Haiku ran the GREEN build+ctest and the RED/GREEN reverse-patch proof (steps A-G); I read the logs (test-red.log, test-green2.log) and git status myself.

## Other findings
None.

## Self-check
- Red/green: with the five product files reverted (`git apply -R fix.patch`) the test fails, EXIT=8, at ProjectMarkersTest.cpp:381 (test-red.log); with the fix re-applied it passes, EXIT=0 (test-green2.log).
- Deterministic: the test uses a fixed generated file (515 markers, ids/ticks computed from the loop index), a QTemporaryDir and direct `Song::loadProject` / `saveProjectFile` calls; no clock, sleep, thread or race.
- Cause and completeness: the loss was in the load-then-save pair. Load now records and reports it (`collectError`, `droppedOnLoad`), and the one save entry point, `Song::saveProjectFile`, refuses. Every other writer (project.save, handoff, collab sync, render temp files) goes through `saveProjectFile` and already surfaces `saveRefusal()` (ControlCommandsProjectFiles.cpp:242, ControlCommandsProjectLifecycle.cpp:338, ControlCommandsHandoff.cpp:221, CollabSessionSync.cpp:176). (Round-1 claim, wrong: undo snapshots ARE affected, because `Song::restoreUndoSnapshot` reloads the snapshot through `ProjectMarkers::load`. Rounds 3-4 held the count across the restore; round 5 replaced that with a count carried inside the snapshot document - see "Round 5".) The other ProjectSection subclasses (SubprojectSection, AlternativesSection) inherit the empty default `saveRefusal()`.
- Collateral: the cap is unchanged and files at or under 512 markers load and save byte-for-byte as before (refusal only when `m_droppedOnLoad > 0`; `clear()` and each `load()` reset it, so Song::clearProject / opening another project lifts it - covered by the test's final clear-and-save step; PartialLoadTest and ProjectSectionTest still pass). No other caller legitimately exceeds the cap: `add()` already returns -1 at MaxMarkers.
- Not ALREADY-FIXED / FALSE-POSITIVE: verdict is FIXED (b17c98e11 only warned; shown by test-red.log).
- Sibling commands (updated in round 3): 33495a1610 changed one command file, src/core/ControlCommandsProjectFiles.cpp, and added no control command. Its siblings: `project.save` (:195) now answers the section refusal before rotation; `project.restore_revision` (:340) writes a retained file's bytes over the live file, not the session in memory, so it cannot write the reduced marker list and needs no gate; `project.get_state` (:477) is read-only; `project.open` (:561) loads, and `ProjectMarkers::load` resets the dropped count, so it lifts the refusal. No app.* command or arrangement.* command (the other names the check lists) shares this code or writes the session, so they are not affected.
- Round-3 red/green for the revision gate: gate-only revert EXIT=8 at ProjectMarkersTest.cpp:456 (test-2-redA.log), full revert EXIT=8 at :381 and :447 (test-3-redB.log), restored EXIT=0 3/3 (test-4-green.log), all in the r3 log folder.
- Round-4 red/green (r4 log folder, details in "Round 4" below): gate-only revert EXIT=8 at ProjectMarkersTest.cpp:458 (test-2-redA.log); undo-hold-only revert EXIT=8 at :509 (test-3-redC.log); every cluster product hunk reverted, on the committed test, EXIT=8 at :381, :447, :507 (test-6-redB.log); restored EXIT=0, 3/3 (test-7-green.log). feature_check EXIT=0 PASS (gates-8.log); submit_check EXIT=0 PASS (submit-9.log).
- Round-4 determinism: the new asserts read `ProjectMarkers::droppedOnLoad()` after the same fixed 515-marker file is loaded. No clock or thread.
- Round-4 completeness: no product code changed this round. `git diff --stat 46c76ba160 HEAD` over the seven product files is empty, so the train merge left the fix as reviewed.

## Round 2 (proof manifest needs a state-sequence)
Round 1's manifest lacked a `state-sequence`, which the rule requires because the fix touches `src/core/Song.cpp` and `src/core/SongSections.cpp`. No product code changed in round 2; the product commit stays 319b074b96. The manifest (`review-b17c98e11.r2/logs/proof.json`) now carries a `state-sequence` artifact for `ProjectMarkersTest::aProjectOverTheCapLoadedThenSavedDoesNotLoseMarkersInSilence`, with these states in the order the test drives them:
1. `clearProject` + `loadProject(over-cap.mmp)` with 515 markers: 512 markers in memory, `errors()` names the markers, `droppedOnLoad()` = 3.
2. `saveProjectFile(out.mmp)` refused: returns false, `saveRefusal()` names the markers, `out.mmp` does not exist, `over-cap.mmp` bytes unchanged.
3. `clearProject()` + `add(96, "Fits")`: 1 marker, `droppedOnLoad()` = 0.
4. `saveProjectFile(out.mmp)` succeeds: `saveRefusal()` empty, `out.mmp` exists.

The r1 log folder no longer exists, so the red/green proof was re-run into the r2 logs (`$L` = .config/kads/zene/state/runs/review-b17c98e11.r2/logs):
- `git diff 9913d36d53 HEAD -- include/ProjectMarkers.h include/ProjectSection.h src/core/ProjectMarkers.cpp src/core/SongSections.cpp src/core/Song.cpp > $L/fix.patch` EXIT=0 (5611 bytes); `git apply -R $L/fix.patch` EXIT=0
- RED build `cmake --build build -j4 --target zene ProjectMarkersTest` > build-2-red.log EXIT=0; RED test `cd build/tests && ctest -R '^ProjectMarkersTest$' --output-on-failure` > test-2-red.log **EXIT=8**: `FAIL! ProjectMarkersTest::aProjectOverTheCapLoadedThenSavedDoesNotLoseMarkersInSilence() 'reported' returned FALSE ... Loc: ProjectMarkersTest.cpp(381)` (the only failing slot)
- `git apply $L/fix.patch` EXIT=0; GREEN build `... --target zene ProjectMarkersTest PartialLoadTest ProjectSectionTest` > build-3-green.log EXIT=0; GREEN test `ctest -R '^(ProjectMarkersTest|PartialLoadTest|ProjectSectionTest)$' --output-on-failure` > test-3-green.log EXIT=0 (3/3 passed). A separate run without any revert (build-1.log / test-1.log) was also EXIT=0, 3/3.
- Gates: `feature_check.py` > gates.log EXIT=0, RESULT: PASS. After the re-apply `git status --short` and `git diff HEAD --stat` were empty.
Delegation (round 2): Haiku ran the build+ctest and the reverse-patch red/green steps; I read test-2-red.log, test-3-green.log and git status myself.

## QA follow-up (review round 2)

The refusal was not answered before `project.save` rotates the keep-3 revision set:
`ControlCommandsProjectFiles.cpp` gated only `isPartialLoad()` before `rotateProjectRevision()` (:256, :276)
and called `Song::saveProjectFile()` afterwards (:285). An over-cap session refuses INSIDE
`saveProjectFile()`, so the refused save had already shifted rev0 -> rev1 -> rev2 and dropped the oldest
retained revision - the loss `PartialLoadTest::aRefusedProjectSaveDoesNotRotateTheRevisionSet` exists to
prevent, and it is the only writer the round-2 claim above ("every other writer ... already surfaces
saveRefusal()") did not cover: it surfaces the refusal, but only after the rotation.

Fixed in the same design: `project.save` now asks `ProjectSection::firstSaveRefusal()` beside the
`isPartialLoad()` gate and answers the refusal before the rotation (src/core/ControlCommandsProjectFiles.cpp).
Regression: `ProjectMarkersTest::aRefusedOverCapSaveDoesNotRotateTheRevisionSet` - an over-cap session and a
target with a full revision set; `project.save` is refused, names the markers, and the live file and rev0..rev2
hold the bytes they held before. Without the gate the slot fails at the revision compare
(`ProjectMarkersTest.cpp:456`, actual `live\n`, expected `rev0\n`); with the whole product change reversed it
fails at `!saved.ok` (`:447`).

## Round 3 (verification only, no new product code)

The round-2 review's [crash-or-data-loss] entry (project.save rotated the keep-3 revision set before the
engine's refusal) is **resolved in commit 33495a1610**: `registerProjectSave` now asks
`ProjectSection::firstSaveRefusal()` right after the `isPartialLoad()` gate and returns
`ControlErrorKind::Refused` (`"<target>: <refusal>"`) before `control::rotateProjectRevision`
(src/core/ControlCommandsProjectFiles.cpp:268-277 gate, rotation afterwards at :288). This round adds no
product code; it rebuilt HEAD 33495a1610 and re-proved both halves of the fix.

Commands (`$L` = /home/kruzzzzy/.config/kads/zene/state/runs/review-b17c98e11.r3/logs; ctest from build/tests):
- `git status --short` empty, `git rev-parse HEAD` = 33495a16109d1db2ae476c8f484055d2b73d4c45
- GREEN: `cmake --build build -j4 --target zene ProjectMarkersTest PartialLoadTest ProjectSectionTest > $L/build-1.log 2>&1` EXIT=0;
  `ctest -R '^(ProjectMarkersTest|PartialLoadTest|ProjectSectionTest)$' --output-on-failure > $L/test-1.log 2>&1` EXIT=0 ("100% tests passed out of 3")
- RED-A, only the new gate reverted: `git diff 33495a1610~1 33495a1610 -- src/core/ControlCommandsProjectFiles.cpp > $L/fix.patch` EXIT=0; `git apply -R $L/fix.patch` EXIT=0;
  `cmake --build build -j4 --target zene ProjectMarkersTest > $L/build-2-redA.log 2>&1` EXIT=0;
  `ctest -R '^ProjectMarkersTest$' --output-on-failure > $L/test-2-redA.log 2>&1` **EXIT=8**:
  `FAIL! : ProjectMarkersTest::aRefusedOverCapSaveDoesNotRotateTheRevisionSet() Compared values are not the same` (Actual `"live\n"`, Expected `"rev0\n"`), `Loc: [../../tests/src/core/ProjectMarkersTest.cpp(456)]`, `Totals: 13 passed, 1 failed`
- RED-B, gate AND the 319b074b96 product hunks reverted: `git diff 9913d36d53 319b074b96 -- include/ProjectMarkers.h include/ProjectSection.h src/core/ProjectMarkers.cpp src/core/SongSections.cpp src/core/Song.cpp > $L/fix-319.patch` EXIT=0; `git apply -R $L/fix-319.patch` EXIT=0;
  `cmake --build build -j4 --target zene ProjectMarkersTest > $L/build-3-redB.log 2>&1` EXIT=0;
  `ctest -R '^ProjectMarkersTest$' --output-on-failure > $L/test-3-redB.log 2>&1` **EXIT=8**:
  `aProjectOverTheCapLoadedThenSavedDoesNotLoseMarkersInSilence() 'reported' returned FALSE` at `ProjectMarkersTest.cpp(381)` and
  `aRefusedOverCapSaveDoesNotRotateTheRevisionSet() '!saved.ok' returned FALSE` at `ProjectMarkersTest.cpp(447)`, `Totals: 12 passed, 2 failed`
- Restore: `git apply $L/fix-319.patch` EXIT=0; `git apply $L/fix.patch` EXIT=0; `git status --short` and `git diff HEAD --stat` empty
- GREEN after restore: `cmake --build build -j4 --target zene ProjectMarkersTest PartialLoadTest ProjectSectionTest > $L/build-4-green.log 2>&1` EXIT=0;
  `ctest -R '^(ProjectMarkersTest|PartialLoadTest|ProjectSectionTest)$' --output-on-failure > $L/test-4-green.log 2>&1` EXIT=0 ("100% tests passed out of 3")
- Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > $L/gates.log 2>&1` EXIT=0, RESULT: PASS (all 8 rows PASS; complexity and file-length: no new item)

Corrections to earlier rounds:
- The round-2 state-sequence listed `droppedOnLoad()` = 3 / = 0. No test asserts that accessor (it has no caller in the tree);
  the states the test actually checks are the marker count (512), the load `errors()` naming markers, the refused save
  (false, `saveRefusal()` names markers, no output file, original bytes unchanged), and the cleared session saving again.
  The round-3 manifest lists only those. Asserting `droppedOnLoad() == 3` was left out of this round on purpose, because
  it would shift the `:456` location the checkpoint asked to re-prove; it stays a non-blocking follow-up.
- Line numbers cited in round 1 (`ControlCommandsProjectFiles.cpp:242`, `:256/:276/:285`) predate 33495a1610.

Known behaviour, for the release note (not changed here): a session that dropped over-cap markers on load cannot autosave
(`MainWindow::autoSave` -> refused `saveProjectFile`) and GUI Save returns false without a dialog, the same as the
existing partial-load refusal. It saves again after `clearProject` or after opening a file that fits the cap.
Also: `dropped = total - offered`, so markers past the cap with an unparseable tick are counted in the warning/refusal text.

Delegation (round 3): Haiku ran the GREEN build+ctest, both reverse-patch RED runs and the restore; I read
test-1/test-2-redA/test-3-redB/test-4-green and `git status` myself and ran feature_check myself (one command).
No Sonnet or DeepSeek use: no code was written this round.

## QA follow-up (review round 3, by the reviewer)

The refusal was not carried across an IN-PLACE restore of the session's own document.
`Song::restoreUndoSnapshot` reloads the session from its snapshot, and that snapshot holds the REDUCED
marker list, so `ProjectMarkers::load` reset `m_droppedOnLoad` to 0 and the next save wrote the 512-marker
list over the file that still had the rest - silently, and through an ordinary undo: every undo step that
carries a snapshot (`UndoHistory` with `persist` on, `control.undo`, a session checkpoint, `project.new`'s
rollback) goes through that restore. Measured before the fix, with the round-3 product code in place:
`ProjectMarkersTest::anUndoRestoreDoesNotLiftTheOverCapSaveRefusal` failed at `:507` - `saveProjectFile`
returned true after `captureUndoSnapshot()` + `restoreUndoSnapshot()` of a 515-marker session.

Fixed here in the same design, the way `m_notLoadedSections` is already held across the restore
(`src/core/SongUndoSnapshot.cpp`): `ProjectMarkers::droppedOnLoad()` is read before the load and put back
after it with `ProjectMarkers::holdDroppedOnLoad()` (only ever raises the count, so `clearProject()` and a
load of another project still take it back to 0). Regression:
`tests/src/core/ProjectMarkersTest.cpp::anUndoRestoreDoesNotLiftTheOverCapSaveRefusal`.

Verified after the fix: `cmake --build build --target zene ProjectMarkersTest` EXIT=0 and
`ctest -R '^(ProjectMarkersTest|PartialLoadTest|ProjectSectionTest|SessionCheckpointTest|SessionCheckpointBudgetTest|UndoHistoryPersistenceTest|UndoHistoryOptionsTest|ProjectContainerSaveTest)$'`
EXIT=0, 8/8 passed; with only the two QA hunks reverted the new slot fails at `:507` EXIT=1.

Gate note: `feature_check.py` could not run in the QA sandbox - its baseline checkout of `9913d36d53` is
refused there (`bunny-guard: ... not allowed in a Bunny lane`), EXIT=1.

## Round 4 (verification only, no new product code)

The round-3 send-back repeats the round-2 [crash-or-data-loss] entry (project.save rotated the keep-3 revision
set before the engine's refusal). It is **resolved in commit 33495a1610**, which is on this branch:
`registerProjectSave` (src/core/ControlCommandsProjectFiles.cpp:268-277) asks `ProjectSection::firstSaveRefusal()`
right after the `isPartialLoad()` gate and returns `ControlErrorKind::Refused` (`"<target>: <refusal>"`) before
`control::rotateProjectRevision` (:288). The reviewer's undo-restore follow-up (46c76ba160,
src/core/SongUndoSnapshot.cpp + `ProjectMarkers::holdDroppedOnLoad`) is also on the branch. The train merge
dfa90f9532 did not touch any of the seven product files (`git diff --stat 46c76ba160 HEAD -- <files>` empty).
This round adds no product code. Its only change is the optional test follow-up: `droppedOnLoad() == 3` after the
over-cap load and `droppedOnLoad() == 0` after `clearProject()` in
`aProjectOverTheCapLoadedThenSavedDoesNotLoseMarkersInSilence` (tests/src/core/ProjectMarkersTest.cpp:374, :394).
Those two lines shift the revision-compare location from :456 to :458.

Commands (`$L` = /home/kruzzzzy/.config/kads/zene/state/runs/review-b17c98e11.r4/logs; B = `cmake --build build -j4 --target zene ProjectMarkersTest PartialLoadTest ProjectSectionTest`; T = `cd build/tests && ctest -R '^(ProjectMarkersTest|PartialLoadTest|ProjectSectionTest)$' --output-on-failure`):
- Patches: `git show --format= 33495a1610 -- src/core/ControlCommandsProjectFiles.cpp > $L/gate.patch`; `git show --format= 46c76ba160 -- include/ProjectMarkers.h src/core/SongUndoSnapshot.cpp > $L/undo.patch`; `git diff 319b074b96^ 46c76ba160 -- include/ProjectMarkers.h include/ProjectSection.h src/core/ProjectMarkers.cpp src/core/SongSections.cpp src/core/Song.cpp src/core/ControlCommandsProjectFiles.cpp src/core/SongUndoSnapshot.cpp > $L/fix.patch` (9032 bytes, every product hunk of the cluster); `git apply -R --check` EXIT=0 for all three
- GREEN (with the new asserts): B > build-1-green.log EXIT=0; T > test-1-green.log EXIT=0 ("100% tests passed out of 3")
- RED-A, only the project.save gate reverted: `git apply -R $L/gate.patch` EXIT=0; B > build-2-redA.log EXIT=0; T > test-2-redA.log **EXIT=8**: `FAIL! : ProjectMarkersTest::aRefusedOverCapSaveDoesNotRotateTheRevisionSet() Compared values are not the same`, Actual `"live\n"`, Expected `"rev0\n"`, `Loc: [../../tests/src/core/ProjectMarkersTest.cpp(458)]`, `Totals: 14 passed, 1 failed`; `git apply $L/gate.patch` EXIT=0
- RED-C, only the undo-restore hold reverted: `git apply -R $L/undo.patch` EXIT=0; B > build-3-redC.log EXIT=0; T > test-3-redC.log **EXIT=8**: `FAIL! : ProjectMarkersTest::anUndoRestoreDoesNotLiftTheOverCapSaveRefusal() '!song->saveProjectFile(target)' returned FALSE`, `Loc: [../../tests/src/core/ProjectMarkersTest.cpp(509)]`, `Totals: 14 passed, 1 failed`; `git apply $L/undo.patch` EXIT=0
- RED-B with the new asserts still in place: `git apply -R $L/fix.patch` EXIT=0; B > build-4-redB.log **EXIT=2** (`ProjectMarkersTest.cpp:374:53: error: 'class lmms::ProjectMarkers' has no member named 'droppedOnLoad'`: the asserts use the accessor the fix adds, so this revert does not compile). test-4-redB.log ran a stale binary and is **not** evidence. `git apply $L/fix.patch` EXIT=0
- RED-B, redone on the committed test: `git diff -- tests/src/core/ProjectMarkersTest.cpp > $L/testedit.patch`; `git apply -R $L/testedit.patch` EXIT=0; `git apply -R $L/fix.patch` EXIT=0; B > build-6-redB.log EXIT=0; T > test-6-redB.log **EXIT=8**: `aProjectOverTheCapLoadedThenSavedDoesNotLoseMarkersInSilence() 'reported' returned FALSE` at `ProjectMarkersTest.cpp(381)`, `aRefusedOverCapSaveDoesNotRotateTheRevisionSet() '!saved.ok' returned FALSE` at `(447)`, `anUndoRestoreDoesNotLiftTheOverCapSaveRefusal() '!song->saveProjectFile(target)' returned FALSE` at `(507)`, `Totals: 12 passed, 3 failed`
- Restore: `git apply $L/fix.patch` EXIT=0; `git apply $L/testedit.patch` EXIT=0; B > build-7-green.log EXIT=0; T > test-7-green.log EXIT=0 ("100% tests passed out of 3"); `git diff --stat` showed only tests/src/core/ProjectMarkersTest.cpp, 2 insertions
- Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > $L/gates-8.log 2>&1` **EXIT=0**, RESULT: PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures PASS; complexity and file-length "no new item in your files"). This is the first feature_check run that includes 46c76ba160; the round-3 QA sandbox could not run it.

Release note (not edited): the shipped notes are per-version files (docs/RELEASE-NOTES-v*.md) covered by
tests/release-honesty-gate.sh, so I did not add to a released note in a bug-fix commit. Line for the next note's
owner: "A project that loaded with more than 512 markers is read-only: autosave and Save refuse (Save returns
without a dialog, as for a partially loaded project) until the project is cleared or a file that fits is opened."

Delegation (round 4): Haiku (2 starts) ran every build and ctest above, including the three reverse-patch REDs and
the restores. I wrote the two test asserts, made the patches, read the FAIL/Loc/Totals lines in test-2-redA,
test-3-redC, test-6-redB and test-7-green myself, and ran feature_check myself (one command). No Sonnet or DeepSeek:
the only code this round was two test lines.

## Other findings (round 4) - superseded
The note below was wrong to call this safe: the round-4 review showed both directions fail (see "Round 5").
- `Song::restoreUndoSnapshot`'s own comment names a caller that puts back "a checkpoint's displaced session", i.e.
  a document that may not be this session's. If such a caller restores a session that fits the cap over one that
  dropped markers, `holdDroppedOnLoad` would keep the refusal. That errs toward refusing a save, not toward losing
  data, and I did not trace the checkpoint callers to see whether it can happen (unverified).

## Round 5 (the dropped-marker count travels with the undo snapshot)

The round-4 review showed that the round-3 hold in `Song::restoreUndoSnapshot` (src/core/SongUndoSnapshot.cpp, read
`droppedOnLoad()` before the load, `holdDroppedOnLoad()` after it) applied the count of the session being REPLACED to
whatever document the restore put back. A session checkpoint (src/core/SessionCheckpoint.cpp:61) restores the
DISPLACED session, so both directions were wrong:
- an undo of an open of a 515-marker project left the fitting session read-only (refused save and autosave);
- restoring the over-cap session's snapshot over a fitting session lifted the refusal, so the 512-marker list could be
  written over the file that holds 515. That is the silent loss this unit exists to remove.
The comment there claiming "every caller restores this same session's own document" was false.

Fix (the checkpoint's design): the count belongs to the document, so the snapshot carries it.
- `ProjectMarkers::save` (src/core/ProjectMarkers.cpp:207-208): only while an undo snapshot is built
  (`ProjectJournal::snapshotIds()`) and `m_droppedOnLoad > 0`, it writes `droppedOnLoad="N"` on `<markers>`.
- `ProjectMarkers::shouldPersist` (:183-186, moved out of the header): `!m_markers.isEmpty() || (snapshotIds() && m_droppedOnLoad > 0)`,
  so a snapshot of an over-cap session whose markers were all deleted still carries the count.
- `ProjectMarkers::load` (:264) sets `m_droppedOnLoad = max(computed dropped, carriedDroppedCount(element))`. `carriedDroppedCount`
  (:51-58) reads the attribute only while `Song::isRestoringUndoSnapshot()`, clamped to [0, MaxMarkers]. Outside a restore it is 0,
  so a project file that claims the attribute is not believed. A carried count is not reported through `collectError`
  again. A snapshot with no `<markers>` keeps the 0 that `clear()` set.
- `ProjectMarkers::holdDroppedOnLoad` is deleted (include/ProjectMarkers.h) and the SongUndoSnapshot.cpp hunk of 46c76ba160
  is reverted entirely, including the false comment.
- SessionCheckpoint, ControlCommandsDocuments and UndoHistory are untouched: every restore caller gets the count of the
  document it restores. The cap, the load warning, the `errors()` report, the `saveRefusal()` text and the project.save
  pre-rotation gate are unchanged. A real saved file can never carry the attribute: it is only written while a snapshot
  is built, and a session with a count > 0 cannot save. No audio-thread code, no new control command, no undo-step change.

Regression tests: new file `tests/src/core/ProjectMarkersSnapshotTest.cpp`, registered in tests/CMakeLists.txt,
tests/all-sources.txt and tests/fork-sources.txt. It is a separate file because four more slots took
ProjectMarkersTest.cpp over the file-length gate (gates-7.log: `FAIL file-length NEW: new file over 500 lines:
tests/src/core/ProjectMarkersTest.cpp (553)`).
- `anUndoOfAnOverCapOpenLeavesTheFittingSessionSavable`: fitting session snapshot -> load 515-marker file -> 3-arg
  `restoreUndoSnapshot(displaced, "b.mmp", {})` -> 1 marker, `droppedOnLoad()==0`, `saveProjectFile` succeeds, empty refusal.
- `restoringAnOverCapSnapshotOverAFittingSessionKeepsTheRefusal`: over-cap snapshot -> clear + fitting session (saves) ->
  restore over-cap snapshot -> 512 markers, `droppedOnLoad()==3`, save to a new path and over the original both refused,
  original bytes identical.
- `anOverCapSessionEmptiedOfMarkersKeepsTheRefusalThroughASnapshot`: over-cap session with every marker deleted ->
  snapshot -> fitting session -> restore -> 0 markers, `droppedOnLoad()==3`, save over the original refused, bytes identical.
- `aSavedFileCannotClaimMarkersItDidNotDrop` (collateral guard): a project file with `<markers droppedOnLoad="3">` and one
  marker loads with `droppedOnLoad()==0` and saves.
ProjectMarkersTest.cpp changes only in the doc comment of `anUndoRestoreDoesNotLiftTheOverCapSaveRefusal`, which repeated the
same false claim. That slot's code is unchanged, and it now passes through the carried count.

Commands (`$L` = /home/kruzzzzy/.config/kads/zene/state/runs/review-b17c98e11.r5/logs; B = `cmake --build build -j4 --target zene ProjectMarkersTest ProjectMarkersSnapshotTest PartialLoadTest ProjectSectionTest`; T = `cd build/tests && ctest -R '^(ProjectMarkersTest|ProjectMarkersSnapshotTest|PartialLoadTest|ProjectSectionTest)$' --output-on-failure`):
- RED before any product change (slots then still in ProjectMarkersTest.cpp): `cmake --build build -j4 --target zene ProjectMarkersTest > $L/build-1-red.log` EXIT=0; `ctest -R '^ProjectMarkersTest$' > $L/test-1-red.log` **EXIT=8**, slots A/B/C fail (`droppedOnLoad() == 0` / `== 3` returned FALSE), `Totals: 16 passed, 3 failed`; D passed.
- Patches: `git apply -R --index $L/hold-46c76.patch` (46c76ba160's product hunks) EXIT=0 -> `git diff --cached -- include/ProjectMarkers.h src/core/SongUndoSnapshot.cpp > $L/patchA-hold-removal.patch`; the carrier `git diff -- include/ProjectMarkers.h src/core/ProjectMarkers.cpp > $L/patchB-carrier.patch`; the whole round `git diff HEAD -- include/ProjectMarkers.h src/core/ProjectMarkers.cpp src/core/SongUndoSnapshot.cpp > $L/fix.patch`; `git apply -R --check` EXIT=0 for all three.
- First green and per-hunk REDs (slots in ProjectMarkersTest.cpp): test-2-green EXIT=0 3/3; test-3-redA EXIT=8 (A at :571); test-4-redB EXIT=8 (:531, :610, :653); test-5-redFull EXIT=8 (:571, :610, :653); wider family `ctest -R '^(ProjectMarkersTest|PartialLoadTest|ProjectSectionTest|SessionCheckpointTest|SessionCheckpointBudgetTest|UndoHistoryPersistenceTest|UndoHistoryOptionsTest|ProjectContainerSaveTest)$'` > test-6-green.log **EXIT=0, "100% tests passed out of 8"**.
- Gates on that layout: gates-7.log EXIT=1, file-length FAIL (above). The four slots moved to the new file; `cmake build` (configure) is refused in this lane (`configure-9.log` EXIT=97, `bunny-guard: configuring is the parent's job`), so `cmake --build build -j4 --target zene ProjectMarkersTest PartialLoadTest ProjectSectionTest > $L/build-10.log` EXIT=0 regenerated the build files itself ("-- Generating done"); `--target ProjectMarkersSnapshotTest > $L/build-11.log` EXIT=0; T > test-11.log EXIT=0, "100% tests passed out of 4".
- Final per-hunk proof on the committed layout:
  - RED-A, only the hold removal reverted (`git apply -R $L/patchA-hold-removal.patch` EXIT=0): B > build-12-redA.log EXIT=0; T > test-12-redA.log **EXIT=8**, `ProjectMarkersSnapshotTest::anUndoOfAnOverCapOpenLeavesTheFittingSessionSavable() 'ProjectMarkers::instance().droppedOnLoad() == 0' returned FALSE. (the restored fitting document inherited a dropped count of 3 from the replaced session)` Loc `ProjectMarkersSnapshotTest.cpp(116)`, `Totals: 5 passed, 1 failed`; restored with `git apply` EXIT=0.
  - RED-B, only the carrier reverted (`git apply -R $L/patchB-carrier.patch` EXIT=0): B > build-13-redB.log EXIT=0; T > test-13-redB.log **EXIT=8**, `restoringAnOverCapSnapshotOverAFittingSessionKeepsTheRefusal` at `ProjectMarkersSnapshotTest.cpp(155)`, `anOverCapSessionEmptiedOfMarkersKeepsTheRefusalThroughASnapshot` at `(198)`, and `ProjectMarkersTest::anUndoRestoreDoesNotLiftTheOverCapSaveRefusal` at `ProjectMarkersTest.cpp(509)`; restored EXIT=0.
  - RED-full, the round-4 product code (`git apply -R $L/fix.patch` EXIT=0): B > build-14-redFull.log EXIT=0; T > test-14-redFull.log **EXIT=8**, A/B/C at `ProjectMarkersSnapshotTest.cpp(116)`, `(155)`, `(198)`, `Totals: 3 passed, 3 failed` (the two review probes reproduced); restored EXIT=0.
  - GREEN: B > build-15-green.log EXIT=0; T > test-15-green.log **EXIT=0**, "100% tests passed out of 4".
- Gates: gates-16.log EXIT=1, only fork-sources and all-sources-reproduce failed, because the new file was untracked and the recipe derives the lists from tracked files. After `git add` of the explicit paths: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > $L/gates-17.log 2>&1` **EXIT=0, RESULT: PASS** (all 8 rows PASS).

Not run: the full ctest suite. The header change recompiles many targets, and I built only those named above. Local TSan is
unavailable. The new code runs where snapshot capture and restore already run (they share the static
`ProjectJournal::s_snapshotIds` flag), which is not the audio thread.

Delegation (round 5): Sonnet wrote the four regression slots and ran the first RED (test-1-red), then moved them to
ProjectMarkersSnapshotTest.cpp with its registrations (I fixed one CMake comment). Haiku ran every later build, ctest and
per-hunk RED/restore (test-2..6, test-11, test-12..15) and the gates-16 run. I designed and wrote the product change
(ProjectMarkers.h/.cpp, the SongUndoSnapshot.cpp revert), split the patches, read the FAIL/Loc/Totals lines myself, and ran
gates-7 and gates-17. No DeepSeek use: the remaining jobs were each smaller than writing a request.

## Self-check (round 5)
- Red/green: see "Round 5". Each product hunk reverted alone is RED (EXIT=8 at ProjectMarkersSnapshotTest.cpp:116; at :155, :198 and ProjectMarkersTest.cpp:509), the whole round reverted is RED (EXIT=8, :116/:155/:198), and the restored tree is GREEN (EXIT=0, 4/4; 8/8 in the wider family).
- Deterministic: fixed generated files (515 or 1 markers, ticks from the loop index) in a QTemporaryDir. Direct `captureUndoSnapshot` / `restoreUndoSnapshot` / `loadProject` / `saveProjectFile` calls; no clock, sleep or thread.
- Cause and completeness: the count now lives in the document the restore reloads, so every caller of `Song::restoreUndoSnapshot` is consistent without its own code: UndoHistory, control.undo, the session checkpoint (`SessionCheckpoint.cpp:61`, pushed from `MainWindow.cpp:999` and `ControlCommandsProjectFiles.cpp:659`) and project.new's rollback. Siblings: `clear()` still zeroes the count (clearProject, opening another project). `restore()` (marker undo/redo) does not touch it. `shouldPersist`, `save` and `load` change together. The project.save pre-rotation gate and `Song::saveProjectFile` refusal are unchanged.
- Collateral: the attribute is written only while `ProjectJournal::snapshotIds()` is set (only inside `captureUndoSnapshot`, SongUndoSnapshot.cpp:76-78) and believed only while `isRestoringUndoSnapshot()`. Ordinary files load exactly as before, and a file that claims the attribute is ignored (`aSavedFileCannotClaimMarkersItDidNotDrop`). Sessions without a dropped count write the same `<markers>` as before (no attribute, and `shouldPersist` unchanged when the count is 0). The cap is unchanged.
- Not ALREADY-FIXED / FALSE-POSITIVE: the verdict is FIXED.

## Other findings (round 5)
- `dropped = total - offered` (src/core/ProjectMarkers.cpp, unchanged) can overstate the count named in the warning and refusal when markers past the cap have an unparseable tick. The refusal itself is never missed.
- A `<marker>` nested deeper than `<markers>` is counted by `elementsByTagName` but never offered, so such a hand-malformed file makes the session read-only. I did not show that this matters.
