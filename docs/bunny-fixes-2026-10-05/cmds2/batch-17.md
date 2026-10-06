# Space Bunny batch 17 — lane `cmds2` (control commands ControlCommands[N-Z])

Worktree `zene-bunny-cmds2`, branch `bugs/bunny-cmds2-2026-10-05`.
Findings adjudicated: 6 — 3 FIXED (one commit, shared seam), 1 REAL-UNTESTED,
1 FALSE-POSITIVE, 1 REAL-UNTESTED (unreachable state).

### Cluster 4932 — The land transaction inverse deletes only the first clip

Verdict: REAL-UNTESTED

Evidence: `src/core/ControlCommandsSessionRecordLand.cpp:178-205`. The pass
counts every landed pair into `clips` (`++clips` per landed entry, `:157`), but
the `__transaction` it publishes names only the FIRST landed clip's id
(`firstClip` / `firstLanded`, `:178-196`) as a `clip.delete` inverse, so
replaying the advertised pair after a multi-clip pass leaves every clip but the
first. The mechanism string the handler itself publishes admits it: "The
recorded inverse names the FIRST clip of the pass, which clip.delete removes
exactly; control.undo restores every touched track, which removes them all."
The real inverse is the per-track journal checkpoint (`control.undo`), which is
the same mismatch already fixed twice on this branch — cluster 4806
(`src/core/ControlCommandsNoteShared.h:72-79`) and `930bf62f7` (cluster 1223).

Commit: none (no deterministic headless test exists — see Red/green)

Test: none

Red/green: none. `session.arrangement_record_land` cannot be driven headlessly:
it consumes a PERFORMANCE the audio thread pushes into the recorder
(`tests/src/core/SessionArrangementRecordTest.cpp:28-33` documents the split),
and no C++ test in `tests/` executes the command through `ControlRegistry`
(`rg -n 'arrangement_record_land' tests` matches only
`tests/control-success-paths.py:48`, a live-audio harness, plus two unrelated
mentions). Per procedure step 2, a finding without a deterministic failing
test gets no product change.

### Cluster 4925 — Undo cannot restore a previously absent session block

Verdict: FALSE-POSITIVE

Evidence: `sessionBlockXml` (`src/core/ControlCommandsSessionShared.h:412-418`)
calls `SessionModel::saveState`, which ALWAYS appends a `<session>` element
(`src/core/SessionModel.cpp:245-246`). The "no session element" branch the
report describes (`:230-243`) only re-emits the preserved unknown root:
`m_preservedUnknownXml` is assigned at `src/core/SessionModel.cpp:313`, after
the tag check at `:295`, so it can only be set when a `<session>` element was
seen. `restoreSessionBlock` (`ControlCommandsSessionShared.h:422-428`) therefore
always finds a block to restore.

Commit: none

Test: none

Red/green: none (no product change warranted)

### Cluster 4939 — Settings edits can claim reversibility without recording an undo step

Verdict: REAL-UNTESTED

Evidence: `control::addUndoStep` returns silently with no journal
(`src/core/ControlReversibility.cpp:147-152`), and the settings commands still
send `reversible: true` (`src/core/ControlCommandsSettings.cpp:196`, transaction
built at `:229`). The state is unreachable in the shipped path: `main.cpp:1634`
calls `setReady(true)` only after `Engine::init` (`src/core/main.cpp:1228`,
`:1354`), so the registry is never ready — and therefore no such command is
dispatchable — while the journal can still be absent. The E19 guard also
refuses a misattributed undo (`tests/src/core/UndoHonestyTest.cpp:210-220`,
`src/core/ControlRegistry.cpp:298-303`, `:415`).

Commit: none

Test: none

Red/green: none. Making the branch reachable headlessly means driving the
commands before `Engine::init`, i.e. without `setReady(true)` — which the
registry requires (dispatch is refused until ready). No deterministic test
exists, so no product change was made. Note (not fixed, out of this batch's
three seams): the `settings.set` description still claims `control.undo`
cannot reverse it, which `CustomAccessTest::theSettingsAreFoundBackedUpAndKept`
(`tests/src/core/CustomAccessTest.cpp:327-360`) disproves.

### Cluster 4937 — `settings.set` does not update the separate scale file

Verdict: FIXED

Evidence: the write path only touched `ConfigManager`, while startup reads the
separate scale file (`src/core/Appearance.cpp:129-133` `scaleFilePath()`,
`:135-141` `storedScale()`). The fix routes undo/redo/apply through a new
`writeSetting(cls, attribute, value, present)` helper
(`src/core/ControlCommandsSettings.cpp`, ~line 95) that calls
`appearance::syncScaleFile()` when the key is `ui/scale` and then
`saveConfigFile()`, matching what preset restore already did
(`src/core/ControlCommandsSettingsPresets.cpp:140-151`).

