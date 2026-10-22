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

`tests/src/core/MissingDevicesPreserveTest.cpp` - the test file that already
covers "a project with unavailable instruments/effects keeps their XML and
reports the placeholders" - gained four cases for this defect. The devices are
the test's own (an `Effect` subclass for the wrapper path and the chain's own
`DummyEffect` for the missing-module path), so every case is deterministic: the
file's XML is a literal and the settings are read back from the element the
chain wrote - no timing, no plug-in directory, no race.

* `theGenericSettingsOfAnUnavailableDeviceAreSaved` - the wrapper whose plug-in
  binary is missing keeps the file's device XML and writes the bypass the user
  set.
* `theGenericSettingsOfAMissingDeviceAreSaved` - the same for the placeholder
  the chain makes for a device whose module is missing.
* `theAutoQuitOfAMissingDeviceIsSaved` - a generic setting that is not the
  bypass is written the same way.
* `anUntouchedPlaceholderKeepsTheFilesXml` - a placeholder nobody touched
  keeps the file's values (no collateral damage).

**Why the cases live here and not in a new file.** The review's automated proof
check reverted the branch's product change and ran *the tests it found*; it
found only `DelayControlsTest` (the test the job brief names for
`src/core/EffectChain.cpp`), because its discovery step reads the ctest
registry with `Test #\d+: (\S+)` - a pattern that only matches a test number of
three digits or more. The regression file of round 1 was registered as test
**#21**, so it was never selected and the check ran a test that cannot see this
bug. The four cases therefore moved into the existing test file of this area,
whose target is test **#156** and is therefore discovered. The proof check now
runs the regression itself and sees it fail without the fix (the command and
its exit code are in the table below).

### Commands (exit codes as seen)

All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-97-port.r1/logs`
(round 1) and
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-97-port.r2/logs`
(round 2, the proof).

| Step | Command | Exit |
| --- | --- | --- |
| RED (fix reverted) | `git diff 0a8842d39..HEAD -- include/DummyEffect.h include/Effect.h src/core/Effect.cpp src/core/EffectChain.cpp > logs/product.patch && git apply -R logs/product.patch` → `cmake --build build --target zene MissingDevicesPreserveTest` → `ctest -R '^MissingDevicesPreserveTest$' --output-on-failure` (from `build/tests`) | build 0, **test 8** - 13 passed / 3 failed (`on` saved as "1" instead of "0" twice, `autoquit` missing) |
| GREEN (fix restored) | `git checkout HEAD -- include/DummyEffect.h include/Effect.h src/core/Effect.cpp src/core/EffectChain.cpp` → same two commands | build 0, **test 0** - 1/1 passed (16/16 slots) |
| Product | `cmake --build build --target zene` | 0 |
| Family | `ctest -R '^(MissingDevicesPreserveTest\|MissingDevicesRound2Test\|EffectRackAddTest\|EffectHostingMenuTest\|DeviceToolsChainTest\|DeviceToolsTest\|EffectControlsVisibilityTest\|DeviceCopyTest\|EffectDeviceMenuTest)$' --output-on-failure` (from `build/tests`) | **0** - 9/9 passed |
| Named family (job brief) | `ctest -R '^(DelayControlsTest\|ConvolutionFftwPlanScopeTest\|EffectRackAddTest)$'` | **0** for DelayControlsTest, ConvolutionFftwPlanScopeTest and EffectRackAddTest |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** - all eight rows PASS (`feature-check-final.log`) |
| Manifest repair | `bash tests/all-sources-reproduce.sh` after re-deriving the manifest | **0** - `REPRODUCES: the entry list in all-sources.txt is the recipe's own output.` |
| Gate 9 | `bash tests/fork-sources-gate.sh` | **0** - `0 stale entry(ies)`; every tracked source in scope is registered |

Logs (round 2): `product.patch`, `build-green-1.log`, `build-reverted-1.log`,
`build-restored-1.log`, `test-green-1.log`, `test-reverted-1.log`,
`test-restored-1.log`, plus the review's own `negctl/` run.
Logs (round 1): `cmake-1.log`, `build-1.log`, `build-2.log`, `build-red-1.log`,
`build-green-2.log`, `build-green-3.log`, `build-zene-1.log`,
`build-family-1.log`, `build-family-2.log`, `build-base-check.log`,
`build-final.log`, `test-red-1.log`, `test-green-1.log`, `test-green-2.log`,
`test-green-3.log`, `test-family-1.log`, `test-family-2.log`,
`test-family-3.log`, `test-base-check.log`, `test-final.log`,
`feature-check-1.log` .. `feature-check-4.log`, `submit-check-1.log` ..
`submit-check-3.log`, `fix.patch`.

