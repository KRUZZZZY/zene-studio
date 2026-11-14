FALSE-POSITIVE

# Cluster gui-1114-menu-navigate-fit-selection: "Fit Selection silently uses the whole-song range when selection is empty"

## Verdict: not a bug in `fitRange`; the patrol saw no notice because an agent instance logs refusals instead of floating them (by design)

The finding's root cause does not match this tree. `fitRange` does not keep the whole-song range when
`selectionSpan` answers false: `selectionSpan` always writes its two out-parameters, and in the
nothing-selected case it writes `begin = INT32_MAX, end = 0`, so the `*end <= *begin` guard on the very
next line refuses. The command-level behaviour is a typed `Refused` with the message
`nothing to fit: the selection is empty`, before any zoom or scroll happens. I measured that on two
binaries: the current tree and the exact binary the patrol clicked (`0.4.0-alpha.7036+d0edd14`).

What the patrol observed - the menu closing with no notice - is the documented agent-mode rule in
`CommandRun::announce`: an unattended (agent) instance has nobody to read a float, so a refusal is
logged instead. The patrol launches Zene through `tests/control_socket_harness.py`, which passes
`--control-socket`; `ControlRegistry` then sets `m_headless` from `isUnattendedRun()`, and `announce()`
returns before creating a `TextFloat`. Luna's triage already closed the same symptom on the sibling
Navigate items (Zoom Back/Forward/Toggle, Nudge Left/Right, Next/Previous Clip, Next/Previous Marker)
as "agent-mode refusal notices are intentionally suppressed"; Fit Selection behaves exactly like them.

## Code evidence

| Step | Location | Code |
| --- | --- | --- |
| Whole-song span is only a default | `src/gui/ControlCommandsArrangerView.cpp:92-93` | `*begin = 0; *end = max(1, song->length()) * ticksPerBar();` |
| The selection branch overwrites both ends through `selectionSpan` | `src/gui/ControlCommandsArrangerView.cpp:100-101` | `else if (fit == "selection") { gui::arrangerzoom::selectionSpan(editor, begin, end); }` |
| Nothing selected: the out-params are written to "empty" | `src/gui/ArrangerZoom.cpp:84-96` | `*begin = numeric_limits<tick_t>::max(); *end = 0; ... return *end > *begin;` |
| A time range writes the range and returns true | `src/gui/ArrangerZoom.cpp:115-125` | `if (range.active()) { *begin = range.start(); *end = range.end(); return true; }` |
| The guard refuses the untouched/empty span | `src/gui/ControlCommandsArrangerView.cpp:102-103` | `if (*end <= *begin) { *error = "nothing to fit: the %1 is empty" } return *end > *begin;` |
| The refusal becomes a typed failure, before `fitSpan` | `src/gui/ControlCommandsArrangerView.cpp:195-199` | `if (!fitRange(...)) return ControlResult::failure(ControlErrorKind::Refused, error); fitSpan(...);` |
| Agent instance is headless | `src/core/ControlRegistry.cpp:159` | `m_headless = isUnattendedRun();` |
| Headless = agent instance or no display | `src/core/UnattendedRun.cpp:83` (`include/UnattendedRun.h:45-57`) | `return s_agentInstance \|\| platformHasNoDisplay();` |
| Refusal is logged in that mode, then the float is suppressed | `src/gui/CommandRun.cpp:61-64,103` | `qWarning("%s refused: %s", ...);` / `if (ControlRegistry::instance()->isHeadless() ...) { return; }` |
| The menu entry runs that path | `src/gui/MenuBarExtras.cpp:70-71,81` | `addCommandAction(..., "arrangement.zoom", {{"fit","selection"})` |
| Patrol launch mode | `terra-pilot/gui.py:84` -> `tests/control_socket_harness.py:258` | `H.start_instance(...)` -> `[binary, "--config", ..., "--control-socket", socket]` |

`git log -L` on `fitRange` shows the initial version (`21f1965ff7`) already reset `begin`/`end` to
`INT32_MAX`/`0` for the selection branch; commit `bcac7c05ce` only moved that loop into
`selectedClipsSpan`. There has never been a revision where a failed selection left the whole-song span in
place.

## Measured evidence (fresh project, no range, no selected clips)

