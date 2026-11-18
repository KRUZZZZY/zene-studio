FIXED

## Finding and evidence

**Verdict: REAL.** Before the fix, `project.save_as_template` at branch HEAD called `Song::saveProjectFile(path)` and returned only `path` and `replaced`; it neither captured the overwritten bytes nor recorded an undo action (`src/core/ControlCommandsProjectLifecycle.cpp`, pre-fix lines 197–209). Its contract row said `irreversible` and instructed callers to copy `default.mpt` aside (`src/core/ControlReversibilityTableShell.cpp`, pre-fix row for `project.save_as_template`). The pre-fix test run confirms `control.undo` refused both default and named replacements as irreversible.

The handler now captures the prior template and, when DataFile will replace it, the prior `.bak` before writing (`src/core/ControlCommandsProjectLifecycle.cpp:316–329`). After a successful write it captures the published bytes and records one byte-budgeted ProjectJournal action whose undo and redo restore the two file states (`src/core/ControlCommandsProjectLifecycle.cpp:340–375`). Failed restoration reaches `control.undo` through the existing action-restore failure channel. The shell contract row now declares `true_inverse` (`src/core/ControlReversibilityTableShell.cpp:52–57`).

`ControlProjectTemplateTest` checks replacement of the default and a named template, exact restoration of the prior template and backup bytes, redo, and undo/redo of a newly created named template (`tests/src/core/ControlProjectTemplateTest.cpp:63–143`). The default-template GUI action and the shell menu both continue to dispatch this same command (`src/gui/MainWindow.cpp:1191–1209`; `src/gui/shell/ShellCommandMenus.cpp:509`). No project format or audio-thread code changed.

## Commands and results

| Purpose | Command and working directory | Exit |
| --- | --- | ---: |
| Original RED before production edits; `build/tests` | `ctest -R ControlProjectTemplateTest --output-on-failure` (log: `test-3.log`) | 8 |
| 4b: reverse only product patch; worktree root | `git diff -- src/core/ControlCommandsProjectLifecycle.cpp src/core/ControlReversibilityTableShell.cpp > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-34-port-port-port-port-port-port.r2/logs/fix.patch" && git apply -R "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-34-port-port-port-port-port-port.r2/logs/fix.patch"` | 0 |
| Rebuild without product patch; worktree root | `cmake --build build --target zene ControlProjectTemplateTest` (log: `build-16.log`) | 0 |
| Final regression RED without product patch; `build/tests` | `ctest -R ControlProjectTemplateTest --output-on-failure` (log: `test-13.log`; default, named and create cases failed at `control.undo`) | 8 |
| 4b: restore product patch; worktree root | `git apply "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-34-port-port-port-port-port-port.r2/logs/fix.patch"` | 0 |
| Rebuild with product patch; worktree root | `cmake --build build --target zene ControlProjectTemplateTest` (log: `build-17.log`) | 0 |
| Final regression GREEN; `build/tests` | `ctest -R ControlProjectTemplateTest --output-on-failure` (log: `test-14.log`) | 0 |
| Rebuild related targets; worktree root | `cmake --build build --target zene ControlProjectTemplateTest ControlProjectArchiveTest ControlCommandsSaveVersionTest` (log: `build-18.log`) | 0 |
| Related CTest family; `build/tests` | `ctest -R 'ControlProject|ControlCommandsSaveVersion' --output-on-failure` (log: `test-15.log`) | 0, 3/3 tests |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-34-port-port-port-port-port-port` (log: `gate-4.log`) | 0, all PASS |
| Post-commit completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-34-port-port-port-port-port-port new-p12-34-port-port-port-port-port-port` (log: `gate-submit-1.log`) | 0, PASS - submit |

The source manifest was regenerated using the recipe in `tests/all-sources.txt`; this registers the new C++ test in the whole-tree scope and removes the 1,148 stale vendored sfizz entries that the recipe explicitly excludes. No gate threshold or baseline changed.

## QA round 2 follow-up

Luna's blocking scope finding was valid: the new regression test was present in `tests/all-sources.txt` but absent from `tests/fork-sources.txt`, so `tests/fork-sources-gate.sh` classified it as inherited and the fork-scoped per-file ratchets did not measure it. Added `tests/src/core/ControlProjectTemplateTest.cpp` to `tests/fork-sources.txt:2361`, with a scope note covering default, named, and new-template undo/redo behavior. No product code changed in this follow-up.

