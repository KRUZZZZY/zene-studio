IMPLEMENTED

# Q050-481.4 - Remove the legacy VST2 host modules

`plugins/VstBase` (`vstbase`, `RemoteVstPlugin32/64`, `NativeLinuxRemoteVstPlugin64`), `plugins/Vestige`, `plugins/VstEffect` and `include/aeffectx.h` are removed; Carla hosts VST2 instead. A missing binary, or one Carla cannot load, opens as the Q050-018 missing-device placeholder, keeps its saved state and is saved unchanged, so conversion can succeed later.

## Acceptance

From `build/tests`: `ctest -R '^(LegacyVst2ProjectTest|AutomationModesTest|CarlaVst2LegacyStateTest|CarlaVst2HostingTest)$' --output-on-failure` exited 0, 4/4 tests; `LegacyVst2ProjectTest` was 10 passed, 0 failed, 0 skipped (`build/duet-logs/041-ctest-green.log`, `041b-lasttest.log`; build exit 0 in `040-build-green.log`).

| Criterion | Tests | Evidence | Exit code |
| --- | --- | --- | --- |
| The legacy VST2 hosts are removed | `LegacyVst2ProjectTest::theLegacyVst2HostsAreNotBuilt` | RED in `010-10-ctest-red.log` (ctest exit 8); GREEN 0 | 0 |
| The corpus loads and renders through Carla | `aLegacyProjectOpensAsCarla`, `theStartHubOpensALegacyProjectAsCarla`, `aConvertedProjectSavesReopensAndRenders`, `aChunklessLegacyProjectRestoresItsDump`, `aMissingBinaryOpensAsAPlaceholderAndKeepsItsState`, `aBinaryCarlaCannotLoadOpensAsAPlaceholder`, `aVst2BinaryResolvesToACarlaInstrument` | RED: `010` log, exit 8 (placeholder cases fail with actual `vestige`, expected `dummyinstrument`); `033-26-build-p5-red.log`, build Error 2 (`instrumentKeyForBinary` is not a member of `lmms::legacyvst2`); GREEN 0 | 0 |

## Gates

`feature_check` exited 0, all PASS (`042-feature-check.log`). `plugin-formats-docs-gate` exited 0, 60 PASS (`044-formats-gate.log`). `submit_check` before this report exited 1 with report items only (`043-submit-check.log`). The run log dir under `~/.local/share` was not writable, so all logs are in `build/duet-logs`.

## Self-check

1. **RED/GREEN.** The round-1 reversal gave log 010 (ctest exit 8: 3 failed, 6 passed) and log 033 (build exit 2); restored GREEN in 035 and again now in 041, exit 0.
2. **Per criterion.** The tests and exit codes are in the table above.
3. **Undo/reversibility.** No command was added or changed (the diff touches no command file); opening a project is the existing load path; no new edit, so no undo step and no reversibility class change; no snapshot regeneration needed.
4. **Realtime.** The only audio-path change is `Song::processAutomations` (`src/core/Song.cpp`), which now checks `p->hasLiveTarget()` (a lock-free atomic flag read) before `firstObject()`, so a targetless clip no longer builds a fallback model on the render thread. It removes an allocation and adds none: no lock, no I/O, no signal. Covered by `AutomationModesTest::testRecordingClipWithoutTargetWritesNothing`, exit 0. The removed modules ran no Zene audio-thread code. Local TSan does not run here; this is reasoned from the code.
5. **Old projects.** Saved `vestige`/`vsteffect` XML is still read by `LegacyVst2Conversion`; the converted project saves, reopens and renders against the old-host reference (`aConvertedProjectSavesReopensAndRenders`); the placeholder keeps its state through save; the project format is unchanged.
6. **Completeness and edge cases.** GUI path is the Start hub Recent list + Return (`theStartHubOpensALegacyProjectAsCarla`); agent path is `project.open` via the test's `run()` helper (`aLegacyProjectOpensAsCarla`); a chunkless dump; a missing binary; a binary Carla cannot load; reopen after save; a file-browser VST2 binary resolving to a Carla instrument.
7. **GUI test.** `theStartHubOpensALegacyProjectAsCarla` calls `QTest::keyClick(hub.recentList(), Qt::Key_Return)` on the real widget (`LegacyVst2ProjectTest.cpp:214`). Loading a project is not an undoable edit, so the test checks the loaded project and the Carla devices instead of undo depth. That test is inherited from Q050-481.3, which is why the check warns that the added lines have no click.
8. **Portability.** The added lines have no `std::filesystem`, no `QT_VERSION`-dependent API and no `small`/`near`/`far`/`interface`/`min`/`max` identifiers (grep of 216 added lines); the macOS `.vst` limit is documented in `docs/050-features/Q050-481.4.md`.
