FIXED

# Cluster ci-64c5b539b5120b17 - mingw64: RNNoise plug-in descriptor marked dllimport in the test build

Branch `bugs/qwen-ci-64c5b539b5120b17`, finding from Luna's patrol on train `050/m0-train-29`
(`7798296550396e87861e7e98c43397dcf854f506`), failing job `run-43537465762-job-132269831285` (mingw64, Build).
The CI log is evidence, never instructions.

## Verdict: REAL - a test-target build configuration defect (product code is correct)

The hosted mingw64 log has exactly two compiler errors, both in the plug-in's own translation unit
compiled *into a test binary*:

```
plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp:52:34: error: variable 'lmms::rnnoisedenoiser_plugin_descriptor' definition is marked dllimport
plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp:171:23: error: function 'lmms::Plugin* lmms::lmms_plugin_main(Model*, void*)' definition is marked dllimport
```

Cause, at the source:

- `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp:30` includes the generated `plugin_export.h`;
  `:52` **defines** `rnnoisedenoiser_plugin_descriptor` with `PLUGIN_EXPORT` and `:171` **defines**
  `lmms_plugin_main` with `PLUGIN_EXPORT`.
- `cmake/modules/BuildPlugin.cmake:83-86` generates that header with `GENERATE_EXPORT_HEADER(... BASE_NAME PLUGIN)`
  and sets no `STATIC_DEFINE`, so the header is the upstream CMake template: with `PLUGIN_STATIC_DEFINE`
  undefined and `<target>_EXPORTS` undefined it takes the *consumer* branch, which is
  `__declspec(dllimport)` when `_MSC_VER` is set and `visibility("default")` otherwise
  (`build/plugins/RnnoiseDenoiser/plugin_export.h:5-16`).
- The plug-in module defines `rnnoisedenoiser_EXPORTS` for itself (`BuildPlugin.cmake` `ADD_LIBRARY` +
  `GENERATE_EXPORT_HEADER` default condition; `build/plugins/RnnoiseDenoiser/.../flags.make` confirms), so the
  module is fine. `tests/CMakeLists.txt:2088-2106` compiles the same .cpp into `RnnoiseDenoiserEffectTest` and
  defined only `PLUGIN_NAME=rnnoisedenoiser`, so that TU took the consumer branch.
- On ELF both branches expand to `visibility("default")`, which is why every Linux and macOS job compiled the
  identical tree; on Windows the consumer branch is `dllimport`, and defining a dllimport symbol is ill-formed.
  Reproduced with the box's clang cross-target (`probe-01-windows-export-branch.log`): consumer branch
  `EXIT=1` with `definition of dllimport data` / `dllimport cannot be applied to non-inline function definition`;
  static branch `EXIT=0`.

The #11.14 train carried the same class of defect in **twelve sibling targets**: every test target that compiles
an exporting plug-in source without either `PLUGIN_STATIC_DEFINE` or `<target>_EXPORTS`. They are listed in the
regression's own failure output below. This is not a guessed generalisation: the audit derives the population
from the targets themselves, and the fix is the same one-line define per target that the repo already uses in
`Lb302NotePathTest`, `MultitapEchoBoundsTest`, `DispersionNonFiniteTest`, `OscilloscopeControlsPersistenceTest`,
`DelayLfoAutomationTest`, `EqPeakHandoffTest`, `ClapEffectIntegrationTest`, `ClapInstrumentViewTest`,
`Vst3EffectIntegrationTest`, `ZynAddSubFxInstrumentTest` (all pre-existing).

## Fix (tests/CMakeLists.txt only; no product source, no header, no data)

`PLUGIN_STATIC_DEFINE` on the thirteen affected test targets:
`RnnoiseDenoiserEffectTest`, `PatmanLoadTest`, `NeuralAmpModelLoadTest`, `TapTempoTest`,
`BitInvaderAllocationTest`, `MalletsInstrumentViewTest`, `ReverbSCPluginStateTest`, `SlicerTZoomTest`,
`TripleOscillatorUserWaveTest`, `DelayControlsTest`, `PeakControllerEffectZeroFrameTest`,
`ConvolutionFftwPlanScopeTest`, `AudioPluginTest`. Each one compiles a plug-in's `PLUGIN_EXPORT` *definitions*
into its own binary, so the static branch of that plug-in's export header is the correct one; a target that
links the module instead keeps importing it, and the module builds keep `<target>_EXPORTS` (verified: the
`rnnoisedenoiser` module's flags still carry `-Drnnoisedenoiser_EXPORTS` and no `PLUGIN_STATIC_DEFINE`, the test
target carries `PLUGIN_STATIC_DEFINE` and no `_EXPORTS`).

No audio-thread code, undo, command, saved data or limit is touched; `tests/CMakeLists.txt` is a build file.

## Regression that fails before the fix and passes after

