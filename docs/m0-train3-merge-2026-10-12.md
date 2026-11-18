# M0 merge train 3 (2026-10-12)

Branch `050/m0-train-3`, from `190242990` (tip of `050/m0-train-2`). Each branch merged with `--no-ff`; a merge that
failed its own tests on the integrated tree, or contradicted an already-merged line, was reverted with
`git revert -m 1` (new commit, no reset) and is listed as skipped. Registry-file conflicts (fork-sources,
upstream-modifications, all-sources) were resolved keep-both and `all-sources.txt` re-derived with its recipe;
`tests/all-sources-reproduce.sh` and `tests/fork-sources-gate.sh` exited 0 after every merge. Snapshot, README canary and
A16 histogram conflicts took the current side and were regenerated at the end.

Note on method: targeted builds (`zene` plus the branch's test targets) do not rebuild every plug-in. After the
Q050-018 merge (new virtual in `Instrument.h`) several tests crashed from stale plug-in modules; a full build fixed them.
Full builds were used from that point on, and the earlier results above it were rechecked after the full build.

| # | Branch | Result | Merge commit | Tests run (exit) |
|---|---|---|---|---|
| 1 | p0fix/rt-q005-q050 | merged | 3b4f1b08d | GranularPitchShifterRealtimeTest (0) |
| 2 | p0fix/q021-q042 | merged | 85301ad2e | LegacyPatternUpgradeTest, VideoTrackTest (0); ControlStoppedSeek failed on the targeted-build tree (SEGV from stale/heap state), passes after the full build |
| 3 | bugs/m0-fix-heap-audioports | merged | fb71e12d4 | AudioPortsModelTest (0) |
| 4 | p0fix/gui-q032-q011 | SKIPPED (merged then reverted, revert e250727ad) | 43344f93a | KnobKeyboardTest FAILS: the branch's new slot `accessibleTargetSurvivesKnobSizingReset` does `QVERIFY(qEnvironmentVariableIsSet("ZENE_TEST_ACCESSIBLE"))`, so the whole-binary ctest `KnobKeyboardTest` (no such env) fails on the branch itself, not a merge slip (`tests/src/gui/KnobKeyboardTest.cpp:416`). MixerSelectionIdentityTest and KnobAccessibleTargetSizingTest passed. Needs a fix on the branch (skip when the env is unset, or isolate the slot). |
| 5 | feat/050-L15-alsa-tuning | merged | 2e6fda6cd | MidiOutQueueTest, SessionTuningPublicationTest (0). MidiOutQueueTest `queuedEventReachesAnAlsaSequencerClientPromptly` failed once inside the gates ctest run at load average 20-30 (369 ms vs 150 ms bound); 3/3 pass alone |
| 6 | feat/050-Q050-027-import-io | merged | baf9f22c7 | ImportTrackIoTest (0) |
| 7 | feat/050-Q050-001-dawproject-deflate | merged | 6d4f4a6f4 | DawProjectDeflateImportTest, DawProjectInterchangeRoundTripTest, RevisionTimelineTest, TrackRenderMenuTest (0) |
| 8 | feat/050-Q050-048-browser-window | merged | 3aabd72e8 | BrowserFeaturesWindowTest (0) |
| 9 | feat/050-Q050-020-midi-export | merged | 7012117dd | MidiExportTest (0) |
| 10 | feat/050-Q050-031-plugin-drops | merged | c22b23663 | ClipImportFileTest, ArrangerFileDropTest, SmfInterchangeTest, SmfInterchangeRoundTripTest (0) |
| 11 | feat/050-Q050-018-missing-instruments | merged | 7952e94ff | MissingDevicesPreserveTest, MissingDevicesRound2Test (0) |
| 12 | feat/050-Q050-045-record-loss | merged | ef90b9bdf | RecordingCrashLossTest (0); ControlRecordingRecovery failed on the stale-plug-in tree, passes after full build |
| 12b | feat/050-Q050-046-undoable-recovery (added by the coordinator) | merged | 3c5657fa2 | RecordingRecoveryMenuTest (0), RecordingCrashLossTest (0); ReversibilityContractTest failed until the histogram line was re-taken at the end (then 0); ControlRecordingRecovery (0 after full build) |
| 13 | feat/050-L02-session-land | SKIPPED (merged then reverted, revert d6362ed6b) | 471f00629 | `tests/src/core/SessionAudioCellPlaybackTest.cpp:204,218,316` do not compile on this toolchain: `for (const QString type : {...})` trips `-Werror=range-loop-construct` (GCC 13.2). ControlSessionRecordLandTest passed, but the other test target does not build. Branch fix: `const QString&`. |
| 14 | feat/050-Q050-044-open-script-undo | merged | ac3f37389 | SessionCheckpointTest, SessionCheckpointBudgetTest, UndoHistoryPersistenceTest, ControlReversibilityTranscript (0 after full build). Code conflict in `src/core/ControlCommandsProjectFiles.cpp` (project.open result schema and transaction) resolved: schema keeps both `missing_devices` (Q050-018) and `checkpoint`; the result calls `insertMissingDevices` and then the branch's `projectOpenTransaction`. |
| 15 | feat/050-Q050-007-v2-container | merged | 52667bad1 | ProjectContainerSaveTest, ProjectContainerSaveAsGuiTest, ControlContainerV2, SessionCheckpointTest, DawProjectInterchangeRoundTripTest (0). `docs/KNOWN-LIMITATIONS.md` conflict resolved keep-both. |
| 16 | bugs/m0-decisions-r3-clips | SKIPPED (merged then reverted, revert 4c23f62fe) | dac75c65e | ArrangementModesTest::aCrossfadeIntoAContainedClipStopsAtItsEnd fails (`earlier->clipEdits().fadeOutTicks` 20, expected 10, `ArrangementModesTest.cpp:148`): the branch changes `ContainedCrossfade.cpp` so a contained-clip crossfade no longer ramps both ends, which contradicts the owner decision 2026-10-08 round 2 that the test (already on the line) pins. The branch did not update that test. A design decision for the owner, not a merge slip. The other 36 clip/link/arrangement/fade tests passed. |
| 17 | bugs/m0-decisions-r3-takes | SKIPPED (merged then reverted, revert ce89884e2) | ce28ba399 | TakeLaneCompTest::removingALaneRepointsItsSegmentsToTheBaseLane fails (`TakeLaneCompTest.cpp:192`, `model.isEmpty()` false after the last lane is removed). Cause: the branch's last commit `cee6ca148` makes `TakeLaneModel::isEmpty()` also require `m_nextLaneIndex == 0` (`include/TakeLane.h`), so the pre-existing test breaks; the branch's own report predates that commit. Needs the test or the semantics reconciled on the branch. |
| 18 | design/m0-automation-lockfree | SKIPPED (merged then reverted, revert dd628cb12) | a4497a5d3 | The two designs do not compose: the branch changes the constructor to `AudioEngineWorkerThread(AudioEngine*, std::size_t recordingProducerSlot)` and `tests/src/core/AudioEngineTeardownTest.cpp:122,159` (on the line) no longer compiles; AutomationModesTest (testReadNeverWrites, testTouchWritesWhileHeldAndReturnsToReading, testLatchHoldsUntilTheTransportRunEnds) and ControlAutomationModesTest::readRideThroughTheSocketCannotTouchTheRecordedAutomation fail (recorded writes do not land) on top of the merged prepass. The design doc (add/add conflict) was kept as ours plus an appendix before the revert. Needs a rebase of the lockfree work onto the prepass line. |
| 19 | bugs/m0-fix-bar5-regressions (head 6e9431101) | merged | eb1431a79 | Coordinator decision: merge although the independent review file says REWORK, because its only REWORK row is `ScriptDawRemovalUndoTest::aRecordedDeviceIsBuiltWithoutTheAudioChangeLock`, which already fails on 190242990 (branch reduces 12 failures to 1) and goes on the known-failures list; all 8 code items were ACCEPT. Conflict only in `tests/upstream-modifications.txt` (keep-both). After the full build the former bar-5 Control* tests pass. |

Final commit: `3799c1662` chore: commands snapshot regenerated from a live instance (825 ids / 76 groups, README canary
824 -> 825), A16 histogram line re-taken (834 = 414 true_inverse + 42 snapshot + 22 irreversible + 356 not_mutating,
which is the portable 825 = 413 + 39 + 22 + 351 plus the wasm option row). `tests/doc-figures-gate.py` PASS.

## Full suite

Full build (`cmake --build build -- -j8 -k`) exit 0. One `ctest` from `build/tests` on the final tree (520 tests,
machine load average 20-30 from other agents' builds): **EXIT=8, 516 passed, 4 failed, 1 skipped**
(ControlRetroCapture skipped). Failures:

| Test | Classification |
|---|---|
| ScriptDawRemovalUndoTest | the one known failure (`aRecordedDeviceIsBuiltWithoutTheAudioChangeLock`, `ScriptDawRemovalUndoTest.cpp:500`); fails identically on rerun |
| MixerConcurrencyTest | load flake: aborted in the full run, passes alone |
| PianoRollBenchmarkTest | load flake (timing): fails in the full run, passes alone |
| ControlMidiReconnect | load flake (ALSA event timing): failed in the full run and in the gates run, passes 3/3 alone and 1 of 2 reruns before that |

No test outside the known list failed reproducibly. ControlReversibilityTranscript passes (the coordinator's note about
a transport.set_tempo failure did not reproduce).

## Gates (`bash tests/run-all-gates.sh`, no coverage)

EXIT=1, only because gate 1 re-runs ctest under load and 3 tests failed there.

| Gate | Result |
|---|---|
| 1 ctest | FAIL: ScriptDawRemovalUndoTest (known), MidiOutQueueTest (load flake, passes 3/3 alone), ControlMidiReconnect (load flake) |
| 2 coverage | SKIP (no `--with-coverage`) |
| 3 no-tautology, 4 complexity, 5 mutation, 6 upstream-regression, 7 file-length, 8 duplication, 9 fork-sources, 10 unregistered-tests, 11 evidence, 12 rt-safety, 13 scripted-checks, 14 release-fitness-selftest, 15 verification-debt-selftest | PASS |

## NEW failures

None attributable to a merge that remain on the branch. Failures found at intermediate points and their cause:
the stale-plug-in crashes (ControlStoppedSeek, ControlRecordingRecovery, UndoHistoryPersistenceTest; vtable change from
the Q050-018 `Instrument::unavailableReason` virtual, fixed by a full build), and the reverted branches listed above
(items 4, 13, 16, 17, 18), each of which introduced its failure on its own.
