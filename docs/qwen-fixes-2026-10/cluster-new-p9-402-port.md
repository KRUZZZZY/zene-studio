FIXED

# cluster new-p9-402-port: active play handles use a replaced sample with a stale snapshot

new-p9-402-port: FIXED

## Decision: REAL

The finding is real on this branch. `SamplePlayHandle` snapshots the clip's window and
length at construction but reads the clip's `Sample` through a pointer:

- `src/core/SamplePlayHandle.cpp:138-139` constructs from `&clip->sample()`.
- `src/core/SamplePlayHandle.cpp:154-155` snapshots the window and starts at its `sourceIn`;
  `:170-171` snapshots the timeline length for a non-linear clip.
- `src/core/SampleClip.cpp:446-462` (`setSampleBuffer`, before this fix) and `:464-491`
  (`replaceSample`, before this fix) assigned a new `Sample` to `m_sample` in place but never
  retired the track's live handles or cleared the clip's playing latch.
- `src/tracks/SampleTrack.cpp:249-252` starts no fresh handle while `isPlaying()` is true.

So after a source swap the old handle keeps rendering the new buffer through the old window and
length - wrong audio or silence for a shorter replacement, and for the pitch-preserving path a
read past the end of the shorter buffer. The sibling `setSampleFile()` already retires the
handles (`src/core/SampleClip.cpp:516-517`, `emit playbackPositionChanged()`).

## Fix (at the cause)

`src/core/SampleClip.cpp:455` and `:484`: both source-swap functions now call
`playbackPositionChanged()` inside the same `requestChangesGuard()` as the swap. That slot removes
the track's `SamplePlayHandle`s and clears the playing latch, so the next pass builds a handle
from the new source and window. It is the same mechanism `setSampleFile()`, mute changes and
position jumps already use. The engine's change mutex is a `std::recursive_mutex`
(`include/AudioEngine.h:994`), so taking it again inside the guard is safe.

`playbackPositionChanged()` (`src/core/SampleClip.cpp:532-540`) keeps its
`clipUpdatesSuspended()` early return, so the take-lane construction path (`newLaneClip`) is
unaffected.

## Callers checked

Every caller of the two changed functions was located on this branch:

- `src/core/SampleClip.cpp:355` (`installFirstPass` -> `setSampleBuffer`), `:369` (`overdub` ->
  `replaceSample`), `:441` (`addHistoryLane` -> `setSampleBuffer`)
- `src/core/AafImportBuild.cpp:100`, `src/core/ControlCommandsClipImportFile.cpp:386`,
  `src/core/ControlCommandsClipProcess.cpp:152`, `src/core/ControlCommandsSample.cpp:263`,
  `src/core/ControlCommandsSampleEdit.cpp:147`, `src/core/ControlCommandsSampleRange.cpp:85,263`,
  `src/core/ControlCommandsSampleTools.cpp:76`, `src/core/ControlCommandsSessionRecordLand.cpp:305`,
  `src/core/SampleFrameEdits.cpp:85`, `src/core/StemTrackBuilder.cpp:97`,
  `src/gui/clips/SampleClipView.cpp:195,557`.

These are control-command handlers, import/build paths and GUI/main-thread paths. `setSampleBuffer`
and `replaceSample` themselves take `requestChangesGuard()`, so none of them can be an
audio-thread call. In every case a source swap under a live handle had the same stale-snapshot
problem; after the fix playback restarts from the current position on the next period, exactly as
after a mute change or `setSampleFile()`.

Sibling checked: `setSampleFile()` already retired the handles. `setSampleWindow()` keeps the same
buffer and its live handle still reads valid memory, so it is outside this finding and unchanged.

## Test matrix

`tests/src/tracks/SampleClipSourceSwapTest.cpp` (new target `SampleClipSourceSwapTest`):

- Normal path: `setSampleBufferRestartsLivePlayback` starts a pass with `SampleTrack::play`,
  replaces the source with `setSampleBuffer`, requires the playing latch to be clear, and starts
  the next pass again.
