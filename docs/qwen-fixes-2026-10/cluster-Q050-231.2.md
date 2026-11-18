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

The first run of the command tests gave exit 8 (`/tmp/zene-q050-231.2-logs/002-test-1.log`): it failed in
`badProposalsAreRefusedAndWriteNothing` at `MixerMaskingReportTest.cpp:304` with `errorKind` NotFound (1) where
InvalidArgs (3) was expected, because a case with an unknown channel went through `resolveMixerChannel`. That case was
then changed to a malformed id (`nope`, InvalidArgs), and commit `8e8930e963` added a dedicated well-formed unknown id
(`ch-9999`) case that asserts NotFound and no write; `MixerMaskingReportTest` then passed, exit 0
(`/tmp/zene-q050-231.2-logs/006-test-p1b.log`).

**Fails without the change.** Product hunks (`git diff 40b4322860..a3c41f5455 -- src include`) reverse-applied,
rebuild exit 0, `ctest -R '^(MixerMaskingReportTest|MixerMaskingReportWindowTest)$'` **exit 8**
(`/tmp/q050-231.2-r1/logs/002-test-3-reverted.log`): the four apply slots fail plus
`MixerMaskingReportWindowTest::applyingAProposalFromTheWindowIsOneUndoStep` (no Apply button). Restored: rebuild
exit 0, ctest exit 0 (`/tmp/q050-231.2-r1/logs/004-test-4-restored.log`). The RED/GREEN run was made on
`a3c41f5455`; the later refactor commits (`4a85efc946`, `6de49f3da1`) only restructure the helpers, and the listed
tests were re-run green after the refactor: `ctest -R
'^(MixerMaskingReportTest|MixerToolsTest|MixerMaskingReportWindowTest|ControlMaskingReport|ReversibilityContractTest|ControlCommandsSnapshot)$'`
-> **exit 0**, 6/6 passed (`/tmp/zene-q050-231.2-logs/013-test-p8.log`). Red/green notes:
`/tmp/q050-231.2-r1/logs/red-green.md`. The run folder's logs directory was not writable for the worker, so all logs
are under `/tmp` at the paths above.

## Self-check

Completion check: `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> Q050-231.2` -> **exit 0**
(`/tmp/zene-q050-231.2-logs/017-submit-check-3.log`), **RESULT: PASS - submit**, no FAIL items. Earlier runs were red:
**exit 1** (`/tmp/zene-q050-231.2-logs/008-submit-check.log`) with 2 FAIL items, then **exit 1**
(`/tmp/zene-q050-231.2-logs/009-submit-check-2.log`) with 1 FAIL item. The complexity and file-length FAILs were then
cleared by the p8 refactor commit (`4a85efc946`, splitting the proposal validation and moving the shared EQ write out
of the 500-line tools file) and this packet's commit (`6de49f3da1`, splitting `check_apply` into small helpers).

- **Working tree clean.** The first run flagged the two doc edits of this section as uncommitted; they are committed
  (`776df70909`). The final run PASSes commits and clean-tree, as well as report, feature record, GUI path and its
  test, live snapshot, canary figures, manual, audio-path scan, added tests, portability and gates.
- **Gates (feature_check.py).** The final run PASSes this item. The earlier `FAIL complexity NEW:
  lmms::applyMaskingProposal@338-402@src/core/ControlCommandsMixerMasking.cpp (CCN 22)` and `FAIL complexity NEW:
  check_apply@76-119@tests/control-masking-report.py (CCN 17)` and `FAIL file-length NEW: new file over 500 lines:
  src/core/ControlCommandsMixerTools.cpp (506)` are gone; every function is now under the CCN target. No gate was
  edited or weakened, and every check in the driver is kept with the same message.
- **ControlMaskingReport after the refactor:** `ctest -R '^ControlMaskingReport$'` -> **exit 0**, 1/1 passed
  (`/tmp/zene-q050-231.2-logs/016-test-p9.log`).

