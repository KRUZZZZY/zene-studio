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
- A session loaded from a file that fits the cap, or cleared, saves exactly as before. No audio-thread code, no control commands, no undo changes. Undo/redo of markers (`restore`) does not touch the flag; `captureUndoSnapshot` uses `buildProjectDocument` and is not affected.

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
- Cause and completeness: the loss was in the load-then-save pair. Load now records and reports it (`collectError`, `droppedOnLoad`), and the one save entry point, `Song::saveProjectFile`, refuses. Every other writer (project.save, handoff, collab sync, render temp files) goes through `saveProjectFile` and already surfaces `saveRefusal()` (ControlCommandsProjectFiles.cpp:242, ControlCommandsProjectLifecycle.cpp:338, ControlCommandsHandoff.cpp:221, CollabSessionSync.cpp:176). `captureUndoSnapshot` uses `buildProjectDocument` directly (SongUndoSnapshot.cpp), so undo snapshots are unaffected. The other ProjectSection subclasses (SubprojectSection, AlternativesSection) inherit the empty default `saveRefusal()`.
- Collateral: the cap is unchanged and files at or under 512 markers load and save byte-for-byte as before (refusal only when `m_droppedOnLoad > 0`; `clear()` and each `load()` reset it, so Song::clearProject / opening another project lifts it - covered by the test's final clear-and-save step; PartialLoadTest and ProjectSectionTest still pass). No other caller legitimately exceeds the cap: `add()` already returns -1 at MaxMarkers.
- Not ALREADY-FIXED / FALSE-POSITIVE: verdict is FIXED (b17c98e11 only warned; shown by test-red.log).
- Sibling commands: I changed no command file (no src/core/ControlCommands*.cpp, no app.* command) and added no control command, so the app.* commands are not affected.

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
