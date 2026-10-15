# Cluster new-p10-3 — the CLAP instrument view keeps stale output-channel information

**Verdict: FIXED**

## Finding
`plugins/ClapInstrument/ClapInstrumentView.cpp:75` built the audio-output channel-count
label once, from `plugin->busLayout().outputs`. `ClapInstrument::reprepare()`
(`plugins/ClapInstrument/ClapInstrument.cpp:203`) already re-reads a changed `clap.audio-ports`
layout and rebuilds the transport, but nothing told the view, so the label kept the old count.

Real: a CLAP plug-in may rescan its audio ports and ask `clap_host.request_restart` while it
runs; the host's poll answers with `reprepare()`, which re-reads the layout and increments
`HostedPlugin::layoutRevision()` (`plugins/ClapEffect/ClapHost.cpp:107-138`,
`ClapHostInternals.h:399-404`). Audio routing followed the new layout; the surface did not.

## Fix (behaviour-preserving otherwise)
- `plugins/ClapInstrument/ClapInstrument.h:135-137` — new signal `outputLayoutChanged()`.
- `plugins/ClapInstrument/ClapInstrument.cpp:218-228` — `reprepare()` emits it after
  `engine->doneChangeInModel()` exactly when the layout revision changed.
- `plugins/ClapInstrument/ClapInstrumentView.cpp:75-88` — the label text moved into
  `updateOutputs`, called once at construction and again on the signal (re-`setFixedSize` so a
  wrapped label resizes). No other behaviour changes.

## Regression test
New `tests/src/plugins/ClapInstrumentViewTest.cpp` (target `ClapInstrumentViewTest`, registered
in `tests/CMakeLists.txt` and `tests/all-sources.txt`). It compiles the real instrument module
(the same shape as `ClapEffectIntegrationTest`), builds the real view, and drives the runtime
layout change through the in-tree fixture. The fixture gained a runtime layout mode
(`tests/data/clap-test-plugin/clap-test-instrument.c`): with `CLAP_TEST_INSTRUMENT_LAYOUT` set,
the single-output class reports a second stereo output port and announces it from `activate()`
with `clap_host_audio_ports.rescan` + `request_restart`, the CLAP way. Unset, the fixture is
unchanged (all other CLAP/instrument tests stay green).

## Evidence

| step | command | result |
|---|---|---|
| RED (unfixed product) | `cmake --build build --target ClapInstrumentViewTest` then `ctest -R ClapInstrumentViewTest --output-on-failure` | `FAIL! ... 'label->text().contains("4 channel")' returned FALSE (the audio-output label went stale: Audio output: 2 channel(s)...)`, ctest EXIT=8 |
| GREEN (fixed) | rebuild + same ctest | `1/1 Test #486: ClapInstrumentViewTest ... Passed`, ctest EXIT=0 |
| proof (revert product patch) | `git diff -- plugins/ClapInstrument/... > fix.patch; git apply -R fix.patch`; rebuild; `ctest -R ClapInstrumentViewTest` | BUILD EXIT=0, ctest EXIT=8 (FAIL, same stale label) |
| proof (reapply) | `git apply fix.patch`; rebuild; `ctest -R ClapInstrumentViewTest` | BUILD EXIT=0, ctest EXIT=0 (Passed) |
| family | `ctest -R "Clap\|InstrumentSetupUiTest\|InstrumentLatencyPdcTest\|ControlDeviceCatalogueTest\|InstrumentLayersCommandsTest" --output-on-failure` | 9/9 passed, EXIT=0 (`ClapBusMapTest`, `ClapHostTest`, `ClapLoaderErrorTest`, `ClapEffectIntegrationTest`, `ClapInstrumentViewTest`, `InstrumentSetupUiTest`, `InstrumentLatencyPdcTest`, `ControlDeviceCatalogueTest`, `InstrumentLayersCommandsTest`) |

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-3.r1/logs/`
(`test-1-red.log`, `test-2-green.log`, `test-6-reverted.log`, `test-7-reapplied.log`,
`test-5-family-all.log`, `build-*.log`).

Note: `ControlDeviceCatalogueTest` and `InstrumentLayersCommandsTest` first failed because the
`patman`/`tripleoscillator` modules were not built in this worktree; after
`cmake --build build --target patman tripleoscillator` they pass (EXIT=0). They are unrelated to
this change.

## Gates (unpiped exit codes)

| gate | command | exit |
|---|---|---|
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | **1** |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | **1** |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| unregistered-tests | `bash tests/unregistered-tests-gate.sh` | 0 |
| upstream regression | `bash tests/no-upstream-regression-gate.sh` | 0 |

Both required ratchet gates are RED **before this change too**: they were re-run on the pristine
tree (all working-tree changes reverse-applied with `git apply -R /tmp/opencode/all-changes.patch`),
and they fail with the identical verdicts (`complexity` EXIT=1, 6 regressions; `file-length`
EXIT=1, 14 regressions; logs `gate-1b-complexity-pristine.log`, `gate-2b-filelength-pristine.log`).
Every reported regression is in a file this change does not touch (e.g.
`src/core/TakeLane.cpp`, `src/core/SmfInterchangeReader.cpp`, `tests/src/gui/XyPadPairTest.cpp`);
none is a `plugins/ClapInstrument/*` file. This is pre-existing baseline drift on the branch,
not a result of this fix. No gate limit or baseline was re-anchored.

## Files committed
- `plugins/ClapInstrument/ClapInstrument.h`
- `plugins/ClapInstrument/ClapInstrument.cpp`
- `plugins/ClapInstrument/ClapInstrumentView.cpp`
- `tests/data/clap-test-plugin/clap-test-instrument.c`
- `tests/src/plugins/ClapInstrumentViewTest.cpp` (new)
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-3.md` (this file)
