# Batch 10 — Space Bunny lane `cmds1`

Six findings triaged against the current tree: three fixed, two false positives,
one real but untestable headlessly. No command was added, removed or renamed and
no command's params or result schema changed.

Environment note: as in batch 09 the build tree has `LIST_PLUGINS:BOOL=OFF`, so
the console module was not built and every console case skipped — which is why
the three new console regression cases first reported SKIP rather than RED.
Building the `console` target (`cmake --build build --target console`, EXIT=0,
producing `build/plugins/libconsole.so`) made them run and expose the two
console defects. The tests re-create mixer channels after `project.new`, which
rebuilds the mixer and otherwise leaves `mixerChannel(2)` null.

## Cluster 3873 — `mixer.console_apply` can take several undo steps

Verdict: FALSE-POSITIVE

Evidence: `ControlRegistry.cpp:337` `runHandler` marks `journal->undoDepth()`
before the handler and calls `ProjectJournal::mergeCheckpointsFrom(mark)` after
it, merging every checkpoint the handler pushed into one. `removeDevicesRecorded`
(`ControlDeviceMoves.cpp:131-154`) does add one `addStructuralUndoStep` per
removed device, but all of them are pushed inside a single `mixer.console_apply`
handler run, so they are merged. The regression test asserts exactly that and
passes on the current tree.

Commit: none

Test: `DeviceDspTest::oneConsoleApplyIsOneUndoStep`

Red/green: none — green on the unmodified tree.
`cmake --build build --target zene DeviceDspTest` EXIT=0; `cd build/tests &&
ctest -R '^DeviceDspTest$'` EXIT=0 (the case passes).

## Cluster 4760 — Removing zero consoles still dirties the project

Verdict: FIXED

Evidence: `src/core/ControlCommandsDeviceTools.cpp:250` (old) called
`Engine::getSong()->setModified()` unconditionally at the end of
`removeConsoles()`, so `mixer.console_apply` with `remove:true` on a project
with no Console replied `{"removed":0}` and still dirtied the song. The call is
now guarded on `removed > 0` (`ControlCommandsDeviceTools.cpp:263`).

Commit: `e6cd4b0b9`

Test: `DeviceDspTest::removingNoConsoleLeavesTheProjectClean`

Red/green:
- red: `cmake --build build --target zene DeviceDspTest` EXIT=0; `cd build/tests && ctest -R '^DeviceDspTest$' --output-on-failure` EXIT=2 (`FAIL! removingNoConsoleLeavesTheProjectClean() '!Engine::getSong()->isModified()' returned FALSE`)
- green: same build EXIT=0; `ctest -R '^DeviceDspTest$'` EXIT=0 (100% tests passed, 0 failed out of 1); `ctest -R '^DeviceToolsTest$'` EXIT=0
- gates: complexity EXIT=0, file-length EXIT=0

## Cluster 5642 — Console creation failures are ignored

Verdict: REAL-UNTESTED

Evidence: real but unreachable headlessly. `placeConsoles`
(`ControlCommandsDeviceTools.cpp:282`) passes `&error` to `recreateDevices` and
ignores it; `recreateDevices` (`ControlDeviceMoves.cpp:107-115`) only sets `*error`
when `recreateEffectFromState` returns null. That can only fail when the
descriptor named by the state cannot be instantiated, and the state handed to
`recreateDevices` was produced by `consoleTemplate`
(`ControlCommandsDeviceTools.cpp:219-230`) from an instance the same catalogue
entry just instantiated successfully on a scratch chain. There is no headless
way to make that instantiation fail for one channel but not the scratch chain,
so no deterministic failing test could be written and no product code was
changed. The proposed fix — propagating the error and rolling back the partial
apply — is also a behaviour change to what a partially applied command reports.

Commit: none

Test: none

Red/green: none.

## Cluster 3872 — Console apply overcounts changed settings

Verdict: FIXED

Evidence: `consoleApply` returned `changed` as `writes.size()`, the number of
requested writes, so applying the drive or character a Console already carried
reported changes although no model moved. `countChangedWrites`
(`ControlCommandsDeviceTools.cpp:241`) now compares each clamped request against
`model->value<float>()` before the writes are applied
(`ControlCommandsDeviceTools.cpp:300`).

Commit: `2d09143db`

