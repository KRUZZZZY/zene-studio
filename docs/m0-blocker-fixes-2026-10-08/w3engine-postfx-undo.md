# M0 blocker fixes: W3EngineTest post-fader insert case (cluster w3engine-postfx-undo)

Branch `bugs/m0-fix-w3engine-postfx-undo` (from `28cc3a0b5`). Test-only batch: no product code changed.

| Item | Cluster | Verdict | Fix commit | Test | Red exit | Green exit | Gate exits |
|---|---|---|---|---|---|---|---|
| 1. `loadBuiltin` passes `"plugin"` to `plugin.load` (the argument is `"device"`), so the case skipped | w3engine-postfx-undo | FIXED | `7d69287ec` | `W3EngineTest` (`postFaderInsertsAreAddressedAndSavedWithTheChannel`) | ctest exit 0 while the case was a SKIP (silent skip; scratch log run1v.log, not committed); after the key fix alone, still SKIP because the modules were not built | 9 passed, 0 skipped, ctest exit 0 | complexity 0, file-length 0, fork-sources 0, no-upstream-regression 0 |
| 2. Second `control.undo` after bypass, undo, save/load on channel 3 leaves the post-fader device on ch-N/post | w3engine-postfx-undo | NOT-A-PRODUCT-DEFECT (test step order); test corrected | `7d69287ec` | same case, line 250 assertion kept and strengthened | exit 1: `FAIL! ... channel(2)->m_postInserts.chain().effects().empty() returned FALSE` at `W3EngineTest.cpp:250` (after the module dependency, before the restore fix) | exit 0 | as above |

## Item 1: evidence
- `src/core/ControlCommandsPlugin.cpp` (`registerPluginLoad`): the argument schema is `target` and `device`. The old test passed `plugin`, so `plugin.load` returned `invalid_args`, `loadBuiltin` returned empty, and the case called `QSKIP`.
- Only `tests/src/core/W3EngineTest.cpp` used `"plugin"` as the key for `plugin.load`. Every other `plugin.load` call in `tests/` (C++ and Python) uses `"device"`.
- The skip was not visible to ctest: ctest reported "Passed" for the skipped run.
- The modules `amplifier` and `receiver` are not built by `--target W3EngineTest`. Other tests that load modules declare `add_dependencies` (for example `PhaseDSidechainTest` on `compressor`, `tests/CMakeLists.txt` near line 1478). The fix adds `add_dependencies(W3EngineTest amplifier receiver)` in the test loop (`tests/CMakeLists.txt`, after the `PluginScanCacheTest` block area, around line 1284). Declared in `tests/upstream-modifications.txt`.

## Item 2: verdict and evidence
Not a product undo defect for post-fader inserts. The second `control.undo` undid an unrecorded journal step that the test's own step order put on top of the stack.

Journal state, read through `ProjectJournal::topStepSerial()`, `undoDepth()` and `redoDepth()` (temporary instrumentation, since removed):

| Point in the case | top serial | undo depth | redo depth | Note |
|---|---|---|---|---|
| after `plugin.load` | 6 | 5 | 0 | the plugin.load step |
| after `plugin.bypass` | 8 | 6 | 0 | |
| after first `control.undo` | 6 | 5 | 1 | undid `plugin.bypass`, as the reply says |
| after `save` on channel 2 | 6 | 5 | 1 | no step |
| after `load(element)` on channel 3 | 6 | 5 | 1 | no step |
| after `load(empty)` on channel 3 | 9 | 6 | 0 | **new unrecorded step** |
| after second `control.undo` | 6 | 5 | 1 | undid serial 9; the post-fader device stayed |

Causes:
1. `EffectChain::clear()` (`src/core/EffectChain.cpp:443-457`) writes `m_enabledModel.setValue(false)` (line 457) and deletes its devices without a journal record. Called with the journal on, it pushes a flag-only step.
2. `PostFaderInserts::load()` with an absent `<postinserts>` element calls `clear()` (`src/core/MixerPostInserts.cpp`, `load`/`clear`).
3. A project load restores post inserts with the journal off: `src/core/Song.cpp:1590` `setJournalling(false)` in `loadProject`, then `src/core/Mixer.cpp:2639` `m_postInserts.load(mixch)` in `Mixer::loadSettings`. The test ran the same restore with the journal on, which the product never does on that path.
4. A `load(element)` with devices pushed no step (serial stayed at 6), which is why only the clear case showed.

The fix is in the test: the channel-3 restore now runs with the journal off, as project load does (`JournalOff` guard), and the test asserts `topStepSerial()` is unchanged across it. The `plugin.load` assertion at line 250 is unchanged, and the new assertion strengthens the test.

Product paths that call `EffectChain::clear()` outside a project load (`Mixer::clearChannel`, `src/core/Mixer.cpp:2294-2300`, reached from `src/gui/MixerView.cpp:578` and `src/gui/MixerView.cpp:736`) were not checked for a journal bracket. Unverified whether those GUI paths push an unrecorded step.

## Observed, NOT fixed in this batch (for the owner)
`undoLastCommand` (`src/core/ControlCommandsControl.cpp`, around lines 381-420) takes the reply name from `registry.lastTransaction()` and never compares the record's `step` with `ProjectJournal::topStepSerial()`. `undoThroughJournal` (around lines 301-322) then calls `journal->undo()` and echoes the record's command. In the run above the second `control.undo` replied `undone_command: "plugin.bypass"` while it undid serial 9 (channel 3's post chain flag). The same happens for any unrecorded journal step, for example a GUI edit followed by an agent undo. The reply's name is therefore wrong in that case. Options are to refuse when the top step is not the record's step, or to name the step actually undone. This is a design choice and is outside this item.

## Not verified
- Other tests that load the amplifier or receiver module without an `add_dependencies` (for example `DeviceToolsTest`, `DeviceDspTest`, `RealtimeSnapshotTest`, the block near `tests/CMakeLists.txt:1256`) may skip silently in targeted builds. Not checked.
- The GUI channel-clear journal bracket (see above).
