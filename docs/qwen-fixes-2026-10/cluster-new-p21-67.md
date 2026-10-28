Verdict: FALSE-POSITIVE

Finding: `setlistGo` dereferences a possibly-null `project.open` command pointer (`src/core/ControlCommandsSessionPerform.cpp:266`, historical line 266; now 266-267).

The null branch cannot be taken in any build, so there is no reachable defect and no product change. Evidence chain:

1. **The dereference exists as described.** `src/core/ControlCommandsSessionPerform.cpp:266` takes `ControlRegistry::instance()->command("project.open")` and `:267` calls `open->handler(...)` with no null check. `command()` returns `nullptr` for a missing id (`src/core/ControlRegistry.cpp:199-203`), so the pointer is unchecked.

2. **The registry singleton is always built with `project.open`.** `ControlRegistry::instance()` constructs the one instance and immediately calls `registerControlCommands(*s_instance)` (`src/core/ControlRegistry.cpp:161-170`). The constructor is private (`include/ControlRegistry.h:290`, under `private:` at `:289`), so nothing else can build a registry.

3. **Registration is unconditional.** `registerControlCommands` calls `registerProjectCommands(registry)` at `src/core/ControlRegistryRegistrations.cpp:87` with no `#if` or `if` around it (checked with `sed -n 63,90p`). That reaches `registerProjectFilesCommands` (`src/core/ControlCommandsProject.cpp:582`), which calls `registerProjectOpen` (`src/core/ControlCommandsProjectFiles.cpp:721`). `registerProjectOpen` sets `cmd.handler` at `:621` and calls `registry.registerCommand(cmd)` at `:713`.

4. **Nothing removes or replaces a command.** `ControlRegistry::registerCommand` refuses a duplicate id with `qFatal` (`src/core/ControlRegistry.cpp:177-185`). Grep for `m_commands` removal or reassignment in `src/core/ControlRegistry.cpp` finds nothing (`logs/013-no-removal.log`, EXIT=1 meaning no match). `ControlRegistry::destroy()` followed by `instance()` rebuilds the same full set.

So the "when" in the finding (the registry does not contain `project.open`) cannot occur. Hardening the check would be defensive only.

**Sibling callers are null-safe or call `invoke()`.** The other `project.open` callers go through `ControlRegistry::invoke()`, which returns `NotFound` for an unknown id (`src/core/ControlRegistry.cpp:262-268`): `src/core/CollabSessionSync.cpp:234`, `src/core/ControlCommandsAlternatives.cpp:208`, and `src/core/ControlCommandsDocuments.cpp:337` (via the local `run()` helper at `:65-67`, which calls `invoke()`). The direct `->handler` call in `setlistGo` is a deliberate choice documented at `src/core/ControlCommandsSessionPerform.cpp:265` (one undo record, not two), not an oversight. Other direct `->handler(` calls, `src/core/ControlCommandsNoteAcross.cpp:130` and `src/core/ControlCommandsRecordTools.cpp:69`, both null-check their command first (`ControlCommandsNoteAcross.cpp:116`, `ControlCommandsRecordTools.cpp:69` ternary). No sibling has the same unchecked pattern.

**Why no regression test.** A test that fails without a fix needs a registry lacking `project.open`. The API has no way to build one: the constructor is private, there is no removal or injection seam, and `instance()` always registers everything. Adding a test-only seam to production code for a state that cannot occur would be a behaviour change without a reachable defect, so it was not done.

**No product files changed.** `git diff --name-only 37eaf07557..HEAD` is empty and the worktree was clean at start (`logs/010-changed-vs-base.txt`). The only committed file is this report.

## Commands and results

| Check | Command (cwd = worktree unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| Gate script, as documented | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/001-feature-check.log 2>&1` | 1 | `001-feature-check.log`: `PermissionError` at `feature_check.py:71` (`logs.mkdir`), because it writes logs to `<worktree>/../<name>-feature-check`, outside the write-jail. The script could not produce its table. |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `002-fork-sources.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `003-all-sources-reproduce.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `004-no-upstream-regression.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `005-check-namespace.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `006-no-tautology.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `007-doc-figures.log` |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | `008-complexity.log` (6 `REGRESSION:` lines, all in files this branch does not change) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | `009-file-length.log` (23 `REGRESSION:` lines, same) |
| Changed-file set | `git diff --name-only 37eaf07557..HEAD` plus `git status --porcelain` | 0 | `010-changed-vs-base.txt`: 0 files |
| Ratchet item-to-changed-file match | inline python (no script file) reading `008`/`009` `REGRESSION:` lines into `011-regression-items.txt` (29 items) and matching each item's file against `010` | not recorded | printed `items in changed files: 0`, so the gate's "new in your files" test yields no failure |

**Gate result.** The six non-ratchet gates pass (EXIT 0). The two ratchets exit 1 only because of pre-existing items. The gate's own rule fails them only for items that are new AND in a changed file. This branch changes no files, so none qualifies and both rows would PASS. This is my reproduction of the script's logic, not the script's output. The script itself could not run in this sandbox (table above). Its checks were run with the same commands it uses, `cwd` set to the worktree, logs in the LOG FOLDER.

**Not run.** No build and no ctest were run, because no product code changed and the verdict rests on reading the code. `zene` was not rebuilt. The RED/GREEN regression procedure (steps 3, 4, 4b) does not apply to a FALSE-POSITIVE verdict.

## Self-check

`submit_check.py` (log `logs/015-submit-check.log`, EXIT=1) passes commits, clean tree, report and the test rule, and fails one row: **gates (feature_check.py)**. That row runs the same script that raised the `PermissionError` at `feature_check.py:71` in `logs/001-feature-check.log`, because it writes logs to `<worktree>/../<name>-feature-check`, outside the write-jail. This is an environment blocker, not a branch failure. The six non-ratchet gates were run with their own commands (EXIT 0, table above), and the two ratchets had no item in a changed file. I did not write into the parent directory and did not edit the shared gate script.

- **Regression test RED/GREEN.** Not applicable. The verdict is FALSE-POSITIVE, so there is no product change to revert and no test was added. The reason is in the report: a test that reaches the null branch would need a test-only seam into a singleton whose constructor is private and which has no removal path (`include/ControlRegistry.h:289-290`, `src/core/ControlRegistry.cpp:161-185`).
- **Deterministic.** Not applicable. No test was written.
- **Fix at the cause and complete.** No fix. The callers and siblings were checked: `invoke()`-based callers at `src/core/CollabSessionSync.cpp:234`, `src/core/ControlCommandsAlternatives.cpp:208` and `src/core/ControlCommandsDocuments.cpp:337` (via `run()` at `:65-67`); the direct handler calls at `src/core/ControlCommandsNoteAcross.cpp:130` (guarded at `:116`) and `src/core/ControlCommandsRecordTools.cpp:69`. None has the unchecked pattern.
- **No collateral damage.** Nothing changed in product code, so no saved data, project load or limit is affected. `git diff --name-only 37eaf07557..HEAD` is empty (`logs/010-changed-vs-base.txt`).
- **Verdict citations.** Given in the body: the dereference at `src/core/ControlCommandsSessionPerform.cpp:266-267`, the singleton at `src/core/ControlRegistry.cpp:161-170`, unconditional registration at `src/core/ControlRegistryRegistrations.cpp:87` → `src/core/ControlCommandsProject.cpp:582` → `src/core/ControlCommandsProjectFiles.cpp:721`, the handler at `:621` and `registerCommand` at `:713`, and no removal path (`logs/013-no-removal.log`).
