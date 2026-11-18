IMPLEMENTED

# Q050-235.1 — Touch workspace choice and launcher targets (stage 1/3)

Branch `feat/050-Q050-235.1-oc`, base `050/m0-train-12` (`07d6d02bd`). Only this stage's acceptance is
built; the arranger (stage 2) and the mixer fader / integrated tablet check (stage 3) are untouched,
and the shared seam they need (`tokens::tapTargetSize()`) is in place.

## Acceptance

| Criterion | Test (ran it myself) | What it proves | Exit |
|---|---|---|---|
| GUI and agent can select and read the workspace; one undo restores the prior choice | `WorkspaceAccessTest::theWorkspaceIsAnAppearanceFieldOfItsOwn` (QtTest) | `window.appearance {"workspace":"touch"}` replies `touch`, `restart_pending` true, `accessible` still off; `ui/workspace` holds `touch`; one `control.undo` puts `standard` back in the reply, `appearance::options()` and the config; `tablet` is a typed `invalid_args`; a stored `sideways` reads as `standard` | 0 (red 8) |
| (same — the GUI entry point actually driven) | `TouchWorkspaceTest::theMenuChoosesTheWorkspaceThroughTheCommand` | builds the real `populateCustomiseExtras` menu, finds **Theme > Workspace > standard / touch**, checks both declare `window.appearance`, **triggers** the standard entry, and reads the choice back from the command reply and the config; then one `control.undo` restores `touch` | 0 (red 8) |
| (same — the agent route over the socket) | ctest `ControlTouchWorkspace` (`tests/control-touch-workspace.py`) | default `standard`; set `touch`; read back; one `control.undo` -> `standard`; `control.redo` -> `touch`, over the real `--control-socket` | 0 (red 8) |
| At 100% global zoom, launcher cells and scene/stop actions have at least 44 × 44 Qt logical pixel hit areas | `TouchWorkspaceTest::launcherCellsAndSceneStopActionsReachTheTouchMinimum` | the process starts with `ui/workspace=touch` in its config (a real restart) at `scale` 1.0; asserts the laid-out `width()`/`height()` of every cell, scene button, per-track stop and Stop All is ≥ 44 × 44 | 0 (red 8; the red run prints `cell 0,0 is 72x26`) |
| (same — the rectangle really is the hit area) | `TouchWorkspaceTest::aPressNearTheCellCornerLaunchesIt` | `QTest::mouseClick` at `QPoint(2,2)` — inside the enlarged rectangle, outside the centred artwork — launches a cell, launches a scene and stops a column, checked against `session.get_state`’s engine reading | 0 (red 8) |
| The touch workspace is not accessible mode | `TouchWorkspaceTest::theTouchWorkspaceIsTheStartedOne`, `::theOldTouchDensityStillMeansAccessibleMode` | `tokens::touchWorkspace()` true while `tokens::accessibleMode()` false; `density:"touch"` still turns accessible mode on and leaves the workspace unchanged | 0 (red 8) |
| A fresh app restart retains the selected workspace in user configuration | ctest `ControlTouchWorkspace` | clean `control.quit`, **new process over the same `--config`**, `window.appearance` -> `touch`, and the config file itself holds `workspace="touch"`; choosing `standard` and restarting again reads `standard` | 0 (red 8) |
| Old configs open exactly as before | `WorkspaceAccessTest::theWorkspaceIsAnAppearanceFieldOfItsOwn`, `::theOldDensityValuesSetAccessibleMode` | an absent `ui/workspace` reads `standard`; the density alias behaves exactly as at base | 0 |

Regression proof (product hunks neutralised, tests untouched — see Self-check):
`ctest -R '^(TouchWorkspaceTest|WorkspaceAccessTest)$'` -> **exit 8** (`red-tests.log`),
`ctest -R '^ControlTouchWorkspace$'` -> **exit 8** (`red-socket.log`). Restored:
both -> **exit 0** (`green-tests2.log`, `socket-green2.log`).

Full green run of the touched suites (`TouchWorkspaceTest`, `WorkspaceAccessTest`, `SessionGridViewTest`,
`SessionSubSceneGridTest`, `SessionTransportBarTest`, `HelpAccessTest`, `DesignTokensTest`,
`ControlTouchWorkspace`): **exit 0, 8/8** (`green-final.log`).

## New commands and reversibility

None. `window.appearance` keeps `true_inverse` and one undo step; it gains the `workspace` argument
and reply field, and `src/core/ControlReversibilityTableDevices.cpp` now lists the workspace among
the four bounded values it writes. `ReversibilityContractTest` (exit 0) leaves the A16 histogram
unchanged (no row changed class). The commands snapshot was regenerated from a live instance with
`snapshot_live.py` (845 ids / 76 groups, the same ids; only `window.appearance`'s schema and
description changed), and `tests/doc-figures-gate.py` passes (exit 0).

## Format changes

None to the project format. The workspace is a user preference in `<config>/ui/workspace`.

## A required gate repair, alongside the feature

`tests/all-sources.txt` at this branch's base (`07d6d02bd`) listed 1148 vendored
`plugins/SfzPlayer/sfizz/**` paths that its own published recipe excludes on purpose, so
`all-sources-reproduce` (and therefore `feature_check.py`) was red on the untouched base before
any change of mine - reproduced with `git stash`-free pristine runs, exit 1 on both the branch and
the canonical `zene-cachyos` checkout at the same commit. The manifest header prescribes exactly
one repair ("run the 'Regenerate with' command ... write its output over the entry list"), and the
same repair was already made on the train separately as `14e19eb4d`. Applying the recipe here is
what makes the gate green; the delta is **0 additions and 1148 removals, every one under the
header's excluded vendored tree**, none of them in `tests/fork-sources.txt`, and no ratchet
baseline ever held one. The new test source (`tests/src/gui/TouchWorkspaceTest.cpp`) is in the
re-derived list. This is recorded here because the reviewer will see the large deletion in the
feature commit.

