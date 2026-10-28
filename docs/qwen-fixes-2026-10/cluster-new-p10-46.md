**Verdict: FIXED**

# Cluster new-p10-46

`plugin.midi_part_add` appended to a track's part list with no bound. Every note-on on that track scans the whole list on the audio thread, so the cost of a note grew with the number of parts a command had added. The command now refuses the part past 128 (`kMaxParts`) with a typed `Refused` error, before any mutation or undo step. Loading a saved list is unchanged, so a list longer than the limit still loads whole and only one more part is refused.

## Evidence (file:line)

Reproduced by reading the code on `bugs/qwen-new-p10-46` at `37eaf07557`:

- The finding's line 172 is `RackZones::addZone` in `addPart`. Before the fix nothing counted the list first: `src/core/ControlCommandsPluginParts.cpp` `addPart` checked the zone's ranges (`:166-169`) and appended it at `:172`. The only check was the per-zone range check.
- The per-note cost is real. `src/tracks/InstrumentTrackNotes.cpp:331-332` (`sendNoteOn`, the note-on path) calls `m_midiParts.channelOfKey` and `m_midiParts.noteOnChannel`. `src/core/InstrumentMidiParts.cpp:57` calls `m_zones.resolve(key, velocity)`, and `src/core/RackZones.cpp:143-156` (`RackZones::resolve`) is a linear first-match scan over every stored zone. `include/RackZones.h:147` documents it as first match in list order.
- `InstrumentMidiParts::channelMask()` (`src/core/InstrumentMidiParts.cpp:79-88`) scans the list again for each channel-wide event.
- Nothing else in the tree caps the part list. `rg` for a part or zone limit in `src/core` and `include` finds none, and `docs/` documents no maximum (the only part-command docs are `docs/KNOWN-LIMITATIONS.md:2760` and the release notes, neither gives a count).

Fix (after the change):

- `src/core/ControlCommandsPluginParts.cpp:106` `constexpr int kMaxParts = 128;` (one part per MIDI key), with its rationale in the comment above it.
- `src/core/ControlCommandsPluginParts.cpp:177-181`: `addPart` returns `ControlResult::failure(ControlErrorKind::Refused, "...the most it holds...")` when `zones.zoneCount() >= kMaxParts`. The check sits before `addZone` (`:183`) and before `addUndoStep` (`:185`), so a refused add changes nothing and records no undo step.

Why the limit does not hit legitimate callers:

- Load paths do not go through the command. `InstrumentMidiParts::loadSettings` (`src/core/InstrumentMidiParts.cpp:98-101`) calls `RackZones::loadSettings` directly, so a saved list longer than 128 (written before this change) loads whole. `midiPartsOverLimitSavedListLoadsWhole` pins this.
- Undo/redo cannot push past the limit. An add's redo runs only when the list is back at its pre-add length, which was below 128 when the add was accepted. A remove's undo restores the length it removed from.
- `plugin.midi_part_set` edits in place and never grows the list, so it is unchanged.
- The audio thread is untouched. The check runs on the control thread before the edit; no allocation, lock or log is added on the audio path.
- No new control command and no description change, so `ControlCommandsSnapshot` does not move.

