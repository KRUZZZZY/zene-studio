IMPLEMENTED

# Q050-231.2 - Apply masking proposals (stage 2/3): one clash, one undo step

Branch `feat/050-Q050-231.2-oc`, cut from the stage-1 unit. The design, its limits and the reversibility figures are
in `docs/050-features/Q050-231.2.md`.

**What was built:**

- **`mixer.apply_masking_proposal`** (`src/core/ControlCommandsMixerMasking.cpp`), `{strip_eq: object}`,
  `mutating: true`, class **TrueInverse** (`src/core/ControlReversibilityTableDevices.cpp`). It shares
  `control::applyChannelEq` (`src/core/ControlCommandsMixerTools.cpp`, declared in `include/ControlMixerSupport.h`)
  with `mixer.channel_eq`, so clamping and the undo rule are identical. The reply is the `channel_eq` reply plus
  `applied` and `before`; a no-op proposal records no step. Validation refuses with `InvalidArgs` (missing or
  non-object `strip_eq`, unknown key, none of low/mid/high, `mid_freq` without `mid`, non-number, out-of-range
  values, malformed channel id) and `NotFound` (well-formed unknown channel), writing nothing.
- **Mixer View > Masking Report...** (`src/gui/MixerMaskingReport.cpp`): new **Apply** button (accessible name
  `Apply proposal`, `controlCommand` property `mixer.apply_masking_proposal`), disabled with no clash row selected,
  refreshes the report after applying. GUI Ctrl+Z goes through `MainWindow::undo` -> `projectJournal()->undo()`, the
  same journal as `control.undo`.
- **Snapshot and counts**: 933 ids / 78 groups; A16 `rows=942 true_inverse=465 snapshot=42 irreversible=25
  not_mutating=410`.

## Acceptance

| Criterion | Test | Command | Exit |
| --- | --- | --- | --- |
| Applying a proposal changes the intended target strip EQ in one reversible transaction. | `MixerMaskingReportTest::applyingAProposalChangesOnlyTheTargetAndOneUndoRestoresIt`, `applyingTheSameProposalAgainChangesNothing`, `boundaryProposalsAreAccepted`, `badProposalsAreRefusedAndWriteNothing`; `MixerMaskingReportWindowTest::applyingAProposalFromTheWindowIsOneUndoStep` (clicks the real Apply button); `ControlMaskingReport` (real binary over the socket) | `ctest -R '^(MixerMaskingReportTest\|MixerToolsTest)$'` (`/tmp/zene-q050-231.2-logs/004-test-2.log`); `ctest -R '^(MixerMaskingReportWindowTest\|MixerMaskingReportTest)$'` (`/tmp/q050-231.2/test-2.log`); `ctest -R '^ControlMaskingReport$'` (`/tmp/q050-231-logs/002-test-5.log`) | 0, 0, 0 |
| One `control.undo` restores the target strip EQ values present before applying. | the same slots (one `control.undo`, all EQ values equal `before`, depth back) and `ControlMaskingReport` ("one control.undo restores the whole EQ and the depth") | same runs | 0 |
| Contract and snapshot stay consistent. | `ReversibilityContractTest`, `ControlCommandsSnapshot` | `ctest -R '^(ReversibilityContractTest\|ControlCommandsSnapshot)$'` (`/tmp/q050-231.2-logs/005-test-6.log`) | 0 |

An earlier command-test run gave exit 8 (`/tmp/zene-q050-231.2-logs/002-test-1.log`): the first run failed at
`badProposalsAreRefusedAndWriteNothing`, `errorKind` NotFound (1) where InvalidArgs (3) was expected, because the test
used the malformed id `nope`; that really is `InvalidArgs`, so the test case was corrected. The NotFound case was fixed
in `8e8930e963` and verified: exit 0 (`/tmp/zene-q050-231.2-logs/006-test-p1b.log`).

**Fails without the change.** Product hunks (`git diff 40b4322860..a3c41f5455 -- src include`) reverse-applied,
rebuild exit 0, `ctest -R '^(MixerMaskingReportTest|MixerMaskingReportWindowTest)$'` **exit 8**
(`/tmp/q050-231.2-r1/logs/002-test-3-reverted.log`): the four apply slots fail plus
`MixerMaskingReportWindowTest::applyingAProposalFromTheWindowIsOneUndoStep` (no Apply button). Restored: rebuild
exit 0, ctest exit 0 (`/tmp/q050-231.2-r1/logs/004-test-4-restored.log`). Red/green notes:
`/tmp/q050-231.2-r1/logs/red-green.md`. The run folder's logs directory was not writable for the worker, so all logs
are under `/tmp` at the paths above.

## Self-check

To be filled after the completion check.