| Binary | Request | Answer |
| --- | --- | --- |
| `build/zene` (`0.4.0-alpha.7037+7798296`, HEAD) | `arrangement.zoom {"fit":"selection"}` | `ok:false, error.kind:"refused", message:"nothing to fit: the selection is empty"` |
| `/home/kruzzzzy/.local/bin/zene-studio-latest` (`0.4.0-alpha.7036+d0edd14`, the patrol's binary) | same | same refusal |
| `build/zene` | `range.set {start:192,end:768}` then `arrangement.zoom {"fit":"selection"}` | `ok:true, pixels_per_bar:247`, scroll to tick 192 |
| `build/zene` | `arrangement.zoom {"fit":"song"}` | `ok:true, pixels_per_bar:400` (it does change the view in a fresh project) |

Logs: `02-evidence-fit-selection.log`, `09-evidence-pinned-patrol-binary.log`.

## Negative control: the finding's described defect is already regressed against

To prove that the tree would catch the reported defect if it existed, I temporarily injected exactly the
described root cause (`fitRange` keeps the whole-song range when `selectionSpan` returns false), rebuilt,
and ran the checks:

- command probe: `EXIT=1`, `fresh arrangement.zoom {fit: selection}` answered
  `ok:true ... "pixels_per_bar":400` and moved the zoom 151 -> 200 (the silent whole-song fit), where the
  real tree answers `refused`;
- the EXISTING test `GrooveMenuTest::aMessageOverTheMainWindowLandsInsideIt` (which triggers the real
  Navigate > Fit Selection action in a fresh project and requires a refusal float) failed with
  `EXIT=1`, `FAIL! ... 'shown.size() == 1' returned FALSE. (Fit Selection with nothing selected showed no refusal)`.

I restored the product file (sha256 back to `920e9ecf...`) and rebuilt; the family is green again. So a
regression guard for this exact behaviour is already committed, and no product change is needed.

## Commands run and exit codes (copied from the runs)

| # | Command (from the worktree unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target GrooveMenuTest ArrangerTimelineTest -j$(nproc)` | 0 | `01-build-evidence-targets.log` |
| 2 | `QT_QPA_PLATFORM=offscreen python3 fit_selection_evidence.py` (build/zene, fresh project) | 0 | `02-evidence-fit-selection.log` |
| 3 | `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure` | 0 (1/1) | `03-test-groovemenu.log` |
| 4 | `bash tests/fork-sources-gate.sh` | 0 | `04-baseline-gate-logs/fork-sources.log` |
| 5 | `bash tests/all-sources-reproduce.sh` | 0 | `04-baseline-gate-logs/all-sources-reproduce.log` |
| 6 | `bash tests/no-upstream-regression-gate.sh` | 0 | `04-baseline-gate-logs/no-upstream-regression.log` |
| 7 | `tests/scripted/check-namespace` | 0 | `04-baseline-gate-logs/check-namespace.log` |
| 8 | `bash tests/no-tautology-gate.sh` | 0 | `04-baseline-gate-logs/no-tautology.log` |
| 9 | `python3 tests/doc-figures-gate.py` | 0 | `04-baseline-gate-logs/doc-figures.log` |
| 10 | `bash tests/complexity-gate.sh --check` | 0 | `04-baseline-gate-logs/complexity.log` |
| 11 | `bash tests/file-length-gate.sh --check` | 1 (red on the train; no REGRESSION line names a file I touched) | `04-baseline-gate-logs/file-length.log` |
| 12 | `cd build/tests && ./GrooveMenuTest aMessageOverTheMainWindowLandsInsideIt` | 0 | `05-test-fit-notice-slot.log` |
| 13 | `cd build/tests && ctest -R '^(GrooveMenuTest\|ArrangerTimelineTest)$' --output-on-failure` | 0 (2/2) | `06-test-family.log` |
| 14 | `ZENE_BINARY=/home/kruzzzzy/.local/bin/zene-studio-latest QT_QPA_PLATFORM=offscreen python3 fit_selection_evidence_binary.py` | 0 | `09-evidence-pinned-patrol-binary.log` |
| 15 | inject the described root cause, `cmake --build build --target zene GrooveMenuTest -j$(nproc)` | 0 | `15-build-negative-control.log` |
| 16 | `QT_QPA_PLATFORM=offscreen python3 fit_selection_evidence.py` (injected defect) | 1 (expected a refusal, got `ok:true`) | `16-evidence-injected-defect.log` |
| 17 | `cd build/tests && ./GrooveMenuTest aMessageOverTheMainWindowLandsInsideIt` (injected defect) | 1 (existing guard: "shown no refusal") | `17-existing-guard-vs-injected-defect.log` |
| 18 | restore product file, `cmake --build build --target zene GrooveMenuTest -j$(nproc)` | 0 | `19-build-restored.log` |
| 19 | `cd build/tests && ctest -R '^(GrooveMenuTest\|ArrangerTimelineTest)$' --output-on-failure` (restored) | 0 (2/2) | `20-test-family-restored.log` |
| 20 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py "$PWD"` | 0 (all rows PASS) | `18-feature-check-canonical-retry.log` |
| 21 | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py "$PWD" gui-1114-menu-navigate-fit-selection` (before the report) | 1 (expected: missing report, nothing committed yet) | `21-submit-check-before-report.log` |
| 22 | `git add docs/qwen-fixes-2026-10/cluster-gui-1114-menu-navigate-fit-selection.md && git commit -m "fix(cluster gui-1114-...)"` | 0 | `22-commit.log` |
| 23 | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py "$PWD" gui-1114-menu-navigate-fit-selection` (after the commit) | 0, every row PASS | `23-submit-check-after-commit.log`, `25-submit-check-final.log` |
| 24 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py "$PWD"` (after the report commit; 1 changed file) | 0, every row PASS | `24-feature-check-final.log` |

The first `feature_check.py` attempt exited 1 on `bunny-guard: 'git -C .../gate-baselines/tree checkout ...'
is not allowed in a Bunny lane` (log `00-feature-check-canonical.log`); the base listing was computed by the
gate work itself, and the retry (row 20) then passed every row.

## Logs: the given LOG FOLDER was not writable from this lane

`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1114-menu-navigate-fit-selection.r1/logs` is
outside this session's write-jail (Landlock: `touch` -> `Permission denied`, also after `chmod u+w`; the
same was reported by the accepted gui-1105 lane). All logs are kept in
`$TMPDIR/zene-qwen-gui-1114-menu-navigate-fit-selection/logs/` (on disk, not `/tmp`) and copied to
`build/gui-1114-logs/` inside the worktree (git-ignored via `/build*/`, so the tree stays clean).

