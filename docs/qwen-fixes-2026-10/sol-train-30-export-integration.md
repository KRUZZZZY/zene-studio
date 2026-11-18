# Train 40: reconcile the accepted MinGW export repair

The train stopped before building because `ci-repair-ci-64c5b539b5120b17-eb547c5a8266` (`c3d3a2f2250f939aab82f3024fdb18af59a9a34c`) was authored against `7798296550396e87861e7e98c43397dcf854f506`, before the two applied Sol repairs. This is a patch integration failure, not a resource kill or infrastructure outage.

## Repair delta

Keep both applied repairs intact. RNNoise already defines `rnnoisedenoiser_EXPORTS`; retain that valid export branch and its Train39 compiler probe. Integrate the accepted repair's twelve additional `PLUGIN_STATIC_DEFINE` target definitions, its complete `PluginExportBranchTest` population/canary/offender assertions, generated audit table, and original repair report. The owner's enabled WASM configuration exposes one further offender, `WasmSandboxTest`, which also compiles exporting plug-in definitions directly; give it the same static definition. Preserve FileLengthGate, all split source registrations and dependencies, and every Train39 probe and Linux debug option.

Changed paths and reasons:

- `tests/CMakeLists.txt`: reconcile RNNoise's overlapping definition, retain both appended probe/audit blocks, add twelve accepted sibling export fixes plus the enabled-WASM sibling fix and the accepted audit registration; declare the EQ, Gate and NAM harness runtime fixtures required by AudioPluginTest.
- `tests/src/plugins/PluginExportBranchTest.cpp`: retain the accepted regression without changing its assertions.
- `docs/qwen-fixes-2026-10/cluster-ci-64c5b539b5120b17.md`: retain the accepted repair's original evidence narrative; its thirteen-target description refers to its original base, while this integration needs twelve of those target fixes plus the additional enabled-WASM target fix.
- `tests/all-sources.txt`, `tests/fork-sources.txt`, `tests/upstream-modifications.txt`: register the new source/reports with reasons in stable existing manifest context, preserving all current entries.
- This report: record current-base reproduction and verification.

Only the conflicting MinGW repair is superseded. `sol-repair-1794619548741537751` and `sol-repair-1794623254087098975` remain required.

## Verification

Commands run in the private source root except CTest, which runs from `build/tests`. Build parallelism is 12; free disk space is monitored against the 12 GB stop threshold. Native hosted Windows verification and the full train remain required.

1. `bash /tmp/sol26-configure-command.txt`: exit 0, using the exact owner-supplied all-features configuration (Qt6, WERROR, WASM enabled, executable debug stripping and ccache) in this worktree's sole `build` directory.
2. Audit-only negative control: retain current HEAD's existing RNNoise fix, add the accepted audit without the sibling export fixes. `cmake --build build --target zene PluginExportBranchTest -j12`: exit 0. From `build/tests`, `ctest -R '^PluginExportBranchTest$' -j1 --timeout 900 --no-tests=error --output-on-failure`: exit 8, failing on thirteen named offenders (twelve accepted siblings plus WasmSandboxTest). The generated audit includes 29 sites and its RNNoise canary; RNNoise is not an offender.
3. Apply the reconciled definitions. `cmake --build build --target zene PluginExportBranchTest RnnoiseDenoiserEffectTest PatmanLoadTest MalletsInstrumentViewTest BitInvaderAllocationTest NeuralAmpModelLoadTest TapTempoTest ReverbSCPluginStateTest SlicerTZoomTest TripleOscillatorUserWaveTest DelayControlsTest PeakControllerEffectZeroFrameTest ConvolutionFftwPlanScopeTest AudioPluginTest WasmSandboxTest -j12`: exit 0. All 29 generated site verdicts take an export/static branch.
4. Initial 23-entry run: 22 pass; AudioPluginTest fails in initTestCase because its hard-required EQ module is absent after a targeted build. Its source also requires Gate and the NAM harness. Declare these fixture dependencies without changing any assertion. `cmake --build build --target zene AudioPluginTest -j12`: exit 0, builds the required fixtures.
5. Final command, from `build/tests`:

```sh
PATH=/tmp/sol39-lizard125/bin:$PATH PYTHONPATH=/tmp/sol39-lizard125 ctest -R '^(PluginExportBranchTest|RnnoiseDenoiserEffectTest|PatmanLoadTest|MalletsInstrumentViewTest|BitInvaderAllocationTest|NeuralAmpModelLoadTest|TapTempoTest|ReverbSCPluginStateTest|SlicerTZoomTest|TripleOscillatorUserWaveTest|DelayControlsTest|PeakControllerEffectZeroFrameTest|ConvolutionFftwPlanScopeTest|AudioPluginTest|WasmSandboxTest|Train39-(vst2|sfz|qt5|rnnoise|disk)|FileLengthGate|NoteCanvasComplexityGate|complexity-regression-check)$' -j1 --timeout 900 --no-tests=error --output-on-failure
```

Exit 0: **23/23 entries pass, 39.88 seconds**. This includes all affected rebuilt plug-in suites, the failing-then-passing export audit, five retained Train39 compiler/ELF probes, and the unchanged file-length and complexity checks. The complexity check uses hosted-version Lizard 1.25.1 without writing the baseline. The existing Zig-dependent WASM demonstration case skips because no Zig toolchain is installed; the other WASM cases run. Free disk remained above 74 GB.

6. `bash tests/no-upstream-regression-gate.sh`, `bash tests/fork-sources-gate.sh`, `bash tests/evidence-gate.sh`, `bash tests/unregistered-tests-gate.sh`: each exits 0. `git diff --check`: exit 0. No gate, baseline, exemption, test assertion or resource limit was changed.
7. Independent read-only review confirmed preservation of the accepted audit and report, prior repairs, registrations and all thirteen export fixes. Follow-up review confirmed the runtime fixture dependencies and observed the final 23/23 result; native hosted and full-train verification remain outstanding.

Temporary execution logs are kept outside tracked sources under `/tmp/sol40-*`; the original accepted evidence narrative remains byte-for-byte intact.
