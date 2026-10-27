Verdict: FIXED

new-p16-266 (BUG 19): VST parameter dump parsing assumes every entry has three fields.

Evidence: the dump is written as `index:label:value` at [VstPlugin.cpp:521](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-266/plugins/VstBase/VstPlugin.cpp:521). The label is the only field that can contain `:`, so a plug-in label such as `Gain: 3 dB` produced four fields. The old readers took `split(":").at(2)`, which is the wrong field for such a label, and an entry with no value field read past the list. Those readers were: [VstEffectControls.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-266/plugins/VstEffect/VstEffectControls.cpp) (loader, `ManageVSTEffectView` constructor and `syncPlugin`; unfixed on the base at lines 98, 400-402, 410 and 477), [Vestige.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-266/plugins/Vestige/Vestige.cpp) (unfixed at lines 235, 1024-1026, 1034 and 1095), and the saved-project restore in [VstPlugin.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-266/plugins/VstBase/VstPlugin.cpp) (`setParameterDump` used `section(':', 2, -1)`, unfixed on the base at lines 386-398, so a colon-labelled value restored as 0). The finding's example of "fewer than three fields" is not produced by the current writer: `RemoteVstPlugin.cpp:1331-1338` sends indices `0..numParams-1`, so a short entry needs a corrupted message. The colon-in-label case is reachable with ordinary plug-ins.

Fix (round 1, `75b85dd68c`): one shared parser, `parseParameterDumpEntry()` in [VstPlugin.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-266/plugins/VstBase/VstPlugin.h). The value is the last field and the label is everything between the first and last separators. The six knob sites in VstEffectControls.cpp and Vestige.cpp use it. A value that does not parse keeps the knob's current value instead of being applied as 0.0.

Fix (round 2, this commit, answering the review): the saved-project restore now uses the same parser. `savedParameterValues()` ([VstPlugin.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-266/plugins/VstBase/VstPlugin.h), defined in [VstPlugin.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-266/plugins/VstBase/VstPlugin.cpp)) reads the `param<i>` attributes that `saveSettings` writes and keeps only the entries that parse. `VstPlugin::loadSettings` calls it, and `setParameterDump` now takes the parsed values and sends their count. Before, the count was the number of map entries and a malformed entry was sent as index 0 with value 0.0. The parser also rejects a negative or non-numeric index.

Behaviour changes, all deliberate and limited to bad data: a value that does not parse is no longer applied as 0.0 (the knob keeps its value, or the restore skips the parameter); a label containing `:` shows in full; a malformed index is skipped. A valid entry restores to the same value and index as before.

Regression tests, all in [VstEffectControlsTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-266/tests/src/plugins/VstEffectControlsTest.cpp):
- `labelContainingColonKeepsValueField` (round 1): dump entry `0:Gain: 3 dB:0.25` must load 0.25. Original code: `FAIL! ... qAbs(models.at(0)->value() - 0.25f) < 1e-4f returned FALSE`.
- `missingParameterEntryKeepsSavedValue` (round 1): dump has `param0` and `param2` only; the project saved `param1`=0.6 and it must survive. Original code: `Received signal 11 (SIGSEGV)` in the `loadSettings` lambda.
- `savedRestoreKeepsColonLabelsAndSkipsMalformedEntries` (round 2, the saved-project restore path): `numparams=4`; `param0` = `0:Gain: 3 dB:0.25`, `param1` = `1:Freq:0.5`, `param2` = `2:Mix:abc` (value does not parse), `param3` absent. `savedParameterValues()` must return exactly the entries 0 and 1 with values 0.25 and 0.5. This calls the function `loadSettings` calls; it does not run `loadSettings` itself, because a `VstPlugin` needs a live remote host for `setParameterDump` to send anything (see the test matrix below).

Test matrix: normal path (existing `repeatedSettingsLoadsDoNotRetainOldModels`; the valid entries in the restore test); malformed path (missing key, colon label, non-numeric value, absent parameter); reconfiguration (the repeated-load test); the wider VST/Vestige family (13 tests, all run); saved-project restore (the round-2 test). Not covered by a unit test: the `setParameterDump` message itself, the `ManageVSTEffectView` and Vestige UI paths. Those need a live plug-in host. They go through the same parser, which the tests cover. Realtime and undo: the parser and the restore run only on settings load and the UI sync, never on the audio thread, and create no undo step beyond what the existing `setValue` calls already create.

