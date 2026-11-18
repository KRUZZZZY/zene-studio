IMPLEMENTED

# Q050-231.3 - Masking Report workflow: completion report

Feature record: `docs/050-features/Q050-231.3.md`. Fix commit: `8655b80a66`. Log paths below are relative to `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-231.3.r4/`.

## What changed

- **`src/gui/MixerMaskingReport.cpp`** (`8655b80a66`, +15 lines): an `m_reported` guard. Ticking or unticking a channel (`itemChanged`) and changing a priority combo (`currentIndexChanged`) call `invalidateReport()`. That clears the clash rows, disables Apply through `updateApplyEnabled()`, and shows "Channel selection or priorities changed. Press Report again." `runReport()` sets `m_reported` only after the clash rows are listed.
- **Tests.** `MixerMaskingReportWindowTest` gains `changingChannelsOrPrioritiesClearsTheStaleReport`, `untickingAChannelClearsTheStaleReport` and `ctrlZInTheReportWindowUndoesTheAppliedProposal` (`8655b80a66`). `MixerMaskingReportTest` gains the 16/17-channel and ten-second clip coverage (`e2f2ee06fb`). `tests/control-masking-report.py` gains the playback-active scenario (`683cb9934d`). The invalidation regression is split into two slots to stay under the complexity target (`503208f22b`).
- **Docs.** The feature record (`2fc54381fe`, `716d5f61be`) and one manual line in `data/manual/index.html` (`2fc54381fe`).
- Not changed: command ids and metadata, `commands_snapshot.json`, A16, project format.

## Acceptance

Ctest commands run in `build/tests`.

| Criterion | test / slot names | exact command | exit code | log path |
|---|---|---|---|---|
| Inspect pairs and ranges for selected channels and priorities; 16 and 17 channels; ten-second clip; invalid selections and proposals refused; playback-active report, apply and undo | `MixerMaskingReportTest`: `clashesNameThePairTheRangeAndTheLowerPriorityTarget`, `sixteenChannelsAreReportedAndSeventeenRefused`, `aClipBeyondTheAnalysisCapIsBoundedAndDeterministic`, `badArgumentsAreRefused` (one channel refused), `badProposalsAreRefusedAndWriteNothing`. `ControlMaskingReport` (`check_playback_active`) | `ctest -R '^MixerMaskingReportTest$' --output-on-failure`; `ctest -R '^ControlMaskingReport$' --output-on-failure` | 0; 0 | `logs/019-p4-core.log`; `logs/020-p4-socket.log` |
| Explicit apply is one undo step. Report and refresh change nothing. Ctrl+Z in the report window undoes the applied cut through `MainWindow`'s Undo | `MixerMaskingReportWindowTest`: `theMaskingReportWindowListsTheClashAndChangesNothing`, `applyingAProposalFromTheWindowIsOneUndoStep`, `ctrlZInTheReportWindowUndoesTheAppliedProposal`. `MixerMaskingReportTest`: `reportingChangesNoEqAndNoUndoDepth`, `applyingAProposalChangesOnlyTheTargetAndOneUndoRestoresIt` | `ctest -R '^MixerMaskingReportWindowTest$' --output-on-failure` (green re-run `-V`); `ctest -R '^MixerMaskingReportTest$' --output-on-failure` | 0 (`p4`); 0 (`p4c` green); 0 | `logs/018-p4-gui.log`; `logs/032-p4c-green-verbose.log` (7 passed, 0 failed); `logs/019-p4-core.log` |
| Changed behaviour: a stale row cannot be applied. Changing a tick or a priority clears the rows and disables Apply until Report runs again | `MixerMaskingReportWindowTest::changingChannelsOrPrioritiesClearsTheStaleReport`, `untickingAChannelClearsTheStaleReport` | Reverted: `git checkout 8655b80a66~1 -- src/gui/MixerMaskingReport.cpp`, build, `ctest -R '^MixerMaskingReportWindowTest$' --output-on-failure`. Restored: `git checkout 8655b80a66 -- src/gui/MixerMaskingReport.cpp`, build, same ctest | reverted **8**; restored **0** | `logs/029-p4c-red-test.log`; `logs/031-p4c-green-test.log` |
| Contract and realtime stay clean | `ReversibilityContractTest`, `RtSafetySweep`, `RtSafetySelfTest` | `ctest -R '^ReversibilityContractTest$' --output-on-failure`; `ctest -R '^RtSafetySweep$' --output-on-failure`; `ctest -R '^RtSafetySelfTest$' --output-on-failure` | 0; 0; 0 | `logs/021-p4-reversibility.log`; `logs/022-p4-rtsweep.log`; `logs/023-p4-rtselftest.log` |

