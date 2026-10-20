FIXED

# Cluster new-p12-98-port — inserting a layer leaves live-note masks at their old indices

`InstrumentLayers::adopt()` shifted the layer slots when a layer was inserted, but the per-key
live-note masks in `m_liveLayers` kept their old bit positions. A note-on routed to a layer at or
above the insertion point therefore remembered the pre-insert layer number, and the matching
note-off reached the newly inserted layer instead of the sounding one, so the sounding layer never
saw its release.

The fix shifts every live mask's bits with `insertBit()` — the mirror image of the existing
`dropBit()` — inside the `ChangeGuard` that already guards the slot shift, so the masks and the
slots can never disagree.

## Evidence (file:line)

| Location | Evidence |
| --- | --- |
| `src/core/InstrumentLayers.cpp:92-97` | `insertBit()` — keeps the bits below the insertion point and moves the inserted-and-higher bits up one. |
| `src/core/InstrumentLayers.cpp:194` | `adopt()` applies `insertBit()` to every `m_liveLayers` mask, inside the `ChangeGuard` opened at `src/core/InstrumentLayers.cpp:187`. |
| `src/core/InstrumentLayers.cpp:248` | `removeLayer()` already did the mirror-image `dropBit()` shift; the removal and insertion paths are now consistent. |
| `src/core/InstrumentLayers.cpp:180-196` | `adopt()` is the only site that shifts layer slots, so the mask shift reaches every caller: `addLayer()` (`:212`), `insertLayer()` (`:231`) and `loadSettings()` (`:346`). |
| `src/core/InstrumentLayers.cpp:465-478` | `liveLayersFor()` stores the layer mask of a note-on and hands it back for the note-off — the masks `insertBit()` now keeps correct. |
| `tests/src/core/InstrumentLayersCommandsTest.cpp:403` | Regression `liveNoteOffFollowsLayerAfterUndoInsert`: a live note is routed to layer 2, layer 1 is removed and reinserted (the undo of the removal), and the original layer's note handle must end up released. |

## Verification transcript

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-98-port.r3/logs` (this round).
Every row below is a command I ran in this round and the exit code it printed.

| Step | Exact command | Log | Exit |
| --- | --- | --- | ---: |
| Build | `cmake --build build --target zene InstrumentLayersCommandsTest -j2` | `build-1.log` | 0 |
| Build sibling test | `cmake --build build --target InstrumentSetupUiTest -j2` | `build-2.log` | 0 |
| GREEN | `ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure` (run from `build/tests`) | `test-1.log` | 0 |
| GREEN, counted | `ctest -V -R '^InstrumentLayersCommandsTest$'` (run from `build/tests`) | `test-2.log` | 0 — 5 passed, 0 failed, 10 skipped (15 test functions, 1 ctest entry) |
| Sibling family | `ctest -R '^InstrumentSetupUiTest$' --output-on-failure` (run from `build/tests`) | `test-3.log` | 0 — 1 ctest entry passed |
| Reverse product hunk | `git diff HEAD~1 HEAD -- src/core/InstrumentLayers.cpp > .../r3/logs/fix.patch` | `fix.patch` | 0 |
| Reverse product hunk | `git apply -R .../r3/logs/fix.patch` | — | 0 |
| Rebuild (reversed) | `cmake --build build --target InstrumentLayersCommandsTest -j2` | `build-3.log` | 0 |
| **RED** | `ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure` (run from `build/tests`) | `test-4.log` | **8** |
| Restore product hunk | `git apply .../r3/logs/fix.patch` | — | 0 |
| Rebuild (restored) | `cmake --build build --target InstrumentLayersCommandsTest -j2` | `build-4.log` | 0 |
| **GREEN** | `ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure` (run from `build/tests`) | `test-5.log` | **0** |
| Complexity gate | `bash tests/complexity-gate.sh --check` | `gate-1.log` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check` | `gate-2.log` | 1 |
| Aggregate gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-98-port` | `gate-3.log` | 0 |
| Pre-submit check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-98-port new-p12-98-port` | `submit-3.log` | 0 |

`submit-3.log` is the final run, against the committed tree. The two earlier runs of the same
command are also in the folder: `submit-1.log` (before this report was rewritten, exit 0) and
`submit-2.log` (before this report was committed, exit 1 — its only failing item was
`working tree clean`, which the commit then resolved).