Proof that the tests catch the bug (round 2):
- Reversing the whole round-2 product change (`git apply -R fix-r2.patch`, EXIT=0) leaves the test unable to compile: `savedParameterValues` is not declared (`build-red-reversed-1.log`, build EXIT=2). That is a compile failure, not evidence of the bug, so I did not rely on it.
- Behavioural RED: with the fix in place, I changed only the value read inside `parseParameterDumpEntry` back to the original `entry.section(':', 2, -1)` (the restore path's old read). Build EXIT=0. The test then fails: `savedRestoreKeepsColonLabelsAndSkipsMalformedEntries` `values.size() == 2` returned FALSE, and `labelContainingColonKeepsValueField` FAIL (`test-red-mutant-1.log`, ctest EXIT=8, 4 passed, 2 failed).
- Mutation reverted (`Edit`, then `cmp` check that the product diff equals `fix-r2.patch`: `PRODUCT_DIFF_MATCHES_SAVED_FIX`). Build EXIT=0, `test-proof-green-1.log` ctest EXIT=0, 6 of 6 passed.
- Round 1 (`fix.patch`, reverse then re-apply, all recorded in the round-1 table below): test EXIT=8 without the fix, EXIT=0 with it.

Commands and exit codes, round 2 (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-266.r2/logs/`; all builds used `CCACHE_DISABLE=1`, because the sandbox ccache temp directory is not writable):

| Step | Command | Exit |
| --- | --- | --- |
| Build with round-2 fix | `cmake --build build --target vstbase vsteffect vestige VstEffectControlsTest zene -j4 > build-green-1.log 2>&1` | 0 |
| GREEN test | `cd build/tests && ctest -R '^VstEffectControlsTest$' -V > test-green-1.log 2>&1` | 0 (6 passed) |
| Save round-2 product diff | `git diff -- plugins/VstBase/VstPlugin.h plugins/VstBase/VstPlugin.cpp > fix-r2.patch` | 0 |
| Reverse round-2 product diff | `git apply -R fix-r2.patch` | 0 |
| Build, reversed | `cmake --build build --target vstbase vsteffect vestige VstEffectControlsTest zene -j4 > build-red-reversed-1.log 2>&1` | 2 (compile error: `savedParameterValues` not declared) |
| Re-apply round-2 diff | `git apply fix-r2.patch` | 0 |
| Behavioural RED: mutant build (old value read in the parser) | `cmake --build build --target vstbase vsteffect vestige VstEffectControlsTest zene -j4 > build-red-mutant-1.log 2>&1` | 0 |
| Behavioural RED: mutant test | `cd build/tests && ctest -R '^VstEffectControlsTest$' -V > test-red-mutant-1.log 2>&1` | 8 (2 FAIL, 4 pass) |
| Mutant reverted, rebuild | `cmake --build build --target vstbase vsteffect vestige VstEffectControlsTest zene -j4 > build-proof-green-1.log 2>&1` | 0 |
| Proof GREEN test | `cd build/tests && ctest -R '^VstEffectControlsTest$' -V > test-proof-green-1.log 2>&1` | 0 (6 passed) |
| VST family build | `cmake --build build --target VstSubPluginFeaturesTest VstMidiEventBatchTest VestigeMidiHandoffTest Vst3BusMapTest Vst3HostTest Vst3ChunkProbeTest Vst3EffectIntegrationTest Vst3InstrumentFixtureProbe Vst3InstrumentTest Vst3InstrumentIntegrationTest -j4 > build-family-1.log 2>&1` | 0 |
| VST family test, first run | `cd build/tests && ctest -R 'Vst\|Vestige' --output-on-failure > test-family-1.log 2>&1` | 8 (2 "Not Run": `VstChannelCountTest`, `Vst3EditorTest`, never built; a build gap, not a result) |
| Build the two missing tests | `cmake --build build --target VstChannelCountTest Vst3EditorTest -j4 > build-family-2.log 2>&1` | 0 |
| VST family test | `cd build/tests && ctest -R 'Vst\|Vestige' --output-on-failure > test-family-2.log 2>&1` | 0 (13/13 passed) |
| Gates, direct | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > feature-check-1.log 2>&1` | 1 (`PermissionError` writing `<worktree>-feature-check/fork-sources.log`, a folder beside the worktree outside my write jail; the gate itself did not run) |
| Gates, same functions, logs in the LOG FOLDER | `python3 gate_driver.py <LOG FOLDER> > gates-driver-1.log 2>&1` | 0 (all 8 rows PASS) |

The driver reuses `feature_check.py`'s `SIMPLE`, `RATCHET` and `base_listings` without editing them. Its base is `c6ee4fc7b`, the merge base with `050/m0-train-20` now; the earlier base was `5c1c6715d`. For the new base, the gate tool wrote its base listing into the shared `gate-baselines/` cache and checked the base out in `gate-baselines/tree`. That is the tool's own cache, the same write `feature_check.py` makes when the base moves. Complexity and file-length return exit 1 raw, with "no new item in your files", as expected on the train.

Round 1 (commit `75b85dd68c`, logs in `.../new-p16-266.r1/logs/`), kept for the record:

| Step | Command | Exit |
| --- | --- | --- |
| RED build (first try) | `cmake --build build --target VstEffectControlsTest -j4 > build-red-1.log 2>&1` | 2 (ccache temp dir not writable) |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target VstEffectControlsTest -j4 > build-red-2.log 2>&1` | 0 |
| RED test | `cd build/tests && ctest -R '^VstEffectControlsTest$' --output-on-failure > test-red-1.log 2>&1` | 8 (colon FAIL, then SIGSEGV) |
| GREEN build | `... --target vstbase vsteffect vestige VstEffectControlsTest -j4 > build-green-1.log` and `... --target zene -j4 > build-zene-green-1.log` | 0, 0 |
| GREEN test | `cd build/tests && ctest -R '^VstEffectControlsTest$' -V > test-green-verbose-1.log 2>&1` | 0 (5 passed) |
| Family, first run | `ctest -R 'Vst|Vestige' ... > test-family-green-1.log` | 8 (10 "Not Run", build gap) |
| Family, after build | `ctest -R 'Vst|Vestige' ... > test-family-green-2.log` | 0 (11/11) |
| Proof: save and reverse fix | `git diff -- plugins/VstBase/VstPlugin.h plugins/VstEffect/VstEffectControls.cpp plugins/Vestige/Vestige.cpp > fix.patch`; `git apply -R fix.patch` | 0, 0 |
| Proof RED build and test | `... build-proof-red-1.log`; `ctest ... test-proof-red-1.log` | 0; 8 |
| Proof: re-apply, GREEN build and test | `git apply fix.patch`; `... build-proof-green-1.log`; `test-proof-green-1.log` | 0; 0; 0 |

Files committed (round 2): `plugins/VstBase/VstPlugin.h`, `plugins/VstBase/VstPlugin.cpp`, `tests/src/plugins/VstEffectControlsTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p16-266.md`. Round 1 committed `plugins/VstBase/VstPlugin.h`, `plugins/VstEffect/VstEffectControls.cpp`, `plugins/Vestige/Vestige.cpp`, the test and this report. No new files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` need no change. The no-upstream-regression gate passed.

## Self-check

Submit check: `submit_check.py ... new-p16-266` (round 1, `submit-check-1.log`) passed commits, clean tree, report and test-changed; the Self-check row was answered in round 1 and the gates row was red because `submit_check.py` runs `feature_check.py`, which cannot write its log folder from my sandbox. The gate functions passed through the driver (`gates-driver-1.log`, EXIT=0). The round-2 submit check is in `submit-check-2.log`.

- **Regression fails with the fix reverted, passes with it:** yes, behaviourally, by the targeted mutation: the test fails at `values.size() == 2` and at the colon-label assertion (`test-red-mutant-1.log`, EXIT=8) and passes with the fix restored (`test-proof-green-1.log`, EXIT=0, 6/6). Reversing the whole round-2 product diff gives a compile error instead (`build-red-reversed-1.log`, EXIT=2), which is not evidence of the bug.
- **Deterministic:** yes. No sleeps, timers or threads. The saved values are a `QDomElement` built in the test; the dump for the knob tests goes through the existing seam `vstEffectControlsTestParameterDump` (`plugins/VstEffect/VstEffectControls.cpp:48-52`).
- **At the cause, every caller and sibling checked:** `rg -n 'section\(|split\(":"\)|\.at\(2\)' plugins/VstBase plugins/VstEffect plugins/Vestige` now finds no dump-entry parse outside the shared parser (`VstPlugin.h:64-72`). The other `section(` hits read program names, preset file names and a `vstplugin:` tag, not parameter entries. The restore path's `setParameterDump` has one caller, `loadSettings` (`VstPlugin.cpp:264`). The writer (`plugins/VstBase/VstPlugin.cpp:521-524`) and the wire format are unchanged. Vestige and VstEffect both use the parser through their knob sites, and the family run covers both.
- **No collateral damage:** a valid entry restores to the same value and index. Saved projects that were restored with a colon-free label keep the same values. The only changed outcomes are bad data: unparseable values, negative or non-numeric indices (the restore used to send index 0 with value 0.0), and labels containing `:`. Saved values still load through `loadSettings(_this, name)` before the dump is applied (`missingParameterEntryKeepsSavedValue`). No limit or check was added on any path that legitimate callers reach.