- **Each new test FAILS with the product change reverted, then PASSES restored.** Product hunks
  (`git diff 40b4322860..a3c41f5455 -- src include`) reverse-applied, rebuild exit 0; `ctest -R
  '^(MixerMaskingReportTest|MixerMaskingReportWindowTest)$'` -> **exit 8**, 4 failed in `MixerMaskingReportTest` and
  1 in `MixerMaskingReportWindowTest` (`/tmp/q050-231.2-r1/logs/002-test-3-reverted.log`). Restored: rebuild exit 0,
  same ctest -> **exit 0**, 2/2 passed (`/tmp/q050-231.2-r1/logs/004-test-4-restored.log`). This run was made on
  `a3c41f5455`; the two later refactor commits only restructure the helpers, and the tests were re-run green after
  them: 6/6 passed, exit 0 (`/tmp/zene-q050-231.2-logs/013-test-p8.log`) and, after this packet's helper split,
  `ctest -R '^ControlMaskingReport$'` -> **exit 0**, 1/1 passed (`/tmp/zene-q050-231.2-logs/016-test-p9.log`). Notes:
  `/tmp/q050-231.2-r1/logs/red-green.md`.
- **Every acceptance criterion proven by a test I ran.** Criterion 1 (apply changes only the target, one reversible
  transaction): `ctest -R '^(MixerMaskingReportTest|MixerToolsTest)$'` -> 0
  (`/tmp/zene-q050-231.2-logs/004-test-2.log`), `ctest -R '^(MixerMaskingReportWindowTest|MixerMaskingReportTest)$'`
  -> 0 (`/tmp/q050-231.2/test-2.log`), `ctest -R '^ControlMaskingReport$'` -> 0
  (`/tmp/q050-231-logs/002-test-5.log`). Criterion 2 (one `control.undo` restores the before values and the depth):
  the same slots in the same runs -> 0, 0, 0. Criterion 3 (contract and snapshot consistent): `ctest -R
  '^(ReversibilityContractTest|ControlCommandsSnapshot)$'` -> 0 (`/tmp/q050-231.2-logs/005-test-6.log`).
- **One undo step and an honest reversibility class.** The handler takes one journal checkpoint and the tests assert
  the undo depth returns to its previous value and all four EQ values equal `before` after one `control.undo`; the
  same slots also assert a repeated apply is a no-op and records no step. The class is **TrueInverse** in
  `src/core/ControlReversibilityTableDevices.cpp` and `ReversibilityContractTest` passes (exit 0, log above).
- **Nothing added runs on the audio thread.** The handler runs on the control (main) thread and only writes EQ
  `FloatModel` values, as `mixer.channel_eq` already does: no allocation, lock, wait, I/O or signal was added on the
  render path, and no mixer period buffer is touched. This is by design: local TSan is not available on this box, so
  it is not a measured result.
- **Old projects still load and render as before; new data round-trips.** No format change and nothing new is saved:
  the command writes only live EQ values that the existing project format already stores, so no new field round-trips
  and old projects load as before. No new save/load test was needed or run.
- **Completeness: real user path and agent path.** GUI: Mixer View > Masking Report..., select a clash row, click
  **Apply**; `MixerMaskingReportWindowTest::applyingAProposalFromTheWindowIsOneUndoStep` clicks the real button and
  checks it ran the registered command (undo depth +1), exit 0 (`/tmp/q050-231.2/test-2.log`). Agent path:
  `mixer.apply_masking_proposal` over the control socket, exercised by `ControlMaskingReport` against a real `zene`,
  exit 0 (`/tmp/q050-231-logs/002-test-5.log`). Edge cases tried: a no-op proposal (no step, `applied` false),
  boundary values (-18/18 dB, 100/10000 Hz), missing/unknown key, `mid_freq` without `mid`, non-numbers,
  out-of-range values, a malformed channel id (`InvalidArgs`), and a well-formed unknown id `ch-9999`
  (`NotFound`, nothing written). Undo/redo row shifts, stale generations and reopen-after-save do not apply: the
  command holds no report row and saves nothing.
- **GUI test drives the visible control and checks the same registered command.** It clicks the real Apply button
  (`Apply proposal`), then checks one `control.undo` restores the values and the undo depth moved by exactly one; it
  does not call the handler or command directly (exit 0, `/tmp/q050-231.2/test-2.log`).
- **Qt 5.15 / MSVC / macOS < 10.15 portability.** The added code uses only existing helpers and Qt 5-compatible
  APIs: no Qt-6-only call, no `windows.h` macro name, no `std::filesystem`. The portability item of the submit check
  passes; no MSVC or macOS build was run on this Linux box.

