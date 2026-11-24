IMPLEMENTED

# Q050-359 - Carla Rack VST3 opening stability

Branch `feat/050-Q050-359-oc`, worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-Q050-359`.
Logs and scratch: `/home/kruzzzzy/.config/kads/zene/state/runs/Q050-359.r1/logs`.

**Reproduced, then fixed at the host path.** On this build `pkg-config carla-native-plugin` finds nothing, so
`CMakeLists.txt:545-558` sets `LMMS_HAVE_WEAKCARLA` and the product links `plugins/CarlaBase/DummyCarla.cpp`, whose
`carla_get_native_rack_plugin()` (`:11`) and `carla_get_native_patchbay_plugin()` (`:12`) answer **nullptr**. The first step
of the sequence the finding describes - put Carla Rack on a track so a plug-in can be loaded inside it - passed that pointer
to `CarlaInstrument`, which calls `fDescriptor->instantiate(&fHost)` (`plugins/CarlaBase/Carla.cpp:203`) with no null check:
`SIGSEGV`, fault address `0x48`, `call *0x48(%rax)`. Both host modules now decline with a reason the control surface reports
(`plugins/CarlaRack/CarlaRack.cpp:52-71`, `plugins/CarlaPatchbay/CarlaPatchbay.cpp:52-69`), exactly as
`plugins/CarlaVst2::lmms_plugin_main` already did (`CarlaVst2.cpp:81`).

Commits: `c0ab4e002d` (product + test + registration), `0e6dc10682` (manual + feature record), then this report.

## Acceptance

| Criterion | Evidence (command, exit) |
|---|---|
| 1. Repeat the exact Carla Rack/VST3 load sequence under a crash trace, or record a bounded not-reproduced result | **Reproduced from the shipped binary**: `python3 .../repro_carla_rack.py build/zene LMMS_PLUGIN_DIR=$PWD/build/plugins` - `plugin.list` offers `carlarack` (`dev-1`, `builtin`, `loadable: true`), then `plugin.load` killed the process; the crash reporter wrote `signal=SIGSEGV(11) fault_addr=0x48` (`logs/repro-2.log`, exit 1 = crashed). **Reproduced from the test**: with the two product hunks reversed, `ctest -R '^CarlaRackNoRuntimeTest$'` → `***Exception: SegFault`, stack frame `lmms::CarlaInstrument::CarlaInstrument(...)` at `call *0x48(%rax)`, **exit 8** (`logs/test-red-1.log`). **With the fix**: same command **exit 0**, `Totals: 6 passed, 0 failed, 0 skipped` (`logs/test-green-2.log`, `logs/test-green-3.log`). **Bounded, not reproduced**: the step *inside* Carla (loading the VST3 in a real Rack) cannot run here - no Carla runtime exists in this build, building one is `tools/build-carla-runtime.sh` (a separate heavy build) and Carla's Rack adds plug-ins through its own native window, which a headless QtTest cannot drive (`docs/050-features/Q050-359.md`, Known limits). |
| 2. If reproduced, fix the host path or route VST3 to the native host, and add a regression test | Host path fixed in both modules; `plugin.load` answers `refused` with *"this build has no Carla runtime, so Carla Rack cannot host anything ... a VST3 loads through Zene's own VST3 host instead (plugin.list format vst3, then plugin.load its dev-&lt;n&gt;)"* (`logs/repro-3-after-fix.log`, exit 0, process alive and answering `control.ping`). Regression test `CarlaRackNoRuntimeTest` (4 cases: agent path `pluginLoadRefusesWithoutTheRuntime`, GUI `theDeviceCardRefusesWithoutTheRuntime`, sibling `carlaPatchbayIsRefusedTheSameWay`, project `aSavedCarlaRackProjectKeepsItsXml`): **exit 8 without the product change, exit 0 with it** (`logs/test-red-1.log`, `logs/test-green-3.log`). |

## What changed

- `plugins/CarlaRack/CarlaRack.cpp`, `plugins/CarlaPatchbay/CarlaPatchbay.cpp` - decline when the build's Carla stand-in
  answers nullptr, and hand the reason to `pluginload::noteInstantiationFailure` (the mechanism `Vst3Effect`, `AuEffect`,
  `ClapEffect` use), so `plugin.load` refuses typed instead of the process dying.
- `tests/src/gui/CarlaRackNoRuntimeTest.cpp` (new), registered in `tests/CMakeLists.txt` (target, `QT_QPA_PLATFORM=offscreen`,
  `add_dependencies` on `carlarack`, `carlapatchbay`, `tripleoscillator`), `tests/fork-sources.txt`, `tests/all-sources.txt`.
- `data/manual/index.html` - a Carla Rack / Carla Patchbay paragraph: what it is for, that a build without a runtime refuses it
  and keeps the project's XML, and that a VST3 goes to Zene's own VST3 host.
- `docs/050-features/Q050-359.md` - the feature record.

No command was added or changed; no project format changed.

## Self-check

**Each new test FAILS with the product change reverted, then PASSES restored.** Reverse step: `git diff --
plugins/CarlaRack/CarlaRack.cpp plugins/CarlaPatchbay/CarlaPatchbay.cpp` into `logs/product-q050-359.patch`, `git apply -R`,
rebuild `carlarack carlapatchbay` (`logs/build-6-red-modules.log`, exit 0), `ctest -R '^CarlaRackNoRuntimeTest$'
--output-on-failure` (from `build/tests`) → **exit 8**, `CarlaRackNoRuntimeTest (SEGFAULT)`, `Received signal 11 (SIGSEGV) ...
accessing address 0x48`, frame `lmms::CarlaInstrument::CarlaInstrument(...)+1205: call *0x48(%rax)`. Restore: `git apply`,
rebuild (`logs/build-7-restore-modules.log`, exit 0), same `ctest` → **exit 0**, 1/1 passed; verbose run
(`logs/test-green-2.log`) shows `Totals: 6 passed, 0 failed, 0 skipped`. The test binary was not rebuilt between the two runs:
only the two product modules changed, and they are what the test loads.

**Every acceptance criterion is proven by a test I ran.** Criterion 1: `CarlaRackNoRuntimeTest` exit 8 red / 0 green, plus
the out-of-test reproduction `logs/repro-2.log` (SIGSEGV) and `logs/repro-3-after-fix.log` (typed refusal, alive).
Criterion 2: the same four cases; the refusal message carries the native-VST3 route, and `plugin.list` still offers the device,
so the refusal - not a hidden row - is what a user meets.

**One undo step per edit; honest reversibility.** No command was added or changed, so no reversibility row moved (the snapshot
check below is the proof nothing did): `plugin.load` keeps `snapshot`, and a *refused* load journals nothing - the test compares
`control.transactions` before and after the refusal and requires the same length
(`CarlaRackNoRuntimeTest::pluginLoadRefusesWithoutTheRuntime`); `plugin.list` stays `not_mutating`.

**Nothing added runs on the audio thread.** My diff touches no audio-path function: the guard runs inside
`lmms_plugin_main`, once, on the thread that instantiates the device (GUI thread or project loader), before any
`CarlaInstrument` exists; `CarlaInstrument::playImpl` and the MIDI hand-off are untouched. The FLAGGED lines from the check
(`ConvolutionReverbEffect::loadImpulse/buildKernel`, `LadspaEffect::pluginInstantiation`, `AudioEngine::removeAudioBusHandle`)
are **not mine** - they come from files this branch inherits from `050/m0-train-35`; `git show --stat c0ab4e002d 0e6dc10682`
lists six files and none of them is one of those.

**Old projects load and render as before; new data round-trips.** No format change and no core code touched.
`CarlaRackNoRuntimeTest::aSavedCarlaRackProjectKeepsItsXml` opens a project whose `<instrument>` names `carlarack` (with a
Carla state element inside), gets the Q050-018 placeholder (`dummyinstrument`), saves again and requires the instrument
element to be byte-identical to the file's own (exit 0). Neighbours that drive the same modules and the catalogue pass:
`ctest -R '^(CarlaPlayImplTest|CarlaParamFloatModelTest|ControlDeviceCatalogueTest)$'` → **exit 0**, 3/3
(`logs/test-neighbours.log`).

**Completeness - real user path and agent path.** Agent: `plugin.list` → `plugin.load dev-<n>` → typed `refused` with the
message, track left with the placeholder. User: a track's device card → **Replace Instrument...** →
`src/gui/shell/DeviceActionMenu.cpp:131` → the real `DevicePicker` (query `Carla Rack`, row shows it, `selectedDevice()` is
`plugin.list`'s id) → `dispatch(... "plugin.load" ...)` → the same refusal, app alive. Edge cases tried: the Patchbay sibling
(same host path, its own null descriptor); a project that names Carla Rack through open + save (reopen after save); a build
with a real runtime (the cases report Skipped, as `CarlaVst2HostingTest` does from the other side); a missing `carlarack`
module (`QSKIP`, naming it).

**The GUI test drives the visible control, and the same registered command ran.** The case triggers the real `QAction` from
the device card's menu (`lmms::test::menuAction(menu, {"Replace Instrument..."})`), answers the modal `DevicePicker` the action
opens, and asserts `picker->selectedDevice()` equals the `dev-<n>` `plugin.list` reports - so the accept went through
`runCommand` → `ControlRegistry::invoke("plugin.load", ...)`; nothing writes the model directly. Undo depth does **not** grow
here because a refused command journals no step (asserted on the agent case); what proves the command ran is the track's
instrument becoming the placeholder (only `plugin.load` does that), and the announcement the GUI path makes is in the verbose
log: `QWARN : CarlaRackNoRuntimeTest::theDeviceCardRefusesWithoutTheRuntime() plugin.load refused: the engine could not load
instrument 'carlarack': this build has no Carla runtime, so Carla Rack cann...` (`logs/test-green-2.log`).

**Portability.** Plain `nullptr` comparisons, `QStringLiteral`, `pluginload::noteInstantiationFailure`; no Qt-6-only API, no
`std::filesystem`, no new POSIX call, none of `small/near/far/interface/min/max` as identifiers. The test's only Qt API surface
is `QLibrary::resolve` through `PluginFactory::PluginInfo` (Qt 5.15 has it) plus QtTest/QtWidgets. This box builds Qt 6 only,
so CI-only compile errors cannot be seen here.

**Command metadata / snapshot.** No command id, schema or description changed. The brief's
`research/reviews/m1-session-2026-10-12/scratch/m0/snapshot_live.py` does not exist in this checkout (`research/` is absent), so
nothing was regenerated; instead the committed snapshot was checked against a live instance:
`ctest -R '^ControlCommandsSnapshot$'` → **exit 0** (`logs/test-commands-snapshot.log`), and `python3 tests/doc-figures-gate.py`
→ **PASS / exit 0** (935 ids / 78 groups).

**Gates.** `python3 .../feature_check.py <worktree>` → **RESULT: PASS**, exit 0 - fork-sources, all-sources-reproduce
(`REPRODUCES`, exit 0), no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length (no new
item in my files). `submit_check.py` was run before this report and after it (see the last run below).

**Proof manifest.** `/home/kruzzzzy/.config/kads/zene/state/runs/Q050-359.r1/logs/proof.json`: unit `Q050-359`, this worktree,
commit `c0ab4e002d712213ce105f40bdcce9ca7b3ecfa2`, a one-sentence claim, and a `regression-test` artifact for
`tests/src/gui/CarlaRackNoRuntimeTest.cpp` with `red_exit` 8 and `green_exit` 0.