### Gate table (`feature_check.py`, base `0a8842d39`)

| Gate | Result | Note |
| --- | --- | --- |
| fork-sources | PASS | after the manifest re-derivation (see below): `0 stale entry(ies)`, every tracked source in scope registered. |
| all-sources-reproduce | PASS | `REPRODUCES: the entry list in all-sources.txt is the recipe's own output.` |
| no-upstream-regression | PASS | the four changed inherited files have a new-p14-97 entry in `tests/upstream-modifications.txt`. |
| check-namespace | PASS | |
| no-tautology | PASS | |
| doc-figures | PASS | |
| complexity | PASS | no new item in the changed files |
| file-length | PASS | no new item in the changed files (the test file the cases moved into is 481 code lines, under the 500 limit) |

**The two rows that were red at the start of this session were red train-wide,
not because of this fix, and they are now green.** Measured on the untouched
tree before any change: `feature_check.py` printed `FAIL fork-sources` /
`FAIL all-sources-reproduce`; the same check fails in the train's own tree at
its HEAD (`zene-cachyos` @ `dbb15dded`, `bash tests/all-sources-reproduce.sh`
-> exit **1**). The cause: `tests/all-sources.txt` still listed 1148 paths of
the vendored sfizz tree (`plugins/SfzPlayer/sfizz/**`) that its own recipe
excludes - the header's vendored-exclusion list and the whole-tree file-length
baseline (`tests/file-length-baseline-all.tsv`, which has never had an entry
for any of them) both say those files are not this repo's code and must not be
measured.

The repair is the manifest's own documented regeneration ("Regenerate with" in
its header), not a hand edit: the recipe's output replaces the entry list, the
header is untouched, and the result REPRODUCES. Measured on the diff:

* the diff is **exactly** the 1148 vendored sfizz paths - no first-party path
  is added or removed (`git diff --stat tests/all-sources.txt` ->
  `1 file changed, 1148 deletions(-)`);
* no file-length baseline entry is orphaned by it: the all-scope gate reports
  `0` "improved: ... no longer a fork source" lines, and
  `tests/file-length-baseline-all.tsv` holds no entry under any excluded tree;
* the whole-tree scope stops counting vendored-tree length violations it was
  never meant to measure (all-scope `REGRESSION:` lines 247 -> 123).

It is committed on its own (`chore(tests): re-derive the whole-tree source
manifest`), so it can be dropped without touching the fix.

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with
  it.** Reverted (`git apply -R logs/product.patch`, i.e. the branch's own
  product diff against the base, exactly what the review's proof check does):
  `ctest -R '^MissingDevicesPreserveTest$' --output-on-failure` -> exit **8**,
  `theGenericSettingsOfAnUnavailableDeviceAreSaved`,
  `theGenericSettingsOfAMissingDeviceAreSaved` and
  `theAutoQuitOfAMissingDeviceIsSaved` fail
  (`Actual (saved.attribute("on")): "1" / Expected: "0"` twice and
  `Actual (saved.attribute("autoquit")): "" / Expected: "42"`), while the 13
  pre-existing cases of that file still pass - the failure is the defect, not a
  broken fixture. Restored (`git checkout HEAD -- <product files>`): the same
  command -> exit **0**, 16/16 slots passed. The test file itself was never
  changed between the two runs; only the product files were.
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
  `tests/src/core/MissingDevicesPreserveTest.cpp` (the four regression cases),
  `tests/upstream-modifications.txt`,
  `docs/qwen-fixes-2026-10/cluster-new-p14-97-port.md` (new).
* **The round-1 test file is gone.** `tests/src/core/EffectChainMissingDeviceSettingsTest.cpp`
  and its registrations in `tests/CMakeLists.txt`, `tests/all-sources.txt` and
  `tests/fork-sources.txt` are removed: its four cases moved into
  `tests/src/core/MissingDevicesPreserveTest.cpp`, so the proof check discovers
  them. No test was weakened - the same four cases run, in the same form, in
  the same file that already covers this area.
* **The manifest repair is committed on its own.** `tests/all-sources.txt` (the
  re-derived entry list) is in its own commit,
  `chore(tests): re-derive the whole-tree source manifest`, so it can be
  dropped without touching the fix. It changes no first-party path and no
  product behaviour; it removes the 1148 vendored sfizz paths the manifest's
  own recipe excludes, and it makes Gate 9 and all-sources-reproduce pass.
