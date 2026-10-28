FIXED - cluster new-p10-284: a ParameterControls key already registered is refused, so it cannot overwrite another model's saved value

Finding: "Duplicate parameter save keys silently make model state ambiguous" (src/core/ParameterControls.cpp, historical line 39).

## Evidence (file:line, current tree)

- The finding is real. `ParameterControls::addParameter` appended any `(model, key)` pair without checking the key
  (original `src/core/ParameterControls.cpp:39-42`, now `:43-56`).
- The key is the name the model is written under. A linear, non-automated model is saved as an attribute:
  `src/core/AutomatableModel.cpp:279` `element.setAttribute( name, publishedValue() );`. A second model with the same key
  therefore overwrites the first one's value in the save file, and `src/core/AutomatableModel.cpp:437-440`
  (`element.hasAttribute( name )` / `element.attribute( name )`) gives both models the single surviving value on load.
  Measured: the RED test below restores `7` where the first model had `3`.
- The intended contract: one key names one parameter in a ParameterControls device's save file. `ParameterControls.h:61`
  documents the registration call; `ParameterAliases.cpp` (`ownKeyOf`/`keysOf`) also relies on the registered list
  being the key-to-model map.
- Fix (at the cause, `src/core/ParameterControls.cpp:43-56`): `addParameter` checks the key against the already
  registered list. A repeat is refused with `qWarning` and not registered; the first registration keeps the key.
  The signature is unchanged (`void`), so the 32 existing call sites are unaffected. The header doc (`include/ParameterControls.h:62`)
  states the refusal.

### Sibling and caller audit

Every in-tree `addParameter` caller (all in `plugins/`), checked for keys that repeat within one device:

| Device | Keys (file:line) | Duplicate? |
| --- | --- | --- |
| TransientShaper | attack, sustain, output (`plugins/TransientShaper/TransientShaper.cpp:77-79`) | no |
| DeEsser | mode, frequency, threshold, range, listen (`plugins/DeEsser/DeEsser.cpp:67-71`) | no |
| ConvolutionReverb | predelay, lowcut, highcut, output (`plugins/ConvolutionReverb/ConvolutionReverb.cpp:135-138`) | no |
| Console | character, drive, output, oversampling, glue, gluethreshold, glueratio, gluemakeup (`plugins/Console/Console.cpp:85-93`) | no |
| DynamicEq | channels, freq/gain/q/threshold/dynamic + band 1..Bands, output (`plugins/DynamicEq/DynamicEq.cpp:87-104`) | no (band keys are type+number; no type prefix collides with another) |
| Vocoder | source, bands, low, high, width, release, output (`plugins/Vocoder/Vocoder.cpp:70-76`) | no |

No existing device registers a duplicate key, so no saved project or device changes behaviour. The refusal only changes
a case that was already corrupting saved state. The readers of the list, `ParameterAliases.cpp:51-60` (`ownKeyOf`),
`ControlCommandsDeviceTools.cpp:191` and `ConsoleStrip.cpp:105` (key lookup, first match wins), get one unambiguous model per key.

Not changed, deliberately: two different keys registered for the same model object (not the reported ambiguity: each key
is saved and loaded consistently); a null model.

## Test

`tests/src/core/ParameterControlsTest.cpp` (new, registered in `tests/CMakeLists.txt`, `tests/fork-sources.txt`,
`tests/all-sources.txt`, `tests/upstream-modifications.txt`):

- `distinctKeysBothRegister` (:69) - two keys, both registered, in order.
- `duplicateKeyIsRejectedAndOtherKeysStillRegister` (:82) - the second "gain" is refused, a later "other" still registers. **RED.**
- `distinctModelsKeepIndependentValuesThroughSaveAndLoad` (:98) - two keys keep independent values through save and load.
- `duplicateKeyDoesNotLetTheRejectedModelOverwriteTheSavedValue` (:124) - the saved "gain" is the first model's `3`, not
  the rejected model's `7`. **RED.**

