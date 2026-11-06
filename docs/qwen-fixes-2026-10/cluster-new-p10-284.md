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

Regression tests, the ones that must FAIL without the fix (negative control below):

- `tests/src/core/ParameterControlsTest.cpp` (new; registered in `tests/CMakeLists.txt`, `tests/fork-sources.txt`,
  `tests/all-sources.txt`, `tests/upstream-modifications.txt`):
  - `duplicateKeyIsRejectedAndOtherKeysStillRegister` (:82) - the second "gain" is refused, a later "other" still registers. **RED.**
  - `duplicateKeyDoesNotLetTheRejectedModelOverwriteTheSavedValue` (:124) - the saved "gain" is the first model's `3`, not the rejected model's `7`. **RED.**
  - `distinctKeysBothRegister` (:69) and `distinctModelsKeepIndependentValuesThroughSaveAndLoad` (:98) - normal-path guards; they pass both ways by design.
- `tests/src/core/DeviceToolsTest.cpp` (`aRepeatedSaveKeyKeepsTheFirstModelsValueThroughTheDeviceSave`, device path):
  a real `Effect` whose `ParameterControls` list registers two models under `gain` is saved with `Effect::saveSettings`
  and loaded with `Effect::loadSettings`. The first model must come back with `0.3`, not the rejected model's `0.7`. **RED.**
  The fixture is `RepeatedSaveKeyEffect` / `RepeatedSaveKeyControls` in the same file.

No timing, sleeps, threads or races. Values are set directly on `FloatModel` objects. Both tests call
`Engine::init(true)` followed by `audioDev()->stopProcessing()` (the same setup as `ConsoleStripTest`/`AutomationModesTest`),
because each `AutomatableModel` registers with the audio engine on construction; the test is not on the audio thread.

### Other tests the review named

`ConsoleStripTest`, `DeviceToolsChainTest` and `EffectDeviceMenuTest` **do not fail without the fix, and I did not make
them fail.** Their subjects never register two parameters under one key: the Console's keys are fixed in
`plugins/Console/Console.cpp:85-93`, `DeviceToolsChainTest` tests chains and `EffectDeviceMenuTest` tests menu entries.
The only way to make them fail would be to add a deliberately duplicated fixture to a test whose purpose is something else.
That would be a test written to fail, not a test of their behaviour, so I left them as no-regression guards for the
in-tree device paths (they confirm the change does not alter those devices). If the review still wants them red, that is a
decision for the reviewer; I recommend against it.

## Negative control (product change reverted, tests kept)

Method, as the loop does it: `git diff 37eaf07557 7183f5a3a3 -- include/ParameterControls.h src/core/ParameterControls.cpp > logs/fix.patch`,
`git apply -R logs/fix.patch`, rebuild the five targets, run ctest, then `git apply logs/fix.patch` and rebuild.

| Test | Without fix | With fix |
| --- | --- | --- |
| ParameterControlsTest | **FAIL** (2 of 6 cases: size 3 != 2; restored 7 != 3) | PASS (6 of 6) |
| DeviceToolsTest | **FAIL** (`aRepeatedSaveKeyKeepsTheFirstModelsValueThroughTheDeviceSave`: 0.7 != 0.3) | PASS |
| ConsoleStripTest | PASS (expected: not a duplicate-key test) | PASS |
| DeviceToolsChainTest | PASS (expected: not a duplicate-key test) | PASS |
| EffectDeviceMenuTest | PASS (expected: not a duplicate-key test) | PASS |

Exit codes: without fix `ctest` **8** (`logs/test-negctl.log`; build exit 0, `logs/build-negctl.log`); with fix `ctest` **0**
(`logs/test-positive.log`; build exit 0, `logs/build-positive.log`).

Note on the first attempt of this round: I passed the five target names as one `--target` argument, which made the build
fail with exit 2 (`make: No rule to make target ...`). The ctest run that followed used the previously built binaries, so it
was discarded and rerun after a correct build. The numbers above are from the rerun only.

## Commands and exit codes (earlier rounds)

Build tree: `/…/zene-qwen-new-p10-284/build`. ctest runs from `build/tests`. `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p10-284`
was set for build commands because the default ccache temp dir in the shared `dsh` area is not writable from this lane
(`ccache: error: failed to create temporary file ... Permission denied`, `new-p10-284.r1/logs/build-regen-1.log`).
Logs from the first round are in `new-p10-284.r1/logs`; this round's are in `new-p10-284.r2/logs`.

| Step | Command | Exit | Log |
| --- | --- | --- | --- |
| Register the test target | `cmake --build build --target EffectControlsVisibilityTest -j4` (regenerates the build system after the CMakeLists edit) | 0 | r1 `build-regen-2.log` |
| RED run (unfixed, first round) | `ctest -R ParameterControlsTest --output-on-failure` | **8** (2 fails) | r1 `test-red-3.log` |
| Proof 4b, RED | build `ParameterControlsTest`, then `ctest -R ParameterControlsTest` (product reverted) | build 0; **ctest 8** | r1 `build-4b-red.log`, `test-4b-red.log` |
| Proof 4b, GREEN | build `ParameterControlsTest`, then `ctest -R ParameterControlsTest` (fix re-applied) | build 0; **ctest 0** | r1 `build-4b-green.log`, `test-4b-green.log` |
| Rebuild dependents | `cmake --build build --target zene -j4` (header changed) | 0 | r1 `build-zene-2.log` |

