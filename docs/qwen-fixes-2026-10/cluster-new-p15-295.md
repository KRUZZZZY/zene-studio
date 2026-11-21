FIXED

# Cluster new-p15-295: writing a numeric value to a form can round it

## Verdict

FIXED. The finding is real. `numberText()` formatted every non-whole number with `QString::number(double)`,
which keeps six significant digits. A value written with `setField()` or taken from a form default went through
that text on its way into the editor and came back rounded, so `0.123456789` was collected as `0.123457` and the
command ran with the changed value.

Fix: `numberText()` now writes a non-whole number with `QLocale::FloatingPointShortest`, the shortest text that
reads back as the same double. The finding suggested `std::numeric_limits<double>::max_digits10` (17 digits). I did
not use it because 17 digits prints `0.1` as `0.10000000000000001` in every summary, range hint, run preview and
list cell, which would change text far outside this bug. The shortest form round-trips exactly and reads the same
as before for every value with six or fewer significant digits.

## Evidence (file:line on the branch after the fix)

Original chain, traced in the code:

- Write: `src/gui/CommandForm.cpp:247-254` (`setField`) -> `src/gui/CommandFormFields.cpp:516-528`
  (`writeValue`, `Kind::Text` at :528 calls `setText(valueText(value))`) -> `src/gui/CommandMetadata.cpp:398`
  (`valueText` -> `numberText`).
- Form defaults: `src/gui/CommandForm.cpp:227-229` write each default through `setField`, so they take the same path.
- Read back: `src/gui/CommandFormFields.cpp:300` (`parseScalar`: `text.toDouble`), reached from `collect()`
  at `src/gui/CommandForm.cpp:273-285`.
- The typed path keeps the text as typed: `src/gui/CommandFormFields.cpp:214` creates a `QDoubleValidator` and
  calls no `setDecimals`, so the rounding comes only from `numberText`.

Fixed formatter: `src/gui/CommandMetadata.cpp:182-187`. Whole numbers below 2^53 are unchanged (`:186`, integer
branch); other values use `QString::number(number, 'g', QLocale::FloatingPointShortest)` (`:187`).

Other callers of the same formatter (checked, consistent with the fix; all read display or argument text, none
persist a project): `fieldRange` `src/gui/CommandMetadata.cpp:334-336`, `valueText` `:398`, `choiceLabel` via
`valueText`, `src/gui/CommandRun.cpp:143`, `src/gui/CommandListDialog.cpp:61`. No other numeric formatting site in the
form path.

## Regression test

`tests/src/gui/CommandFormTest.cpp` (three new functions, existing file, no new file to register):

- `aNumberWrittenToAFieldIsReadBackUnrounded`: `setField("ratio", 0.123456789)`, `collect()`, expects `0.123456789`.
- `aNumberDefaultIsSentUnrounded`: the same value as a `CommandCall::defaults` entry, expects it unchanged.
- `aNumberReadsAsItsShortestExactText`: `valueText` of `0.5`, `1234567`, `0.1` (unchanged display) and
  `0.123456789` (`"0.123456789"`).

No timing, sleeps, or races. The tests use the existing fake-command schema in that file.

## Commands run and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-295.r1/logs`. Build and test commands were
run from the worktree (tests from `build/tests`). `CCACHE_TEMPDIR` was set to a `/tmp` directory in the second build
attempt onward (see the first row).

