FIXED

# cluster new-p16-68 - a malformed effect count can silently discard project effects

## Verdict: REAL

`EffectChain::loadSettings()` trusted the `numofeffects` attribute as the loop
limit. Before the fix, `src/core/EffectChain.cpp:209` read the attribute and
`:213` was `while( !node.isNull() && fx_loaded < plugin_cnt )`. Any file with
`numofeffects` missing, zero, negative, non-numeric or smaller than its actual
`<effect>` children had those children skipped. The next save wrote the
shortened `m_effects` list, so the effects were lost for good.

The count is written by `saveSettings()` from the real list
(`src/core/EffectChain.cpp:160`), so for a file this build wrote the attribute
is redundant. Using it as a limit gave a malformed writer's claim authority
over the elements that were actually present.

Every path into the loader goes through this one function:
`Mixer::loadSettings()` calls `m_fxChain.restoreState(...)`
(`src/core/Mixer.cpp:3009-3013`), `Rack::loadSettings()` calls
`chain->restoreState(...)` (`src/core/Rack.cpp:418-421`), and the control /
preset path ends in the same `restoreState` -> `loadSettings`. Correcting the
function fixes all of them; no sibling reader needed a separate change
(`saveSettings` and `ControlCommandsReference::chainWith` already compute the
count from the elements).

## Fix

`src/core/EffectChain.cpp:210-266`:

- the loop now walks every child and loads every `<effect>` element, so the
  elements are the source of truth;
- the declared count is kept only for validation;
- `fx_loaded != declared_effects` is reported through `Song::collectError()`
  (`src/core/EffectChain.cpp:258-266`), so a malformed file is visible rather
  than silently repaired;
- `saveSettings()` already writes `m_effects.size()`, so the next save writes
  the recovered count and a later open cannot lose the devices.

No header changed. `loadSettings()` stays on the control thread and keeps the
existing model-change guard; the audio-thread processing path was not touched.
There are no platform-specific changes; the test is a `QTEST_GUILESS_MAIN`.

## Regression test

`tests/src/core/MixerRoutingBackwardCompatTest.cpp:601-672`,
`malformedEffectCountDoesNotDiscardPresentEffects()`. It covers:

- normal path: `numofeffects="1"` with one effect loads one, no error;
- failure paths: zero, negative, non-numeric and missing counts each load the
  one effect that is present and report the mismatch;
- exhaustion paths: a count smaller than the elements loads all elements; a
  count larger than the elements loads all elements and reports the mismatch;
- reconfiguration: for every case the chain is saved again and the test checks
  the rewritten `numofeffects` equals the real element count, so the next open
  is stable.

The test uses unique unknown effect names, so it deterministically builds
`DummyEffect`s and needs no plug-in binary.

## Commands and results (all logs under the run log folder)

RED before the product fix:

    cmake --build build --target MixerRoutingBackwardCompatTest -j4    EXIT=0
    cd build/tests && ctest -R MixerRoutingBackwardCompatTest --output-on-failure    EXIT=8

`test-1-red.log`: `loaded 0 effects, expected 1` for the `numofeffects="0"`
case; 19 passed, 1 failed.

GREEN with the fix:

    cmake --build build --target MixerRoutingBackwardCompatTest -j4    EXIT=0
    cd build/tests && ctest -R MixerRoutingBackwardCompatTest --output-on-failure    EXIT=0

Step 4b (product patch only, generated with `git diff -- src/core/EffectChain.cpp`):

    git apply -R .../logs/fix.patch                    EXIT=0
    cmake --build build --target MixerRoutingBackwardCompatTest -j4    EXIT=0
    cd build/tests && ctest -R MixerRoutingBackwardCompatTest --output-on-failure    EXIT=8
    git apply .../logs/fix.patch                       EXIT=0
    cmake --build build --target MixerRoutingBackwardCompatTest -j4    EXIT=0
    cd build/tests && ctest -R MixerRoutingBackwardCompatTest --output-on-failure    EXIT=0

Logs: `build-3-red.patch.log`, `test-3-red.patch.log`, `build-4-green.patch.log`,
`test-4-green.patch.log`. The RED run without the product code fails at the
same assertion; the GREEN run passes.

