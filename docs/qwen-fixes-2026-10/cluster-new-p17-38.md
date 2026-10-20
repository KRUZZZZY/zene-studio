FIXED

# Cluster new-p17-38 — a no-op VCA link update adds an undo step and marks the project modified

Branch `bugs/qwen-new-p17-38`, base `2da2ed356`. Finding: `src/core/ControlCommandsVcaNest.cpp:99` on the base tree (the `addUndoStep` line in `change()`); line numbers below are base-tree numbers unless marked "now".

## Verdict and evidence

**FIXED** — the finding is real on the base tree.

- `change()` (`src/core/ControlCommandsVcaNest.cpp:95` on the base; `:97` now) called `control::addUndoStep(...)` and `Engine::getSong()->setModified()` unconditionally, and attached a `__transaction` payload, even when `next` equals the group's current `before` state.
- Both `vca.set_links` (base `:134`–`:145`, `return change(...)` at base `:144`; now `:146`–`:157`, `:156`) and `vca.set_parent` (base `:112`–`:132`, `return change(...)` at base `:131`; now `:124`–`:144`, `:143`) go through that one function, so both had the defect.
- The registry records a no-op call as changing nothing only when the handler supplies no `__transaction` and the call pushed no journal step (`src/core/ControlRegistry.cpp:298`–`:302`, `:451`–`:452`, class `TrueInverse` for both rows at `src/core/ControlReversibilityTableMixing.cpp:44`, `:46`). The fix relies on that.

Fix: `change()` now compares `next` with `before` (`GroupLinks::operator==`, defaulted, `:57`). Only when they differ does it record the undo step, apply, mark modified and attach the `__transaction`. The result still reports the group's state and links for an unchanged request.

Regression test: `tests/src/core/ControlVcaNestTest.cpp` (new, 6 cases; 3 are the regression and 3 are controls for the real-change path).

## Commands and exit codes

Build-tree note: the first `cmake --build` failed with `No rule to make target` (the Makefile predated the new CMake entry). Regenerated with `cmake --build build --target cmake_check_build_system` (EXIT=0, `logs/regen-1.log`). One build failed on a ccache temp-dir permission error (`logs/build-1-red.log`, EXIT=2), so later builds set `CCACHE_TEMPDIR=/tmp/ccache-tmp-p17-38`.

