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
