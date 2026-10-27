Verdict: FIXED

new-p16-266 (BUG 19): VST parameter dump parsing assumes every entry has three fields.

Evidence: the dump is written as `index:label:value` at [VstPlugin.cpp:521](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-266/plugins/VstBase/VstPlugin.cpp:521). The label is the only field that can contain `:`, so a plug-in label such as `Gain: 3 dB` produced four fields. The old readers used `split(":").at(2)` and took the wrong field: [VstEffectControls.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-266/plugins/VstEffect/VstEffectControls.cpp) (loader, `ManageVSTEffectView` constructor and `syncPlugin`, unfixed on the base at lines 98, 400-402, 410 and 477) and the same three sites in [Vestige.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-266/plugins/Vestige/Vestige.cpp) (unfixed at lines 235, 1024-1026, 1034 and 1095). A missing `paramN` key gives an empty string, which splits to one field, and `at(2)` reads past the list. The finding's example of "fewer than three fields" is not produced by the current writer: `RemoteVstPlugin.cpp:1331-1338` sends indices `0..numParams-1`, so a short entry needs a corrupted message. The colon-in-label case is reachable with ordinary plug-ins.

Fix: one shared parser, `parseParameterDumpEntry()` in [VstPlugin.h:48-68](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-266/plugins/VstBase/VstPlugin.h:48). The value is the last field and the label is everything between the first and last separators. It returns nothing for fewer than three fields or a value that does not parse, and each call site then keeps the knob's current value. All six sites now use it: [VstEffectControls.cpp:90, 398, 472](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-266/plugins/VstEffect/VstEffectControls.cpp:90) and [Vestige.cpp:227, 1022, 1094](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-266/plugins/Vestige/Vestige.cpp:227).

Behaviour change, deliberate: a value that does not parse used to be applied as 0.0 and now leaves the knob as it was. A valid entry parses to the same value as before. Labels with `:` now show in full, where they showed only the first fragment.

Regression tests (in [VstEffectControlsTest.cpp:91](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-266/tests/src/plugins/VstEffectControlsTest.cpp:91) and `:111`):
- `labelContainingColonKeepsValueField`: dump entry `0:Gain: 3 dB:0.25` must load 0.25. Original code: `FAIL! ... qAbs(models.at(0)->value() - 0.25f) < 1e-4f returned FALSE`.
- `missingParameterEntryKeepsSavedValue`: dump has `param0` and `param2` only; the project saved `param1`=0.6 and it must survive. Original code: `Received signal 11 (SIGSEGV)` in the `loadSettings` lambda.

Test matrix: normal path (existing `repeatedSettingsLoadsDoNotRetainOldModels`, plus the new valid-entry assertions); malformed path (missing key, colon label); reconfiguration (the repeated load test); the wider VST/Vestige family (11 tests, all run). The ManageVSTEffectView and Vestige UI paths need a live plug-in host and have no unit test here; the shared parser they call is covered through `loadSettings`. Realtime and undo: the parser runs only on settings load and the UI sync, never on the audio thread, and creates no undo step beyond what the existing `setValue` calls already create.

Commands and exit codes (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-266.r1/logs/`):

| Step | Command | Exit |
| --- | --- | --- |
| RED build (first try) | `cmake --build build --target VstEffectControlsTest -j4 > build-red-1.log 2>&1` | 2 (ccache temp dir not writable in this sandbox) |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target VstEffectControlsTest -j4 > build-red-2.log 2>&1` | 0 |
| RED test | `cd build/tests && ctest -R '^VstEffectControlsTest$' --output-on-failure > test-red-1.log 2>&1` | 8 (colon FAIL, then SIGSEGV) |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target vstbase vsteffect vestige VstEffectControlsTest -j4 > build-green-1.log 2>&1` | 0 |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene -j4 > build-zene-green-1.log 2>&1` | 0 |
| GREEN test | `cd build/tests && ctest -R '^VstEffectControlsTest$' --output-on-failure > test-green-1.log 2>&1` | 0 (1/1) |
| GREEN test, per-function | `cd build/tests && ctest -R '^VstEffectControlsTest$' -V > test-green-verbose-1.log 2>&1` | 0 (5 passed: init, existing, 2 new, cleanup) |
| Family build | `CCACHE_DISABLE=1 cmake --build build --target VstSubPluginFeaturesTest VstMidiEventBatchTest VestigeMidiHandoffTest Vst3BusMapTest Vst3HostTest Vst3ChunkProbeTest Vst3EffectIntegrationTest Vst3InstrumentFixtureProbe Vst3InstrumentTest Vst3InstrumentIntegrationTest -j4 > build-family-1.log 2>&1` | 0 |
| Family test (before those binaries were built) | `cd build/tests && ctest -R 'Vst|Vestige' --output-on-failure > test-family-green-1.log 2>&1` | 8 (10 of 11 "Not Run", build gap, not a result) |
| Family test | `cd build/tests && ctest -R 'Vst|Vestige' --output-on-failure > test-family-green-2.log 2>&1` | 0 (11/11 passed) |
| Proof: save fix | `git diff -- plugins/VstBase/VstPlugin.h plugins/VstEffect/VstEffectControls.cpp plugins/Vestige/Vestige.cpp > fix.patch` | 0 |
| Proof: revert fix | `git apply -R fix.patch` | 0 |
| Proof: RED build | `CCACHE_DISABLE=1 cmake --build build --target vstbase vsteffect vestige VstEffectControlsTest zene -j4 > build-proof-red-1.log 2>&1` | 0 |
| Proof: RED test | `ctest -R '^VstEffectControlsTest$' -V > test-proof-red-1.log 2>&1` | 8 (same two failures as above) |
| Proof: re-apply fix | `git apply fix.patch` | 0 |
| Proof: GREEN build | `CCACHE_DISABLE=1 cmake --build build --target vstbase vsteffect vestige VstEffectControlsTest zene -j4 > build-proof-green-1.log 2>&1` | 0 |
| Proof: GREEN test | `ctest -R '^VstEffectControlsTest$' -V > test-proof-green-1.log 2>&1` | 0 |

Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exited 1 before any gate ran. It writes logs to `<worktree>/../zene-qwen-new-p16-266-feature-check`, which is outside my write jail, and it raised `PermissionError`. I did not edit the shared script. Instead I ran the same gate functions from `feature_check.py` (`SIMPLE`, `RATCHET`, `base_listings` from the cached base `5c1c6715d`, the same "new item in your files" rule) with logs in the LOG FOLDER: `gate_driver.py` → `gates-1.log`, exit 0. All eight rows pass: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures (exit 0 each); complexity and file-length (raw exit 1, "no new item in your files", as expected on the train). No gate row was edited or re-anchored.

Files committed: `plugins/VstBase/VstPlugin.h`, `plugins/VstEffect/VstEffectControls.cpp`, `plugins/Vestige/Vestige.cpp`, `tests/src/plugins/VstEffectControlsTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p16-266.md`. No new files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` need no change, and the no-upstream-regression gate passed.
