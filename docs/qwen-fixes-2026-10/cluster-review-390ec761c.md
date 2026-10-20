ALREADY-FIXED
review-390ec761c: ALREADY-FIXED
5349: ALREADY-FIXED
new-p8-219: ALREADY-FIXED

# review-390ec761c / 5349 / new-p8-219 — same-file batch

All three findings point at `src/core/SessionPlayback.cpp`, and all three are
already fixed on this branch. The fixes were accepted earlier in the train; the
line numbers in the findings moved. I added one regression per finding to
`tests/src/core/SessionPlaybackTest.cpp` and proved each one red by reversing the
relevant historical product hunk, then green by restoring it. No product code was
changed by this batch.

## Evidence

### review-390ec761c — occupied-to-empty cells are reclaimed

- `src/core/SessionPlayback.cpp:44` computes `hasContent = patternId >= 0 || clip != nullptr`.
- `src/core/SessionPlayback.cpp:52-62` removes an existing entry when the cell loses
  its content: the last entry moves into the hole, the last slot is cleared and
  `m_count` drops. This is a bounded move over fixed `std::array` storage (no
  allocation, no lock) and it keeps `Capacity`/lookup semantics.
- `src/core/SessionPlayback.cpp:72-81` skips an unknown empty cell, so it never
  consumes an entry in the first place.
- Historical fix: `9ce2e115d` (`fix(rework 390ec761c): a cell that lost its pattern
  kept its content-table entry`), with `c2071bdee` preserving the audio-cell entry
  by testing `!hasContent` rather than `patternId < 0`.
- New regression: `aCellThatGoesEmptyGivesItsEntryBackThroughTheScheduler`
  (`tests/src/core/SessionPlaybackTest.cpp:395`) churns every table entry
  real -> empty through the scheduler queue, then requires a later live cell to
  install and play.

### 5349 — empty cells do not consume the table

- `src/core/SessionPlayback.cpp:72-81` returns before the capacity check when a cell
  has neither a pattern nor a clip. The model still publishes every cell (so a cell
  that changes to empty is cleared), but the table stores only cells with content.
- Historical fix: `390ec761c` (`fix(playback): an empty cell consumed an entry of
  the content table`, Luna cluster 5349).
- New regression: `aGridOfEmptyCellsLeavesTheTableForTheMidiCell`
  (`tests/src/core/SessionPlaybackTest.cpp:414`) builds a `1 x (Capacity + 1)` model
  whose first `Capacity` cells are empty and whose last cell is MIDI, publishes the
  whole grid through `publishSessionSlotContent`, launches the last cell and
  requires pattern 7. Before the fix the empty cells filled the 128-entry table and
  the MIDI cell was refused (observed patternId -1).

### new-p8-219 — dropped audio clips have a renderable handoff

- An audio cell publishes `patternId = -1` together with its clip:
  `src/core/SessionPlayback.cpp:219-234` calls `sessionaudio::prepare` on the model
  thread and forwards `clip`/`audio.shape`.
- `SessionSlotContent` carries the clip and shape (`include/SessionPlayback.h:66-69`),
  `install` stores both (`src/core/SessionPlayback.cpp:67-68, 92-93`), and
  `playbackForColumn` puts both on the view the renderer reads
  (`src/core/SessionPlayback.cpp:178-179`).
- The render side already exists: `src/core/SessionAudioCells.cpp:111-141`
  (`SessionCellPlayHandle::play`) reads `view.clip`, loops it with the shape's gain
  and loop region, and is covered by `SessionAudioCellPlaybackTest` (a launched file
  cell and a recorded take render frame-for-frame, plus zero-allocation probes).
- Historical fixes: `363054390` (`feat(session): a launched audio cell plays its
  clip`, WF-055 step 2) and `174ab288f` (audio-cell gain/loop/transpose/tempo warp).
- New regression: `anAudioCellCarriesItsClipToTheRenderPath`
  (`tests/src/core/SessionPlaybackTest.cpp:432`) publishes `patternId -1` with a
  sentinel clip and shape, launches the cell and requires the view to carry the clip
  pointer, gain and loop region while the id stays negative.

## RED/GREEN (exact commands, exact exit codes)

All commands were run from the worktree root unless a `cd` is shown. Logs are in
`build/qwen-logs-review-390ec761c/` (the requested
`~/.local/share/zene-bunny-fix/qwen/runs/review-390ec761c.r1/logs` path is outside
this session's writable roots: every write there, including `touch`, returns
`Permission denied`; the directory itself is owned by this user and has no ACL).

Bug review-390ec761c:

```
git apply -R build/qwen-logs-review-390ec761c/fix-bug1.patch          EXIT=0
CCACHE_TEMPDIR=/tmp/ccache-tmp cmake --build build --target SessionPlaybackTest -j$(nproc)   EXIT=0
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^SessionPlaybackTest$' --output-on-failure   EXIT=8
  FAIL aCellThatGoesEmptyGivesItsEntryBackThroughTheScheduler: Actual -1, Expected 9
  FAIL aCellThatLosesItsPatternGivesItsEntryBack: Actual 128, Expected 0
git apply build/qwen-logs-review-390ec761c/fix-bug1.patch             EXIT=0
rebuild                                                               EXIT=0
ctest -R '^SessionPlaybackTest$'                                      EXIT=0 (1/1 passed)
```

Bug 5349:

```
git apply -R build/qwen-logs-review-390ec761c/fix-bug2.patch          EXIT=0
CCACHE_TEMPDIR=/tmp/ccache-tmp cmake --build build --target SessionPlaybackTest -j$(nproc)   EXIT=0
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^SessionPlaybackTest$' --output-on-failure   EXIT=8
  FAIL aGridOfEmptyCellsLeavesTheTableForTheMidiCell: Actual -1, Expected 7
  FAIL emptyCellsDoNotConsumeTheContentTable: Actual -1, Expected 7
git apply build/qwen-logs-review-390ec761c/fix-bug2.patch             EXIT=0
rebuild                                                               EXIT=0
ctest -R '^SessionPlaybackTest$'                                      EXIT=0 (1/1 passed)
```

Bug new-p8-219:

```
git apply -R build/qwen-logs-review-390ec761c/fix-bug3.patch          EXIT=0
CCACHE_TEMPDIR=/tmp/ccache-tmp cmake --build build --target SessionPlaybackTest -j$(nproc)   EXIT=0
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^SessionPlaybackTest$' --output-on-failure   EXIT=8
  FAIL anAudioCellCarriesItsClipToTheRenderPath: Actual view.clip 0x0, Expected clip 0x55948de1b284
git apply build/qwen-logs-review-390ec761c/fix-bug3.patch             EXIT=0
rebuild                                                               EXIT=0
ctest -R '^SessionPlaybackTest$'                                      EXIT=0 (1/1 passed)
```

Affected family after the final state:

```
CCACHE_TEMPDIR=/tmp/ccache-tmp cmake --build build --target SessionModelTest ControlSessionRecordLandTest -j$(nproc)  EXIT=0
cd build/tests && QT_QPA_PLATFORM=offscreen \
  ctest -R '^(SessionPlaybackTest|SessionPlaybackRenderTest|SessionAudioCellPlaybackTest|SessionModelTest|ControlSessionRecordLandTest)$' \
  --output-on-failure                                                EXIT=0 (5/5 passed)
```

## Self-check

- **Regression test fails with the product change reverted:** proved above per bug;
  the red commands exited 8 with the named assertions, the restored commands exited 0.
  (This batch changes no product code; the reversed patches are the historical
  product hunks, generated with `git diff -R` against the pre-fix file.)
- **Deterministic:** the tests use the same injected `SessionClockContext` and local
  `SessionScheduler`/`SessionModel`/table as the rest of the file; no wall clock,
  sleeps, threads or environment races.
- **Cause and siblings checked:** `install` (`SessionSlotContentTable`), its queue
  producer `publishSlotContent`, the audio-thread `applySlotContent`, the model
  publisher `publishSessionSlotContent`, and the `playbackForColumn` reader were all
  inspected together. `clear()` still drops every entry; `reset()` still drops content
  with launch state. Callers: project load (`src/core/Song.cpp:2148`),
  `session.launch_slot` (`src/core/ControlCommandsSessionLaunch.cpp:101`) and
  record-arm (`src/core/ControlCommandsSessionAudioRecord.cpp:155`) all still publish
  the whole grid, which is what clears a cell that became empty.
- **No collateral damage:** no product or serialisation code changed; the only
  committed source change is a test file. `SessionModelTest`,
  `ControlSessionRecordLandTest`, `SessionPlaybackRenderTest` and
  `SessionAudioCellPlaybackTest` all pass (5/5 family run).
- **Realtime:** no new audio-thread work. The existing reclaim is a bounded array
  move in `install`; the existing audio render path is covered by the zero-allocation
  probes in `SessionAudioCellPlaybackTest::theCellRendererDoesNotAllocate` and
  `theInstrumentCellRendererDoesNotAllocateEither`.
- **Undo/commands:** no command, undo step or reversibility class changed.
- **Old projects:** no parser, writer, model or product path changed; old projects
  load exactly as before.
- **If ALREADY-FIXED:** the code and the historical commits are cited above by
  `file:line` (`src/core/SessionPlayback.cpp:44`, `:52-62`, `:72-81`, `:178-179`,
  `:219-234`) and by commit (`390ec761c`, `9ce2e115d`, `c2071bdee`, `363054390`,
  `174ab288f`), and the red/green runs prove the behaviour.

## Files committed

- `tests/src/core/SessionPlaybackTest.cpp` (three regression cases; no product code).
- `docs/qwen-fixes-2026-10/cluster-review-390ec761c.md` (this report).
