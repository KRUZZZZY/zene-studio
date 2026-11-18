FIXED

# Cluster gui-1106-menu-navigate-zoom-out — Zoom Out silently no-ops at minimum zoom

## Verdict: REAL, fixed

Navigate > Zoom Out sends `arrangement.zoom {step: -4}` (`src/gui/MenuBarExtras.cpp:74`). Navigate > Zoom In and the
arranger panel shortcuts (`src/gui/PanelShortcuts.cpp:97-99`) send the same command with ±4. `requestedZoom` clamps the
stepped value to the zoom model's range (`src/gui/ControlCommandsArrangerView.cpp:146-153`; range 0..`ZOOM_STEPS`,
`src/gui/editors/SongEditor.cpp:86,98`). The zoom model's starting value comes from the saved `ui/songeditorzoom`
setting (`SongEditor.cpp:185`), so a fresh project can start at the minimum (4 px per bar). Before this fix, the step at
the bound left the model unchanged and `zoom()` still returned `ControlResult::success`. The menu runner shows nothing
for a success with `showResult == false` (`src/gui/CommandRun.cpp:90`), so the click had no visible effect and no
message. The finding's repro matches this exactly.

The sibling commands already refuse when they cannot move: `arrangement.zoom_back`, `zoom_forward` and `zoom_toggle`
return a `NotFound` failure (`ControlCommandsArrangerView.cpp`, `zoomBack`/`zoomForward`/`zoomToggle`). The step zoom was
the odd one out.

## Fix (at the cause)

`src/gui/ControlCommandsArrangerView.cpp:155-160`: a new helper `stepAtBound(editor, args)`. It is true for a step zoom
(no `pixels_per_bar`; the step is non-zero, default 1) whose clamped target equals the current value. In that case
`zoom()` returns `Refused` "already at the zoom limit" (`:188`). The menu runner shows that failure as a 4 s message
(`CommandRun.cpp:93-96`). So Zoom Out at the minimum, and Zoom In at the maximum, now tell the user why nothing
happened.

The check is placed after the `start`/`end` and `fit` branches, so span and fit zooms, which take precedence over
`step`, are unchanged.

Scope / callers checked:
- `pixels_per_bar` (absolute) zooms are unchanged and still succeed when the value is already shown. Callers:
  `MenuBarExtras.cpp:123` presets, `ControlCommandsViewMemory.cpp:298` view recall, `tests/control-customisation.py:350`.
  The test asserts this.
- `step: 0` is still a success (nothing was asked).
- An oversized step from below the max still clamps and succeeds (`anOversizedZoomStepClampsToMaximum`, passes).
- The command's schema and description are unchanged, so the ControlCommandsSnapshot does not move. No new command was
  added.
- GUI thread only; no audio-thread code is touched. A refused zoom does not change the model, so it pushes nothing onto
  the zoom history (`remember` only runs when the value changed, `:194`). No undo involvement (zoom is interface state).
- No saved data is affected.

## Regression test

`tests/src/gui/ArrangerTimelineTest.cpp:499` `aZoomStepAtItsBoundIsRefusedNotASilentSuccess`. It is deterministic and
uses no timing. It covers four cases:
- At the minimum, step -4 is `Refused` and the value is unchanged.
- From the minimum, step +4 succeeds (min+4).
- At the maximum, step +4 is `Refused`.
- A repeated absolute `pixels_per_bar: 40` stays ok.

## Integration (r2 — this worktree, target 050/m0-train-26 @ af010bc3cc)

The accepted patch (`bugs/qwen-gui-1106-menu-navigate-zoom-out @ eac7b710`) is based on the pinned target itself
(`git merge-base eac7b710 af010bc3cc` = af010bc3cc), so the three-way merge applies cleanly. `git diff eac7b710 --
src/gui/ControlCommandsArrangerView.cpp tests/src/gui/ArrangerTimelineTest.cpp
docs/qwen-fixes-2026-10/cluster-gui-1106-menu-navigate-zoom-out.md` is empty: the integrated tree is byte-identical to
the accepted patch, so there is no semantic difference from it to verify beyond the builds and tests below.

