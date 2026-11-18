# cmds2 batch-04 — Luna findings 5722, 5727, 2896, 5723, 5724, 2050

Branch `bugs/bunny-cmds2-2026-10-05`. Six findings, five FIXED, one REAL-UNTESTED.

Shared fixture for the four plugin-state findings: an instrument track whose built-in
plugin is addressed as `inst` (`revtest::addInstrumentTrack`). Its state document is
the track preset, and `Track::savePreset` writes the track NAME into it
(`src/core/Track.cpp:454`) while `Track::loadPreset` reads it back
(`src/core/Track.cpp:609`), so `track.rename` / `track.get_state` make device state
observable. This build exposes no automatable parameter on that device and no loadable
built-in effect, which is why the pre-existing `firstAutomationPointIsOneUndoableStep`
fails and several device cases skip.

### Cluster 5722 — Preset-load redo uses the current file contents
Verdict: FIXED
Evidence: `src/core/ControlCommandsPluginPreset.cpp:315-325` — `registerPresetLoad`'s redo
closure captured `path` and re-read the file via `controlReadFileBytes`, so a preset replaced
between undo and redo decided what the redo applied, and a deleted preset made the redo do
nothing while still reporting `redone: true`. The closure now captures the `bytes` the load
restored.
Commit: 8e988406b
Test: UndoHonestyTest::thePresetLoadRedoReplaysTheBytesItLoaded
Red/green:
- `cmake --build build --target zene UndoHonestyTest` EXIT=0
- `cd build/tests && ctest -R '^UndoHonestyTest$' --output-on-failure` EXIT=8 — `Actual "decoy" Expected "alpha"`
- `cmake --build build --target zene UndoHonestyTest` EXIT=0
- `cd build/tests && ctest -R '^UndoHonestyTest$' --output-on-failure` EXIT=0 (and a second consecutive run EXIT=0)
- `cmake --build build --target zene ControlRegistryTest CustomAccessTest` EXIT=0; `ctest -R '^ControlRegistryTest$'` EXIT=0; `ctest -R '^CustomAccessTest$'` EXIT=0
- `bash tests/complexity-gate.sh --check` EXIT=0; `bash tests/file-length-gate.sh --check` EXIT=0

### Cluster 5727 — Quarantine accepts paths that the scan will not match
Verdict: FIXED
Evidence: `src/core/ControlCommandsPluginScanEdit.cpp` — `handleQuarantineAdd`/`handleQuarantineRemove`
checked only that `path` was non-empty, while `PluginFactory.cpp:509` matches quarantine entries
against a candidate's `file.absoluteFilePath()`. A relative entry was stored and never skipped.
Both handlers now refuse a non-absolute path (`QFileInfo::isAbsolute`).
Commit: 5eee4bc14
Test: ControlRegistryTest::quarantineRefusesARelativePath
Red/green: RED showed `plugin.quarantine_add` accepting `./bad-plugin.so`;
`cmake --build build --target zene ControlRegistryTest` EXIT=0;
`ctest -R '^ControlRegistryTest$'` EXIT=0; `ctest -R '^PluginScanCacheTest$'` EXIT=0; both gates EXIT=0.

### Cluster 2896 — Valid Windows absolute paths are rejected
Verdict: REAL-UNTESTED
Evidence: REAL but platform-only. `src/core/ControlCommandsPluginState.cpp:91` and
`src/core/ControlCommandsProject.cpp:216` (also `ControlCommandsRenderStems.cpp:119`,
`ControlCommandsMeterFile.cpp:147`, `ControlCommandsMasteringRun.cpp:151`,
`ControlStemSupport.cpp:480`, `BounceInPlace.cpp:191`) accept an absolute path only when it
starts with `/`, so `C:\exports\state.xpf` and `\\server\share\state.xpf` are refused on
Windows. The suggested `QFileInfo::isAbsolute()` is platform-aware but, on this Linux build,
returns false for exactly the strings the finding is about — a headless test can only assert
the wrong thing here. No product change made.
Commit: none
Test: none
Red/green: none

### Cluster 5723 — Failed state loads leave an undo step behind
Verdict: FIXED
Evidence: `src/core/ControlCommandsPluginState.cpp:184-196` — `control::addUndoStep` ran before
`controlRestoreDeviceState`, so a refused document (wrong device, malformed) still pushed a step
that an undo would later spend on a device that never changed. The step is now registered only
after the restore succeeds.
Commit: 4c407b786
Test: ReversibilityUndoTest::aFailedStateLoadRecordsNoUndoStep
Red/green: RED showed undo depth 2 where 1 was expected after a refused load;
`cmake --build build --target zene ReversibilityUndoTest` EXIT=0;
`ctest -R '^ReversibilityUndoTest$'` EXIT=8 (10 passed, only the pre-existing
`firstAutomationPointIsOneUndoableStep` failing, 2 skipped); both gates EXIT=0.

### Cluster 5724 — Redo of a state load reads the current file contents
Verdict: FIXED
Evidence: `src/core/ControlCommandsPluginState.cpp:187-196` — the same shape as 5722 on the
state-load side: `registerStateLoad`'s redo closure captured `path` and re-read the file. It now
captures the bytes the load restored.
Commit: 1f4a794e9
Test: UndoHonestyTest::theStateLoadRedoReplaysTheBytesItLoaded
Red/green:
- `cmake --build build --target zene UndoHonestyTest ReversibilityUndoTest` EXIT=0
- `cd build/tests && ctest -R '^UndoHonestyTest$' --output-on-failure` EXIT=8 — `Actual "decoy" Expected "alpha"`
- `cmake --build build --target zene UndoHonestyTest ReversibilityUndoTest` EXIT=0
- `ctest -R '^UndoHonestyTest$'` EXIT=0; `ctest -R '^ReversibilityUndoTest$'` EXIT=8 (11 passed, only the pre-existing failure)
- both gates EXIT=0

### Cluster 2050 — The plugin state load transaction advertises an unusable inverse
Verdict: FIXED
Evidence: `src/core/ControlCommandsPluginState.cpp:200-215` — the record advertised
`plugin.state_save` as the inverse with only a `note`; that command needs `target`, `plugin` and
`path`, and saving the current state would not restore the previous one. The record now advertises
the action checkpoint the command actually took (`control.undo` through
`control::transactionPayload`), matching `ControlReversibilityTableAction.cpp:153`.
Commit: 8c3e916c1
Test: ReversibilityUndoTest::theStateLoadInverseIsTheCheckpointItActuallyRecorded
Red/green: RED replaying the advertised inverse was refused with `InvalidArgs`;
`cmake --build build --target zene ReversibilityUndoTest` EXIT=0;
`ctest -R '^ReversibilityUndoTest$'` EXIT=8 (11 passed, only the pre-existing failure);
both gates EXIT=0.

Leftovers: `registerPresetLoad` still has two shapes this batch fixed elsewhere and that were NOT
changed here (they would need their own test and commit): it registers its undo step BEFORE
`controlRestoreDeviceState` (`src/core/ControlCommandsPluginPreset.cpp:307-313`, the 5723 shape),
and it advertises `plugin.state_load` with only a `note` as its inverse
(`src/core/ControlCommandsPluginPreset.cpp:328-336`, the 2050 shape). The pre-existing
`ReversibilityUndoTest::firstAutomationPointIsOneUndoableStep` failure ("the loaded instrument
exposes no automatable parameter to place a point on") is unrelated to this batch and remains.
The remaining `startsWith('/')` absolute-path checks (2896) are untouched for the platform reason
given above.
