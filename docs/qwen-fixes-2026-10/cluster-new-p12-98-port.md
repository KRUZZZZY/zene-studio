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
| `tests/src/gui/InstrumentSetupUiTest.cpp:453` | Regression `liveNoteOffFollowsLayerAfterUndoInsert`: a live note is routed to layer 2, layer 1 is removed and reinserted (the undo of the removal), and the original layer's note handle must end up released. |
| `tests/src/gui/InstrumentSetupUiTest.cpp:201` | `layerTemplateXml()` — the track's own state as the XML an inserted layer is built from. |

## The regression's home and the file-length ratchet

The regression first lived in `tests/src/core/InstrumentLayersCommandsTest.cpp`. That file was
already at **498** code lines before the regression (the 500-line gate limit), so any addition to
it pushed it over and I had re-anchored it in `tests/file-length-baseline.tsv`. **That re-anchor
is gone**: the baseline entry `tests/src/core/InstrumentLayersCommandsTest.cpp 539` has been
removed, the core test file is byte-identical to its state before this cluster
(`git diff 47b57a6ae~1 -- tests/src/core/InstrumentLayersCommandsTest.cpp` is empty), and the
regression now lives in the other test file that covers this code,
`tests/src/gui/InstrumentSetupUiTest.cpp`, at **436** code lines — under the limit, with no
baseline entry and no gate re-anchor.

`bash tests/file-length-gate.sh --check` now measures 19 fork-scope sources over 500 code lines
(20 while the re-anchor was in place) and reports the same 14 unrelated pre-existing violations;
neither of this cluster's files is among them.

