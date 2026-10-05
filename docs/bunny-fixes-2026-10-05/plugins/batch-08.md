# Plugins batch-08 — Luna clusters 5451, 5456, 2617, 5454, 4533, 1789

Branch `bugs/bunny-plugins-2026-10-05`. Six findings, one fixed, one
SKIPPED-SCOPE, four REAL-UNTESTED. Only the component-handler fix touched
product code; the four untested findings are real code-shape observations whose
proposed fixes are design changes or have no deterministic headless RED.

### Cluster 5451 — Vestige locks the plugin mutex on the audio path

Verdict: SKIPPED-SCOPE

Evidence: `plugins/Vestige/Vestige.cpp:417-419` —
`processLock()` is `m_pluginMutex.tryLock(Engine::getSong()->isExporting() ? -1 : 0)`,
byte-identical to `plugins/LadspaEffect/LadspaEffect.cpp:239`,
`plugins/ZynAddSubFx/ZynAddSubFx.cpp:370` and `plugins/VstEffect/VstEffect.cpp:126`.
This is the documented, intentional host contract: `include/AudioPlugin.h:110-123`
states "Some LMMS plugins currently require a lock around their process method
... Until all process methods are made lock-free, plugins may override these
lock/unlock methods", and the non-blocking consumption sites are
`include/AudioPlugin.h:166` and `:266`. `handleMidiEvent()`
(`Vestige.cpp:433-441`) uses the same blocking lock as `ZynAddSubFx.cpp:386-410`.