## Red/green

- **p2 (fix commit, before the test split).** `git checkout HEAD~1 -- src/gui/MixerMaskingReport.cpp` (product file only). Build exit 0 (`logs/009-p2-red-build.log`). `ctest -R '^MixerMaskingReportWindowTest$' --output-on-failure` **exit 8** (`logs/010-p2-red-test.log`): failing slot `changingChannelsOrPrioritiesClearsTheStaleReport()`, "Compared values are not the same"; 5 passed, 1 failed. Restored: build exit 0 (`logs/011`), ctest **exit 0** (`logs/012-p2-green-test.log`), 100% passed.
- **p4c (final test file, after the split).** Product file reverted to `8655b80a66~1`. Build exit 0 (`logs/028-p4c-red-build.log`). ctest **exit 8** (`logs/029-p4c-red-test.log`): failing slots `changingChannelsOrPrioritiesClearsTheStaleReport()` and `untickingAChannelClearsTheStaleReport()`, both "Compared values are not the same"; 5 passed, 2 failed. The Ctrl+Z slot passed on the reverted file, as expected. Restored to `8655b80a66`: build exit 0 (`logs/030`), ctest **exit 0** (`logs/031`), `-V` totals 7 passed, 0 failed (`logs/032`).

## Gates

| Gate | exit | log path |
|---|---|---|
| `python3 tests/doc-figures-gate.py` | 0 (`RESULT: PASS`, 933 ids / 78 groups) | `logs/017-p4-docfigs.log`; `logs/034-p5-docfigs.log`; `logs/035-p5b-docfigs.log` |
| `feature_check.py <worktree>` | 0 (`RESULT: PASS`, all eight gates) | `logs/033-p4c-gates.log`. Earlier run failed on complexity with exit 1 (`logs/024-p4-gates.log`), fixed by `503208f22b` |
| `RtSafetySweep` | 0 | `logs/022-p4-rtsweep.log` |
| `RtSafetySelfTest` | 0 | `logs/023-p4-rtselftest.log` |
| `ReversibilityContractTest` | 0 | `logs/021-p4-reversibility.log` |

Snapshot and A16: no command metadata changed. `git diff --stat 8655b80a66~1 HEAD` lists six files: the manual HTML, the feature record, the GUI product file, `tests/control-masking-report.py`, `tests/src/core/MixerMaskingReportTest.cpp` and `tests/src/gui/MixerMaskingReportWindowTest.cpp`. No snapshot, A16 or reversibility-table file is in the range.

## Realtime

- The only product file changed is `src/gui/MixerMaskingReport.cpp`. Its new handlers run on the GUI (main) thread and touch only widget state and the `m_reported` flag.
- No audio-path file is touched. The undo checkpoint is still taken in the apply handler, as before.
- Static realtime gates: `RtSafetySweep` exit 0 and `RtSafetySelfTest` exit 0 (see Gates).
- Local TSan is not available on this box, so the race argument rests on design, not a run.

## Portability

- Qt 5.15-compatible APIs only. The product code uses `QOverload<int>::of(&QComboBox::currentIndexChanged)` (`src/gui/MixerMaskingReport.cpp:116`) and `connect` to `QTableWidget::itemChanged`. Tests use `QTest::mouseClick`, `QTest::keyClick(QWindow*, ...)`, `QTest::qWaitForWindowActive` and `QWindow`.
- No new files, paths or identifiers beyond the names listed above. No filesystem access added.
- Qt 5.15 compatibility is argued from the API list, not compiled against a Qt 5.15 SDK here. The hosted matrix was not run locally.

## Known limits

