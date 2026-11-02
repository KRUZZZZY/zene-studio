IMPLEMENTED

# Q050-481.1 - Host VST2 instruments through Carla (stage 1/4)

Branch `feat/050-Q050-481.1-oc` (base `050/m0-train-24`, `2a7fd9f9c7`). Feature record:
`docs/050-features/Q050-481.1.md`. Logs (numbered) in
`~/.local/share/zene-bunny-fix/qwen/runs/Q050-481.1.r1/logs/`.

A new `carlavst2` module hosts each VST2 instrument in its own Carla Rack (native in process, Windows through Carla's
Wine bridge), found by Carla's out-of-process discovery tools; `plugin.list` has a `vst2` format, the device picker
and instrument browser show it, `plugin.load` and the device card's Replace Instrument... load it; parameters,
automation, presets, project state (with the plug-in's chunk) and the plug-in's own editor work through Carla.
Proof is one QtTest, `CarlaVst2HostingTest`, against an in-tree VST2 fixture (native + Windows PE) and a real Carla
runtime built from the pinned submodule by `tools/build-carla-runtime.sh`.

## Acceptance

Command for every row: `ctest -R '^CarlaVst2HostingTest$' -V` from `build/tests` (final tree: exit 0, 10/10,
logs `78-ctest.log` and `87-final-ctest.log`; three runs in a row at load ~60: exit 0, logs `72-rerun-{1,2,3}.log`).

| Criterion | Test (CarlaVst2HostingTest::) | Exit |
| --- | --- | --- |
| 1. VST2 instruments appear in the GUI picker and plugin.list and load through plugin.load. | `vst2InstrumentsAreListed` (plugin.list vst2 entries, native + win64, last in catalogue); `theDeviceCardReplacesTheInstrumentWithAVst2` (triggers the device card's Replace Instrument..., picks the VST2 row in the device picker, loads through plugin.load); `pluginLoadHostsTheNativeInstrumentAndItPlays` | 0 |
| 2. The instrument editor, parameter automation, presets, and project state save/load work through Carla. | `theInstrumentsOwnEditorOpensThroughCarla` (plugin.editor_open/close + clicking the view's editor button; the plug-in reports its editor open/closed), `automationDrivesAVst2Parameter` (automation.add_point drives the plug-in in playback), `presetsSaveAndLoad`, `projectStateRoundTripsThroughCarla` (binary, parameter, chunk-only state and rendered output identical after save/load), `pluginLoadHostsTheNativeInstrumentAndItPlays` (param_set + undo) | 0 |
| 3. An in-tree test VST2 module proves native loading and a Wine-loadable fixture proves the bridge path. | native cases above; `theWindowsFixtureRunsThroughTheWineBridge` (PE fixture discovered through Wine, loaded in Carla's Wine bridge, param_set, render, state round trip) | 0 |

Revert proof: non-test hunks reversed (`git apply -R` of the implementation commit's product paths), stale module
removed, rebuilt: **exit 8**, 8/8 hosted cases FAIL (`66-revert2-ctest.log`; first revert run `58-revert-ctest.log`,
exit 8). Restored and rebuilt: **exit 0** (`69`..`72`, `78`).

## What is not proven here (honest limits)

- Bridged **chunk restore**: Carla passes the chunk to its bridge through `/var/tmp`, which this sandbox cannot write
  (`touch /var/tmp/x` -> Permission denied). The test checks it wherever that directory is writable and logs the
  reason otherwise; bridged chunk *save*, parameters, render and state round trip are checked. Native chunk restore is
  checked.
- The Wine-bridged plug-in's **editor** (a Carla Wine window) is not exercised.
- Hosted CI has no Carla runtime: there the test reports Skipped. It fails (does not skip) whenever a runtime and the
  fixture exist and the VST2 host does not work.
- One bridged run (`69-final-green.log`, right after a full rebuild on a loaded box) rendered silence; after the test
  started a persistent wineserver for its prefix, 5 further runs passed (`72-rerun-{1,2,3}`, `76`, `78`), plus the final run `87`.
- Found while testing, upstream Carla (submodule, not changed): water's `removeEllipsis` corrupts the heap when the
  library's directory path contains `..` (release runtime + glibc malloc checker: clean path exit 0, `..` path 139;
  logs `84`, `85`). The test environment uses a normalised path.

## Self-check

`python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> Q050-481.1` and `feature_check.py` both try to
create `<worktree>/../zene-feat-q050-481.1-feature-check/`, outside this lane's write-jail (PermissionError). The same
gates were run with an identical copy of `feature_check.py` whose only change is the log directory (`/tmp/q481-fc`):
fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures PASS (exit 0);
complexity and file-length PASS (no new item in my files) - `RESULT: PASS`, exit 0 (`79-feature-check.log`).

- **Each new test fails with the product change reverted, passes restored?** Yes: reverted exit 8 (8/8 hosted cases
  FAIL), restored exit 0 (10/10). Commands and logs above.
- **Every acceptance criterion proven by a test I ran?** Yes, table above; every row `CarlaVst2HostingTest`, exit 0.
- **One undo step per edit; honest reversibility classes?** No new command and no class changed. `plugin.param_set`
  on a VST2 parameter is one journalled step: the test sets it, renders, `control.undo`, and reads/renders the old
  value. `plugin.load` stays `snapshot` (instrument replacement), `plugin.list` `not_mutating`.
  `ReversibilityContractTest` passes.
- **Nothing new on the audio thread allocates/frees/locks/waits/does I/O/emits?** Yes. Audio path is the unchanged
  `CarlaInstrument::playImpl`. Discovery (QProcess), `carla_add_plugin`, state/preset I/O, editor calls and
  `ui_idle` run on the GUI/control thread; `CarlaVst2Instrument::engineCallback` only stores two relaxed atomics.
  Carla's own process call for a bridged plug-in waits on its bridge (Carla's design; the same holds for the
  RemoteVstPlugin path stage 4 removes). TSan does not run here: reasoned, not measured.
- **Old projects load and render as before; new data round-trips?** Projects without VST2-through-Carla are
  untouched: no format change, Carla Rack/Patchbay behaviour unchanged (all new `CarlaInstrument` behaviour is off by
  default; `CarlaParamFloatModelTest`, `CarlaMidiHandoffTest`, `MissingDevicesPreserveTest` pass). New data
  round-trips: `projectStateRoundTripsThroughCarla` and the bridged round trip (exit 0). Converting VeSTige / VST-effect
  projects is stage 3.
- **Portability?** No Qt-6-only API (QProcess, QDir::listSeparator, Qt::SkipEmptyParts are Qt >= 5.14);
  `dlfcn.h`/`dladdr` behind `#ifndef LMMS_BUILD_WIN32` (Windows uses `carla_get_library_folder`); no
  `std::filesystem`; no `small/near/far/interface/min/max` identifiers; the fixture is C11, clean under
  `gcc -Wall -Wextra -Wpedantic -Werror` and `clang -Wall -Wextra -Werror` (MSVC builds at `/W2`, which does not raise
  the CRT deprecation warnings); the test's plug-in-directory symlinks are POSIX-only (Windows uses the build
  directory; every case skips there for want of a runtime); the PE fixture build is best effort and never fails the
  build.
- **Commands snapshot / canary?** Regenerated live with `snapshot_live.py` (878 ids / 77 groups, `55-snapshot.log`);
  `tests/doc-figures-gate.py` PASS; `ControlCommandsSnapshot` passes.
- **GUI and agent through the same command?** Loading: device card Replace Instrument... -> device picker ->
  `plugin.load` (test triggers the action). Editor: the view's button sends `plugin.editor_open` (test clicks it) and
  the agent sends the same command. Parameters/presets/automation: existing GUI controls and commands.
- **Registration?** New files in `tests/fork-sources.txt`; changed inherited files in
  `tests/upstream-modifications.txt` with reasons; `tests/all-sources.txt` re-derived (REPRODUCES); the test is in
  `tests/CMakeLists.txt` with `add_dependencies` on `carlavst2`, `tripleoscillator` and both fixtures.
- **Docs?** `data/manual/index.html` (VST2 through Carla paragraph), `docs/050-features/Q050-481.1.md`.
- **Working tree clean?** Yes after the final commit (`git status --porcelain` empty).
