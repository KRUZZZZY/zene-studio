ALREADY-FIXED

# cluster new-p14-375 - editing a live warp map can race audio playback

## Verdict

ALREADY-FIXED on this branch. The finding describes the code before Q050-063:
`WarpMarkers::set()` does still rewrite its own array and count, but the clip no
longer shares that authored object with the render thread. Every map write goes
through `Clip::publishWarp`, which publishes a new immutable generation in
`WarpPublication`; a playback pass takes exactly one generation and reads that
(`Clip::WarpPassScope` / `Clip::passWarp`).

## Evidence (file:line)

- `include/WarpMarkers.h:236-240` - the class contract: `set()`/`append()` are
  *authoring* calls (control thread, stack only); `sourceFrameAt` /
  `timelineOffsetAt` / `framesPerTickAt` are the allocation-free, lock-free
  readers. `include/WarpMarkers.h:284-305` is the `set()` the finding names.
- `include/WarpPublication.h:76-83` - design: the control thread publishes a new
  generation and swaps one pointer; a render pass counts in before reading it.
  `:92-99` allocates/frees only on the control thread; `:112-124` is the
  one-atomic take/drop a pass performs. The old generation is kept while any
  pass is counted in and freed by the control thread later.
- `include/Clip.h:283-290` - `m_warp` is the control thread's copy; the render
  thread reads `m_warpLive`. `include/Clip.h:312-314` - `publishWarp()` is the
  one write. `include/Clip.h:327-345` - `snapshotWarp()`, `WarpPassScope` and
  `passWarp()`.
- `src/core/Clip.cpp:840-846` - `publishWarp()` validates (the caller validates
  before it gets here), stores `m_warp` and publishes the generation; no lock,
  no wait. `src/core/Clip.cpp:848-873` - the pass scope is thread-local and
  `passWarp()` answers from the pass's generation.
- `src/core/SampleClip.cpp:680-703` - `setWarpMarkers()` builds and validates a
  local `candidate` off to the side, then `publishWarp(candidate)`; a refused set
  changes nothing. `clearWarpMarkers()` publishes an empty map.
- `src/tracks/SampleTrack.cpp:225-227` - each clip's pass takes one
  `WarpSnapshot` before deriving its window/lead; `:302-304` the
  `SamplePlayHandle` is constructed inside that same `WarpPassScope`.
- `src/core/SamplePlayHandle.cpp:156-162` - the handle copies the pass's map
  into its own `m_warp` at construction, so rendering reads a value copy
  (`SamplePlayHandle.cpp:654` uses only that copy).
- `src/core/Clip.cpp:820-832` - save/load and checkpoint restore also write only
  through `publishWarp` (reset-on-absence publishes the empty map), so a
  restored/undone map is published the same way.
