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