The observation is accurate — the export-time `-1` arm can block the render
thread — but this is rule 9 known-by-design, and the suggested fix ("use a
synchronization scheme that never blocks the audio thread") requires either new
lock-free access to a third-party DSP object or a redesign of the shared
`ProcessMutex` contract, which is the realtime-rule-6 SKIPPED-SCOPE case.

Commit: none

Test: none

Red/green: none

### Cluster 5456 — VST3 component handler rejects every interface query

Verdict: FIXED

Evidence: `plugins/Vst3Effect/Vst3ComponentHandler.h:107` — `queryInterface()`
previously ended in `*obj = nullptr; return kNoInterface;` with no branch for
either interface the class actually implements, so a plugin asking for
`IComponentHandler::iid` or `FUnknown::iid` got a hard failure and could
disable host edit callbacks. Fixed to return `kInvalidArgument` for a null
`obj`, and on a match via `FUnknownPrivate::iidEqual` set `*obj`, call
`addRef()` and return `kResultOk`, following the SDK pattern already used by
`MappingController` in the same test file.

Commit: `8bf0fda78`

Test: `Vst3HostTest::testComponentHandlerAnswersInterfaceQueries`

Red/green: RED — `cd build/tests && ctest -R '^Vst3HostTest$' --output-on-failure` →
EXIT=8, `Actual ... queryInterface(IComponentHandler::iid) -1`,
`Totals: 13 passed, 1 failed`.
GREEN — `cmake --build build --target zene Vst3HostTest` → EXIT=0, then the
same ctest → EXIT=0, direct run `Totals: 14 passed, 0 failed`.
Gates — `bash tests/complexity-gate.sh --check` EXIT=0,
`bash tests/file-length-gate.sh --check` EXIT=0.

### Cluster 2617 — Generated VST3 parameter views cannot scroll

Verdict: REAL-UNTESTED

Evidence: `plugins/Vst3Effect/Vst3EffectControlDialog.cpp:54-83` and
`plugins/Vst3Instrument/Vst3InstrumentView.cpp:57-88` both build a
`QGridLayout` of knobs and then call `adjustSize(); setFixedSize(sizeHint());`
with no `QScrollArea` wrapper and no cap against the available screen size, so
a plug-in with many visible parameters does overflow and become unreachable.

The code shape is confirmed, but no deterministic headless RED exists. The
only GUI-capable VST3 binary, `tests/src/plugins/Vst3InstrumentIntegrationTest.cpp`
(`QTEST_MAIN`, `QApplication`), documents near lines 577-590 that generated Knob
grids cannot be constructed there because `getGUI()` is null under
`Engine::init(true)`. The in-tree `again` fixture additionally exposes only two
parameters (Bypass id 0, Gain id 1), which never exceeds screen size. Per the
lane procedure, product code was left untouched.

Commit: none

Test: none

Red/green: none

### Cluster 5454 — VST3 state-save failures can silently lose plug-in state

Verdict: REAL-UNTESTED

Evidence: `plugins/Vst3Effect/Vst3EffectControls.cpp:103` —
`m_effect->plugin()->saveState(&componentState, &controllerState);` discards the
returned `bool` (`HostedPlugin::saveState` at `plugins/Vst3Effect/Vst3Host.cpp:401-422`
returns a real status), and `appendState` then skips empty buffers at
`Vst3EffectControls.cpp:104-112`, so a failed state save is written out as a
successful project save with no state node.

Confirmed in the current tree, but not headlessly reproducible: neither
`tests/data/vst3-chunk-probe/vst3-chunk-probe.cpp:437-461` nor the
`vst3-test-effect` `again` sample has a `getState`/`setState` path that fails,
so no fixture deterministically drives the failure branch. Product code left
untouched.

Commit: none

Test: none

Red/green: none

### Cluster 4533 — VST3 state-restore failures are ignored

Verdict: REAL-UNTESTED

Evidence: `plugins/Vst3Effect/Vst3EffectControls.cpp:125` —
`m_effect->plugin()->loadState(componentState, controllerState);` discards the
`bool` from `HostedPlugin::loadState` (`Vst3Host.cpp:424-460`), and the models
are then resynchronised at `:128-131` from whatever the plug-in retained, with
no warning surfaced to the user.

Same blocker as cluster 5454 and the same decision: the defect is real code
shape, but no in-tree VST3 fixture rejects its stored state, so no deterministic
headless RED can be produced and product code was not changed.

Commit: none

Test: none

Red/green: none

### Cluster 1789 — Read-only VST3 parameters are exposed as editable controls

Verdict: REAL-UNTESTED

Evidence: `plugins/Vst3Effect/Vst3Host.cpp:278` correctly sets
`desc.readOnly = (info.flags & ParameterInfo::kIsReadOnly) != 0;` and
`plugins/Vst3Effect/Vst3ParamDescriptor.h:176` stores the flag, but a bounded
search of `plugins/` finds no consumer of `readOnly` outside bundled
CarlaBase/JUCE third-party code. Model construction ignores it at
`plugins/Vst3Effect/Vst3EffectControls.cpp:42-56` and
`plugins/Vst3Instrument/Vst3Instrument.cpp:131-144`, and `Vst3ParamModel`
(`plugins/Vst3Effect/Vst3Parameter.h:51-70`,
`plugins/Vst3Parameter.cpp:104-114`) has no read-only gate.

Implementing the gate is not a minimal fix: `AutomatableModel::setValue`
(`include/AutomatableModel.h:246`, `src/core/AutomatableModel.cpp:315`) is
non-virtual and `fittedValue` (`AutomatableModel.cpp:425`) clamps to the
parameter range, while `AutomatableModel` exposes no `setDisabled`/`isDisabled`
API. A correct fix therefore means new core API plus an automation-path gate,
which is a design change outside this lane. No test was attempted.

Commit: none

Test: none

Red/green: none

Leftovers: clusters 2617, 5454, 4533 and 1789 are confirmed-but-unfixed. Each
needs either a VST3 fixture that can be made to fail `getState`/`setState` or
report `kIsReadOnly`, or a core `AutomatableModel` read-only API before this
lane can produce a red/green cycle; 2617 additionally needs a GUI-capable VST3
test binary that can actually construct `getGUI()`-backed Knob grids headlessly.