Related family:

    cmake --build build --target RackTest MissingDevicesRound2Test RoutingGraphLiveTest ControlChainPresetTest -j4    EXIT=0
    cd build/tests && ctest -R "^(RackTest|MissingDevicesRound2Test|RoutingGraphLiveTest|ControlChainPresetTest)$" --output-on-failure    EXIT=8

`RackTest`, `MissingDevicesRound2Test` and `RoutingGraphLiveTest` passed.
`ControlChainPresetTest` has one failure,
`aCapturedChainAppliesToASecondTrackWithTheSameDevicesAndValues()` at
`ControlChainPresetTest.cpp:592` (`loadEffects(source, 2, &ids)` returned 1).
That test does not call `EffectChain::loadSettings`; it loads devices through
`plugin.load`. I reverted this fix, rebuilt that target and reran it, and it
failed identically without the fix (`test-7-preset-nofix.log`, EXIT=8), so it
is a pre-existing environment/plugin-loading failure, not collateral damage.
`MixerRoutingBackwardCompatTest` remained green in the restore run
(`test-8-restore.log`).

Product rebuild:

    cmake --build build --target zene -j4    EXIT=0

Gates:

    python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>    EXIT=0

`gates-2.log`: every gate PASS, `RESULT: PASS`.

Registration: `src/core/EffectChain.cpp` is an inherited file, so the change is
listed in `tests/upstream-modifications.txt` with the cluster and the regression
test name. The test file is an existing build source already tracked by the
tree.

## Other findings

- `ControlChainPresetTest::aCapturedChainAppliesToASecondTrackWithTheSameDevicesAndValues()`
  fails in this build tree because only one of two loadable effects can be
  loaded through `plugin.load` (`loadEffects(source, 2, &ids)` returns 1). It
  fails with and without this fix and is unrelated to `EffectChain::loadSettings`.

## Self-check

- **Regression test fails with the product change reverted and passes with it.**
  Step 4b used `fix.patch` from `git diff -- src/core/EffectChain.cpp`.
  Reverted: build EXIT=0, `ctest -R MixerRoutingBackwardCompatTest` EXIT=8
  (`test-3-red.patch.log`, `loaded 0 effects, expected 1`). Reapplied: build
  EXIT=0, same ctest EXIT=0 (`test-4-green.patch.log`). The initial RED run
  before any fix was also EXIT=8 (`test-1-red.log`) and the fixed run EXIT=0
  (`test-2-green.log`).
- **Deterministic.** The test uses fixed XML strings and a local `EffectChain`
  per case; the effect names are unknown to the plug-in factory, so each becomes
  a deterministic `DummyEffect` with no plug-in binary, thread or plug-in scan.
  There is no wall-clock timing, sleep, timer or race: the only engine setup is
  the existing `QTEST_GUILESS_MAIN` harness, whose audio device is stopped.
  Mismatch reporting is read synchronously from `Song::collectError()`.
- **Cause and completeness.** The only entry point is `EffectChain::loadSettings`,
  reached through `restoreState` from `Mixer::loadSettings`
  (`src/core/Mixer.cpp:3009-3013`) and `Rack::loadSettings`
  (`src/core/Rack.cpp:418-421`), and from the control/preset restore path. All
  now load every element. `saveSettings` writes the real count
  (`src/core/EffectChain.cpp:160`), and `ControlCommandsReference::chainWith`
  already recomputes the count from the elements. `DataFile`'s legacy upgrade
  only copies the attribute during a format migration, so it does not trust it
  as a load bound.
- **No collateral damage.** A well-formed file still loads exactly as before:
  the normal `numofeffects="1"` case is asserted to load one effect and report
  no error. The fix never rejects an element or a caller; it loads all elements
  present. A scan of every `.mmp` fixture under `tests/` found no fxchain whose
  `numofeffects` differs from its `<effect>` child count, so no existing fixture
  gains an error. The only files that now report an error are files whose count
  was already wrong and whose effects were previously discarded.
- **ALREADY-FIXED / FALSE-POSITIVE.** Not applicable: the verdict is REAL.
- **Sibling commands in changed command files.** This cluster changed no command
  file and added or altered no control command. It touched
  `src/core/EffectChain.cpp`, its regression test, the inherited-file
  registration line and this report. The listed `app.*` and `arrangement.*`
  commands live in other files and are not affected by the loader change; no
  command registration or schema was edited.