## Gates (exit codes)

All eight rows of `feature_check.py` PASS: fork-sources 0, all-sources-reproduce 0,
no-upstream-regression 0, check-namespace 0, no-tautology 0, doc-figures 0, complexity “no new item
in your files”, file-length “no new item in your files”. The runner's own
`<worktree>-feature-check` log directory could not be created in this sandbox (the parent directory
`…/zene-studio-program` refuses writes even after `chmod`), so the same script was run unmodified in
its checks with its log path pointed at `/tmp` (`/tmp/feature_check_run.py`; the only three edits are
`CACHE`, `logs` and the `FIX-BASE` path); results are in `logs/feature-check-correct.log`.

## Self-check

- **Runner environment (two non-code observations).** `submit_check.py` reports one FAIL,
  “gates (feature_check.py)”, because `feature_check.py` hardcodes its log directory to
  `<worktree>/../<name>-feature-check` and **this sandbox refuses every write in the worktree's
  parent** (`…/zene-studio-program`): `touch` and `mkdir` there fail with EACCES even after a
  successful `chmod`, while the lane worktree, `/tmp`, `~/.cache` and `.git` directories stay
  writable. The script itself is unmodified and all eight gate rows PASS when run against this
  worktree with only that log path redirected to `/tmp` (`/tmp/feature_check_run.py`, exit 0,
  `logs/feature-check-correct.log`). Its “canary figures (README)” WARN is a false positive of the
  added-line regex: the snapshot re-serialised and its `+` lines mention existing ids such as
  `control.undo`, but no id was added or removed (845 ids / 76 groups before and after,
  `doc-figures-gate.py` exit 0, README's canary unchanged).

- **Each new test FAILS with the product change reverted, then PASSES restored.** Reverted the
  product hunks to the base while keeping the new API so the tests compile and *neutralising every
  behaviour* (reversible patch `logs/red-mutilation.patch`: `ui/workspace` never read or written,
  the `workspace` argument ignored, `touchWorkspace()` false, `tapTargetSize()` = `targetSize()`,
  launcher back to 72 × 26). Rebuild `logs/red-build2.log` exit 0; `ctest -R
  '^(TouchWorkspaceTest|WorkspaceAccessTest)$'` **exit 8** with the expected messages (including
  `cell 0,0 is 72x26`); `ctest -R '^ControlTouchWorkspace$'` **exit 8**. Restored the real patch,
  rebuild `logs/green-build.log` exit 0; both ctests **exit 0**. (A first attempt that reverted the
  API entirely does not compile the new tests — recorded in `logs/red-build.log` exit 2 — so the
  behavioural replay above is the honest red proof.)
- **Every acceptance criterion is proven by a test I ran.** The table names the test and exit code
  for each; every log is under `/tmp/Q050-235.1.r1/logs/` (the harness's run directory
  `~/.local/share/zene-bunny-fix/qwen/runs/Q050-235.1.r1/logs` refuses writes from this sandbox, like
  the worktree's parent, so the logs live in `/tmp` and are named exactly).
- **One undo step, honest reversibility class.** `appearanceCommand` compares all four fields,
  writes once through `Appearance::setOptions`, and records a single `addUndoStep(before -> after)`;
  `WorkspaceAccessTest` asserts exactly one `control.undo` restores the prior workspace, and
  `ControlTouchWorkspace` asserts undo then redo of a single change. `window.appearance` remains
  `true_inverse` (it writes user config, and its inverse writes the captured `before` back).
  `ReversibilityContractTest` passes.
- **Nothing added runs on the audio thread.** The diff touches `Appearance` (config read/write on the
  GUI/command path), `DesignTokens` (a cached `static const bool`, read like the palette),
  `SessionGridView` (widget construction), `WorkspaceMenus`/`ControlCommandsHelp` (menus/commands)
  and the reversibility table. No render, `processImpl`, `play`, audio buffer, lock, wait or signal
  on any audio path. `ConfigManager::saveConfigFile()` is a GUI/command-thread file write; nothing
  on the audio thread calls `appearance::setOptions` or `tokens::touchWorkspace()`.
  The `submit_check.py` audio-path heuristic matched on the word `processImpl` **in this report's
  own prose**, not in a changed source file: `git diff <base>..HEAD | grep -nE 'processImpl|playImpl|
  renderPeriod|processAudioBuffer|AudioEngine::'` finds that single documentation line and nothing
  else, so no audio-path function is touched at all.
- **Old projects load and render exactly as before; new data round-trips.** No project-format or
  save/load file changed; the preference is `<config>/ui/workspace`, absent means `standard`. The
  launcher's standard size is byte-for-byte the previous 72 × 26, so existing layouts are unchanged.
- **Portability (Qt 5.15 / MSVC / macOS < 10.15).** Only Qt classes already used across the tree
  (`QString`, `QStringList`, `QJsonObject`, `QToolButton`, `QAction`, `QMenu`, `QTemporaryDir`,
  `QTest::mouseClick`); no Qt-6-only API and no `QT_VERSION`-sensitive code; no `windows.h` macro
  names (`small`, `near`, `far`, `interface`, `min`, `max` as identifiers); no `std::filesystem`;
  the test uses `std::max`/`std::pair` through the config’s existing includes and `<utility>`.