| Purpose | Exact command and working directory | Exit |
| --- | --- | ---: |
| Rebuild application and focused project test targets; worktree root | `cmake --build build --target zene ControlProjectTemplateTest ControlProjectArchiveTest ControlCommandsSaveVersionTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-34-port-port-port-port-port-port.r3/logs/build-1.log" 2>&1` | 0 |
| Focused project CTest family; `build/tests` | `ctest -R 'ControlProject|ControlCommandsSaveVersion' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-34-port-port-port-port-port-port.r3/logs/test-1.log" 2>&1` | 0, 3/3 tests |
| Feature gates; worktree root | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-34-port-port-port-port-port-port" > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-34-port-port-port-port-port-port.r3/logs/gate-2.log" 2>&1` | 0, all PASS |
| Post-commit completion check; worktree root | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-34-port-port-port-port-port-port" new-p12-34-port-port-port-port-port-port > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-34-port-port-port-port-port-port.r3/logs/gate-submit-1.log" 2>&1` | 0, PASS - submit |

The focused family ran `ControlProjectArchiveTest`, `ControlProjectTemplateTest`, and `ControlCommandsSaveVersionTest`. The feature table reports PASS for `fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity`, and `file-length`.

## Files committed

- `src/core/ControlCommandsProjectLifecycle.cpp`
- `src/core/ControlReversibilityTableShell.cpp`
- `tests/src/core/ControlProjectTemplateTest.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-34-port-port-port-port-port-port.md`

## Self-check

1. **Regression proof:** With product files reversed, `cmake --build build --target zene ControlProjectTemplateTest` exited 0 and `ctest -R ControlProjectTemplateTest --output-on-failure` exited 8 (`build-16.log`, `test-13.log`); the default, named, and create cases failed at `control.undo`. After reapplying the exact product patch, the same rebuild exited 0 and CTest exited 0 (`build-17.log`, `test-14.log`). The earlier original-code RED was also exit 8 (`test-3.log`).
2. **Determinism:** `tests/src/core/ControlProjectTemplateTest.cpp:63-143` writes fixed template and backup byte arrays into its own `QTemporaryDir`, then drives save, undo, and redo synchronously through `ControlRegistry`. It uses no clock, sleeps, races, or timing seam.
3. **Cause, callers, and related project commands:** `src/core/ControlCommandsProjectLifecycle.cpp:294-381` is the shared `project.save_as_template` handler. The default-template GUI action and shell menu both dispatch that command (`src/gui/MainWindow.cpp:1191-1209`; `src/gui/shell/ShellCommandMenus.cpp:509`), so the default and named paths share the same inverse. `project.new` reads a template (`ControlCommandsProjectLifecycle.cpp:250`); `project.import`, `project.export_midi`, and `transport.set_metronome` use separate handlers at lines 383, 426, and 501; `project.save_version` writes a new version path (reversibility row `ControlReversibilityTableShell.cpp:64`). None replaces the template file. The focused project family passed 3/3 tests.
4. **Collateral damage:** The fix changes no session document format, serialization, or project load path. It adds no size limit; captured undo/redo bytes use the existing ProjectJournal byte budget. Normal project saves and old project loading are untouched.
5. **Alternative verdict prompt:** Not applicable: the verdict is REAL. The pre-fix handler at `src/core/ControlCommandsProjectLifecycle.cpp` did not record an inverse, the pre-fix row was `Irreversible` in `src/core/ControlReversibilityTableShell.cpp:52`, and the RED run above proves `control.undo` could not restore a replaced template.
6. **Completion-check sibling IDs:** Each listed sibling stays consistent because it does not overwrite a template. `app.about` and `app.online_help` are dialog/browser actions (`ControlReversibilityTableShell.cpp:142,146`); `app.ask` and `app.whats_new` read bundled descriptions and release notes (`ControlReversibilityTableW3Custom.cpp:61,63`); `app.tasks` and `app.task_cancel` inspect/cancel background tasks (`ControlReversibilityTableW3Library.cpp:96-97`); `app.help`, `app.tours`, `app.tour`, and `transport.low_latency` are interface/device state (`ControlReversibilityTableDevices.cpp:99,119-121`); `app.version` and `transport.get_state` are reads (`ControlReversibilityTablePassive.cpp:250,336`); `transport.audition_start` and `transport.audition_stop` only manage audition state and restore mutes (`ControlReversibilityTableArranger.cpp:113-116`); `transport.groove` changes global swing through its own inverse (`ControlReversibilityTableDevices.cpp:147`). None shares the template file write path changed here.
