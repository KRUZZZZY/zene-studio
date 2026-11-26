ALREADY-FIXED

# cluster new-p8-265 — VST3 parameter values can disappear when saving project state

Verdict: **ALREADY-FIXED** on `bugs/qwen-new-p8-265`. The finding describes a real defect (both
save paths wrote only the plug-in's state chunks, so a plug-in that keeps no state, or whose state
call fails, reloaded its defaults), but the branch already carries the fix, its restore side and the
regression tests. Three commits name this cluster:

- `3749b602d3` `fix(vst3): preserve parameters when state chunks are absent (cluster new-p8-265)`
- `8f40a4f801` `fix(vst3): keep chunk restoration authoritative (cluster new-p8-265)`
- `54f16b5f0c` `fix(cluster stress-4, round 2): restore writable parameters a successful state load did not bring back`

This packet makes **no product change**. Its diff is this report; `git status` was clean before it
was written and `git diff --stat` is empty for every product file.

## What the finding claimed, against the code today

The finding's three parts, checked one by one (line numbers are today's, not the patrol's).

1. **"The controls save only state chunks; they do not save exposed parameter model values as a
   fallback."** Not true any more.
   - `plugins/Vst3Effect/Vst3EffectControls.cpp:119-129` writes a `<parameters>` element with one
     `<parameter id= value=>` per writable descriptor, read from `paramNormalized`, **outside** any
     branch on the state result. `plugins/Vst3Instrument/Vst3Instrument.cpp:477-487` is the same
     block in the instrument's `saveSettings`.
   - The value written is the model's value at save time, not a polled copy: both hosts mirror a
     model change into the lock-free snapshot synchronously in the `Model::dataChanged` connection
     (`Vst3EffectControls.cpp:48-52`, `Vst3Instrument.cpp:144-147`), and the 100 ms `poll()` only
     pulls plug-in -> model.
   - Restore: `plugins/Vst3Effect/Vst3Parameter.cpp:60-79` (`restoreSavedParameters`) is called
     unconditionally after `loadState` by both load paths (`Vst3EffectControls.cpp:152`,
     `Vst3Instrument.cpp:509`). It skips unknown ids, unparseable or non-finite values and
     read-only parameters (`:71-75`), skips a value the plug-in already reports within `1e-6`
     (`:77`), and otherwise pushes the saved value back through `notifyController` (`:78`), which
     updates both the host snapshot and the edit controller (`Vst3HostParameters.cpp:91-102`).