The reported "newer line" is the sibling accepted patch for gui-1106-menu-navigate-zoom-in (`c16334ca93`, merged on
`050/m0-train-27`), which adds a different helper `stepBoundProblem` in the same region of
`src/gui/ControlCommandsArrangerView.cpp` (after `requestedZoom`, plus a check inside the `else`). That line is not on
train-26: `stepBoundProblem` is absent here and `tests/src/gui/ArrangerZoomBoundTest.cpp` does not exist. There is
therefore no conflict to resolve on the pinned target and the accepted design (`stepAtBound`) is retained unchanged.
Forward-merge hazard (outside this pinned integration): when train-26 is later merged into train-27 the two patches
meet in the same lines; both refuse the same no-op step but differ in message and placement, so that merge needs a
deliberate reconciliation.

Affected failure paths (all through the one handler `zoom()`):
- `arrangement.zoom {step: -4}` at the minimum is now `Refused` ("already at the zoom limit") instead of a silent
  success: Navigate > Zoom Out (`src/gui/MenuBarExtras.cpp:74`), the arranger panel Zoom Out shortcut
  (`src/gui/PanelShortcuts.cpp:99`) and the navigator's `-` key (`src/gui/ArrangerNavigator.cpp:361`).
- `arrangement.zoom {step: +4}` at the maximum is likewise `Refused`: Navigate > Zoom In (`MenuBarExtras.cpp:73`),
  the panel Zoom In shortcut (`PanelShortcuts.cpp:97`), the navigator's `+`/`=` key.
- Any non-zero step whose clamped target already equals the current value (e.g. `step: 1` at the bound, an oversized
  step at the bound) takes the same path.
- Unaffected: `pixels_per_bar` absolute zooms (Zoom Presets `MenuBarExtras.cpp:123`, view recall
  `src/gui/ControlCommandsViewMemory.cpp:298`, `tests/control-customisation.py:350`,
  `tests/src/gui/VideoClipsGuiTest.cpp:152`), `start`/`end` span zooms (`ArrangerNavigator.cpp:387`,
  `src/gui/ArrangerOverlays.cpp:225`, `src/gui/ArrangerRulerLanes.cpp:503`), `fit` zooms (their own earlier
  `Refused`), and `step: 0`.

Supported-platform hazards: none new. The change is GUI-thread only (`src/gui`), uses only constructs already in the
file (`QStringLiteral`, `IntModel`, `QJsonObject`, `std::clamp`), adds no Qt-6-only API, no windows.h macro, no
std::filesystem, no new file and no registration change; the command schema and description are untouched, so the
ControlCommandsSnapshot canary does not move. Realtime: no audio-thread code is touched.

### r3 re-verification (LOG FOLDER = /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-navigate-zoom-out.r3/logs)

r2 was accepted by QA (`runs/...r2/qa-session-2.log`: verdict ACCEPT, no blocking items, no rework instructions). r3
re-dispatched the same integration job, so this round re-checks it with no code change. The branch head is still
`7ea5ec2884` on the pinned target `af010bc3cc`. `git merge-base eac7b710 af010bc3cc` = af010bc3cc. `git diff --stat
eac7b710 -- src tests` is empty (EXIT=0), so the tree is still byte-identical to the accepted patch. The only file this
round changes is this report.