Commit: `61839117e`

Test: `CustomAccessTest::theSettingScaleReachesTheFileStartupReads`

Red/green: Red `cd build/tests && ctest -R '^CustomAccessTest$' --output-on-failure` EXIT=8 (fails at
`tests/src/core/CustomAccessTest.cpp:432`: the scale file keeps the old value after
`settings.set ui/scale`). Green `cmake --build build --target zene CustomAccessTest` EXIT=0;
`cd build/tests && ctest -R '^CustomAccessTest$' --output-on-failure` EXIT=0 (`CustomAccessTest Passed 0.52 sec`).

### Cluster 4940 — `settings.set` advertises the wrong inverse for a previously absent key

Verdict: FIXED

Evidence: pre-fix, the advertised inverse for an absent key was `settings.set`
with an empty value, which sets an empty key instead of deleting it, even
though the undo closure captured `present`. The row now advertises
`settings.set` with the previous value when the key was present, and
`control.undo` with no args when it was absent (the `930bf62f7` /
cluster-1223 pattern — the journal checkpoint is the inverse).

Commit: `61839117e`

Test: `CustomAccessTest::theSettingUndoOfAnAbsentKeyDeletesItAgain`

Red/green: Red `cd build/tests && ctest -R '^CustomAccessTest$' --output-on-failure` EXIT=8 (fails at
`tests/src/core/CustomAccessTest.cpp:420`: the marker key is still present after the advertised
inverse is replayed). Green `cmake --build build --target zene CustomAccessTest` EXIT=0;
`cd build/tests && ctest -R '^CustomAccessTest$' --output-on-failure` EXIT=0.

### Cluster 4941 — Undoing `audio.device_set` does not restore an absent key

Verdict: FIXED

Evidence: the `audio.device_set` undo closure captured only `previous` and
always wrote that string back, so undoing an absent `audioengine/audiodev`
key created it with an empty value. The closure now captures `present` and
deletes the key through `writeSetting` when it was absent; the advertised
inverse row matches (device command when the key was there, `control.undo`
when it was not — the empty device name it used to advertise is rejected by
the command).

Commit: `61839117e`

Test: `CustomAccessTest::theDeviceUndoOfAnAbsentKeyDeletesItAgain`

Red/green: Red `cd build/tests && ctest -R '^CustomAccessTest$' --output-on-failure` EXIT=8 (fails at
`tests/src/core/CustomAccessTest.cpp:397`: the absent device key is present again after the inverse
replay). Green `cmake --build build --target zene CustomAccessTest` EXIT=0;
`cd build/tests && ctest -R '^CustomAccessTest$' --output-on-failure` EXIT=0.

## Wider run

`cmake --build build --target zene ReversibilityContractTest ReversibilityUndoTest UndoHonestyTest ControlResultSchemaTest ControlVerbInverseTest CollabPolicyTest ControlRegistryTest` EXIT=0

`cd build/tests && ctest -R '^(ReversibilityContractTest|ReversibilityUndoTest|UndoHonestyTest|ControlResultSchemaTest|ControlVerbInverseTest|CollabPolicyTest|ControlRegistryTest|ControlCommandsSnapshot|CustomAccessTest)$' --output-on-failure` EXIT=8

Passing: ControlCommandsSnapshot (schema canary intact), ControlRegistryTest,
ControlResultSchemaTest, ControlVerbInverseTest, ReversibilityContractTest,
CustomAccessTest, UndoHonestyTest, CollabPolicyTest.
Failing: `ReversibilityUndoTest::firstAutomationPointIsOneUndoableStep` —
"'!parameter.isEmpty()' returned FALSE (the loaded instrument exposes no
automatable parameter to place a point on)". Environment/plugin dependent
(`Lv2 plugin SUMMARY: 73 of 87 loaded`, `CLAP: could not load "" '' does not
export clap_entry`); it touches no settings or device-set path
(`rg 'settings|device_set' tests/src/core/ReversibilityUndoTest.cpp` matches
only comments), and it was not introduced by this batch.

Gates before commit: `bash tests/complexity-gate.sh --check` EXIT=0;
`bash tests/file-length-gate.sh --check` EXIT=0.

Leftovers: cluster 4932 (land inverse advertises `clip.delete` for the first
clip only — real, needs a live-performance harness this lane does not have);
cluster 4939 (honest `reversible:true` on a null journal — unreachable before
`Engine::init`, no headless test); the stale `settings.set` description that
still denies `control.undo` can reverse it; the pre-existing
`ReversibilityUndoTest::firstAutomationPointIsOneUndoableStep` plugin-scanning
failure. The three FIXED findings share commit `61839117e` because all three
reasons are the same seam (`writeSetting` in `ControlCommandsSettings.cpp`) —
splitting them would have meant three commits where two contain no source
change.
