IMPLEMENTED

# Q050-481.3 - Open older VST2 projects as Carla devices

Legacy VST2 instruments and effects convert to Carla devices during project load, preserving saved state and automation where supported, with fallback to the legacy device when a binary is missing or Carla cannot load it. The saved project can be reopened and rendered against the old-host reference.

## Acceptance

Both rows: `ctest -R '^(CarlaVst2LegacyStateTest|LegacyVst2ConversionTest|LegacyVst2ProjectTest|CarlaVst2HostingTest)$' --output-on-failure` from `build/tests`, exit 0, 4/4 tests, [log 068](../../build/duet-logs/068-r3p3-green-ctest.log).

| Criterion | Tests | Evidence | Exit code |
| --- | --- | --- | --- |
| Fixtures convert with the same plug-in, automation, parameters and chunk state | `LegacyVst2ProjectTest::aLegacyProjectOpensAsCarla`, `theStartHubOpensALegacyProjectAsCarla`, `aChunklessLegacyProjectRestoresItsDump`, `LegacyVst2ConversionTest`, and `CarlaVst2LegacyStateTest` (6 cases) | Converted devices retain the fixture state and automation; the parameter dump applies only up to the plug-in’s real parameter count. | 0 |
| Saves, reopens and renders against its old-host reference | `aConvertedProjectSavesReopensAndRenders`, `theOldHostRendersTheReference`, and generator `writeTheFixtureWithTheOldHost` (log 022) | The round trip renders against the old-host reference. | 0 |

## Round 2 changes

Commit `ce518841c` adds namespace end comments and splits functions to reduce CCN: `applyProgram`/`parseDumpEntry`, `findHostDescriptor`/`findMatchingKey`, and `CarlaVst2Instrument::loadLegacySettings`. These changes do not change behaviour.

## Round 3 changes

Review r2 found that `applyLegacyState` trusted the project’s `numparams`: an extra valid-index entry could overwrite a restored parameter, and a huge count looped without bound. Commit `acfd081ad` caps iteration at Carla’s loaded parameter count (`calls.parameterCount()`, read once); a negative or non-numeric count applies nothing. It also adds the hermetic QtTest `CarlaVst2LegacyStateTest`, which compiles `plugins/CarlaBase/CarlaVst2Legacy.cpp` directly against a fake 2-parameter plug-in, with no Carla runtime and no QSKIP. The reviewer’s fix commit `1a8fa63635` was not on this branch, so this is our own implementation.

## Gates

The restored build exited 0 (log 067); ctest exited 0, 4/4 (log 068). `feature_check` exited 0 with all 8 PASS (log 069; base `c20ef591b`, 28 changed files). `submit_check` before this report exited 1 with only report FAILs (log 058).

## Self-check

1. **RED/GREEN.** Base `c20ef591b` is the merge-base with `050/m0-train-31`. The 15 non-test files listed in `build/duet-logs/red-product-files-r2.txt` were reversed with `git apply -R` using `build/duet-logs/q050-481.3-product-r2.patch`. The `/usr/bin/cmake -k` rebuild exited 2 (log 053): `LegacyVst2ConversionTest` and `LegacyVst2ProjectTest` could not compile, with `fatal error: LegacyVst2Conversion.h: No such file or directory`. The round-2 `LegacyVst2ConversionTest` RED was inconclusive (missing header, stale binary); the reviewer confirmed the conversion RED independently. Ctest exited 8 (log 054): `LegacyVst2ProjectTest` failed six of ten cases, with three passed and one skipped: `aLegacyProjectOpensAsCarla`, `theStartHubOpensALegacyProjectAsCarla`, `aConvertedProjectSavesReopensAndRenders`, `aChunklessLegacyProjectRestoresItsDump`, `aMissingBinaryKeepsTheLegacyDevice`, and `aBinaryCarlaCannotLoadFallsBackToTheLegacyReader`. It ran the test binary built before reversal, which could not recompile without the header, against core and plug-in libraries rebuilt without the product. The stale `LegacyVst2ConversionTest` binary passed in the same run because it carries its own copy of the converter. After `git checkout HEAD -- <files>` restored the files, the build exited 0 (log 055) and ctest exited 0, 3/3 (log 056).

   **Round 3.** Only the cap hunk was reverted (`git show HEAD~1:plugins/CarlaBase/CarlaVst2Legacy.cpp`). The test binary was rebuilt from the reverted source (log 065, exit 0), so it was not stale. Ctest exited 8 (log 066): `FAIL! : CarlaVst2LegacyStateTest::anOversizedDumpCannotOverwriteAParameter() Compared values are not the same` (actual 3 calls, expected 2), and timed out at 120 s in `aHugeCountStopsAtThePluginsParameters`; `QT_LOGGING_RULES='default.warning=false'` muted qWarning to bound the log. Restored, the build exited 0 (log 067) and ctest exited 0 with 4/4 (log 068).