- `strip_eq` in a proposal is absolute, not a delta. `mixer.apply_masking_proposal` accepts any well-formed `strip_eq`, so an agent holding an old proposal can still apply the values recorded when it was produced. The GUI blocks stale rows (any tick or priority change clears them, and Report must run again before Apply works). The command itself does not check freshness.
- Only one mid bell per channel exists. Two proposals that need the mid band on the same target overwrite each other; the last applied wins, and each is its own undo step.
- Priorities are arguments only and are not saved in the project.
- Instrument tracks are not analysed yet (they appear in `unanalysed_tracks`).
- The p2 red/green was run on the pre-split test file; p4c is the run against the final test file.

## Self-check

1. **New test fails reverted and passes restored.** Yes. p2: reverted product file, ctest **exit 8** with `changingChannelsOrPrioritiesClearsTheStaleReport` failing (`logs/010`); restored **exit 0** (`logs/012`). p4c: reverted, **exit 8** with two slots failing (`logs/029`); restored **exit 0** (`logs/031`).
2. **Each acceptance criterion proven, with test and exit.** Four rows in the Acceptance table. Row 1: `MixerMaskingReportTest` exit 0, `ControlMaskingReport` exit 0. Row 2: `MixerMaskingReportWindowTest` exit 0, `MixerMaskingReportTest` exit 0. Row 3: reverted 8, restored 0. Row 4: `ReversibilityContractTest`, `RtSafetySweep`, `RtSafetySelfTest` all exit 0.
3. **One undo step per applied proposal; honest classes unchanged.** `applyingAProposalFromTheWindowIsOneUndoStep` checks undo depth +1 after apply and restored after undo (`tests/src/gui/MixerMaskingReportWindowTest.cpp:252`, `:269`). Core: `applyingAProposalChangesOnlyTheTargetAndOneUndoRestoresIt`, exit 0. Classes: `mixer.masking_report` is `not_mutating`; `mixer.apply_masking_proposal` is `TrueInverse`, serialised as `true_inverse` (`src/core/ControlReversibility.cpp:45`). No command metadata changed in the range; `ReversibilityContractTest` exit 0 (`logs/021`).
4. **Nothing added on the audio thread; only product file is GUI; no audio path changed; static RT gates pass.** The only `src/` file changed is `src/gui/MixerMaskingReport.cpp`. The other changed files are tests, the feature record and one manual HTML line (`data/manual/index.html`), which is not product code. `RtSafetySweep` exit 0 and `RtSafetySelfTest` exit 0.
5. **Old projects unchanged.** No format, save/load or serialisation code is touched. The six files in the range are listed under Gates. The feature record states "Project format: unchanged. Nothing is saved."
6. **Completeness.** Walked through: Mixer menu, `Masking Report...` (`open->trigger()`); tick (`tickChannel`: `setCheckState`, emitting `itemChanged`) and priority (`setCurrentIndex`, emitting `currentIndexChanged`); Report (`QTest::mouseClick`); select row; Apply (`QTest::mouseClick`, invoking `mixer.apply_masking_proposal` through `ControlRegistry`); Ctrl+Z (real key event). Agent path: `ControlMaskingReport` socket test (`logs/020`). Invalid one-channel report: `badArgumentsAreRefused` (`channels=[1]` gives `InvalidArgs`). 16/17 boundary: `sixteenChannelsAreReportedAndSeventeenRefused`. Ten-second clip: `aClipBeyondTheAnalysisCapIsBoundedAndDeterministic`. Playback-active: `check_playback_active` in `tests/control-masking-report.py` (run through `logs/020`).
7. **GUI test drives visible widgets.** Yes, with one exception. The menu entry is triggered (`open->trigger()`), the window's buttons are found by name and clicked with `QTest::mouseClick` (Report, Apply), and Ctrl+Z is a real `QTest::keyClick` on `window->windowHandle()` after `getGUI()->mainWindow()->show()` and `qWaitForWindowActive`. The same registered commands run (`mixer.masking_report`, `mixer.apply_masking_proposal`, `control.undo_depth`). Exception: ticks and priorities are set through the widget API (`setCheckState`, `setCurrentIndex`), not synthetic clicks on those cells. Those calls emit the same signals a click would.
8. **Qt 5.15 portability.** As in the Portability section: Qt 5.15 APIs only, `QOverload` used for the combo signal, no new files, paths or identifiers. Not compiled against a Qt 5.15 SDK here; the hosted matrix was not run locally.