## Family (final state, after the round-2 commit)

Eight targets built (`zene` plus `ParameterControlsTest`, `DeviceToolsTest`, `ConsoleStripTest`, `DeviceToolsChainTest`,
`EffectControlsVisibilityTest`, `EffectDeviceMenuTest`, `ControlDeviceCatalogueTest`, `ControlDeviceVendorTest`, build exit 0,
`r2/logs/build-final.log`). `ctest -R "ParameterControlsTest|ConsoleStripTest|DeviceToolsTest|DeviceToolsChainTest|EffectControlsVisibilityTest|EffectDeviceMenuTest|ControlDeviceCatalogueTest|ControlDeviceVendorTest"`
exit **8**, `r2/logs/test-final-family.log`: 7 pass, 1 fails.

The failing case is `ControlDeviceCatalogueTest::aTruncatedPatchKeepsThePreviouslyLoadedSound`
(`tests/src/core/ControlDeviceCatalogueTest.cpp:189`): `Patman plugin is unavailable`. The Patman plugin library is not built
in this lane's build tree (`find build -name "*patman*.so*"` returns nothing), and the same run logs
`plugin-scan-cache: cannot write ".../Documents/Zene Studio/plugin-scan-cache.json" - "Permission denied"`. That case never
touches ParameterControls (Patman is not in the `addParameter` list above). I did not build the base commit to prove it red
there, so it is attributed to the lane's build scope and sandbox and is unverified on base.

## Gates (final state, commit `ca04cf120c`)

Run one by one in the worktree (`r2/logs/gate-*.log`), because `feature_check.py` could not run here (see below).

| Gate | Command | Exit |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (train-red) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (train-red) |

Ratchets. The exit 1 is the train's standing red: complexity lists `WasmSandbox::loadModuleBytes`, `XyPadPairTest`,
`DawProjectReadTracks`, `setStrip`; file-length lists `Vst3Host.cpp`, `TakeLane.cpp`, `WasmSandbox.cpp`, `ArrangeToolsTest.cpp`, and others.
No complexity item names a changed file. The file-length list does include
`tests/src/core/DeviceToolsTest.cpp (565)` as "new file over 500 lines". That is the same item the base commit already
produces: at `37eaf07557` the file was 637 lines (`git show 37eaf07557:tests/src/core/DeviceToolsTest.cpp | wc -l`) and it is
not in `tests/file-length-baseline.tsv`. `feature_check.py` compares items by name with the base commit's output, so the item
is not new, though the file did grow by 64 lines in this round. The new function `addParameter` is about CCN 3.

**`feature_check.py` did not run.** It writes its per-gate logs to `<worktree>/../<name>-feature-check/`, which is outside
this lane's write-jail. It died with `PermissionError: [Errno 13]` at `logs.mkdir` (`tests/…/feature_check.py:71`; first-round log
`r1/logs/feature-check-1.log`). The six simple gates and two ratchets were run by hand above, with the same commands. The
tool itself was not run, so its PASS/FAIL table is not a claim.

## Self-check

Verdict is FIXED, so the ALREADY-FIXED / FALSE-POSITIVE item does not apply; the code evidence is in "Evidence" above.

- **Regression tests FAIL without the change and PASS with it:** yes for `ParameterControlsTest` (2 RED cases) and for
  `DeviceToolsTest::aRepeatedSaveKeyKeepsTheFirstModelsValueThroughTheDeviceSave` (device path). Both measured in the negative
  control table above. The other three tests the review named are not regression tests for this bug; they pass both ways, and
  that is stated, not hidden.
- **Deterministic:** yes. No wall-clock time, sleeps, threads or races. Values are set directly and read back.
  The engine setup is the same as in the existing device tests, with the dummy device stopped.
- **Fix at the cause, and complete:** the cause is `addParameter` accepting a repeated key, and the save/load path
  (`AutomatableModel.cpp:279`, `:437-440`) is keyed by that name. All 32 `addParameter` call sites in `plugins/` were checked
  (table above): none repeats a key, so the check changes nothing for them. The readers of the list
  (`ParameterAliases.cpp:51-60`, `ControlCommandsDeviceTools.cpp:191`, `ConsoleStrip.cpp:105`) now see one model per key.
  The header contract is updated (`include/ParameterControls.h:62`); the signature is unchanged, so no caller needed editing.
- **No collateral damage:** no saved project changes. A key is refused only when it is already registered; a project saved
  before this change has, at worst, one key that was overwritten, and loading such a file was already ambiguous. No device in
  the tree registers a duplicate, so the refusal is not hit by any existing device. No new limit was added (the check is
  "already registered", not a count). The one behaviour change is the warning on a repeat, which is on the constructor path.
- **Realtime / undo / commands:** constructor-time only; no audio-thread code changed; no control command added; the
  command snapshot is untouched (doc-figures gate exit 0).
- **Known gaps:** `feature_check.py` could not run from this write-jail (see above). The Patman case of
  `ControlDeviceCatalogueTest` is unverified on the base commit.
