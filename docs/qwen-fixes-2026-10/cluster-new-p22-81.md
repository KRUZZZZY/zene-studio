FIXED

new-p22-81: FIXED

Evidence: `src/core/ControlCommandsTrackFolderSets.cpp:346` now requires `tracks` and validates every array member as a string. `tests/src/core/TrackFolderTest.cpp:945` covers an object, number, string, non-string array member, and missing `tracks`; each must return `InvalidArgs` without creating a set. The valid and duplicate-ID save case remains covered by `aRepeatedTrackIdIsSavedAsOneMember`.

RED/GREEN and proof commands (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-81.r1/logs`):

- RED build: `cmake --build build --target zene TrackFolderTest -j2` — EXIT=0 (`001-red-p22-build.log`).
- RED test: from `build/tests`, `ctest -R '^TrackFolderTest$' --output-on-failure` — EXIT=8 (`002-red-p22-test.log`); `visibilitySetSaveRefusesMalformedTrackLists` observed `errorKind` 0 instead of `InvalidArgs`.
- GREEN build/test: same build command — EXIT=0 (`003-green-p22-build.log`); same ctest command — EXIT=0 (`004-green-p22-test.log`).
- 4b remove product patch: `git diff -- src/core/ControlCommandsTrackFolderSets.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-81.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-81.r1/logs/fix.patch` — EXIT=0.
- 4b red rebuild/test without fix: same build command — EXIT=0 (`005-red-p22-4b-build.log`); same ctest command — EXIT=8 (`006-red-p22-4b-test.log`).
- 4b restore/rebuild/test: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-81.r1/logs/fix.patch` — EXIT=0; same build command — EXIT=0 (`007-green-p22-4b-build.log`); same ctest command — EXIT=0 (`008-green-p22-4b-test.log`).

Affected family: `TrackFolderTest`, 1/1 registered CTest tests executed. Initial gate run `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p22-81` — EXIT=1 (`009-p22-gates.log`): the base manifest included 1,148 vendored `plugins/SfzPlayer/sfizz` paths its published recipe excludes. After regenerating `tests/all-sources.txt` from that recipe, the same gate command — EXIT=0 (`013-p22-gates-final.log`); all eight gate rows PASS.

Files committed for this bug: `src/core/ControlCommandsTrackFolderSets.cpp`, `tests/src/core/TrackFolderTest.cpp`, `tests/upstream-modifications.txt`, `tests/all-sources.txt`, and this report.

new-p23-30: FALSE-POSITIVE

Evidence: the redo closure at `src/core/ControlCommandsTrackFolderSets.cpp:268` calls `TrackContainer::showAllTracks()`, and that method emits `visibilityChanged()` at `src/core/TrackContainerFolders.cpp:254`. `tests/src/core/TrackFolderTest.cpp:1140` asserts one signal after show-all, undo, and redo. The focused test on this code path passed before any change to it.

Commands: from `build/tests`, `./TrackFolderTest showAllRedoEmitsVisibilityChanged` — EXIT=0 (`022-p23-focused-test.log`). The full `TrackFolderTest` run also printed `PASS : TrackFolderTest::showAllRedoEmitsVisibilityChanged()` in `015-red-p20-test.log`; that overall ctest had EXIT=8 solely because the stale-inverse regression was still red. No production change was needed for this finding.

new-p20-18: FIXED

Evidence: `src/core/ControlCommandsTrackFolderSets.cpp:96` detects stored IDs whose tracks are no longer live. Replacing a set with stale members and removing an inactive set with stale members now report `control.undo`, which restores the captured definition exactly; the normal live-member inverse remains `track.visibility_set_save`. `tests/src/core/TrackFolderTest.cpp:997` deletes a member through `track.remove`, then replays both reported inverses and checks the stale ID is restored.

RED/GREEN and 4b commands (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-81.r1/logs`):

- RED build: `cmake --build build --target zene TrackFolderTest -j2` — EXIT=0 (`014-red-p20-build.log`).
- RED test: from `build/tests`, `ctest -R '^TrackFolderTest$' --output-on-failure` — EXIT=8 (`015-red-p20-test.log`); replay of the replacement inverse failed with `no track trk-50`.
- GREEN build/test: same build command — EXIT=0 (`016-green-p20-build.log`); same ctest command — EXIT=0 (`017-green-p20-test.log`).
- 4b remove product patch: `git diff -- src/core/ControlCommandsTrackFolderSets.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-81.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-81.r1/logs/fix.patch` — EXIT=0.
- 4b red rebuild/test without fix: same build command — EXIT=0 (`018-red-p20-4b-build.log`); same ctest command — EXIT=8 (`019-red-p20-4b-test.log`), again failing on the stale track ID.
- 4b restore/rebuild/test: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-81.r1/logs/fix.patch` — EXIT=0; same build command — EXIT=0 (`020-green-p20-4b-build.log`); same ctest command — EXIT=0 (`021-green-p20-4b-test.log`).

Affected family: `TrackFolderTest`, 1/1 registered CTest tests executed. After the inverse-selection helper was factored to keep `saveSet` within the complexity limit, final rebuild `cmake --build build --target zene TrackFolderTest -j2` — EXIT=0 (`024-final-build.log`) and ctest `ctest -R '^TrackFolderTest$' --output-on-failure` from `build/tests` — EXIT=0 (`026-final-test.log`).

Final gate: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p22-81` — EXIT=0 (`025-final-gates.log`), all eight rows PASS. The prior attempt (`023-final-gates.log`, EXIT=1) identified `saveSet` at CCN 12; extracting `saveSetInverse` brought it under the limit. The p22 gate's initial manifest failure and repaired pass are recorded in its section above.

Files committed for p20 and the p23 false-positive test: `src/core/ControlCommandsTrackFolderSets.cpp`, `tests/src/core/TrackFolderTest.cpp`, `tests/upstream-modifications.txt`, and this report. The earlier p22 commit also includes `tests/all-sources.txt`, regenerated from its own recipe to clear the inherited source-registration gate failure.

## Self-check

- The p22 regression fails with its product patch reverted: build `005-red-p22-4b-build.log` EXIT=0, ctest `006-red-p22-4b-test.log` EXIT=8 (`errorKind` 0 instead of `InvalidArgs`); restored build `007-green-p22-4b-build.log` EXIT=0 and ctest `008-green-p22-4b-test.log` EXIT=0.
- The p20 regression fails with its product patch reverted: build `018-red-p20-4b-build.log` EXIT=0 and ctest `019-red-p20-4b-test.log` EXIT=8 (`track.visibility_set_save` cannot resolve deleted `trk-50`); restored build `020-green-p20-4b-build.log` EXIT=0 and ctest `021-green-p20-4b-test.log` EXIT=0. The final refactored code also rebuilt and passed ctest (`024-final-build.log`, `026-final-test.log`, both EXIT=0).
- The p23 finding is false: the focused regression passes on the existing path (`022-p23-focused-test.log`, EXIT=0), and `TrackContainer::showAllTracks()` emits the signal at `src/core/TrackContainerFolders.cpp:254`.
- Determinism: these are synchronous control-registry calls with concrete JSON values and model assertions; no clocks, sleeps, asynchronous waits, or races. The p23 check uses `QSignalSpy` on the same-thread `visibilityChanged()` signal.
- Cause and siblings: `track.visibility_set_save`'s schema rejects bad input before `saveSet` resolves tracks. The inverse logic covers both save replacement and inactive `track.visibility_set_remove`; live-member command inverses are preserved, while either stale-ID path uses the recorded one-step undo. The existing duplicate-ID save case covers the ordinary valid path.
- Data compatibility: no visibility-set project serialization or loader code changed. Empty arrays remain valid; existing project sets, including stale IDs, remain stored as before and can be restored exactly by the recorded inverse. The full `TrackFolderTest` still passes.
- False-positive evidence: new-p23-30 is FALSE-POSITIVE because `showAll`'s redo closure calls `showAllTracks()` (`src/core/ControlCommandsTrackFolderSets.cpp:268`), which emits at `src/core/TrackContainerFolders.cpp:254`; the focused regression passes (`022-p23-focused-test.log`, EXIT=0).
- Final registration/build gates: `025-final-gates.log` records `feature_check.py` EXIT=0 with all eight rows PASS; after this Self-check section was completed, the required final-tree rerun is `030-final-gates-after-self-check.log`. `TrackFolderTest` ran as 1/1 registered CTest test and passed.
- Commits: `40c6653e1` records new-p22-81; `4e057e8ba` records new-p20-18 and the new-p23-30 false-positive regression.