The RED run (`test-4.log`) fails exactly at the new assertion:

```
FAIL!  : InstrumentLayersCommandsTest::liveNoteOffFollowsLayerAfterUndoInsert() 'heldNote->isReleased()' returned FALSE. (the held note's release missed its original layer after undo)
Totals: 4 passed, 1 failed, 10 skipped, 0 blacklisted, 1148ms
```

With the product hunk restored, the same command exits 0 (`test-5.log`).

### The two direct gates exit 1 — on unrelated, pre-existing violations

Both direct gates are red on this train. Every violation they name is in a file this branch's fix
does not touch; `git show --stat 47b57a6ae` (the fix commit) lists only
`src/core/InstrumentLayers.cpp`, `tests/src/core/InstrumentLayersCommandsTest.cpp`,
`tests/upstream-modifications.txt`, `tests/file-length-baseline.tsv` and the report. The aggregate gate
`feature_check.py` (exit 0) compares against the same baselines scoped to *my* files and reports
`PASS complexity — no new item in your files` and `PASS file-length — no new item in your files`.

`bash tests/complexity-gate.sh --check` → exit 1, 6 REGRESSION entries, none of them mine:

```
REGRESSION: new function over target: dotCentreAlong@76-120@tests/src/gui/XyPadPairTest.cpp (CCN 16)
REGRESSION: new function over target: lmms::interchange::readdetail::parseNotes@94-151@src/core/DawProjectReadTracks.cpp (CCN 13)
REGRESSION: new function over target: lmms::adjust@215-249@src/core/ControlCommandsMixerTools.cpp (CCN 12)
REGRESSION: new function over target: lmms::setStrip@250-274@src/core/ControlCommandsMixerStrip.cpp (CCN 11)
REGRESSION: new function over target: lmms::control::rotateProjectRevision@151-222@src/core/ProjectRevisions.cpp (CCN 11)
REGRESSION: new function over target: lmms::importedOverrides@168-205@src/gui/ControlCommandsShortcuts.cpp (CCN 11)
```

`bash tests/file-length-gate.sh --check` → exit 1, 14 REGRESSION entries, none of them mine:

```
REGRESSION: new file over 500 lines: src/core/SmfInterchangeReader.cpp (504)
REGRESSION: new file over 500 lines: src/core/TakeLane.cpp (520)
REGRESSION: new file over 500 lines: tests/src/core/CollabTransportTest.cpp (541)
REGRESSION: new file over 500 lines: tests/src/core/ControlAutomationScriptTest.cpp (521)
REGRESSION: new file over 500 lines: tests/src/core/ControlDeviceCatalogueTest.cpp (567)
REGRESSION: new file over 500 lines: tests/src/core/ControlEditCommandsTest.cpp (511)
REGRESSION: new file over 500 lines: tests/src/core/CustomAccessTest.cpp (556)
REGRESSION: new file over 500 lines: tests/src/core/DeviceDspTest.cpp (752)
REGRESSION: new file over 500 lines: tests/src/core/HardwareIoTest.cpp (556)
REGRESSION: new file over 500 lines: tests/src/core/NotePlaybackTest.cpp (624)
REGRESSION: new file over 500 lines: tests/src/core/RackMacrosTest.cpp (535)
REGRESSION: new file over 500 lines: tests/src/core/RevisionTimelineTest.cpp (600)
REGRESSION: new file over 500 lines: tests/src/core/VideoTrackTest.cpp (589)
REGRESSION: new file over 500 lines: tests/src/gui/ArrangerTimelineTest.cpp (689)
```

My own file is in neither list: `tests/src/core/InstrumentLayersCommandsTest.cpp` is measured at
539 code lines and is grandfathered by `tests/file-length-baseline.tsv:7`
(`tests/src/core/InstrumentLayersCommandsTest.cpp	539`), so the gate reports no growth for it, and
the new `insertBit()` / `adopt()` lines keep `src/core/InstrumentLayers.cpp` far below the limit.

### The file-length re-anchor: where the reason lives

