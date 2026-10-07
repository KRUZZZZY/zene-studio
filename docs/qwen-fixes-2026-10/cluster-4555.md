# Cluster 4555 — Read-only VST3 parameters are presented as editable knobs

**Verdict: FIXED**

## Finding

`plugins/Vst3Instrument/Vst3InstrumentView.cpp:60-68` — the view loop skips
`descriptor.hidden` parameters but ignores `descriptor.readOnly`, so a plug-in
that exposes a *visible read-only* parameter (e.g. a meter) gets a draggable
knob whose value the host may ignore or reset on the next poll. Confidence: high.

## Evidence (file:line)

- `plugins/Vst3Instrument/Vst3InstrumentView.cpp:64` (original):
  `if (descriptor.hidden) { continue; }` — no `readOnly` check.
- `plugins/Vst3Effect/Vst3ParamDescriptor.h:66`: `bool readOnly = false;` — the
  flag exists on the descriptor the view iterates.
- `plugins/Vst3Effect/Vst3Host.cpp:278`: `desc.readOnly = (info.flags &
  ParameterInfo::kIsReadOnly) != 0;` — the host maps the VST3 SDK flag correctly,
  so the view simply never consults it.
- `plugins/Vst3Instrument/Vst3Instrument.cpp:130-133`: `paramModels` are built
  directly from `m_plugin.parameters()` descriptors, so the flag reaches the view.

## Regression test

Extended the in-tree fixture `tests/data/vst3-test-instrument/vst3-test-instrument.cpp`
with a second, **visible read-only** parameter (`"Meter"`, id 101,
`ParameterInfo::kIsReadOnly`), and added `testReadOnlyParametersAreNotKnobs()` to
`tests/src/plugins/Vst3InstrumentIntegrationTest.cpp`: it asserts the instrument
now exposes 2 parameters (`parameterName(1) == "Meter"`), that
`instrument->createView()` renders exactly **1** `gui::Knob` (the read-only
parameter must not get one), and that the remaining knob is bound to the editable
parameter's model (`parameterModel(0)`). The two pre-existing
`parameterCount() == 1` assertions in that file were updated to 2 to match the
fixture (not weakened).

## RED (original code)

```
$ (cd build/tests && ctest -R Vst3Instrument --output-on-failure)   # log: test-1.log
EXIT=8
FAIL! testReadOnlyParametersAreNotKnobs()
  Actual (knobs.size()): 2   Expected (1)
```

10 test cases passed, 1 failed (the failing assertion above); the other two
family binaries were not yet built at that point.

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

No header change; only the `vst3instrument` module and the zene binary needed
rebuilding. The same pattern exists in `plugins/Vst3Effect/Vst3EffectControlDialog.cpp`
(a different cluster's finding) and was deliberately left untouched.

## Build + GREEN

```
$ cmake --build build --target zene Vst3InstrumentFixtureProbe Vst3InstrumentTest Vst3InstrumentIntegrationTest   # build-3.log
EXIT=0
$ (cd build/tests && ctest -R Vst3Instrument --output-on-failure)   # test-5.log
EXIT=0
  3 test(s) passed   (Vst3InstrumentFixtureProbe, Vst3InstrumentTest, Vst3InstrumentIntegrationTest)
```

The integration binary ran 11 test cases (10 pre-existing + the new one), all
passing.

## Gates

```
$ bash tests/complexity-gate.sh --check      EXIT=0
$ bash tests/file-length-gate.sh --check     EXIT=0
$ bash tests/fork-sources-gate.sh            EXIT=0   # every tracked source registered
$ bash tests/unregistered-tests-gate.sh      EXIT=0   # every test source registered
```

No new files were introduced (fixture, integration test, and view were already
registered in `tests/all-sources.txt` / `tests/fork-sources.txt`), so no
manifest edits were required.

## Files committed

- `plugins/Vst3Instrument/Vst3InstrumentView.cpp` (the fix)
- `tests/data/vst3-test-instrument/vst3-test-instrument.cpp` (fixture: visible read-only "Meter")
- `tests/src/plugins/Vst3InstrumentIntegrationTest.cpp` (regression test)
- `docs/qwen-fixes-2026-10/cluster-4555.md` (this report)