- Existing regression coverage already in the tree:
  `tests/src/core/WarpMarkersTest.cpp:116-165`
  (`aPublishedGenerationIsWholeAndFreedOnlyWhenUnheld`) and
  `tests/src/gui/Warp/SampleClipWarpEditTest.cpp:382-424`
  (`aPassKeepsOneMapWhileACommitLands`). The accepted Q050-063 report says the
  same thing: `docs/qwen-fixes-2026-10/cluster-Q050-063.md:27-29,45,52`
  ("the race the reviews found" ... "each clip's pass ... holds one
  generation").

## Regression test added here

`tests/src/tracks/SampleClipWarpTest.cpp:512`
`anEditDuringAPassChangesTheNextPassNotThisOne` is the deterministic form of the
finding: take a pass snapshot, enter `Clip::WarpPassScope`, commit a different
map through `SampleClip::setWarpMarkers()`, and assert that the in-flight pass
still reads and maps through its own generation, while the next pass sees the
commit. It uses the pass scope as the seam - no second thread, no sleep, no
wall-clock timing.

Test matrix checked:

- normal: edit during a pass (the new test).
- refusal: a non-monotonic set is rejected and leaves the published map intact
  (`WarpMarkersTest::nonMonotonicSetsAreRejected`,
  `SampleClipWarpTest::nonMonotonicMarkersAreRejectedOnTheClip`).
- exhaustion: more than `WarpMarkers::MaxMarkers` is refused without publishing
  (`WarpMarkersTest::setsAreSortedAndCapacityIsRefused`).
- reconfiguration/clear: an empty map publishes no generation; a held generation
  survives it (`WarpMarkersTest:154-164`).
- audio thread: `WarpMarkers` lookups allocate nothing
  (`WarpMarkersTest::lookupDoesNotAllocate`); the snapshot take/drop is one
  atomic operation each (`WarpPublication.h:112-124`); the play handle reads its
  own copy.
- platform: plain C++20 `std::atomic`/`std::span`; the tests run offscreen and
  no Qt-6-only API is used.

## Commands and EXIT codes

All logs in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-375.r1/logs/`.

Build and baseline run of the existing publication test:

```
cmake --build build --target WarpMarkersTest -j4        -> EXIT=0   (build-1-warpmarkerstest.log)
ctest -R '^WarpMarkersTest$' --output-on-failure        -> EXIT=0   (test-1-warpmarkerstest.log)
```

The new test, before and after the (temporary) negative control:

```
cmake --build build --target SampleClipWarpTest -j4     -> EXIT=0   (build-2-sampleclipwarptest.log)
ctest -R '^SampleClipWarpTest$' --output-on-failure     -> EXIT=0   (test-2-sampleclipwarptest.log)
```

Negative control. Since this is ALREADY-FIXED there is no product change of mine;
to prove the test is sensitive to the pre-existing fix I temporarily disabled
the pass-generation read (`src/core/Clip.cpp`, `passWarp()` returned `m_warp`) and
recorded the diff:

```
git diff -- src/core/Clip.cpp > .../logs/fix-disabled.patch   -> EXIT=0
git apply .../logs/fix-disabled.patch                          -> EXIT=0  (tree broken)
cmake --build build --target SampleClipWarpTest -j4            -> EXIT=0  (build-3-red.log)
ctest -R '^SampleClipWarpTest$' --output-on-failure            -> EXIT=8  (test-3-red.log)
    FAIL! : SampleClipWarpTest::anEditDuringAPassChangesTheNextPassNotThisOne()
    'clip->warpMarkers() == committed' returned FALSE
git apply -R .../logs/fix-disabled.patch                       -> EXIT=0  (fix restored)
cmake --build build --target SampleClipWarpTest -j4            -> EXIT=0  (build-4-green.log)
ctest -R '^SampleClipWarpTest$' --output-on-failure            -> EXIT=0  (test-4-green.log)
```

No product file is modified in the committed tree (`git status` clean apart from
the report and test; `git diff` shows only the test).

The whole `Warp` ctest family:

```
cmake --build build --target ClipWarpPersistenceTest SampleClipWarpEditTest \
  LoopImportWarpTest ControlWarpCommandsTest WarpQuantizeTest WarpMarkersTest \
  LoopDropWarpTest SampleClipWarpModeMenuTest SampleClipWarpModesTest \
  SampleClipWarpTest -j4                                -> EXIT=0   (build-5-warp-family.log)
ctest -R 'Warp' --output-on-failure                     -> EXIT=0   (test-5-warp-family.log, 12/12)
```

Gates:

```
python3 .../qwen/feature_check.py <worktree>            -> EXIT=0   (feature-check-1.log)
```

`fork-sources`, `all-sources-reproduce`, `no-upstream-regression`,
`check-namespace`, `no-tautology`, `doc-figures`, `complexity`, `file-length`
all PASS; `RESULT: PASS`.

## Self-check

- **The regression test fails with the product change reverted and passes with
  it.** There is no product change in this verdict (the fix is Q050-063). The
  test was proven sensitive to that fix by disabling its snapshot read: build 0,
  ctest `EXIT=8` with exactly the new test failing; after restoring the source,
  build 0 and ctest `EXIT=0` (logs `build-3-red.log`, `test-3-red.log`,
  `build-4-green.log`, `test-4-green.log`).
- **Deterministic.** The test uses `Clip::snapshotWarp()` + `Clip::WarpPassScope`
  with an injected replacement marker set; it starts no thread, sleeps not at
  all, and makes no timing assumption. Its whole body is arithmetic on the
  mapping and equality on the published generations.
- **Fix at the cause and complete.** Checked every writer: `SampleClip`'s
  setters, clear, save/load and checkpoint restore all reach `publishWarp`;
  `Clip`'s copy constructor publishes before the track holds the copy
  (`src/core/Clip.cpp:119-124`). Checked every audio reader: the pass takes a
  snapshot in `SampleTrack::play`, the play handle is built inside the scope and
  copies the map, `prefades`/`leadOutputFramesAt` are called inside the same
  scope, and no audio path reads `m_warp` directly. The GUI drag previews a
  local `WarpMarkers` and commits once (`src/gui/clips/SampleClipView.cpp:767-777`,
  `853-872`). Everything stays consistent.
- **No collateral damage.** No product file, project format, command, undo path
  or gate was changed. The change is one deterministic test plus its
  registration line. Old projects still have no `<warp>`/empty map and publish
  no generation; all 12 Warp-family tests pass, including
  `ClipWarpPersistenceTest`.
- **ALREADY-FIXED evidence.** The code references above (`WarpPublication.h`,
  `Clip.cpp:840-873`, `SampleTrack.cpp:225-227,302-304`,
  `SamplePlayHandle.cpp:156-162`) show the snapshot handoff; the accepted
  Q050-063 record (`docs/qwen-fixes-2026-10/cluster-Q050-063.md:27-29,45,52`)
  records that this exact race was the fix's subject.

## Other findings

None. The class `WarpMarkers` itself still has a non-atomic `set()`; that is
intentional and safe only because all writes are control-thread authoring calls
on values that are then published (the class contract at `WarpMarkers.h:236-240`).
An audio-thread read of the map outside a `WarpPassScope` would read the control
copy; the audit above found no such reader. This is the accepted Q050-063 design
limitation, not a new defect.
