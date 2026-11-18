# Cluster 5426 — a clip child under a folder track crashes loading

Verdict: **FIXED**

## Finding

A malformed project carrying a recognised clip element (e.g. `<midiclip>`) inside a
folder track (`<track type="7">`) crashed loading: the loader dereferenced the
null returned by `TrackFolder::createClip()`.

## Evidence

- `src/core/Track.cpp:172-173` (`claimClipOrPreserveChild`): `Clip* clip = track.createClip(TimePos(0));`
  followed by `clip->restoreState(element);` with no null check.
- `src/tracks/TrackFolder.cpp:174-177`: `Clip * TrackFolder::createClip( const TimePos & ) { return nullptr; }`
  — a folder carries no clips of its own (`include/TrackFolder.h:125` declares the override).
- The walk reaches the branch because a folder track's `nodeName()` is `"trackfolder"`, so a
  `<midiclip>` child is not claimed by `loadTrackSpecificSettings` and falls into
  `claimClipOrPreserveChild` (`src/core/Track.cpp:742`).

## Fix

`claimClipOrPreserveChild` now checks the `createClip()` result for null and preserves the
element verbatim via `captureUnclaimed(element, name, unclaimed)` — the same mechanism SPEC-ARCH-4
1.6.1/1.6.3 already uses for children a track cannot claim: the next save re-emits it
(`Track::saveTrack` → `reemitUnclaimed`, `src/core/Track.cpp:577`) and the load report names it.
No other behaviour changed.

## Regression

Added `aClipElementUnderAFolderTrackIsPreservedNotCrashing` to the existing
`tests/src/core/UnclaimedElementsTest.cpp` (already registered in `tests/fork-sources.txt:2055`;
no new product files). It loads a project with `<track type="7">` containing a `<midiclip>`,
asserts the load survives, the element is reported unclaimed
(`/song/trackcontainer/track[0]/midiclip`), the folder gains no clips, and a load → save
round trip re-emits the element exactly once.

Before the fix the slot crashed (null dereference): `test-1.log` shows the QtTest crash
stack trace in `aClipElementUnderAFolderTrackIsPreservedNotCrashing` and
`0% tests passed, 1 tests failed out of 1`. After the fix: `100% tests passed, 0 tests failed out of 1`.

## Commands and exit codes (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5426.r1/logs`)

| Step | Command | EXIT |
|---|---|---|
| RED build | `cmake --build build --target UnclaimedElementsTest > LOGS/build-1.log 2>&1` | 0 |
| RED test (original code) | `QT_QPA_PLATFORM=offscreen ctest -R UnclaimedElementsTest --output-on-failure > LOGS/test-1.log 2>&1` (from `build/tests`) | 8 |
| GREEN rebuild (test target) | `cmake --build build --target UnclaimedElementsTest > LOGS/build-2.log 2>&1` | 0 |
| GREEN rebuild (`zene`) | `cmake --build build --target zene > LOGS/build-3.log 2>&1` | 0 |
| GREEN test | `QT_QPA_PLATFORM=offscreen ctest -R UnclaimedElementsTest --output-on-failure > LOGS/test-2.log 2>&1` (from `build/tests`) | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1` | 0 |

Affected-family test count: 1 test executed in each `ctest -R UnclaimedElementsTest` run
(`1/1 Test #120: UnclaimedElementsTest`).

## Files committed

- `src/core/Track.cpp` (the null check; entry in `tests/upstream-modifications.txt` extended with the reason)
- `tests/src/core/UnclaimedElementsTest.cpp` (regression slot)
- `tests/upstream-modifications.txt` (reason for the Track.cpp change)
- `docs/qwen-fixes-2026-10/cluster-5426.md` (this report)