Test: `DeviceDspTest::applyingTheValueAlreadyThereReportsNoChange`

Red/green:
- red: `cmake --build build --target zene DeviceDspTest` EXIT=0; `cd build/tests && ctest -R '^DeviceDspTest$' --output-on-failure` EXIT=2 (`FAIL! applyingTheValueAlreadyThereReportsNoChange() '...changed...toInt() == 0' returned FALSE`)
- green: same build EXIT=0; `ctest -R '^DeviceDspTest$'` EXIT=0 (100% tests passed, 0 failed out of 1); `ctest -R '^DeviceToolsTest$'` EXIT=0
- gates: complexity EXIT=0, file-length EXIT=0

## Cluster 5670 — DSP state omits mixer rack and post-insert chains

Verdict: FIXED (post-fader inserts); the rack half is left undone

Evidence: real. `appendChannelChains` resolved each channel and reported only
`target.chain`, which `resolveChannelTarget` sets to `m_fxChain`, so a device in
a channel's post-fader inserts was absent from the project-wide `dsp.get_state`
`chains` and `device_count`. Each channel's inserts are now resolved through
`postinserts::targetOf` and appended under that id
(`ControlCommandsDsp.cpp:113`), the same way `plugin.param_search` reaches them
(`ControlCommandsDeviceParams.cpp:239-240`, commit `989114333`). The rack half is
skipped: `chainJson` keys a chain by `target.id`, and a mixer rack chain has no
addressable target id — `rack.*` commands address it by `channel` plus a `chain`
index — so reporting them would mean inventing a new target id form, i.e.
changing the result schema, which is out of scope for this lane.

Commit: `fea441bad`

Test: `DeviceToolsTest::aPostFaderInsertIsReportedByTheProjectWideState`

Red/green:
- red: `cmake --build build --target zene DeviceToolsTest` EXIT=0; `cd build/tests && ./DeviceToolsTest aPostFaderInsertIsReportedByTheProjectWideState` EXIT=1 (`FAIL! 'found == 1' returned FALSE`)
- green: same build EXIT=0; `ctest -R '^DeviceToolsTest$'` EXIT=0 (100% tests passed, 0 failed out of 1); `ctest -R '^ControlRegistryTest$'`, `'^ControlChainPresetTest$'`, `'^DeviceDspTest$'`, `'^ChainPresetMenuTest$'`, `'^DevicePanelTest$'`, `'^ModulatorPanelTest$'` EXIT=0 each
- gates: complexity EXIT=0, file-length EXIT=0

The test pins that the post chain is reported, is non-empty and carries the
device just loaded; it does not pin an exact count, because the earlier
`aPostFaderInsertParameterIsFound` case leaves an insert of its own on channel 2.

## Cluster 5655 — Invalid dither mode is accepted as a setting

Verdict: FALSE-POSITIVE

Evidence: the handler ignores `ditherModeFromName`'s return
(`ControlCommandsExport.cpp:160`), but `mode` is declared
`enumProperty(ditherModeNames())` (`ControlCommandsExport.cpp:137`) and
`ControlRegistry::invoke` runs `validateArgs(cmd->argsSchema, args)`
(`ControlRegistry.cpp:251`) before the handler, with `checkEnum`
(`ControlSchema.cpp:102`, called at `ControlSchema.cpp:156`) rejecting an
unlisted value. The handler therefore never sees an unrecognised mode. Confirmed empirically: a
throwaway probe case invoking `export.set_dither` with `{"mode":"bogus"}`
reported `errorKind == InvalidArgs` and left `ditherMode()` at `Off`; the probe
was reverted and is not part of the tree.

Commit: none

Test: none (the probe was temporary and reverted)

Red/green: none — the behaviour is already correct.

## Leftovers

Cluster 5670's rack half is deliberately unfixed: mixer rack chains still do not
appear in `dsp.get_state`, and fixing them needs a target id form that does not
exist yet — a design decision, and adding one changes the result schema.

Cluster 5642 is left as-is: the ignored `recreateDevices` error is real but
cannot be provoked headlessly, and the proposed rollback is a behaviour change.

The build tree still has `LIST_PLUGINS:BOOL=OFF`. I built the `console` target
into `build/` only to make the console cases run; that artefact is untracked and
no build configuration was changed.