| # | Command | EXIT | Log | Result |
|---|---|---|---|---|
| 1 | `cmake --build build --target CommandFormTest -j4` | 2 | build-1-red.log | Environment failure: ccache could not write to `/run/user/1000/ccache-tmp`. Not a code error. |
| 2 | `CCACHE_TEMPDIR=/tmp/zene-qwen-p15-295-ccache cmake --build build --target CommandFormTest -j4` | 0 | build-2-red.log | Built. |
| 3 | `cd build/tests && ctest -R '^CommandFormTest$' --output-on-failure` | 8 | test-1-red.log | My first test draft did not fill the schema's required `name`, so two tests failed with "Name is required" before reaching the bug. Test corrected. |
| 4 | Same build, after the test correction (unfixed product code) | 0 | build-3-red.log | Built. |
| 5 | `ctest -R '^CommandFormTest$' --output-on-failure` (RED) | 8 | test-2-red.log | **RED**: 26 passed, 3 failed. `aNumberWrittenToAFieldIsReadBackUnrounded` ("Actual 0.123457, Expected 0.123456789"), `aNumberDefaultIsSentUnrounded` (same), `aNumberReadsAsItsShortestExactText` (`"0.123457"` vs `"0.123456789"`). |
| 6 | Fix applied; `cmake --build build --target zene CommandFormTest -j4` | 0 | build-4-green.log | Built. |
| 7 | `ctest -R '^CommandFormTest$' --output-on-failure` (GREEN) | 0 | test-3-green.log | **GREEN** (100% passed). |
| 8 | Step 4b, first cycle: `git diff -- src/gui/CommandMetadata.cpp > fix.patch && git apply -R fix.patch` | 0 | fix.patch | Product diff reversed. |
| 9 | Build `zene CommandFormTest` | 0 | build-5-proof-unfixed.log | Built. |
| 10 | `ctest -R '^CommandFormTest$' --output-on-failure` | 8 | test-4-proof-unfixed.log | Same 3 failures as row 5. |
| 11 | `git apply fix.patch`; build; `ctest -R '^CommandFormTest$' -V` | 0 | test-5-proof-fixed.log | **Totals: 29 passed, 0 failed.** |
| 12 | Final code (after the line-wrap in `numberText`): `git diff -- src/gui/CommandMetadata.cpp > fix-final.patch && git apply -R fix-final.patch` | 0 | fix-final.patch | Reversed again. |
| 13 | Build `zene CommandFormTest` | 0 | build-8-proof-unfixed-final.log | Built. |
| 14 | `ctest -R '^CommandFormTest$' --output-on-failure` | 8 | test-7-proof-unfixed-final.log | **RED on final code**: 26 passed, 3 failed (same 3). |
| 15 | `git apply fix-final.patch`; build `zene CommandFormTest HelpAccessTest CommandPaletteTest` | 0 | build-9-proof-fixed-final.log | Built. |
| 16 | `ctest -R '^(HelpAccessTest\|CommandPaletteTest\|CommandFormTest)$' -V` | 0 | test-8-green-final-family.log | **GREEN on final code**: CommandPaletteTest 11 passed; CommandFormTest 29 passed; HelpAccessTest 12 passed. |

Affected family: 3 ctest tests executed (`HelpAccessTest`, `CommandPaletteTest`, `CommandFormTest`), 52 QtTest
cases passed, 0 failed.

Gates. The wrapper could not run here (see the first gate row). I ran the eight gate commands it lists directly, from
the worktree, on the final tree:

| Command | EXIT | Log |
|---|---|---|
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (first run) | 1 | gate-1-feature-check.log |
| same, final tree | 1 | gate-3-feature-check-wrapper.log |
| `bash tests/fork-sources-gate.sh` | 0 | gate-3-fork-sources.log |
| `bash tests/all-sources-reproduce.sh` | 0 | gate-3-all-sources-reproduce.log |
| `bash tests/no-upstream-regression-gate.sh` | 0 | gate-3-no-upstream-regression.log |
| `tests/scripted/check-namespace` | 0 | gate-3-check-namespace.log |
| `bash tests/no-tautology-gate.sh` | 0 | gate-3-no-tautology.log |
| `python3 tests/doc-figures-gate.py` | 0 | gate-3-doc-figures.log |
| `bash tests/complexity-gate.sh --check` | 1 | gate-3-complexity.log |
| `bash tests/file-length-gate.sh --check` | 1 | gate-3-file-length.log |

- **Wrapper, EXIT 1, environment:** `feature_check.py:71` calls `logs.mkdir()` on
  `<projects>/zene-qwen-new-p15-295-feature-check`, which is beside the worktree and outside the write-jail. It fails
  with `PermissionError` before any gate runs. I did not edit the gate or create that directory. Its base-listing
  cache also writes outside the jail (`gate-baselines`, a `git worktree add` in `zene-cachyos`), so I did not run it.