| Step | Command | EXIT |
| --- | --- | --- |
| GREEN build | `cmake --build build --target zene ArrangerTimelineTest -j4 > LOGS/01-build-green.log` | 0 |
| GREEN test | `cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure > LOGS/02-test-green.log` | 0 (1/1) |
| 4b revert | `git diff af010bc3cc -- src/gui/ControlCommandsArrangerView.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` | 0 |
| 4b RED build | `cmake --build build --target zene ArrangerTimelineTest -j4 > LOGS/03-build-red.log` | 0 |
| 4b RED test | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > LOGS/04-test-red.log` | **8** (`ArrangerTimelineTest.cpp(504)`: `'!out.ok' returned FALSE. (Zoom Out at the minimum zoom reported success without zooming)`) |
| 4b reapply | `git apply LOGS/fix.patch` | 0 |
| 4b GREEN build | `cmake --build build --target zene ArrangerTimelineTest -j4 > LOGS/05-build-green-restored.log` | 0 |
| 4b GREEN test | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > LOGS/06-test-green-restored.log` | 0 (1/1) |
| Family build (attempt 1, my error) | `07-build-family.log`: zsh passed the nine test names as one target word ("No rule to make target"), so only `zene` was rebuilt. The ctest run after it (`08-test-family.log`, EXIT=8, 8/9 failed) ran against test binaries that had not been built. This was a harness mistake, not a product result | 2 / 8 |
| Family build | `cmake --build build -j4 --target zene ArrangerTimelineTest VideoClipsGuiTest LoopDropWarpTest ClipWaveformEditTest BrowserDragUrlTest ArrangerFileDropTest ArrangerTakePunchTest ArrangerOverlaysVideoLaneTest ArrangerKeysNavigatorTest > LOGS/10-build-family.log` | 0 |
| Family tests | `ctest -R '^(ArrangerTimelineTest\|VideoClipsGuiTest\|LoopDropWarpTest\|ClipWaveformEditTest\|BrowserDragUrlTest\|ArrangerFileDropTest\|ArrangerTakePunchTest\|ArrangerOverlaysVideoLaneTest\|ArrangerKeysNavigatorTest)$' --output-on-failure > LOGS/11-test-family.log` | 0 (9/9) |
| Gates (as given) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py . > LOGS/09-gates.log` | 1: `PermissionError` creating `<worktree>-feature-check/fork-sources.log`. That path is outside this lane's write-jail, so no gate ran |
| Gates (log dir moved) | `python3 -I runs/...r3/fc-script/feature_check_local.py <worktree> > LOGS/13-gates.log` | **0**: 8/8 PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity "no new item in your files", file-length "no new item in your files") |

The local gate copy differs from the original in exactly two lines (`LOGS/12-fc-copy.diff`). `ROOT` is pinned to the real
qwen dir, so it uses the same FIX-BASE and baselines. The logs dir is moved into the LOG FOLDER. No gate, limit or
baseline is changed.

### r2 evidence (LOG FOLDER = /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-navigate-zoom-out.r2/logs)

| Step | Command | EXIT |
| --- | --- | --- |
| GREEN build | `cmake --build build --target zene ArrangerTimelineTest -j4 > LOGS/01-build-green.log` | 0 |
| GREEN test | `cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure > LOGS/03-test-green.log` | 0 (1/1) |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py . > LOGS/02-gates.log` | 0 (8/8 PASS) |
| 4b revert | `git apply -R LOGS/fix.patch` | 0 |
| 4b RED build | `cmake --build build --target zene ArrangerTimelineTest -j4 > LOGS/04-build-red.log` | 0 |
| 4b RED test | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > LOGS/05-test-red.log` | **8** (`'!out.ok' returned FALSE. (Zoom Out at the minimum zoom reported success without zooming)`) |
| 4b reapply | `git apply LOGS/fix.patch` | 0 |
| 4b GREEN build | `cmake --build build --target zene ArrangerTimelineTest -j4 > LOGS/06-build-green-restored.log` | 0 |
| 4b GREEN test | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > LOGS/07-test-green-restored.log` | 0 (1/1) |
| Family build | `cmake --build build --target zene ArrangerTimelineTest VideoClipsGuiTest LoopDropWarpTest ClipWaveformEditTest BrowserDragUrlTest ArrangerFileDropTest ArrangerTakePunchTest ArrangerOverlaysVideoLaneTest ArrangerKeysNavigatorTest -j4 > LOGS/08-build-family.log` | 0 |
| Family tests | `ctest -R '^(ArrangerTimelineTest\|VideoClipsGuiTest\|LoopDropWarpTest\|ClipWaveformEditTest\|BrowserDragUrlTest\|ArrangerFileDropTest\|ArrangerTakePunchTest\|ArrangerOverlaysVideoLaneTest\|ArrangerKeysNavigatorTest)$' > LOGS/09-test-family.log` | 0 (9/9) |