2. **Per criterion.** Criterion 1 is covered by `LegacyVst2ProjectTest` cases `aLegacyProjectOpensAsCarla`, `theStartHubOpensALegacyProjectAsCarla`, and `aChunklessLegacyProjectRestoresItsDump`, plus `LegacyVst2ConversionTest` and `CarlaVst2LegacyStateTest`. Criterion 2 is covered by `aConvertedProjectSavesReopensAndRenders` and `theOldHostRendersTheReference`. Both exit 0 in log 068.
3. **Undo.** There is no new or changed command. Conversion runs inside the existing `project.open`, whose snapshot class is unchanged. Opening a project is not an edit and resets undo history, so it adds no undo step.
4. **Realtime.** Conversion runs on the GUI thread during project load in `InstrumentTrack`/`EffectChain` loadSettings. `CarlaVst2Instrument::loadSettings`/`loadLegacySettings` pause the VST2 worker with `m_renderer.pause()` before Carla calls and resume it afterward. `playImpl` is unchanged and does not wait. The submit check flags audio-path files because `CarlaVst2.cpp` contains `playImpl`; no line in `playImpl` or any render function changed. The cap runs at project load on the GUI thread while the VST2 worker is paused; no audio-path change. TSan cannot run on this box, so this is reasoned, not measured.
5. **Projects.** Projects without VeSTige or vsteffect never enter the converter: `convertInstrument`/`convertEffect` return null unless the name is vestige/vsteffect. The saved format is unchanged and the in-memory `<zene-legacy-vst2>` marker is never saved. `aMissingBinaryKeepsTheLegacyDevice` and `aBinaryCarlaCannotLoadFallsBackToTheLegacyReader` cover fallback; `CarlaVst2LegacyStateTest` covers oversized, huge, negative and non-numeric `numparams`. `aConvertedProjectSavesReopensAndRenders` saves, reopens and renders against the old-host reference: bar 1.5 = (0.840000033, 0.25), bar 3.5 = (0.840000033, 0.800000012).
6. **Completeness.** The GUI path is the Start hub's Recent item, whose Return goes through ControlRegistry `project.open` (`src/gui/StartHub.cpp:83,126,136`); the other tests use the agent path `project.open`. Edge cases include oversized, huge, negative and non-numeric `numparams`, a missing binary, a binary Carla cannot load, a chunkless dump, malformed dump entry `param2="9:Bogus:x"`, reopening after save, and automation clip id 23571 retargeted to the converted model.
7. **GUI test.** `theStartHubOpensALegacyProjectAsCarla` does `keyClick(Qt::Key_Return)` on `hub.recentList()`; the hub invokes the registered `project.open`. The test checks `projectFileName`, the `carlavst2`/`carlavst2effect` descriptors and the render. Undo depth does not grow because open is not an edit.
8. **Portability.** There is no Qt-6-only API: `QDomElement`, `QFileInfo`, `QRegularExpression`, and `std::optional` work with Qt 5.15/C++17+. There are no `windows.h` macro names (`min`, `max`, `near`, `far`, `small`, `interface` identifiers) and no `std::filesystem`. Wine/`.dll` conversion takes the same path but is untested here because there is no PE toolchain.