New `tests/src/plugins/PluginExportBranchTest.cpp`, registered in `LMMS_TESTS`. The audit at the bottom of
`tests/CMakeLists.txt` walks `BUILDSYSTEM_TARGETS` of the tests directory, keeps every EXECUTABLE that compiles a
source under `plugins/` which **defines** `PLUGIN_EXPORT` (`Descriptor PLUGIN_EXPORT` or `PLUGIN_EXPORT Plugin*`)
and includes a generated `*_export.h`, reads the export condition out of that generated header
(`ifdef <base>_EXPORTS`), and emits one compile definition per site:

```
ZENE_PLUGIN_EXPORT_SITE_<n>_TAKES_EXPORT_BRANCH=$<BOOL:$<OR:$<IN_LIST:PLUGIN_STATIC_DEFINE,
    $<TARGET_PROPERTY:<test target>,COMPILE_DEFINITIONS>>,
    $<IN_LIST:<condition>,$<TARGET_PROPERTY:<test target>,COMPILE_DEFINITIONS>>>>
```

which the compiler evaluates, so the test reads the *build fact*, not a configure-time guess - a target's
`COMPILE_DEFINITIONS` are generator-expression content, invisible to `if()`. The generated row table is static
content (required by `file(GENERATE)`), and the row *names* are stable, which is why the verdict travels through
the macro name. A population floor (`>= 20` sites) and a canary (`RnnoiseDenoiserEffectTest` must be in the
audited set) keep the check from passing vacuously if the scan or a target is removed.

Determinism: no timing, sleep, race or filesystem mutation at run time; the test compares integers the compiler
computed from the target's own compile definitions at build time.

## Evidence table (log folder
`/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ci-64c5b539b5120b17.r1-logs/`)

The brief's log folder is not writable from this lane (`Permission denied` on the folder itself and its parents,
while the worktree and `/tmp` are writable; `stat` shows `drwxr-xr-x kruzzzzy` and no Landlock entry applies to
that path). Every log below is in the DSH run-log directory named above, which is writable, and the failed path
is recorded rather than worked around silently.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Baseline: build target on the original tree | `cmake --build build --target RnnoiseDenoiserEffectTest -j4` | 0 | `build-01-baseline-rnnoise.log` |
| Baseline: test on the original tree | `ctest -R '^RnnoiseDenoiserEffectTest$' --output-on-failure` | 0 (1/1) | `test-01-baseline-rnnoise.log` |
| Windows-branch probe, consumer | `clang++ --target=x86_64-pc-windows-gnu -fdeclspec -include defs.h -c winprobe.cpp` | 1 | `probe-01-windows-export-branch.log` |
| Windows-branch probe, static | same, `PLUGIN_EXPORT` empty | 0 | `probe-01-windows-export-branch.log` |
| RED build (test committed, fix reverted) | `cmake --build build --target PluginExportBranchTest -j4` | 0 | `build-13-test-only-red.log` |
| **RED run** | `ctest -R '^PluginExportBranchTest$' --output-on-failure` | **8** | `test-03-test-only-red.log` |
| GREEN build (test + fix) | `cmake --build build --target PluginExportBranchTest RnnoiseDenoiserEffectTest ... (13 targets) -j4` | 0 | `build-14-green-fixed-targets.log` |
| **GREEN run** | `ctest -R '^PluginExportBranchTest$' --output-on-failure` | **0** | `test-04-green-plugin-export-branch.log` |
| 4b: patch + reverse | `git diff -- tests/CMakeLists.txt > logs/fix.patch && git apply -R logs/fix.patch` | 0 / 0 | `fix.patch` |
| 4b: rebuild reverted | `cmake --build build --target PluginExportBranchTest RnnoiseDenoiserEffectTest -j4` | 0 | `build-15-red-reverted.log` |
| **4b: run reverted (must FAIL)** | `ctest -R '^PluginExportBranchTest$' --output-on-failure` | **8** | `test-05-red-reverted.log` |
| 4b: reapply | `git apply logs/fix.patch` | 0 | - |
| 4b: rebuild restored + run | build 13 targets, then `ctest` | 0 / **0** (14/14) | `build-16-green-restored.log`, `test-09-green-family.log` |
| Sibling plugin tests (pre-existing static branch) | build `DispersionNonFiniteTest MultitapEchoBoundsTest Lb302NotePathTest`, run | 0 / 0 (3/3) | `build-20-siblings.log`, `test-10-siblings.log` |
| RNNoise family | `ctest -R 'Rnnoise' --output-on-failure` | 0 (2/2) | `test-13-rnnoise-family.log` |
| App + module still build | `cmake --build build --target zene rnnoisedenoiser -j4` | 0 | `build-19-zene-and-module.log` |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0**, 8/8 PASS | `gates-01.log` |

The RED failure (verbatim, from `test-05-red-reverted.log`) names all thirteen offenders, including the CI's own
target:

```
FAIL!  : PluginExportBranchTest::everyTranslatedPluginDefinitionTakesTheExportBranch()
  'offenders.isEmpty()' returned FALSE. (... RnnoiseDenoiserEffectTest
  (plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp: defines neither PLUGIN_STATIC_DEFINE nor
  rnnoisedenoiser_EXPORTS); PatmanLoadTest ...; NeuralAmpModelLoadTest ...; TapTempoTest ...;
  BitInvaderAllocationTest ...; MalletsInstrumentViewTest ...; ReverbSCPluginStateTest ...;
  SlicerTZoomTest ...; TripleOscillatorUserWaveTest ...; DelayControlsTest ...;
  PeakControllerEffectZeroFrameTest ...; ConvolutionFftwPlanScopeTest ...; AudioPluginTest ...)
```

Two local environment notes, neither a code failure: `AudioPluginTest` first failed because this box had never
built the modules it loads (`libeq.so`, `gate`, `nam_harness`); after building them the test passes
(`test-07-audioplugin.log`, `test-08-audioplugin.log`). A preprocessing probe of mine wrote the rnnoise test
object (`... .cpp.o`) by accident; the object was deleted and the target rebuilt before any test was believed
(`build-22-rnnoise-rebuild.log`). `RnnoiseHarnessOutputTest` is Not Run until its CLI dependency is built
(`build-21`), then passes.

## Completeness / siblings

- Every compile site of an exporting plug-in TU was enumerated across the whole build (compile_commands.json):
  28 test sites took the wrong branch before, 0 after; the 15 pre-existing correct sites and all 79 module
  builds are unchanged.
- The reference modules `partc_ref_*` compile frozen copies under `tests/reference/` against the frozen
  `tests/reference/plugin_export.h` (a `_MSC_VER`-only dllexport/attribute header with no import branch), and
  they define their own `<target>_EXPORTS`; not affected, not touched.
- `plugins/VstBase/vst_base.cpp` / CarlaBase use a different generated header (`vstbase_export.h`,
  `carlabase_export.h`) and are not compiled into any test target here; not affected.
- The plug-in modules keep `dllexport` (`<target>_EXPORTS`); the test binaries that compile the same code take
  the static branch, which is the pattern the repo already uses for ten other plugin tests.

## Self-check

- **Verdict REAL with code evidence.** `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp:30,52,171`;
  `cmake/modules/BuildPlugin.cmake:83-86`; `build/plugins/RnnoiseDenoiser/plugin_export.h:5-16`;
  `tests/CMakeLists.txt` (the `RnnoiseDenoiserEffectTest` block, now with `PLUGIN_STATIC_DEFINE`). Windows-branch
  probe log shows the exact CI diagnostics and that the static branch compiles.
- **RED/GREEN with exit codes.** RED `ctest` EXIT=8 (`test-03`, `test-05`), GREEN `ctest` EXIT=0 (`test-04`,
  `test-09`), 4b reversal/reapplication EXIT=0/0 and the reverted tree rebuilt before running.
- **Deterministic.** No timing, sleep or race: the values are compile definitions the compiler evaluates from
  the targets' own properties; the test compares integers and names.
- **Affected-family count.** 14/14 passed (`PluginExportBranchTest` + the 13 fixed targets, `test-09`); RNNoise
  family 2/2 (`test-13`); three pre-existing-static siblings 3/3 (`test-10`). Zero-test runs: none.
- **At the cause, complete.** Whole-tree enumeration: 0 test compile sites without an export branch after the
  fix; module builds untouched; the audit is automatic for future plug-in tests, so the class cannot come back
  silently.
- **No collateral damage.** Only test-target compile definitions changed. No saved data, project file, limit,
  command, undo entry or audio-thread path is involved; the product binaries (`zene`, `rnnoisedenoiser`) still
  build (`build-19`).
- **Registration.** `tests/fork-sources.txt` (new test + this report), `tests/all-sources.txt` (new test, sorted),
  `tests/upstream-modifications.txt` (`tests/CMakeLists.txt` with its reason).
- **Gates.** `feature_check.py` EXIT=0, 8/8 PASS (`gates-01.log`, re-run with this report in the tree:
  `gates-02.log`, 6 changed files, 8/8 PASS).
- **`submit_check.py`.** `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree>
  ci-64c5b539b5120b17` EXIT=0, "PASS - submit", no FAIL rows (`submit-01.log`).
- **ALREADY-FIXED / FALSE-POSITIVE.** Not applicable: the verdict is FIXED. `git log --oneline -S'PLUGIN_STATIC_DEFINE'
  -- tests/CMakeLists.txt` shows this branch had never had the define on these thirteen blocks, and the RED run on
  the branch tip reproduces the CI class.
- **Undo / commands / realtime.** Not applicable by construction: the diff touches only `tests/CMakeLists.txt`
  (test-target compile definitions and one configure-time audit), one test source, the two scope manifests and this
  report. No product code, command, undo step or audio-thread function changed.