## Verification transcript

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-98-port.r3/logs`.
Every row is a command I ran in this round, with the exit code it printed.

| Step | Exact command | Log | Exit |
| --- | --- | --- | ---: |
| Build | `cmake --build build --target zene InstrumentSetupUiTest InstrumentLayersCommandsTest -j2` | `build-5.log` | 0 |
| GREEN | `ctest -R '^InstrumentSetupUiTest$' --output-on-failure` (run from `build/tests`) | `test-6.log` | 0 |
| GREEN, counted | `ctest -V -R '^InstrumentSetupUiTest$'` (run from `build/tests`) | `test-7.log` | 0 — 3 passed, 0 failed, 4 skipped (7 test functions, 1 ctest entry) |
| Sibling family | `ctest -V -R '^InstrumentLayersCommandsTest$'` (run from `build/tests`) | `test-8.log` | 0 — 4 passed, 0 failed, 10 skipped (14 test functions, 1 ctest entry) |
| Reverse the product hunk | `git diff ded355440871a86cc907cdc2d6465f2d3d597a6b 47b57a6ae15ad44ba1563c6c97564c246e0221f0 -- src/core/InstrumentLayers.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-98-port.r3/logs/fix.patch` | `fix.patch` | 0 |
| Reverse the product hunk | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-98-port.r3/logs/fix.patch` | — | 0 |
| Rebuild (reversed) | `cmake --build build --target zene InstrumentSetupUiTest -j2` | `build-6.log` | 0 |
| **RED** | `ctest -R '^InstrumentSetupUiTest$' --output-on-failure` (run from `build/tests`) | `test-9.log` | **8** |
| Restore the product hunk | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-98-port.r3/logs/fix.patch` | — | 0 |
| Rebuild (restored) | `cmake --build build --target zene InstrumentSetupUiTest -j2` | `build-7.log` | 0 |
| **GREEN** | `ctest -R '^InstrumentSetupUiTest$' --output-on-failure` (run from `build/tests`) | `test-10.log` | **0** |
| File-length gate | `bash tests/file-length-gate.sh --check` | `gate-11.log` | 1 |
| Complexity gate | `bash tests/complexity-gate.sh --check` | `gate-12.log` | 1 |
| File-length gate, whole tree | `bash tests/file-length-gate.sh --check --scope all` | `gate-13.log` | 1 |
| Complexity gate, whole tree | `bash tests/complexity-gate.sh --check --scope all` | `gate-14.log` | 1 |
| Aggregate gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-98-port` | `gate-15.log` | 0 |
| Pre-submit check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-98-port new-p12-98-port` | `submit-5.log`, `submit-6.log` | 0 |

The gate rows are the runs made after the code-and-test commit (`93a9c8680`) landed; the same
commands run before it, in `gate-6.log` … `gate-10.log`, produced byte-identical REGRESSION lists
and the same exit codes. `submit-5.log` is the pre-submit run against that committed tree and
`submit-6.log` the same command re-run after the report's citation commit — both exit 0 with a
clean working tree. The only change after `submit-6.log` is this paragraph.

The two commit references in the red proof are stable: `ded355440871a86cc907cdc2d6465f2d3d597a6b`
is the parent of the fix commit `47b57a6ae15ad44ba1563c6c97564c246e0221f0`, so the patch is the
fix commit's own product hunk and does not move when later commits land on the branch. The RED run
(`test-9.log`) fails exactly at the new assertion:

```
FAIL!  : InstrumentSetupUiTest::liveNoteOffFollowsLayerAfterUndoInsert() 'heldNote->isReleased()' returned FALSE. (the held note's release missed its original layer after undo)
Totals: 2 passed, 1 failed, 4 skipped, 0 blacklisted, 1408ms
```

With the product hunk restored, the same command exits 0 (`test-10.log`).

### The direct gates exit 1 — on unrelated, pre-existing violations

Both direct gates are red on this train. Every violation they name is in a file this branch's fix
does not touch; `git show --stat 47b57a6ae` (the fix commit) lists only
`src/core/InstrumentLayers.cpp`, `tests/src/core/InstrumentLayersCommandsTest.cpp`,
`tests/upstream-modifications.txt`, `tests/file-length-baseline.tsv` and the report, and this
round's commits touch `tests/src/gui/InstrumentSetupUiTest.cpp` and
`tests/file-length-baseline.tsv`. The aggregate gate `feature_check.py` (exit 0) compares against
the same baselines scoped to *my* files and reports `PASS complexity — no new item in your files`
and `PASS file-length — no new item in your files`.

`bash tests/file-length-gate.sh --check` → exit 1 (`gate-11.log`), header
`file-length-gate: 1792 fork-scope sources measured; 19 exceed 500 code lines`, 14 REGRESSION
entries, none of them mine:

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

`bash tests/complexity-gate.sh --check` → exit 1 (`gate-12.log`), 6 REGRESSION entries, none of
them mine:

```
REGRESSION: new function over target: dotCentreAlong@76-120@tests/src/gui/XyPadPairTest.cpp (CCN 16)
REGRESSION: new function over target: lmms::interchange::readdetail::parseNotes@94-151@src/core/DawProjectReadTracks.cpp (CCN 13)
REGRESSION: new function over target: lmms::adjust@215-249@src/core/ControlCommandsMixerTools.cpp (CCN 12)
REGRESSION: new function over target: lmms::setStrip@250-274@src/core/ControlCommandsMixerStrip.cpp (CCN 11)
REGRESSION: new function over target: lmms::control::rotateProjectRevision@151-222@src/core/ProjectRevisions.cpp (CCN 11)
REGRESSION: new function over target: lmms::importedOverrides@168-205@src/gui/ControlCommandsShortcuts.cpp (CCN 11)
```

The whole-tree scope (`--scope all`, `gate-13.log` and `gate-14.log`, both exit 1) is red on
unrelated violations as well; neither `tests/src/gui/InstrumentSetupUiTest.cpp` (436 code lines)
nor `tests/src/core/InstrumentLayersCommandsTest.cpp` (498 code lines) is named in either output.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reversing only
  the product hunk (`git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-98-port.r3/logs/fix.patch`,
  exit 0), rebuilding (exit 0) and running
  `ctest -R '^InstrumentSetupUiTest$' --output-on-failure` gives exit **8** with
  `FAIL! ... 'heldNote->isReleased()' returned FALSE` (`test-9.log`). Restoring the hunk
  (`git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-98-port.r3/logs/fix.patch`,
  exit 0), rebuilding (exit 0) and running the same command gives exit **0** (`test-10.log`).
  The affected family runs 7 test functions: 3 passed, 0 failed, 4 skipped (`test-7.log`); the
  sibling `InstrumentLayersCommandsTest` ctest entry also passes (`test-8.log`).
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
  before, and the new data round-trips unchanged. No limit or check was added, so no other caller
  can be hit by one. The change adds nothing to an audio-thread path: the shift happens in
  `adopt()`, on the control-thread layer-edit path under `ChangeGuard`, and it only does relaxed
  atomic loads/stores on the existing `std::array<std::atomic<std::uint32_t>, NumKeys>` — no
  allocation, no free, no lock, no wait, no logging, no allocating Qt calls.
- **Undo stays one honest step.** The regression reproduces the undo of a layer removal through the
  public `removeLayer()` / `insertLayer()` pair; the fix adds no command, no control-command id and
  no undo command of its own, so the `ControlCommandsSnapshot` canary does not move.

## Explicit files committed

- `src/core/InstrumentLayers.cpp` — the fix (`insertBit()` + the mask shift in `adopt()`), commit `47b57a6ae`
- `tests/src/gui/InstrumentSetupUiTest.cpp` — the regression `liveNoteOffFollowsLayerAfterUndoInsert`
- `tests/src/core/InstrumentLayersCommandsTest.cpp` — back to its pre-cluster content (the regression moved out)
- `tests/file-length-baseline.tsv` — the re-anchor entry removed again
- `tests/upstream-modifications.txt` — registers the modified upstream file, naming the regression's new home
- `docs/qwen-fixes-2026-10/cluster-new-p12-98-port.md` — this report

FIXED