Affected-family test count: 9 ctest executables, all passing.

## Evidence (r1 — the accepted attempt, LOG FOLDER = /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-navigate-zoom-out.r1/logs)

Builds used `CCACHE_DISABLE=1`. The first build (`01-build-red.log`, EXIT=2) failed only because ccache could not
write its temp dir `~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6` (Permission denied, outside the write-jail).

| Step | Command | EXIT |
| --- | --- | --- |
| RED build | `cmake --build build --target ArrangerTimelineTest -j4 > LOGS/03-build-red.log` | 0 |
| RED test | `cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure > LOGS/04-test-red.log` | 8 (44 passed, 1 failed: `'!out.ok' returned FALSE. (Zoom Out at the minimum zoom reported success without zooming)`) |
| GREEN build | `cmake --build build --target zene ArrangerTimelineTest VideoClipsGuiTest LoopDropWarpTest ClipWaveformEditTest BrowserDragUrlTest -j4 > LOGS/05-build-green.log` | 0 |
| GREEN + family | `ctest -R '^(ArrangerTimelineTest\|VideoClipsGuiTest\|LoopDropWarpTest\|ClipWaveformEditTest\|BrowserDragUrlTest)$' > LOGS/06-test-green.log` | 0 (100% of 5) |
| Family (rest of the `arrangement.zoom` users) | build `ArrangerFileDropTest ArrangerTakePunchTest ArrangerOverlaysVideoLaneTest ArrangerKeysNavigatorTest` (`07`, EXIT 0); `ctest -R '^(those four)$' > LOGS/08-test-family.log` | 0 (100% of 4) |
| 4b revert | `git diff -- src/gui/ControlCommandsArrangerView.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` | 0 |
| 4b reverted build / test | `cmake --build build --target zene ArrangerTimelineTest -j4` (`09`) / `ctest -R '^ArrangerTimelineTest$'` (`10`) | 0 / **8** (same assertion fails) |
| 4b reapply | `git apply LOGS/fix.patch` | 0 |
| 4b reapplied build / test | `11` / `ctest -R '^ArrangerTimelineTest$'` (`12`) | 0 / **0** (100% of 1) |
| Gates | see below (`14-gates.log`) | 0 — all 8 rows PASS |

Affected-family test count: 9 ctest executables (ArrangerTimelineTest has 45 cases), all passing.

Gates: `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` ran with EXIT=1 (`13-gates.log`). It
crashed with `PermissionError` because it tries to create `<worktree>/../<name>-feature-check`, which is outside the
write-jail. To get around this I ran an identical copy (`runs/.../fc-script/feature_check_local.py`). Its only diffs are
`ROOT` pinned to the real qwen dir, so the same FIX-BASE and cached baselines are used, and `logs` pointed at
`LOGS/feature-check`. Run as `python3 -I .../feature_check_local.py <worktree>`, it gave EXIT=0:
fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology and doc-figures PASS, and
complexity and file-length PASS ("no new item in your files"). `ControlCommandsArrangerView.cpp` is 497 lines (≤ 500).

Registration: both changed files are fork-owned and already listed in `tests/fork-sources.txt`
(`:2066`, `:2832`). No new files.

Files committed (integration commit on top of af010bc3cc): `src/gui/ControlCommandsArrangerView.cpp`,
`tests/src/gui/ArrangerTimelineTest.cpp`, `docs/qwen-fixes-2026-10/cluster-gui-1106-menu-navigate-zoom-out.md` — the
same three files as the accepted patch, byte-identical to it.

## Self-check

