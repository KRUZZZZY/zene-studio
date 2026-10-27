FALSE-POSITIVE

# Cluster new-p16-252: automated base-note defaults stay in the old octave

## Finding

Claim (Luna patrol): in the element-based base-note path of
`src/core/UpgradeExtendedNoteRange.cpp`, the code records the base-note id but does not shift the
element's `value`, so `<basenote id="7" value="57"/>` keeps 57 after upgrade while its automation
points move by +12.

## Verdict: FALSE-POSITIVE

The element's stored value is shifted by +12 on this branch, in the same branch that records the id.
The shift has been there since the file was introduced (`5335f6d8c6`, #6548); it was not added by a
later fix.

### Evidence (file:line on `bugs/qwen-new-p16-252`, base `7641e9493`)

- `src/core/UpgradeExtendedNoteRange.cpp:132-140`: when the base note has a `value` attribute, the
  value is read as float (`:138`) and written back as `value + 12` (`:139`).
- `src/core/UpgradeExtendedNoteRange.cpp:142-145`: the id is then collected into
  `automatedBaseNoteIds` (`:145`). This is the "records the id" part of the finding. It sits in the
  same block as the value shift, not in place of it. The finding's `~155` is past the end of this
  block.
- `src/core/UpgradeExtendedNoteRange.cpp:163`: the automation time values are shifted separately in
  `fixAutomationPattern`. Both shifts run in the same upgrade pass, so the stored value and the
  automation points move together.
- `src/core/AutomatableModel.cpp:246-260`: an automated base note is saved as an element carrying
  `id`, `value` and `scale_type`. The element path that the upgrade rewrites is the one the model
  writes.

### Regression test (guard, not a RED proof)

`tests/src/core/UpgradeExtendedNoteRangeTest.cpp`: the `upgradeAutomationPatterns` helper now also
returns the upgraded `<basenote>` element value, and a new test
`automatedBaseNoteStoredValueIsShifted` asserts 57 becomes 69 for the finding's scenario (base note
id 7 with an automation pattern targeting it). The new test passes on the unmodified code, so it is
not a RED test for a bug that does not exist. It is kept as a guard for the behaviour the finding
describes.

Sensitivity check: a temporary mutation that removed the `+ 12` at `UpgradeExtendedNoteRange.cpp:139`
made the new assertion fail with `Actual 57, Expected 69`. The mutation was reverted, and
`git diff -- src/core/UpgradeExtendedNoteRange.cpp` is empty afterwards. So the test does detect the
failure mode described in the finding.

No product fix was made. The finding's "checked numeric conversion" is not implemented: the
existing `toFloat()` (`:138`) is unchecked, as it was before. A malformed `value` therefore becomes
12 after upgrade. That was the behaviour before this branch, and changing it is outside the reported
defect, so it is left as is.

## Commands run (EXIT codes)

Environment note: the default ccache temp dir (`~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`)
is outside the write-jail, so the first build failed with `ccache: ... Permission denied`. Builds
were rerun with `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp`. The gate script writes its logs to
`<worktree-parent>/<name>-feature-check`, which is also outside the write-jail. Its unmodified
invocation failed with `PermissionError`, so the gates were run with only the log directory
redirected (see below). Gate logic was not changed.