No timing, sleeps or races. The test needs the engine (`Engine::init(true)`, the same as ConsoleStripTest), because
each `AutomatableModel` registers with the audio engine on construction; it is not on the audio thread.

## Commands and exit codes

Build tree: `/…/zene-qwen-new-p10-284/build`. Run from `build/tests` for ctest. `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p10-284`
was set for the build commands because the default ccache temp dir in the shared `dsh` area is not writable from this lane
(`ccache: error: failed to create temporary file ... Permission denied`, `logs/build-regen-1.log`).

| Step | Command | Exit | Log |
| --- | --- | --- | --- |
| Register the test target | `cmake --build build --target EffectControlsVisibilityTest -j4` (regenerates the build system after the CMakeLists edit) | 0 | `logs/build-regen-2.log` |
| RED build | `cmake --build build --target ParameterControlsTest -j4` | 0 | `logs/build-red-5.log` |
| **RED run (unfixed)** | `QT_QPA_PLATFORM=offscreen ctest -R ParameterControlsTest --output-on-failure` | **8** (2 fails: size 3 != 2; restored 7 != 3) | `logs/test-red-3.log` |
| GREEN build | `cmake --build build --target ParameterControlsTest -j4` | 0 | `logs/build-green-1.log` |
| **GREEN run (fixed)** | `ctest -R ParameterControlsTest --output-on-failure` | **0** (6 of 6 cases pass, 1 test) | `logs/test-green-1.log` |
| Direct run | `./ParameterControlsTest` (in `build/tests`) | 0 | `logs/test-green-direct.log` |
| Proof, step 4b, revert | `git diff -- src/core/ParameterControls.cpp include/ParameterControls.h > logs/fix.patch && git apply -R logs/fix.patch` | 0 / 0 | - |
| Proof, step 4b, RED | build `ParameterControlsTest`, then `ctest -R ParameterControlsTest` | build 0; **ctest 8** (2 fails, same assertions) | `logs/build-4b-red.log`, `logs/test-4b-red.log` |
| Proof, step 4b, reapply | `git apply logs/fix.patch` | 0 | - |
| Proof, step 4b, GREEN | build `ParameterControlsTest`, then `ctest -R ParameterControlsTest` | build 0; **ctest 0** | `logs/build-4b-green.log`, `logs/test-4b-green.log` |
| Rebuild dependents | `cmake --build build --target zene -j4` (header changed) | 0 | `logs/build-zene-2.log` |
| Family | `ctest -R "ParameterControlsTest|ConsoleStripTest|DeviceToolsTest|DeviceToolsChainTest|EffectControlsVisibilityTest|EffectDeviceMenuTest|ControlDeviceCatalogueTest|ControlDeviceVendorTest"` (after building all 8 targets, `logs/build-family-1.log`, exit 0) | **8** (7 pass, 1 fail, see below) | `logs/test-family-1.log` |