Sibling not changed (out of this finding's scope): `rack.zone_add` (`src/core/ControlCommandsRackZones.cpp:104`) and `rack.layer_zone_add` (`src/core/ControlCommandsRackLayers.cpp:319`) append to `RackZones` with the same unbounded per-note scan. I found no count limit in those handlers, but did not read them line by line. They have their own contract (routing and layer racks) and would need their own decision, so they are a follow-up, not part of this fix.

## Regression test

`tests/src/core/InstrumentLayerZonePublishTest.cpp`: `midiPartsAddRefusedAtTheLimit` (line 187) and `midiPartsOverLimitSavedListLoadsWhole` (line 215), with the helpers `partCount`, `manyParts`, `transactionCount`, `saveAndReload` (lines 66-99).

- `midiPartsAddRefusedAtTheLimit` fills the list to 128 through the model, then checks: the 129th `plugin.midi_part_add` is refused with `ControlErrorKind::Refused`, the count stays at 128, the undo transaction count does not grow; removing `part-127` frees one slot and the next add succeeds.
- `midiPartsOverLimitSavedListLoadsWhole` fills to 130, saves and reloads the track, and checks the count is still 130 and one more add is refused.
- The file was chosen over the new file the finding's brief suggests, for a reason found while working: `InstrumentLayersCommandsTest.cpp` is at the file-length ratchet (497 code lines at base; 549 with the two cases), and this lane's build tree is already configured, so a new test file could not be registered (`cmake build` is blocked by the bunny guard). `InstrumentLayerZonePublishTest.cpp` is 86 code lines, is already registered, and its own header says it was split out of `InstrumentLayersCommandsTest` for this reason.
- The cases fail rather than skip when no instrument is loadable (`QVERIFY2` on `m_device`). A skip counts as a pass in ctest, which hid the RED state on the first run (see Commands).

## Commands and exit codes

Setup (environment, not code):

- Build failed first with `ccache: error: failed to create temporary file ... Permission denied`, because ccache's configured temp dir is under the shared `zene-bunny-fix/dsh` path. Fixed for this lane only by `export CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p10-46` (no config change).
- `plugin.midi_*` and the layer tests skip with "this build exposes no loadable instrument" in a targeted build: `build/plugins` had 0 `.so` files. Built `cmake --build build --target audiofileprocessor -j4` (EXIT=0) to supply an instrument. This is why the existing MX-114 cases now run and pass in this tree.

RED (fix reverted):

| Step | Command | EXIT |
| --- | --- | --- |
| Revert product fix | `git diff -- src/core/ControlCommandsPluginParts.cpp > $LOGS/fix.patch && git apply -R $LOGS/fix.patch` | 0 / 0 |
| Build | `cmake --build build --target InstrumentLayerZonePublishTest zene -j4 > $LOGS/build-red-2.log` | 0 |
| Run | `cd build/tests && ctest -R "^InstrumentLayerZonePublishTest$" --output-on-failure > $LOGS/test-red-2.log` | **8** (0% passed, 1 failed) |
| Binary detail | `build/tests/InstrumentLayerZonePublishTest -v2` | 2 FAIL: `midiPartsAddRefusedAtTheLimit` "a 129th part was accepted"; `midiPartsOverLimitSavedListLoadsWhole` `!refused.ok` |

GREEN (fix restored):

| Step | Command | EXIT |
| --- | --- | --- |
| Restore fix | `git apply $LOGS/fix.patch` (same diff verified with `diff -q` against a fresh `git diff`) | 0 |
| Build | `cmake --build build --target InstrumentLayerZonePublishTest zene -j4 > $LOGS/build-green-2.log` | 0 |
| Run | `cd build/tests && ctest -R "^InstrumentLayerZonePublishTest$" --output-on-failure > $LOGS/test-green-2.log` | **0** (100% passed, 1 of 1) |
| Binary detail | `build/tests/InstrumentLayerZonePublishTest -v2` | Totals: 7 passed, 0 failed, 0 skipped |

Earlier RED attempts, recorded for honesty: the first `ctest` run on the unfixed tree passed (EXIT 0), because the instrument tests skipped (no plugin built). A later RED on a new test file could not run: `cmake --build` reported no target, and the configure step is blocked by the guard, so that file was dropped in favour of the one above.

Family (`ctest -R "^(InstrumentLayerZonePublishTest|InstrumentLayersCommandsTest|InstrumentRoutingTest|ReversibilityUndoTest|ReversibilityUndoDeviceTest|InstrumentSetupUiTest)$"`), fix restored, EXIT **8** (5 of 6 pass):

| Test | ctest | Binary totals |
| --- | --- | --- |
| InstrumentLayerZonePublishTest | Passed | 7 passed |
| InstrumentLayersCommandsTest | Passed | 14 passed, 0 failed, 1 skipped (`multiOutputBusesRouteCreateUndo`: no `clapinstrument` module, pre-existing) |
| InstrumentRoutingTest | Passed | 18 passed |
| ReversibilityUndoTest | Passed | (ctest only) |
| InstrumentSetupUiTest | Passed | 6 passed, 3 skipped |
| ReversibilityUndoDeviceTest | **SegFault** | EXIT 139 |

`ReversibilityUndoDeviceTest` was already crashing before this change. Baseline: with the product fix reverted and `zene` + that test rebuilt, `build/tests/ReversibilityUndoDeviceTest -v2` exits 139 (`test-device-baseline.log`). Its earlier failure mode, four `the LADSPA host plugin 'ladspaeffect' is not in this build` failures, appeared before this lane built `libladspaeffect.so`; building it (for the baseline run) exposed the crash. The test does not touch MIDI parts (`grep` for `midi_part` in it finds none).

Gates. `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` could not run: it exits with `PermissionError` creating `<worktree>-feature-check` next to the worktree (`projects/zene-studio-program/`), which is outside this lane's write-jail (log `feature-check-1.log`, EXIT 1). It also writes a baseline worktree into the shared `zene-cachyos` repo. I did not modify the gate. Instead I ran each command it lists, from the worktree, with logs in the LOG FOLDER:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | **1** |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | **1** |

The two ratchet gates are red on the train, as the brief says. None of their reported items is in a file this change touched: the 29 REGRESSION lines (6 complexity, 23 file-length) name other files (for example `src/core/TakeLane.cpp`, `src/wasm/WasmSandbox.cpp`, `plugins/Vst3Effect/Vst3Host.cpp`, `tests/src/core/InstrumentRoutingTest.cpp`, which this change did not edit). `InstrumentLayersCommandsTest.cpp` now appears in neither list, because it is back at its base content. `addPart` stays far under CCN 10 (one added `if`).

## Files committed

- `src/core/ControlCommandsPluginParts.cpp` (+11): `kMaxParts` and the refusal in `addPart`.
- `tests/src/core/InstrumentLayerZonePublishTest.cpp` (+93 / -1): the two regression cases and their helpers; the header line now also names this cluster.
- `docs/qwen-fixes-2026-10/cluster-new-p10-46.md`: this report.

No header changed, so no other target needed a rebuild. No test registration changed (the file was already registered).

## Self-check

Completion check before this commit (`submit_check.py <worktree> new-p10-46`, EXIT 1, output in `submit-check-1.log`): FAIL "commits on the branch" and "working tree clean" (cleared by the commit that carries this report); "tests added or changed" now passes (two cases in `InstrumentLayerZonePublishTest.cpp`); "gates (feature_check.py)" still FAILS, because the script cannot run from this lane (see Gates above). The six gate commands it lists exit 0 when run directly; the two ratchets exit 1 on train-level items outside this change.

- **Regression fails without the change and passes with it:** RED `ctest -R "^InstrumentLayerZonePublishTest$"` EXIT 8 with the product diff reverted (`test-red-2.log`); GREEN EXIT 0 with it restored (`test-green-2.log`). Both use the same `fix.patch`, the product file's diff only.
- **Deterministic:** no sleep, timer, thread or random input. The list is filled through the model's own seam, `RackZones::assign` (`include/RackZones.h:135`), and the save/reload uses an in-memory `QDomDocument`. The only inputs are the fixed counts 128 and 130.
- **At the cause, complete:** the cause is that `addPart` (`src/core/ControlCommandsPluginParts.cpp:183`, redo at `:189`) is the only command that grows a track's part list. Every writer of `midiParts()` in `src` and `include` was checked: `addZone` (`:183`, `:189`) is now bounded at the command or argued safe for redo; `insertZone` (part-remove undo) restores a length it removed; `replacePart` (`plugin.midi_part_set`) keeps the length; `removeZone` shrinks; `InstrumentTrackNotes.cpp:424` (`m_midiParts.loadSettings`, project load) is deliberately unlimited. The sibling `rack.zone_add` / `rack.layer_zone_add` has the same unbounded scan and is a follow-up, not changed here.
- **No collateral damage:** project load is untouched (`InstrumentMidiParts.cpp:98-101`, reached from `InstrumentTrackNotes.cpp:424`; `midiPartsOverLimitSavedListLoadsWhole` pins it). The check refuses only the add that would exceed 128. Callers under the limit still pass: `InstrumentLayersCommandsTest` (14 passed, its MIDI-part cases add one or two parts), `InstrumentRoutingTest` (18 passed), `ReversibilityUndoTest` (passed, uses `plugin.midi_part_add`). No change to the note path, to accepted adds' undo steps, or to any command description or schema, so the snapshot does not move.
- **Verdict is REAL:** the unbounded add is at `ControlCommandsPluginParts.cpp:183` (before the fix, no count check before it), and each note-on pays a linear scan of that list (`InstrumentTrackNotes.cpp:332` → `InstrumentMidiParts.cpp:57` → `RackZones.cpp:143-156`) on the audio thread.