| # | Command | EXIT | Log |
|---|---|---|---|
| 1 | `cmake --build build --target UpgradeExtendedNoteRangeTest -j4` (ccache temp dir unwritable) | 2 | `logs/build-1.log` |
| 2 | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp cmake --build build --target UpgradeExtendedNoteRangeTest -j4` | 0 | `logs/build-2.log` |
| 3 | `cd build/tests && ctest -R '^UpgradeExtendedNoteRangeTest$' --output-on-failure` (new test built) | 0 | `logs/test-2.log` (100% passed, 1 test) |
| 4 | `cd build/tests && ./UpgradeExtendedNoteRangeTest -v2` (unmodified source) | 0 | `logs/run-direct-unmodified.log` (9 passed, 0 failed) |
| 5 | mutation: `UpgradeExtendedNoteRange.cpp:139` set to `value` (no +12); `cmake --build ... -j4` | 0 | `logs/build-3-mutant.log` |
| 6 | same binary: `./UpgradeExtendedNoteRangeTest -v2` | 1 | `logs/run-mutant-red.log` (1 failed: `automatedBaseNoteStoredValueIsShifted`, Actual 57, Expected 69) |
| 7 | revert mutation; `git diff --stat -- src/core/UpgradeExtendedNoteRange.cpp` (empty); rebuild | 0 | `logs/build-4-restored.log` |
| 8 | `./UpgradeExtendedNoteRangeTest -v2` (restored) | 0 | `logs/run-restored-green.log` (9 passed, 0 failed) |
| 9 | `cd build/tests && ctest -R '^UpgradeExtendedNoteRangeTest$' --output-on-failure` (final) | 0 | `logs/test-3-final.log` (100% passed, 1 test) |
| 10 | `python3 feature_check.py <worktree>` (unmodified) | 1 | `PermissionError` creating `zene-qwen-new-p16-252-feature-check` (write-jail) |
| 11 | feature gates via the unmodified `feature_check.py` source with `logs = Path("<run>/logs/gates-1")` substituted (`python3 -I -c ...`) | 0 | `logs/gates-1.log`: `RESULT: PASS` |

Affected test family: `UpgradeExtendedNoteRangeTest`, 1 ctest entry, 9 test functions executed, 0 failed.

Gate table (run 11):

| Gate | Result |
|---|---|
| fork-sources | PASS |
| all-sources-reproduce | PASS |
| no-upstream-regression | PASS |
| check-namespace | PASS |
| no-tautology | PASS |
| doc-figures | PASS |
| complexity | PASS (no new item in changed files) |
| file-length | PASS (no new item in changed files) |

## Files committed

- `tests/src/core/UpgradeExtendedNoteRangeTest.cpp` (test helper output parameter and one new test)
- `docs/qwen-fixes-2026-10/cluster-new-p16-252.md` (this report)

No new files were added, so no registration in `tests/fork-sources.txt` or
`tests/upstream-modifications.txt` was needed.

## Self-check

`submit_check.py new-p16-252` (log `logs/submit-check-1.log`, EXIT 1 before the commit below). Its
items:

- **Commit on the branch / clean tree:** FAIL before this commit (nothing committed, tree dirty).
  Resolved by committing the two files above; the final check is rerun after the commit.
- **Gates (feature_check.py):** FAIL inside `submit_check`. That script runs the unmodified
  `feature_check.py`, which hits `PermissionError` creating
  `zene-qwen-new-p16-252-feature-check` outside the write-jail (command 10 above). The same gate
  logic with only the log directory redirected returns `RESULT: PASS` (command 11,
  `logs/gates-1.log`). This is an environment limit, not a gate failure, and I did not change the
  loop's gate script.
- **Regression test FAILS with the product change reverted and PASSES with it:** not applicable as a
  RED proof, since no product change was made and the bug is not present. The new test passes on the
  unmodified code (command 4). The sensitivity mutation (command 5 and 6, exit 1, `Actual 57, Expected
  69`) shows the test catches the failure mode the finding describes. Restored source passes
  (command 8).
- **Deterministic (no wall-clock, sleeps or races):** yes. The test builds an in-memory `QDomDocument`
  and runs `UpgradeExtendedNoteRange::upgrade()` on it. It uses fixed integer values (57, 10, 3) and
  no timers, threads or files.
- **Fix at the cause, every caller and sibling consistent:** no product change. The sibling paths
  checked are the attribute path (`:114-119`, `value` as a plain attribute on the instrument parent,
  shifted by `+ 12`) and the preset path (`:434-446`). Both shift the stored base note, which agrees
  with the element path at `:138-139`.
- **No collateral damage:** no product code changed, so saved projects and presets load as before.
  The only test change adds an output parameter to the existing helper and adds one test. The two
  existing callers were updated to pass it and their assertions are unchanged.
- **FALSE-POSITIVE evidence (code):** `src/core/UpgradeExtendedNoteRange.cpp:138-139` (value shifted
  by +12 in the element path) and `:145` (id collected in the same block). Details in the Evidence
  section above.