- **Integration (r2):** the accepted patch is based on the pinned target (`git merge-base eac7b710 af010bc3cc` =
  af010bc3cc) and applies cleanly; `git diff eac7b710 -- <the three files>` is empty, so the integrated tree is
  byte-identical to the accepted patch and there is no semantic difference. The "newer line" is the sibling
  gui-1106-menu-navigate-zoom-in patch (`c16334ca93`) on `050/m0-train-27`, absent on train-26; there is no conflict
  on the pinned target and the accepted design (`stepAtBound`) is retained. The forward-merge reconciliation with
  train-27 is flagged above.
- **Fails reverted, passes restored:** yes (r2). `git apply -R LOGS/fix.patch`, rebuild EXIT=0, then
  `ctest -R '^ArrangerTimelineTest$'` EXIT=**8** (`05-test-red.log`: `'!out.ok' returned FALSE. (Zoom Out at the
  minimum zoom reported success without zooming)`); `git apply LOGS/fix.patch`, rebuild EXIT=0, then the same ctest
  EXIT=**0** (`06-build-green-restored.log`, `07-test-green-restored.log`). The r1 logs 09-12 show the same for the
  original attempt.
- **Deterministic:** yes. There are no sleeps, timers or threads. The test puts the zoom model straight at its bound
  with `IntModel::setValue(minValue()/maxValue())`. That is the seam: it stands in for the saved `ui/songeditorzoom`
  endpoint. It then calls the command synchronously through `ControlRegistry::invoke`.
- **At the cause and complete:** the refusal sits in `zoom()`, the one handler behind every step zoom: the Navigate menu
  Zoom In/Out (`MenuBarExtras.cpp:73-74`), the panel shortcuts (`PanelShortcuts.cpp:97-99`) and the agent surface. One
  check covers both bounds and both directions. Absolute `pixels_per_bar` callers (`MenuBarExtras.cpp:123`,
  `ControlCommandsViewMemory.cpp:298`, `tests/control-customisation.py:350`) are deliberately unaffected. The test
  asserts the repeated-absolute case still succeeds. All 9 ctest executables that drive `arrangement.zoom` pass.
- **No collateral damage:** nothing is saved or loaded differently. The zoom model, its range and the
  `ui/songeditorzoom` setting are untouched, so old projects load as before. The new check only fires when the clamped
  step target equals the current value. An oversized step that still moves the view (`anOversizedZoomStepClampsToMaximum`)
  and `step: 0` keep succeeding. GUI thread only; no undo step (zoom is interface state); no command or schema change.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable. The verdict is FIXED (REAL; RED shown above).
- **Sibling commands in ControlCommandsArrangerView.cpp:**
  - `arrangement.zoom_back`, `zoom_forward` and `zoom_toggle` already refuse (`NotFound`) when they have nothing to move
    to. `zoom` is now consistent with them.
  - `arrangement.zoom` with `fit` already refuses (`Refused`) when there is nothing to fit. With `start`/`end` it
    always sets a span, which is unaffected.
  - `arrangement.scroll`, `nudge`, `select_clip`, `next_transient`, `fit_tracks`, `set_tool`, `modes`,
    `modifier_tools`, `navigator`, `ruler_lanes` and `get_state` do not use `requestedZoom`/`stepAtBound` or the zoom
    model's step clamp. They are not affected and were not changed.
- **r3:** this round makes no code change. The tree is byte-identical to the accepted patch. RED is EXIT=8 at the same
  assertion and GREEN is EXIT=0 (`r3/logs/04`, `06`). The family is 9/9 (`11`). Gates are 8/8 PASS, EXIT=0 (`13`), using
  the log-dir-moved copy, because the stock script cannot write its log dir from inside this jail (`09`, EXIT=1).
- **Gates row in submit_check:** in r2 `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py .` ran directly
  and gave RESULT: PASS, EXIT=0 (`02-gates.log`, 8/8 rows: fork-sources, all-sources-reproduce,
  no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length). In r1 the same
  script died with `PermissionError` creating `<worktree>/../<name>-feature-check` (outside that sandbox's
  write-jail; `13-gates.log`, EXIT=1), and an identical copy with only its log dir moved into the LOG FOLDER gave
  RESULT: PASS, EXIT=0 (`14-gates.log`).