- Sibling path: `replaceSampleRestartsLivePlayback` does the same through `replaceSample`.
- Reconfiguration: both replacements are shorter buffers than the source that was playing, so the
  old window/length snapshot would be out of range.
- Failure and exhaustion: the fix adds no failure path, no allocation and no limit. It reuses the
  existing handle-removal call.
- Audio-thread constraints: the change is in two control-thread functions that already take the
  engine change guard; the render code is untouched and no new control command was added.
- Platforms: plain C++20/Qt with no platform-specific API, so the Qt 5/MSVC builds see the same
  code.

The test uses no sleeps, timers or render-thread waits: it observes the playing latch that
`SampleTrack::play` itself consults.

## Commands and EXIT codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-402-port.r2/logs`.

| Stage | Command | EXIT |
| --- | --- | ---: |
| First test build (target absent before the generated build system refreshed) | `cmake --build build --target SampleClipSourceSwapTest -j4 > .../build-1.log 2>&1; echo EXIT=$?` | 2 |
| Direct reconfigure refused by the lane guard | `cmake -S . -B build > .../configure-1.log 2>&1; echo EXIT=$?` | 97 |
| Refresh the generated build system | `cmake --build build --target rebuild_cache > .../configure-2.log 2>&1; echo EXIT=$?` | 0 |
| Build the regression before the fix | `cmake --build build --target SampleClipSourceSwapTest -j4 > .../build-2.log 2>&1; echo EXIT=$?` | 0 |
| RED before the fix | `ctest -R '^SampleClipSourceSwapTest$' --output-on-failure > .../test-1-red.log 2>&1; echo EXIT=$?` | 8 |
| Rebuild with the fix | `cmake --build build --target SampleClipSourceSwapTest -j4 > .../build-3.log 2>&1; echo EXIT=$?` | 0 |
| GREEN with the fix | `ctest -R '^SampleClipSourceSwapTest$' --output-on-failure > .../test-2-green.log 2>&1; echo EXIT=$?` | 0 |
| 4b: save the product diff | `git diff -- src/core/SampleClip.cpp > .../fix.patch; echo DIFF_EXIT=$?` | 0 |
| 4b: revert the product diff | `git apply -R .../fix.patch; echo REVERT_EXIT=$?` | 0 |
| 4b: rebuild reverted | `cmake --build build --target SampleClipSourceSwapTest -j4 > .../build-4-reverted.log 2>&1; echo BUILD_EXIT=$?` | 0 |
| 4b: RED reverted | `ctest -R '^SampleClipSourceSwapTest$' --output-on-failure > .../test-3-red-4b.log 2>&1; echo RED_EXIT=$?` | 8 |
| 4b: restore the fix | `git apply .../fix.patch; echo APPLY_EXIT=$?` | 0 |
| 4b: rebuild restored | `cmake --build build --target SampleClipSourceSwapTest -j4 > .../build-5-restored.log 2>&1; echo BUILD_EXIT=$?` | 0 |
| 4b: GREEN restored | `ctest -R '^SampleClipSourceSwapTest$' --output-on-failure > .../test-4-green-4b.log 2>&1; echo GREEN_EXIT=$?` | 0 |
| Rebuild the application after the shared-object change | `cmake --build build --target zene -j4 > .../build-6-zene.log 2>&1; echo EXIT=$?` | 0 |
| First family run (family binaries not built yet) | `ctest -R '^SampleClip' --output-on-failure -j4 > .../test-5-family.log 2>&1; echo EXIT=$?` | 8 |
| Build the family binaries | `cmake --build build --target SampleClipWarpEditTest SampleClipWarpModeMenuTest SampleClipStretchTest SampleClipRubberBandTest SampleClipWarpModesTest SampleClipTransientRepeatTest SampleClipTransientRepeatSafetyTest SampleClipWarpTest SampleClipWindowTest SampleClipMuteCursorTest -j4 > .../build-7-family.log 2>&1; echo EXIT=$?` | 0 |
| Family rerun | `ctest -R '^SampleClip' --output-on-failure -j4 > .../test-6-family.log 2>&1; echo EXIT=$?` | 0 |
| Manifest check before staging the new test | `bash tests/all-sources-reproduce.sh > .../all-sources-1.log 2>&1; echo EXIT=$?` | 1 |
| Stage the new test | `git add tests/src/tracks/SampleClipSourceSwapTest.cpp; echo ADD_EXIT=$?` | 0 |
| Manifest check after staging | `bash tests/all-sources-reproduce.sh > .../all-sources-2.log 2>&1; echo EXIT=$?` | 0 |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../feature-check-1.log 2>&1; echo EXIT=$?` | 0 |

RED (`test-1-red.log`) reports both cases failing on the unfixed tree:

```text
FAIL!  : SampleClipSourceSwapTest::setSampleBufferRestartsLivePlayback() '!clip->isPlaying()' returned FALSE. (setSampleBuffer left the stale handle playing)
FAIL!  : SampleClipSourceSwapTest::replaceSampleRestartsLivePlayback() '!clip->isPlaying()' returned FALSE. (replaceSample left the stale handle playing)
Totals: 2 passed, 2 failed, 0 skipped, 0 blacklisted, 725ms
```

The 4b RED run (`test-3-red-4b.log`) repeats the same two failures with the product patch
reverted. GREEN (`test-2-green.log`, `test-4-green-4b.log`) and the family run
(`test-6-family.log`) report all tests passing (11/11 in the family).

The gate table (`feature-check-1.log`) is eight PASS rows:

```text
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