| Step | Command (run from the worktree unless noted) | EXIT | Log |
|---|---|---|---|
| RED build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p17-38 cmake --build build --target ControlVcaNestTest` | 0 | `logs/build-2-red.log` |
| RED test | `cd build/tests && ctest -R ControlVcaNestTest --output-on-failure` (fix not yet applied) | **8** | `logs/test-1-red.log` — `Totals: 3 passed, 3 failed` |
| GREEN build | same build command, with fix | 0 | `logs/build-3-green.log` |
| GREEN test | `ctest -R ControlVcaNestTest --output-on-failure` | **0** | `logs/test-2-green.log` — 1/1 passed |
| Direct run | `./ControlVcaNestTest` (in `build/tests`) | 0 | `logs/direct-6-nest.log` — `Totals: 6 passed, 0 failed` |
| 4b make patch | `git diff -- src/core/ControlCommandsVcaNest.cpp > logs/fix.patch` | 0 | — |
| 4b revert | `git apply -R logs/fix.patch` | 0 | `logs/revert-1.log` |
| 4b unfixed build | `cmake --build build --target ControlVcaNestTest` | 0 | `logs/build-4-unfixed.log` |
| 4b unfixed test | `ctest -R ControlVcaNestTest --output-on-failure` | **8** | `logs/test-3-unfixed.log` — `Totals: 3 passed, 3 failed` (the same 3 no-op cases fail) |
| 4b re-apply | `git apply logs/fix.patch` | 0 | `logs/reapply-1.log` |
| 4b refixed build | `cmake --build build --target ControlVcaNestTest` | 0 | `logs/build-5-refixed.log` |
| 4b refixed test | `ctest -R ControlVcaNestTest --output-on-failure` | **0** | `logs/test-4-refixed.log` — 1/1 passed |

Failing assertions on the unfixed build (`logs/test-3-unfixed.log`): `aNoOpSetLinksRecordsNoUndoStepAndLeavesTheProjectUnmodified` — `journal->undoDepth()` = 1, expected 0 (`tests/src/core/ControlVcaNestTest.cpp:83`); `aNoOpSetParentRecordsNoUndoStepAndLeavesTheProjectUnmodified` — 1 vs 0 (`:110`); `aNoOpAfterARealChangeLeavesThatChangeAsTheOnlyUndoStep` — 2 vs 1 (`:160`).

Affected family, `ctest -R Vca` from `build/tests` (`logs/test-5-family.log`, `logs/test-8-family.log`): 9 tests run; ControlVcaNestTest, ControlVcaCommandsTest, ControlVcaEditGroupsTest, ControlVcaLockedEditsTest, VcaGroupTest, VcaRefreshAtomicityTest and ControlVcaCommands passed; VcaStripLifetimeTest passed; **VcaStripViewTest segfaulted** in the family run (`logs/test-8-family.log`, EXIT=8). See the flake note below. The 9 tests executed (not zero).

Gates (`feature_check.py` logic, see the note below): `logs/gates-3-feature-check.log`, **EXIT=0**, RESULT PASS on all eight rows (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length). The manifest-reproduce diff printed `REPRODUCES`.

### Gate-run note (tooling)

`feature_check.py` writes its per-gate logs to `<worktree>/../<name>-feature-check`, which is outside this lane's write-jail (`Permission denied` on the first run, `logs/gates-1.log`). I did not edit the shared script. I ran a copy at `/tmp/p17-38-gates/feature_check_copy.py` that differs in exactly two lines: `ROOT` points at the original `qwen/` directory (so `FIX-BASE` and the gate-baseline cache are the real ones), and the log folder is `/tmp/p17-38-gates/feature-check-logs`. Its checks are unchanged.

### VcaStripViewTest flake (not this change, not proven either way)

`VcaStripViewTest` is in the `Vca` family. It is not a test I changed, and its failures are not in the no-op path:

- On the fixed build: a family run segfaulted (`logs/test-8-family.log`); three ctest runs gave 0, 8 (segfault), 0 (`logs/test-10-gui-fixed-run{1,2,3}.log`); four direct runs gave 0, 0, 0 and **124** (timeout at 150 s, `logs/gui-fixed-direct-{1..4}.log`).
- On the unfixed build: one run passed (`logs/test-9-unfixed-gui.log`, EXIT=0); one run **hung** for over 14 minutes until I stopped it (`logs/test-11-gui-unfixed-run1.log` — no exit code recorded, the run was killed by me).
- The segfault's stack reports the crash in `aStripWhoseGroupIsRemovedRetiresAtOnce` (accessing address 0). Within the 12 lines after that definition, `tests/src/gui/VcaStripViewTest.cpp` makes no `vca.set_links`/`set_parent` call; I did not trace the whole test.

Conclusion: the GUI test is flaky with and without this change (hang and segfault both occur on the unfixed build and the fixed build). I have not shown that the baseline flakes at the same rate, and I have not fixed it. It needs a separate look.

## Self-check

1. **RED/GREEN proof.** The test fails without the product change (3 of 6 cases; `logs/test-1-red.log` and `logs/test-3-unfixed.log`, both EXIT=8) and passes with it (`logs/test-2-green.log` and `logs/test-4-refixed.log`, EXIT=0). Patch round-trip (`fix.patch`, `revert-1.log`, `reapply-1.log`) is recorded above.
2. **Deterministic.** No sleeps, wall-clock or threads. The test uses the registry (`run`), `ProjectJournal::undoDepth()`, `Song::isModified()`, and `Song::noteProjectSaved()` (a public method, the same seam `tests/src/core/MixerToolsTest.cpp:500`–`:509` uses) to clear the modified flag before each case. Its only other input is `clearGroups()` / `createGroup()` from `ControlVcaTestSupport.h`. The VcaStripViewTest flake above is a different test and was not made deterministic here.
3. **At the cause, complete.** The defect is in `change()`, which is the only write path for both `vca.set_links` (`:156` now) and `vca.set_parent` (`:143` now), so both are fixed by one change. `apply()`, `linksOf()` and the undo closure are unchanged. Not checked: `vca.set_gain`, `set_mute`, `set_solo` and `vca.rename` in `src/core/ControlCommandsVca.cpp` (their `addUndoStep` calls are at `:156`, `:206`, `:296`). They are not this finding, and I did not examine them for the same no-op behaviour. If they have it, it is a separate finding.
4. **No collateral damage.** The change only alters requests whose links equal the group's current links. Real changes take the same path as before: the regression test's control cases show one undo step and a working `control.undo` (`logs/direct-6-nest.log`). No file format, save path or limit changes, so projects and saved data load as before. One behaviour change, derived from code and not measured: a no-op call now records no transaction and no provenance entry (`ControlRegistry.cpp:298`–`:302`, `:451`–`:452`), where it used to record one. This is the intended effect of the fix.
5. **Verdict evidence.** Verdict is FIXED, so no "already fixed" or "false positive" claim is made.
6. **Sibling commands.** `src/core/ControlCommandsVcaNest.cpp` registers only `vca.set_parent` and `vca.set_links` (`:167`–`:179` now). The other `vca.*` commands are defined in other files and do not call `change()` (its only callers are `:143` and `:156` now), so this diff does not reach them. Per command: `vca.set_links`, `vca.set_parent` — the fix itself (`:143`, `:156` now). `vca.assign`, `vca.color_members`, `vca.create`, `vca.edit_fade`, `vca.edit_move`, `vca.edit_slip`, `vca.edit_split`, `vca.edit_trim`, `vca.get_state`, `vca.list`, `vca.remove`, `vca.rename`, `vca.set_gain`, `vca.set_mute` — not affected by this change (separate handlers, no shared call). Their own no-op behaviour was NOT examined: `vca.set_gain`, `set_mute` and `set_solo` in `src/core/ControlCommandsVca.cpp` have their own undo records, so they may share the same unconditional-undo pattern. That is unverified and not claimed consistent; it would need its own finding and regression test.
7. **Gate row (`submit_check` "gates").** `submit_check` runs the stock `feature_check.py`, which exits 1 with `PermissionError` when it creates `<worktree>/../zene-qwen-new-p17-38-feature-check` (outside the write-jail; see `logs/gates-1.log`). This is a tooling limit, not a code result. The same checks, with only the log folder redirected, pass on the committed tree (`logs/gates-3-feature-check.log`, EXIT=0). The real script's row is therefore unverified in this lane, not failed on code.

Realtime: `change()` is on the control-command path, not the audio thread. The new `operator==` is a field-wise comparison of plain values. Nothing new allocates, locks or logs on any audio path.