## Files committed

- `docs/qwen-fixes-2026-10/cluster-gui-1114-menu-navigate-fit-selection.md` (this report)

No product or test file changed: the behaviour the finding describes is absent, and the existing
`GrooveMenuTest` already guards it (proved with the injected-defect run).

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).** There is
  no product change to revert; the finding is FALSE-POSITIVE. The already-committed guard
  `GrooveMenuTest::aMessageOverTheMainWindowLandsInsideIt` was run against an injected copy of the
  finding's exact root cause: `EXIT=1`, `FAIL! ... 'shown.size() == 1' returned FALSE. (Fit Selection with
  nothing selected showed no refusal)` (`17-existing-guard-vs-injected-defect.log`), and the command probe
  (`EXIT=1`) saw the silent whole-song fit (`16-evidence-injected-defect.log`). On the real tree the same
  test is `EXIT=0` (`05-test-fit-notice-slot.log`) and the probe is `EXIT=0` (`02-evidence-fit-selection.log`).
- **Deterministic: no wall-clock timing, sleeps or races.** The probe drives the real socket synchronously
  (bounded reads, no sleeps chosen by the test); the QTest guard calls `QAction::trigger()` and reads the
  float synchronously (`announce()` creates it before returning) and only uses one `QTRY` for the scroll
  value after the real scrollbar move.
- **At the cause and complete: every caller and sibling checked.** `fitRange` has one caller
  (`ControlCommandsArrangerView.cpp:195`), and the selection branch is the only one touched by this finding.
  `selectionSpan`'s other caller, `zoomAnchor` (`ControlCommandsArrangerView.cpp:141`), already tests its
  bool and keeps its fallback; `keyboardAnchor` (`ArrangerZoom.cpp:135-143`) does the same. Fit Song and Fit
  Loop take the other branches; both still succeed (measured). `tick_t` is `int32_t`
  (`include/LmmsTypes.h:35`), wide enough for `INT32_MAX` sentinel and `*end > *begin`; no narrowed type is
  involved. `fit_tracks` is a different command and untouched. Siblings in this file (`zoom_back`,
  `zoom_forward`, `zoom_toggle`, `set_tool`) are not on the fit path.
- **No collateral damage: old projects and saved data load as before.** No product file, save format, undo
  step, command registration or limit changed. Nothing here runs on the audio thread. The only checks on the
  path are the existing `*end <= *begin` guard and the zoom model clamp.
- **If ALREADY-FIXED or FALSE-POSITIVE: the code or contract that shows it.** `fitRange`'s branch
  (`ControlCommandsArrangerView.cpp:100-103`) calls `selectionSpan`, which always writes both ends
  (`ArrangerZoom.cpp:84-96,115-125`), so an empty selection yields `end <= begin` and a `Refused`; measured
  on the current build and on the patrol's pinned binary. The missing notice is
  `CommandRun.cpp:61-64,103` with `ControlRegistry.cpp:159` (`isUnattendedRun()` true for the patrol's
  `--control-socket` launch, `terra-pilot/gui.py:84` -> `tests/control_socket_harness.py:258`), which is the
  documented agent-mode rule and how Luna's triage closed the sibling Navigate refusals. If the product
  owner wants refusals floated in agent mode, that is a separate cross-cutting decision (it would change
  every menu command's behaviour, not Fit Selection), not this finding.