- **Complexity and file-length, EXIT 1 raw:** these are red on the train on purpose. The 21 REGRESSION lines are in
  other files. None names `CommandMetadata.cpp` or `CommandFormTest.cpp` (`grep -c` = 0 on the final logs), so under
  the wrapper's rule (fail only for a NEW item in a changed file) both rows pass for this branch. I did not compute
  the base listings, since no item is in a changed file.

Not run: the other test binaries that include the command-form headers (`WorkspaceAccessTest`,
`InstrumentSetupUiTest`, `EffectDeviceMenuTest`, `RackMenuTest`, `PluginNativeEditorTest`, `MixerWindowsTest`). They
are outside this bug's family. I searched `tests/src` for six-digit or exponent expectations and found none that
depends on this formatter (the only hits are the new tests and unrelated constants).

## Behaviour outside the bug

- Values with six or fewer significant digits display exactly as before (`0.5`, `0.1`, `1234567` are asserted).
- Values with more digits now show them in summaries, range hints, run previews and list cells (for example
  `0.123456789 to 1` where it showed `0.123457 to 1`). This is the same formatter, not a separate change.
- `numberText` feeds display and argument text only. It is not used to save or load a project, so old projects load
  exactly as before.
- No command, no new control command, no ControlCommandsSnapshot change. No undo change. No audio-thread code.

## Self-check

Answers to the completion check's five judgement items. Evidence is the log or command-table row above.

1. **The regression test FAILS with the product change reverted and PASSES with it.** Yes. Reverted (rows 12-14): EXIT 8, and `aNumberWrittenToAFieldIsReadBackUnrounded`, `aNumberDefaultIsSentUnrounded` and `aNumberReadsAsItsShortestExactText` fail (`test-7-proof-unfixed-final.log`). Restored (rows 15-16): EXIT 0, `CommandFormTest` 29/29. The earlier cycle (rows 8-11) gives the same result.

2. **The test is deterministic.** Yes. The new tests have no wall-clock timing, sleeps, threads or races. They set literal values (`0.123456789`, `0.5`, `1234567`, `0.1`) through `CommandForm::setField()`, `collect()` and `valueText()`, and assert exact values and text. The schema is the fake command already registered in that file.

3. **The fix is at the cause and complete.** The cause is the single formatter `numberText()` (`src/gui/CommandMetadata.cpp:182-187`). I checked its callers and the callers of `valueText()`, which calls it: `fieldRange` (`:334-336`), `valueText` (`:398`), `choiceLabel` (through `valueText`), `src/gui/CommandRun.cpp:143`, `src/gui/CommandListDialog.cpp:61`. All read display or argument text, so they stay consistent. The write path (`src/gui/CommandFormFields.cpp:528`) and read path (`:300`) need no change: the number editor's validator sets no decimals (`:214`). Form defaults take the same path (`src/gui/CommandForm.cpp:227-229`). `max_digits10` was rejected because it prints `0.1` as `0.10000000000000001` in every label (see Verdict).

4. **No collateral damage.** No limit or check was added, so no caller can be refused. Projects are not written through this formatter, so saved data and old projects load as before. Values with six or fewer significant digits display identically (asserted in `aNumberReadsAsItsShortestExactText`). Values with more digits show their full digits in summaries, range hints, run previews and list cells; that is the intended effect of the same formatter. The change is UI text only: no command, no new control command, no ControlCommandsSnapshot change, no undo change, nothing on the audio thread.

5. **Verdict evidence.** The verdict is FIXED, so no ALREADY-FIXED or FALSE-POSITIVE proof is needed. The defect is real: the pre-fix `numberText()` called `QString::number(number)` (six significant digits), reached as traced above (`CommandForm.cpp:247-254` -> `CommandFormFields.cpp:528` -> `CommandMetadata.cpp:398`). The unfixed run reproduces it: `0.123457` for `0.123456789` (`test-2-red.log`, `test-7-proof-unfixed-final.log`).

**Gates row (`feature_check.py`) shows FAIL in this sandbox.** The wrapper cannot create its log directory beside the worktree (`feature_check.py:71`, outside the write-jail), so no gate row is computed. It is not a gate result. The eight gate commands the wrapper runs all pass (exit 0), except complexity and file-length, which are red on the train on purpose and have no REGRESSION item in a changed file (exit 1 raw). The gates row needs a run from a session that can write beside the worktree.
