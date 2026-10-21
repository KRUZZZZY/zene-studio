new-p14-97-port: FIXED

## new-p14-97-port - Saving an unavailable plug-in discards edits to its generic effect settings

**Verdict: FIXED.** The finding is real and reachable on this tree; the fix was
re-applied by hand from the accepted commit `950d0d239` (cluster new-p14-97),
which could not go on this train because of a merge conflict in
`src/core/EffectChain.cpp`. Nothing was merged, cherry-picked or rebased: every
hunk was re-written against this tree's code.

### Evidence (before the fix, on this tree)

* `src/core/EffectChain.cpp:146` - the placeholder branch wrote the file's own
  XML back and skipped the device's state entirely:
  `if (auto dummy = dynamic_cast<DummyEffect*>(effect)) { _this.appendChild(_doc.importNode(dummy->originalPluginData(), true)); }`
  and `src/core/EffectChain.cpp:151` - the wrapper branch did the same with
  `effect->preservedPluginData()`.
  Neither branch wrote the placeholder's generic settings - its enabled flag,
  its wet/dry mix, its auto-quit knob, the user's device options - so a bypass
  the user set on the placeholder was dropped by the next save and the file
  kept the value it had.
* `include/Effect.h` had no way to tell the user's edits from the file's
  values: there was no accessor for the generic settings and nothing recorded
  what the device was loaded with.
* `include/DummyEffect.h:101-107` - the placeholder took only the stable id out
  of the file's element; its models kept their defaults, so the placeholder did
  not even hold the device's settings.
* The user-visible path: a project with a missing plug-in is opened (the chain
  makes a `DummyEffect` for a module whose binary is missing, and a hosted
  wrapper whose binary is missing stays a placeholder), the user bypasses the
  device on its card, the project is saved, and the file still carries the old
  `on` attribute.

### The fix (re-applied by hand)