2. **"failed `saveState()` results are ignored."** Not true any more. Both save paths check the
   result and warn: `Vst3EffectControls.cpp:104-108` ("state chunks could not be saved; writing
   parameter fallback") and `Vst3Instrument.cpp:462-466`. `HostedPlugin::saveState`
   (`plugins/Vst3Effect/Vst3Host.cpp:386-407`) returns `false` for every result other than
   `kResultOk`/`kNotImplemented`, so a real failure reaches the caller. There is no error channel in
   the DAW's project-writer API, so the warning plus the always-written fallback is the surface.
3. **"A `kNotImplemented` state response is treated as success."** True, and that is the intended
   contract, not a defect: `IComponent::getState`/`IEditController::getState` return
   `kNotImplemented` when an implementation keeps no state (the SDK base classes do). The VST3 SDK's
   own preset helper accepts exactly that pair —
   `plugins/CarlaBase/carla/source/includes/vst3sdk/public.sdk/source/vst/vstpresetfile.cpp:75-78`
   (`verify()` = `result == kResultOk || result == kNotImplemented`) — and JUCE's host deliberately
   does not warn for it
   (`plugins/CarlaBase/carla/source/modules/juce_audio_processors/format_types/juce_VST3PluginFormat.cpp:85-90`).
   The host can no longer lose data either way, because the `<parameters>` block is written whatever
   the state call returned. The code says the same at `Vst3Host.cpp:393-396`.

Siblings and callers checked: `HostedPlugin::saveState` has exactly two callers
(`Vst3EffectControls.cpp:104`, `Vst3Instrument.cpp:462`), and both handle the result; only two
save paths and two load paths exist for VST3 state
(`Vst3EffectControls.cpp:100-160`, `Vst3Instrument.cpp:458-518`), and both pairs use the one shared
restore function.

## Test matrix

| Path | Covered by | Result this session |
| --- | --- | --- |
| Normal: parameter changed, chunks saved and loaded | `Vst3EffectIntegrationTest::testStateRoundTripThroughMmp` (`tests/src/plugins/Vst3EffectIntegrationTest.cpp:365`), `Vst3InstrumentIntegrationTest::testStateSurvivesTheProjectFile` (`:648`) | PASS |
| Failure/absent chunks: no state chunk in the file | `Vst3EffectIntegrationTest::testParameterFallbackWithoutStateChunks` (`:418`), `Vst3InstrumentIntegrationTest::testParameterFallbackWithoutStateChunks` (`:740`) | PASS, and FAIL with the fix reverted |
| Successful chunk load that does not carry a value | `testAValueTheChunksDoNotCarryIsRestoredFromTheHost` (effect `:541`, instrument `:1114`) | PASS, and FAIL with the fix reverted |
| Exhaustion / reconfiguration | no limit added; a plug-in with no writable parameters writes no `<parameters>` element (`if (parameters.hasChildNodes())`), so parameterless plug-ins and old files are unchanged; `prepare()`/`reprepare()` does not touch saved state | read, not separately exercised |
| Audio thread | save/load run on the control thread only; no allocation, lock, wait or log added to any render path by this packet (no product change) | read |
| Platforms | the touched code uses only `QDomDocument`/`QDomElement` and `std::isfinite`; no Qt-6-only API | built and run on Linux/Qt6 here; MSVC/Qt5 not run |

## Regression evidence: the landed fix, reverted and restored

The two tests named above fail when the product half of the fix is removed and pass when it is put
back. Everything below was run in this session; the exit codes are the ones written into the logs.

```
$ cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p8-265
$ git diff -R -- plugins/Vst3Effect/Vst3EffectControls.cpp plugins/Vst3Instrument/Vst3Instrument.cpp \
      > /home/kruzzzzy/.config/kads/zene/state/runs/new-p8-265.r1/logs/fix.patch     # EXIT=0
$ git apply -R .../logs/fix.patch                                                   # EXIT=0
$ git diff --stat
 plugins/Vst3Effect/Vst3EffectControls.cpp | 30 ++----------------------------
 plugins/Vst3Instrument/Vst3Instrument.cpp | 29 ++---------------------------
 2 files changed, 4 insertions(+), 55 deletions(-)
$ cmake --build build --target Vst3EffectIntegrationTest Vst3InstrumentIntegrationTest -j4
      > .../logs/build-3-red.log 2>&1                                   # BUILD_RED_EXIT=0
$ cd build/tests && ctest -R "Vst3(Effect|Instrument)IntegrationTest" --output-on-failure
      > .../logs/test-3-red.log 2>&1                                    # CTEST_RED_EXIT=8
```

`test-3-red.log`: `0% tests passed, 2 tests failed out of 2` —
`FAIL! Vst3EffectIntegrationTest::testParameterFallbackWithoutStateChunks` at
`Vst3EffectIntegrationTest.cpp(452)` (`!stateElement.firstChildElement("parameters").isNull()`),
`FAIL! …::testAValueTheChunksDoNotCarryIsRestoredFromTheHost` at `:565` (`restored 0`),
`FAIL! Vst3InstrumentIntegrationTest::testStateSurvivesTheProjectFile` at `:725`,
`FAIL! …::testParameterFallbackWithoutStateChunks` at `:753`,
`FAIL! …::testAValueTheChunksDoNotCarryIsRestoredFromTheHost` at `:1128`.

```
$ git apply .../logs/fix.patch                                                      # EXIT=0
$ cmake --build build --target Vst3EffectIntegrationTest Vst3InstrumentIntegrationTest -j4
      > .../logs/build-4-green.log 2>&1                                 # BUILD_GREEN_EXIT=0
$ cd build/tests && ctest -R "Vst3(Effect|Instrument)IntegrationTest" --output-on-failure
      > .../logs/test-4-green.log 2>&1                                  # CTEST_GREEN_EXIT=0
```

`test-4-green.log`: `100% tests passed out of 2` in 3.13 s. The baseline before any edit is
`test-1.log` (same command, same two tests, `EXIT=0`). `fix.patch`, `step4b.log`,
`build-1.log`, `build-2-red.log`, `test-1.log`, `test-2-red.log`, `build-3-red.log`,
`test-3-red.log`, `build-4-green.log` and `test-4-green.log` are all in the log folder.

The tests are deterministic: fixed injected values (`0.3` saved, `0.9` after the save, restored
`0.3`), a fixed 48-frame block, no clock, timer or thread in the assertion path. The effect test has
no wait at all; the instrument test's pre-existing `QTest::qWait(400)` calls (written before this
packet, in `testParameterFallbackWithoutStateChunks` and `testStateSurvivesTheProjectFile`) are not
what makes its assertions true — the model mirrors into the snapshot synchronously at
`Vst3Instrument.cpp:144-147`, and under the reverted code it fails at the `<parameters>` presence
check (`:753`) before any value is compared.

