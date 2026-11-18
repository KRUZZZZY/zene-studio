# cluster new-p10-431 — Invalid visibility-set IDs become track ID 0

Verdict: **FIXED**

## Finding
`src/core/TrackContainerFolders.cpp:146` (finding's line; current tree line 146 at
report time): `loadVisibilitySetState` parsed every `tracks="..."` token with an
unchecked `id.toInt()`. An unparseable token ("bad") silently becomes 0, and 0 is a
valid track ID, so a corrupted/hand-edited project's set claims a track it never
named — applying it can show track 0 and hide other tracks.

## Evidence (file:line)
- `src/core/TrackContainerFolders.cpp:146` — `set.trackIds.push_back(id.toInt());`
  with no conversion check (the bug).
- `src/core/Song.cpp:2272` — `loadVisibilitySetState(element)` is reached from the
  real project load for the `<visibilitysets>` element (`TrackContainer::visibilitySetsNodeName()`,
  `include/TrackContainer.h:236` = `"visibilitysets"`).
- `src/core/TrackContainerFolders.cpp:200` — `applyVisibilitySet` matches `track->id()`
  against the parsed IDs, so a spurious 0 selects the real first track.

## Fix
`src/core/TrackContainerFolders.cpp` — validate each token with Qt6's
`QString::toInt(bool *ok)` and `continue` on unparseable tokens; parseable IDs
(including a real 0) are kept in file order. No header change; only
`lmmsobjs`/test targets rebuilt.

## Regression
New slot `invalidVisibilitySetIdsAreDroppedOnLoad` in the existing area test
`tests/src/core/TrackFolderTest.cpp`: a hand-written current-version project
carries `<visibilitysets><set name="Mixed" tracks="1,bogus,0"/></visibilitysets>`;
after the real `loadProject`, the set must carry exactly `{1, 0}`.

## Commands and EXIT codes (all logs in this run's log folder)
RED on the original code:
- `cmake --build build --target TrackFolderTest > LOGS/build-1-red.log 2>&1` → EXIT=0
- `cd build/tests && ctest -R TrackFolderTest --output-on-failure > LOGS/test-1.log 2>&1` → EXIT=8
  (log `test-1-red.log`; the harness wrote the log as `test-1-red.log`)
  Failing assertion: `FAIL! : TrackFolderTest::invalidVisibilitySetIdsAreDroppedOnLoad()`
  `Actual (static_cast<int>(set->trackIds.size())): 3  Expected (2)` — "bogus" joined as 0.
  All other slots passed; 1 ctest test executed.

GREEN after the fix:
- `cmake --build build --target TrackFolderTest > LOGS/build-3-green.log 2>&1` → BUILD_EXIT=0
- `cd build/tests && ctest -R TrackFolderTest --output-on-failure > LOGS/test-3-green.log 2>&1` → TEST_EXIT=0
  (`100% tests passed, 0 tests failed out of 1`)

Test-catches-the-bug proof (step 4b):
- `git diff -- src/core/TrackContainerFolders.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` → REVERSED_OK
- `cmake --build build --target TrackFolderTest > LOGS/build-4-reverse.log 2>&1` → BUILD_EXIT=0
- `cd build/tests && ctest -R TrackFolderTest --output-on-failure > LOGS/test-4-reverse.log 2>&1` → TEST_EXIT=8 (FAIL without the fix)
- `git apply LOGS/fix.patch` → REAPPLIED_OK
- `cmake --build build --target TrackFolderTest > LOGS/build-5-reapply.log 2>&1` → BUILD_EXIT=0
- `cd build/tests && ctest -R TrackFolderTest --output-on-failure > LOGS/test-5-reapply.log 2>&1` → TEST_EXIT=0 (PASS with the fix)

Gates (after the final edit):
- `bash tests/complexity-gate.sh --check` → EXIT=0
- `bash tests/file-length-gate.sh --check` → EXIT=0

Affected-family test count: `ctest -R TrackFolderTest` executed 1 test (TrackFolderTest),
with all ~40 QtTest slots; only the new slot failed pre-fix.

## Files committed
- `src/core/TrackContainerFolders.cpp` (the fix)
- `tests/src/core/TrackFolderTest.cpp` (the regression slot + fixture helper)
- `docs/qwen-fixes-2026-10/cluster-new-p10-431.md` (this report)

No manifest changes: `src/core/TrackContainerFolders.cpp` is already registered in
`tests/fork-sources.txt` (line 1588); `tests/src/core/TrackFolderTest.cpp` was an
existing registered test (`tests/CMakeLists.txt:720`).