* `include/Effect.h`, `src/core/Effect.cpp`:
  * `Effect::saveGenericSettings()` (`src/core/Effect.cpp:109`) holds the
    generic settings a save writes (enabled flag, wet/dry mix, auto-quit knob,
    stable id, the user's device options). `Effect::saveSettings()`
    (`src/core/Effect.cpp:172`) now calls it and then writes the controls'
    state - the bytes it writes for a normal device are the ones it wrote
    before (the attributes keep their order; a child element is still written
    after the attributes, as XML requires).
  * `Effect::genericSettings()` (`src/core/Effect.cpp:125`) reads the same
    values back as a name/value map; `Effect::rememberLoadedGenericSettings()`
    (`src/core/Effect.cpp:144`) remembers the ones the device was loaded with.
    `Effect::setPreservedPluginData()` (`include/Effect.h:177`) calls it: the
    device's state was taken out of the file's XML a moment before, so what it
    holds now is what the file said, and a later difference is the user's edit.
  * `Effect::preservedXmlWithChangedGenericSettings()`
    (`src/core/Effect.cpp:150`) writes an imported copy of the file's own XML
    with the settings that differ from the loaded ones merged in.
* `include/DummyEffect.h:108` - the placeholder loads the device's settings out
  of the file's element (`loadSettings(m_originalPluginData)`) and remembers
  them, so it stands in for the device the file named. The stable id keeps the
  behaviour of new-p10-146: `Effect::loadSettings()` takes the id out of the
  file's element for a document element and calls
  `ProjectIds::noteLoadAssignment()` when there is none - exactly what the
  placeholder's constructor did before.
* `src/core/EffectChain.cpp:146,151` - both placeholder branches now write
  `effect->preservedXmlWithChangedGenericSettings(...)`: the file's own XML,
  imported into the document being saved, with the generic settings the user
  changed on the placeholder merged in.

### Regression test

`tests/src/core/EffectChainMissingDeviceSettingsTest.cpp` (fork-NEW, registered
in `tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/fork-sources.txt`).
The devices are the test's own (an `Effect` subclass for the wrapper path and
the chain's own `DummyEffect` for the missing-module path), so every case is
deterministic: the file's XML is a literal and the settings are read back from
the element the chain wrote - no timing, no plug-in directory, no race.

* `theBypassOfAnUnavailableDeviceIsSaved` - the wrapper whose plug-in binary is
  missing keeps the file's device XML and writes the bypass the user set.
* `theBypassOfAMissingDeviceIsSaved` - the same for the placeholder the chain
  makes for a device whose module is missing.
* `theAutoQuitOfAMissingDeviceIsSaved` - a generic setting that is not the
  bypass is written the same way.
* `anUntouchedPlaceholderKeepsTheFilesXml` - a placeholder nobody touched
  keeps the file's values (no collateral damage).

### Commands (exit codes as seen)

All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-97-port.r1/logs`.

| Step | Command | Exit |
| --- | --- | --- |
| RED (fix reverted) | `git diff -- include/Effect.h include/DummyEffect.h src/core/Effect.cpp src/core/EffectChain.cpp > logs/fix.patch && git apply -R logs/fix.patch` → `cmake --build build --target EffectChainMissingDeviceSettingsTest` → `ctest -R '^EffectChainMissingDeviceSettingsTest$' --output-on-failure` (from `build/tests`) | build 0, **test 8** - 3 failing slots (`on` saved as "1" instead of "0", `autoquit` missing) |
| GREEN (fix re-applied) | `git apply logs/fix.patch` → same two commands | build 0, **test 0** - 1/1 passed |
| Product | `cmake --build build --target zene` | 0 |
| Family | `ctest -R '^(EffectChainMissingDeviceSettingsTest\|MissingDevicesPreserveTest\|MissingDevicesRound2Test\|EffectRackAddTest\|EffectHostingMenuTest\|DeviceToolsChainTest\|DeviceToolsTest\|EffectControlsVisibilityTest\|DeviceCopyTest\|EffectDeviceMenuTest)$' --output-on-failure` (from `build/tests`) | **0** - 10/10 passed |
| Named family (job brief) | `ctest -R '^(DelayControlsTest\|ConvolutionFftwPlanScopeTest\|EffectRackAddTest)$'` | **0** for DelayControlsTest, ConvolutionFftwPlanScopeTest and EffectRackAddTest |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **1** - the two pre-existing rows below; the other six rows PASS |

Logs: `cmake-1.log`, `build-1.log`, `build-2.log`, `build-red-1.log`,
`build-green-2.log`, `build-green-3.log`, `build-zene-1.log`,
`build-family-1.log`, `build-family-2.log`, `build-base-check.log`,
`test-red-1.log`, `test-green-1.log`, `test-green-2.log`, `test-green-3.log`,
`test-family-1.log`, `test-family-2.log`, `test-base-check.log`,
`feature-check-1.log`, `fix.patch`.

### Gate table (`feature_check.py`, base `0a8842d39`)

| Gate | Result | Note |
| --- | --- | --- |
| fork-sources | FAIL | pre-existing: `tests/all-sources.txt` does not reproduce from its own header recipe (1148 vendored `plugins/SfzPlayer/sfizz` / `plugins/NeuralAmp/rtneural` paths the recipe excludes). Identical at the base commit with 0 changed files. |
| all-sources-reproduce | FAIL | the same pre-existing manifest/recipe mismatch. |
| no-upstream-regression | PASS | the four changed inherited files have a new-p14-97 entry in `tests/upstream-modifications.txt`. |
| check-namespace | PASS | |
| no-tautology | PASS | |
| doc-figures | PASS | |
| complexity | PASS | no new item in the changed files |
| file-length | PASS | no new item in the changed files (the new test file is 142 code lines) |

The two failing rows were measured **before** any change as well (the first
`feature_check.py` run of this session, on the untouched tree, reported exactly
`FAIL fork-sources` / `FAIL all-sources-reproduce` and the same manifest
mismatch), so they are a train-level state, not this fix. The same check fails
in the train's own tree at its HEAD (`zene-cachyos` @ `dbb15dded`,
`bash tests/all-sources-reproduce.sh` -> exit **1**, same manifest mismatch),
which is the proof that it is not caused by this branch. The manifest was left
alone on purpose: re-deriving it deletes 1148 vendored paths from a
whole-tree scope file that every lane shares, and it has nothing to do with this
bug.

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with
  it.** Reverted (`git apply -R logs/fix.patch`): `ctest -R
  '^EffectChainMissingDeviceSettingsTest$' --output-on-failure` -> exit **8**,
  `theBypassOfAnUnavailableDeviceIsSaved`, `theBypassOfAMissingDeviceIsSaved`
  and `theAutoQuitOfAMissingDeviceIsSaved` fail:
  `Actual (saved.attribute("on")): "1" / Expected: "0"` at
  `tests/src/core/EffectChainMissingDeviceSettingsTest.cpp:155` and `:182`, and
  `Actual (saved.attribute("autoquit")): "" / Expected: "42"` at `:203`.
  Restored (`git apply logs/fix.patch`): the same command -> exit **0**, 1/1
  passed. The test was run in both states from the same source tree; the test
  file itself was never changed between the two runs.
* **The test is deterministic.** It builds the file's XML from a literal string,
  drives the chain through its public API (`appendEffect`, `setEnabled`,
  `autoQuitModel()->setValue`) and reads the saved element back. No clock, no
  sleep, no thread, no plug-in directory, no injected randomness.
* **The fix is at the cause and complete.** The cause is that the placeholder's
  generic settings were never written; the fix writes them. Every caller and
  sibling was checked:
  * `Effect::saveSettings()` has two callers of the generic part -
    `saveGenericSettings()` (new) and `EffectChain::saveSettings()`'s normal
    branch (`effect->saveState()`), which now gets the identical bytes: the
    attribute order is unchanged (`on`, `wet`, `autoquit`, `id`, device
    options) and the controls' state is still a child element.
  * `setPreservedPluginData()` has exactly one caller
    (`src/core/EffectChain.cpp:207`, the wrapper path), and it is called after
    `restoreState()` -> `loadSettings()`, so the settings remembered are the
    file's.
  * `DummyEffect`'s constructor has two callers:
    `src/core/EffectChain.cpp:213` (the missing-module placeholder, now
    loading the file's settings) and `src/gui/modals/EffectSelectDialog.cpp:227`
    (a placeholder with a null element - `loadSettings()` on a null element is
    a no-op for every model, and the stable id falls back to
    `ProjectIds::noteLoadAssignment()`, exactly as the constructor did before).
  * The chain has two placeholder branches - the `DummyEffect` one and the
    wrapper one - and both were changed, so neither path keeps the bug.
* **No collateral damage.** An untouched placeholder keeps the file's bytes
  exactly (`anUntouchedPlaceholderKeepsTheFilesXml`): the merged copy is the
  file's own imported element, and only a setting that differs from the loaded
  one is written over it. A normal (available) device saves the same bytes as
  before. No new limit, check or command was added; no control command was
  touched, so the control-command snapshot does not move. Old projects load as
  before: the placeholder now loads the file's own values into its models,
  which is what the file already said.
* **Realtime, undo and commands.** Nothing was added to the audio thread: the
  changed code is the save path (control thread) and the model load. No
  allocation, lock, wait, I/O or logging was introduced on any audio path; no
  new signal is emitted. Undo is untouched - the fix changes what a save
  writes, not what a user action records, and no control command was added.
* **Qt 5.15 / MSVC / macOS portability.** The change uses only `QDomElement`,
  `QMap`, `QString` and `QDomDocument` - all present in Qt 5.15 as well. No
  Qt-6-only API, no `windows.h` macro name, no `std::filesystem`.
* **Pre-existing failures, measured, not assumed.** `ReversibilityUndoTest` and
  `UndoHistoryPersistenceTest` fail on this tree; they were re-run with the
  product change reverted (`git apply -R logs/fix.patch`,
  `logs/build-base-check.log`, `logs/test-base-check.log`) and fail identically
  (`value 0.5 is outside the range 5..1000 of parameter 'Start frequency'` and
  `the baseline needs a real instrument (tripleoscillator)`), so they are not
  caused by this fix.
* **Files committed with the fix.** `include/Effect.h`, `include/DummyEffect.h`,
  `src/core/Effect.cpp`, `src/core/EffectChain.cpp`,
  `tests/src/core/EffectChainMissingDeviceSettingsTest.cpp` (new),
  `tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/fork-sources.txt`,
  `tests/upstream-modifications.txt`,
  `docs/qwen-fixes-2026-10/cluster-new-p14-97-port.md` (new).
