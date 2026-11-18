# Cluster 4555 — Read-only VST3 parameters are presented as editable knobs

**Verdict: FIXED**

## Finding

`plugins/Vst3Instrument/Vst3InstrumentView.cpp:60-68` — the generated view loop
skips `descriptor.hidden` parameters but ignores `descriptor.readOnly`, so a
plug-in that exposes a *visible read-only* parameter (e.g. a meter) gets a
draggable knob whose value the plug-in may ignore or reset on its next poll.
Confidence: high.

## Evidence (file:line)

- `plugins/Vst3Instrument/Vst3InstrumentView.cpp:64` (original):
  `if (descriptor.hidden) { continue; }` — no `readOnly` check.
- `plugins/Vst3Effect/Vst3ParamDescriptor.h:66`: `bool readOnly = false;` — the
  flag exists on the descriptor the view iterates.
- `plugins/Vst3Effect/Vst3Host.cpp:278`: `desc.readOnly = (info.flags &
  ParameterInfo::kIsReadOnly) != 0;` — the shared host (`HostedPlugin::load`,
  used by the instrument too, `Vst3Instrument.cpp:99`) maps the SDK flag.
- `plugins/Vst3Instrument/Vst3Instrument.cpp:130`: `paramModels` are built
  directly from `m_plugin.parameters()` descriptors, so the flag reaches the
  view once the plug-in actually sets it.

## Regression test

Extended the in-tree fixture `tests/data/vst3-test-instrument/vst3-test-instrument.cpp`
with a second, **visible read-only** parameter (`"Meter"`, id 101,
`ParameterInfo::kIsReadOnly`) and added `testReadOnlyParametersAreNotKnobs()` to
`tests/src/plugins/Vst3InstrumentIntegrationTest.cpp`: it asserts the instrument
exposes 2 parameters (`parameterName(1) == "Meter"`), that
`instrument->createView()` renders exactly **1** `gui::Knob` (the read-only
parameter must not get one), and that the surviving knob is bound to the editable
parameter's model (`parameterModel(0)`). The two pre-existing
`parameterCount() == 1` assertions in that file were updated to 2 to match the
fixture (not weakened).

## RED (original code)

```
$ (cd build/tests && ctest -R Vst3Instrument --output-on-failure)   # log: test-1.log
EXIT=8
FAIL!  : lmms::Vst3InstrumentIntegrationTest::testReadOnlyParametersAreNotKnobs()
   Actual (knobs.size()): 2   Expected (1)
Totals: 10 passed, 1 failed
```

## Fix

`plugins/Vst3Instrument/Vst3InstrumentView.cpp` — omit read-only parameters from
the knob grid (the finding's second suggested option; smallest correct change):

```cpp
-    if (descriptor.hidden) {
+    // Read-only parameters (e.g. meters) are informational: the host may ignore
+    // or reset values sent for them, so they must not be presented as editable
+    // knobs. They remain visible through the native parameter editor.
+    if (descriptor.hidden || descriptor.readOnly) {
         continue;
     }
```

No header change; only the `vst3instrument` module and the test binaries needed
rebuilding. The same pattern exists in `plugins/Vst3Effect/Vst3EffectControlDialog.cpp`
(a different cluster's finding) and was deliberately left untouched.

### Fixture correction (root cause of an intermediate failure)

After the view fix, the test still saw 2 knobs (`test-6.log`, EXIT=8). A temporary
`qDebug()` in the view loop (`test-7.log`) showed `readOnly == false` for "Meter"
at runtime: the SDK's `ParameterContainer::addParameter` signature
(`build/vst3sdk/public.sdk/source/vst/vstparameters.cpp:416`) is
`(title, units, stepCount, default, flags, tag)` — the fixture had passed
`kIsReadOnly` in the **stepCount** slot. Corrected the fixture to pass it as
flags:

```cpp
parameters.addParameter (STR16 ("Meter"), nullptr, /*stepCount*/ 0, 0.0,
                         ParameterInfo::kIsReadOnly, kMeterParam);
```

The debug print was removed from the view before the final build.

## Build + GREEN

```
$ cmake --build build --target vst3instrument vst3-test-instrument Vst3InstrumentIntegrationTest Vst3InstrumentTest Vst3InstrumentFixtureProbe   # build-6.log
EXIT=0
$ (cd build/tests && ctest -R Vst3Instrument --output-on-failure)   # log: test-8.log
EXIT=0
  100% tests passed, 0 tests failed out of 3
```

All three family binaries (`Vst3InstrumentFixtureProbe`, `Vst3InstrumentTest`,
`Vst3InstrumentIntegrationTest`) ran and passed; the integration binary's new
test case passed among its 11 cases.

## Gates

```
$ bash tests/complexity-gate.sh --check      EXIT=0   (PASS, check mode)
$ bash tests/file-length-gate.sh --check     EXIT=0   (PASS, check mode)
$ bash tests/fork-sources-gate.sh            EXIT=0   (all tracked sources registered)
$ bash tests/unregistered-tests-gate.sh      EXIT=0   (all test sources registered)
```

No new files were introduced (fixture, integration test, and view were already
registered in `tests/all-sources.txt` / `tests/fork-sources.txt`), so no
manifest edits were required.

## Files committed

Commit 1 (`fix(vst3): cluster 4555 — read-only VST3 parameters presented as editable knobs`):

- `plugins/Vst3Instrument/Vst3InstrumentView.cpp` (the fix)
- `tests/data/vst3-test-instrument/vst3-test-instrument.cpp` (fixture, first revision)
- `tests/src/plugins/Vst3InstrumentIntegrationTest.cpp` (regression test)
- `docs/qwen-fixes-2026-10/cluster-4555.md` (this report, first revision)

Commit 2 (fixture flag-slot correction + corrected evidence in this report):

- `tests/data/vst3-test-instrument/vst3-test-instrument.cpp`
- `docs/qwen-fixes-2026-10/cluster-4555.md`