## Gates

```
$ python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>
      > .../logs/feature-check-1.log 2>&1                                           # FEATURE_CHECK_EXIT=0
```

```
feature-check kads-new-p8-265 (base 3436a808b, 0 changed files; ...)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Yes, with the
  exit codes above: `CTEST_RED_EXIT=8` (both binaries fail, five assertions) after
  `git apply -R .../logs/fix.patch`, `CTEST_GREEN_EXIT=0` after `git apply .../logs/fix.patch`.
  The reverted state is the pre-fix code the finding describes (chunks only, `saveState()` result
  dropped); `step4b.log` records the revert's own `EXIT=0` and the 4-insertion/55-deletion diffstat.
- **Deterministic, no wall-clock timing.** The effect-side regression test
  (`Vst3EffectIntegrationTest.cpp:418`) injects its values (`setValue(0.3f)`, then `0.9f`) into a
  fixed 48-frame `AudioBus`, drives one `processAudioBuffer` call and reads the sample back; no
  sleep, timer, thread or clock. The seam that makes the saved value current is the synchronous
  `Model::dataChanged` connection (`Vst3EffectControls.cpp:48-52`), not a poll. The instrument-side
  test is pre-existing and carries two `QTest::qWait(400)` calls that its assertions do not need
  (see above); this packet does not touch it.
- **Fix at the cause and complete; callers and siblings checked.** The cause was "only chunks are
  persisted"; the branch persists the writable parameter values unconditionally and restores every
  one the plug-in's own state did not bring back, through one shared function
  (`Vst3Parameter.cpp:60`). The only two callers of `HostedPlugin::saveState`
  (`Vst3EffectControls.cpp:104`, `Vst3Instrument.cpp:462`) both handle the boolean, and the two load
  paths (`Vst3EffectControls.cpp:144-152`, `Vst3Instrument.cpp:501-509`) both restore.
- **No collateral damage.** The `<parameters>` element is additive: an old `.mmp` without it finds
  no children, so `restoreSavedParameters` does nothing and the load is byte-for-byte the previous
  behaviour; a plug-in with no writable parameters writes no element; read-only parameters
  (`:71-75`) and unknown ids are skipped, so a saved file from another plug-in version cannot push a
  value into the wrong parameter. No limit or validation check was added. The round-trip tests
  (`testStateRoundTripThroughMmp`, `testStateSurvivesTheProjectFile`) still pass.
- **ALREADY-FIXED proof.** `plugins/Vst3Effect/Vst3EffectControls.cpp:104-129` (result checked and
  warned, `<parameters>` always written), `plugins/Vst3Instrument/Vst3Instrument.cpp:462-487` (the
  same), `plugins/Vst3Effect/Vst3Parameter.cpp:60-79` (shared restore), calls at
  `Vst3EffectControls.cpp:152` and `Vst3Instrument.cpp:509`; the `kNotImplemented` contract at
  `Vst3Host.cpp:393-396` matches `vstpresetfile.cpp:75-78` and `juce_VST3PluginFormat.cpp:85-90`.
  Landing commits `3749b602d3`, `8f40a4f801`, `54f16b5f0c`.
- **Realtime / undo / commands.** This packet adds no product code, no command and no undo entry;
  the save and load paths run on the control thread and the shared restore path allocates nothing on
  the audio thread (`Vst3Parameter.cpp` touches only the snapshot array and the edit controller).

## Other findings

- `schemas/proof.json` is not present on this box (`~/.config/kads/zene/` holds only `state/`), so
  the manifest uses the field names from the brief plus the `kind`/`file`/`line`/`note` shape that
  earlier ALREADY-FIXED reports on this branch use.
- `Vst3InstrumentIntegrationTest::testParameterFallbackWithoutStateChunks` and
  `testStateSurvivesTheProjectFile` still contain `QTest::qWait(400)` calls that the synchronous
  model->snapshot mirror makes unnecessary. Pre-existing test code, harmless, not touched here.
- The VST3 `getState` result is only surfaced as a `qWarning`. The project writer has no error
  channel to report it through; a user-visible save warning would be a separate feature, not this
  bug.