## Registration

- `tests/fork-sources.txt`: the new `tests/src/tracks/SampleClipSourceSwapTest.cpp` entry.
- `tests/all-sources.txt`: the whole-tree scope entry; `all-sources-reproduce.sh` reproduces after
  the file is tracked (`all-sources-2.log`, EXIT=0).
- `tests/CMakeLists.txt`: the new ctest target, next to `SampleClipWindowTest.cpp`.
- `tests/upstream-modifications.txt`: `src/core/SampleClip.cpp` and `tests/CMakeLists.txt`, with
  this cluster's reason.

## Self-check

- The regression test FAILS with the product change reverted and PASSES with it: 4b RED
  `RED_EXIT=8` (`test-3-red-4b.log`) and GREEN `GREEN_EXIT=0` (`test-4-green-4b.log`).
- The test is deterministic: it uses no wall-clock timing, sleeps or races. It observes the
  playing latch that `SampleTrack::play` reads, plus a direct call to `SampleTrack::play`; the
  source swap is synchronous and the test never starts the audio thread.
- Cause and completeness: both source-swap functions are changed, their sibling `setSampleFile()`
  already did this, and all callers found by search are control/import/GUI paths that already
  take the engine change guard.
- No collateral damage: no save/load code or data format changed. The load path assigns
  `m_sample` directly (`src/core/SampleClip.cpp:1118-1119`) and never goes through these two
  functions, so old projects load exactly as before. No limit or check was added.
- Realtime: the two functions run on the control thread under `requestChangesGuard()`; the render
  path and `SamplePlayHandle` are untouched. Handle disposal uses the existing
  `removePlayHandlesOfTypes` path. Nothing added allocates, frees, locks, waits, logs or emits a
  signal on the audio thread.
- Undo and commands: no journal entry, no command and no reversibility class changed; the
  ControlCommandsSnapshot canary is untouched.
- Behaviour change: only a clip whose source is replaced while it plays. It now restarts from the
  current position on the next period instead of rendering through the stale snapshot.
- GUI: no `src/gui` change.
- Concurrency: TSan does not run on this box. The swap and the handle removal happen under one
  hold of the engine's recursive change mutex, which the render path also takes, so no period can
  see the new buffer together with an old handle.

## Other findings

- `SampleClip::playbackPositionChanged()` dereferences `st` after a `dynamic_cast` that it only
  null-checks for the early return (`src/core/SampleClip.cpp:534-539`). This is harmless while a
  `SampleClip` only lives on a `SampleTrack`, and it predates this change.
- A direct `cmake -S . -B build` is refused by the lane guard (EXIT=97); the supported refresh is
  `cmake --build build --target rebuild_cache`.