The re-anchor of the grown test file was done with the gate's own single-file flow,
`bash tests/file-length-gate.sh --reanchor-file tests/src/core/InstrumentLayersCommandsTest.cpp "reason"`.
The reason is **not** stored in the baseline: `tests/file-length-baseline.tsv` holds only
`path<TAB>line-count` (no reason column) — the gate prints the reason and stores the measurement
(`tests/file-length-gate.sh:188-206`). The reason for this re-anchor was recorded in the re-anchor
run's output, `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-98-port.r2/logs/reanchor.log`:

```
RE-ANCHORED (single file, fork scope): tests/src/core/InstrumentLayersCommandsTest.cpp none -> 539 lines
reason: cluster new-p12-98 regression added liveNoteOffFollowsLayerAfterUndoInsert
```

and it is restated here: the regression `liveNoteOffFollowsLayerAfterUndoInsert` legitimately grew
`tests/src/core/InstrumentLayersCommandsTest.cpp` past 500 code lines, so that one file's baseline
entry was set to its measured 539. No other baseline entry was touched and no gate limit was
changed.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reverting only
  the product hunk (`git apply -R .../r3/logs/fix.patch`, exit 0), rebuilding (exit 0) and running
  `ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure` gives exit **8** with
  `FAIL! ... 'heldNote->isReleased()' returned FALSE` (`test-4.log`). Restoring the hunk
  (`git apply .../r3/logs/fix.patch`, exit 0), rebuilding (exit 0) and running the same command
  gives exit **0** (`test-5.log`). The affected family runs 15 test functions: 5 passed, 0 failed,
  10 skipped (`test-2.log`); the sibling `InstrumentSetupUiTest` ctest entry also passes
  (`test-3.log`).
- **The test is deterministic.** No wall-clock timing, no sleeps, no threads and no races. The
  test drives the live-note path through the public seam `InstrumentTrack::processInEvent()` with an
  internal `MidiEvent` (`MidiNoteOn`, then `MidiNoteOff`) and reads the state of the note handle the
  layer itself acquired (`NotePlayHandle::isReleased()`), so the assertion depends only on the
  mask bookkeeping, not on time.
- **The fix is at the cause and is complete.** `adopt()` is the only function that shifts layer
  slots, and the mask shift sits next to it, so all three callers are covered — `addLayer()`
  (`src/core/InstrumentLayers.cpp:212`), `insertLayer()` (`:231`) and `loadSettings()` (`:346`).
  Siblings checked and left consistent: the removal path `removeLayer()` keeps its mirror-image
  `dropBit()` (`:248`); `clear()` zeroes the masks (`:276`); the zone bookkeeping
  `zonesWithRoomAt()` / `zonesWithout()` (`:281-304`) already moved the chains the same way;
  `liveLayersFor()` (`:465`) reads the masks the shift now keeps correct. `insertBit()` is called
  only with `inserted` in `[1, MaxLayers]` (`MaxLayers == 8`, `include/InstrumentLayers.h:88`), so
  `1u << inserted` never overflows the 32-bit mask.
- **No collateral damage.** The change touches only the in-memory `m_liveLayers` masks; nothing
  saved is affected (`saveSettings()` at `src/core/InstrumentLayers.cpp:317` persists the layers,
  the zones and the mix, not the live masks), so old projects and saved data load exactly as
  before,   and the new data round-trips unchanged. No limit or check was added, so no other caller
  can be hit by one. The change adds nothing to an audio-thread path: the shift happens in
  `adopt()`, on the control-thread layer-edit path under `ChangeGuard`, and it only does relaxed
  atomic loads/stores on the existing `std::array<std::atomic<std::uint32_t>, NumKeys>` — no
  allocation, no free, no lock, no wait, no logging, no allocating Qt calls.
- **Undo stays one honest step.** The regression reproduces the undo of a layer removal through the
  public `removeLayer()` / `insertLayer()` pair; the fix adds no command, no control-command id and
  no undo command of its own, so the `ControlCommandsSnapshot` canary does not move.

## Explicit files committed

- `src/core/InstrumentLayers.cpp` — the fix (`insertBit()` + the mask shift in `adopt()`)
- `tests/src/core/InstrumentLayersCommandsTest.cpp` — the regression `liveNoteOffFollowsLayerAfterUndoInsert`
- `tests/upstream-modifications.txt` — registers the modified upstream file with its reason
- `tests/file-length-baseline.tsv` — the single-file re-anchor entry for the grown test file
- `docs/qwen-fixes-2026-10/cluster-new-p12-98-port.md` — this report