Family result: ParameterControlsTest, EffectControlsVisibilityTest, ControlDeviceVendorTest, DeviceToolsTest,
DeviceToolsChainTest, ConsoleStripTest and EffectDeviceMenuTest pass. **ControlDeviceCatalogueTest fails one case,
`aTruncatedPatchKeepsThePreviouslyLoadedSound`** (`tests/src/core/ControlDeviceCatalogueTest.cpp:189`):
`Patman plugin is unavailable`. The Patman plugin library is not built in this lane's build tree (`find build -name
"*patman*.so*"` returns nothing), and the same run also logs `plugin-scan-cache: cannot write ".../Documents/Zene Studio/plugin-scan-cache.json" - "Permission denied"`.
That case never touches ParameterControls (Patman is not in the `addParameter` list above). I did not build the base commit to prove it red there, so this is
attributed to the lane's build scope and sandbox, not to this change, and is unverified on base.

Gates (`tests/`, run one by one in the worktree, because `feature_check.py` could not run here, see below):

| Gate | Command | Exit (after commit `7183f5a3a3`) |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (train-red, see below) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (train-red, see below) |

Ratchets: the exit 1 is the train's standing red (complexity: `WasmSandbox::loadModuleBytes`, `XyPadPairTest`,
`DawProjectReadTracks`, `setStrip`; file-length: `Vst3Host.cpp`, `TakeLane.cpp`, `WasmSandbox.cpp`, `ArrangeToolsTest.cpp`, ...).
None of the 6 + 23 `REGRESSION:` lines names `ParameterControls.h`, `ParameterControls.cpp`, `ParameterControlsTest.cpp`
or `tests/CMakeLists.txt` (`logs/gate2-complexity.log`, `logs/gate2-file-length.log`). The new function
`ParameterControls::addParameter` is CCN about 3 and the file is well under 500 lines. So nothing new is attributable to
this change in the ratchets, which `feature_check.py` would also report as PASS.

**`feature_check.py` did not run.** It writes its per-gate logs to `<worktree>/../<name>-feature-check/`, i.e.
`/…/zene-studio-program/zene-qwen-new-p10-284-feature-check`, which is outside this lane's write-jail. It died with
`PermissionError: [Errno 13]` at `logs/feature-check-1.log` (line 71 of the script, `logs.mkdir`). Its six simple gates and
two ratchets were run by hand above, and the table is the same set of commands. The tool itself was not run, so
its PASS/FAIL table is not a claim.

## Self-check

Verdict is FIXED, so the ALREADY-FIXED / FALSE-POSITIVE item does not apply; the code evidence is in "Evidence" above.

- **Regression test FAILS with the change reverted and PASSES with it:** yes. Reverted (`git apply -R logs/fix.patch`, exit 0), build exit 0, `ctest -R ParameterControlsTest` exit **8** with two failures (`logs/test-4b-red.log`); reapplied (`git apply logs/fix.patch`, exit 0), build exit 0, `ctest` exit **0**, 6 of 6 PASS (`logs/test-4b-green.log`). The RED before the fix was also seen first, on the unfixed code (`logs/test-red-3.log`, exit 8).
- **Deterministic:** yes. No wall-clock time, sleeps, threads or races in the test. Every value is set directly on `FloatModel` objects (`setValue(3.f)`, `setValue(7.f)`) and read back. The only environment is `Engine::init(true)` followed by `audioDev()->stopProcessing()`, the same setup `AutomationModesTest` and `ConsoleStripTest` use; that stops the dummy device thread from rendering, so the test runs single-threaded.
- **Fix at the cause, and complete:** the cause is `addParameter` accepting a repeated key, and the save/load path (`AutomatableModel.cpp:279`, `:437-440`) is keyed by that name. All 32 `addParameter` call sites in `plugins/` were checked (table above): none repeats a key, so the check changes nothing for them. The lookups that depend on the list (`ParameterAliases.cpp:51-60`, `ControlCommandsDeviceTools.cpp:191`, `ConsoleStrip.cpp:105`) now see one model per key. The header contract is updated (`include/ParameterControls.h:62`), and the signature is unchanged, so no caller needed editing.
- **No collateral damage:** no saved project changes. A key is refused only when it is already registered; a project saved before this change has, at worst, one key that was overwritten, and the load of such a file was already ambiguous. No device in the tree registers a duplicate, so the refusal is not hit by any existing device. No new limit was added (the check is "already registered", not a count), so nothing that legitimately exceeds a count is affected. The one behaviour change is the warning on a repeat, which is on the constructor path.
- **Not run from this lane (blocking the completion check's gate item):** `feature_check.py` (see "Gates" above). The gate the completion check reports as FAIL is that script's `PermissionError`; the six simple gates and the two ratchets were run by hand and are recorded above.
- **Known gap:** the family's `ControlDeviceCatalogueTest` Patman case (`aTruncatedPatchKeepsThePreviouslyLoadedSound`) was not proven red on the base commit; see the family note above.
