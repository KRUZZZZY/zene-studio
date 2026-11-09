FIXED

# gui-1030-tab-mixer-add-effect - Add effect button can be clipped off-screen

## Verdict: REAL (Luna's root cause is the secondary one on this branch)

Luna named the channel scroll area's six-strip minimum (`src/gui/MixerView.cpp:203`) and said the rack falls
outside a mixer panel narrower than the combined minimum. That is real, but measured on this branch the larger
minimum is the mode bar: `MixerModeBar` is one rigid `QHBoxLayout` of ~16 controls whose `minimumSizeHint()` is
1576 px (`src/gui/MixerModeBar.cpp`, constructor). `MixerView`'s outer layout (`src/gui/MixerView.cpp:309`,
`SetMinimumSize`) passes the widest child's minimum up through the shell panel, so with the Inspector
(min 200 px, `src/gui/shell/Shell.cpp:204`) and the Browser (min 220 px, `src/gui/shell/Shell.cpp:205`) open the
main window's minimum width is **2000 px** in Mix view. A 1024-wide screen cannot show that window, and the
effect rack - the right-most region of the mixer body, holding **Add effect** - is beyond the screen edge.

The channel area is independently too wide for 1024: its zone measured 448 px minimum (6 x 64 px strips plus
the 64 px add column), and with master (64 px min) and rack (245 px) the body alone needed 763 px, i.e. a
1183 px window with both side panels. Both parts had to shrink for the rack to be reachable.

Measurements (offscreen probe, before the fix): `MainWindow` minimum 2000x384; `MixerView` minimumSize
1576x418; mode bar minimumSizeHint 1576x38; channels zone minimum 448x380; rack zone 245x380; the visible
`Add effect` button mapped to x = 1338 in a window that cannot be narrower than 2000 px.

## Fix

- `src/gui/MixerModeBar.cpp`: the bar's controls now live in one row inside `ModeBarScroll`, a `QScrollArea`
  with minimum width 0 that scrolls sideways when the bar is narrower than its controls and asks for the
  scroll bar's extra height only while it is shown. Every control, object name, accessible name, command
  property, menu and signal is unchanged; the bar's public accessors are untouched. At a wide window there is
  no scroll bar and the bar is the same height as before.
- `src/gui/MixerView.cpp:203`: the channel area's minimum width is two strips, not six (the area scrolls, so
  this only matters in a narrow panel; six are not needed for any width-dependent behaviour).
- `tests/upstream-modifications.txt`: one appended line for `src/gui/MixerView.cpp`. `src/gui/MixerModeBar.cpp`
  and `tests/src/gui/MixerSelectionIdentityTest.cpp` are already in `tests/fork-sources.txt`.

No new command, nothing on the audio thread, no saved-data change, no change to the zone layout or its
persisted format.

## Regression test

`tests/src/gui/MixerSelectionIdentityTest.cpp`,
`theAddEffectButtonStaysOnAThousandPixelScreen`: switches to Mix view, reveals the Inspector, the Browser and
the Mixer, then asserts the window's minimum width is <= 1024 px (`minimumSizeHint`, so the check does not
depend on a window manager), resizes it to 1024x768, asserts the width stuck, and asserts the selected
channel's `Add effect` button is visible and wholly inside the window after intersecting every ancestor's
rect. It restores the Arrange view and 1400x900 at the end. No sleeps, timers or wall-clock use; the only
events processed are Qt's own posted layout requests.

## Evidence

| Step | Command (from the worktree unless noted) | Result |
| --- | --- | --- |
| build with fix | `cmake --build build --target zene MixerSelectionIdentityTest -j"$(nproc)" > .../logs/build-1.log 2>&1` | EXIT=0 |
| GREEN | `cd build/tests && ctest -R '^MixerSelectionIdentityTest$' --output-on-failure > .../logs/test-1-green.log 2>&1` | EXIT=0, 1/1 passed |
| fix patch | `git diff -- src/gui/MixerView.cpp src/gui/MixerModeBar.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` | EXIT=0 |
| build reverted | `cmake --build build --target zene MixerSelectionIdentityTest -j"$(nproc)" > .../logs/build-2-red.log 2>&1` | EXIT=0 |
| RED | `cd build/tests && ctest -R '^MixerSelectionIdentityTest$' --output-on-failure > .../logs/test-2-red.log 2>&1` | EXIT=8, `FAIL! ... 'win->minimumSizeHint().width() <= 1024' returned FALSE. (window minimum width 2000)` |
| re-apply | `git apply .../logs/fix.patch` | EXIT=0 |
| rebuild | `cmake --build build --target zene MixerSelectionIdentityTest -j"$(nproc)" > .../logs/build-3-green.log 2>&1` | EXIT=0 |
| GREEN again | `cd build/tests && ctest -R '^MixerSelectionIdentityTest$' --output-on-failure > .../logs/test-3-green.log 2>&1` | EXIT=0, 1/1 passed |
| family build | `cmake --build build --target <all 21 Mixer* targets> -j"$(nproc)" > .../logs/build-4-family.log 2>&1` | EXIT=0 |
| family | `cd build/tests && ctest -R '^Mixer' --output-on-failure > .../logs/test-4-family.log 2>&1` | EXIT=0, 21/21 passed |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-1.log 2>&1` | EXIT=0, RESULT: PASS |

Accepted-round logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1030-tab-mixer-add-effect.r1/logs`;
integration-round logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1030-tab-mixer-add-effect.r2/logs`.

Committed files: `src/gui/MixerModeBar.cpp`, `src/gui/MixerView.cpp`,
`tests/src/gui/MixerSelectionIdentityTest.cpp`, `tests/upstream-modifications.txt`, this report.

## Integration onto 050/m0-train-26 (round 2)

This is the integration of accepted commit `096c85c163ad5eb8dd320bdae038eedbf47805db` onto target
`af010bc3cc055d477045a54e2d866f59a95017d7` (`050/m0-train-26`, branch
`integration/gui-1030-tab-mixer-add-effect-af010bc3cc`). The round-1 QA review
(`qwen/qa/reviews/gui-1030-tab-mixer-add-effect.r1.md`) returned ACCEPT with empty `blocking`, `notes` and
`rework_instructions`, so there were no review change requests to apply on top of the accepted patch. The
accepted commit's parent is that exact target commit, so the driver's three-way application had no
conflicting hunk to resolve; `integration-apply.log` records the four code, test and registration paths applied
cleanly. The issue report is the fifth changed file and includes this integration section. `git diff --exit-code
096c85c163ad -- src/gui/MixerModeBar.cpp src/gui/MixerView.cpp tests/src/gui/MixerSelectionIdentityTest.cpp
tests/upstream-modifications.txt` is empty (EXIT=0), so the integrated product code and regression test are
identical to the accepted patch. No design change, no new conflict resolution and no new command was needed.

Failure paths checked for the integrated change:

- Mixer narrower than the mode-bar row: the row scrolls sideways; the scroll bar's height is added only while
  it is shown, so no control is cut off at the bottom.
- Mixer wide enough for the whole row: scroll-bar range is zero, the bar keeps its old height and layout.
- Channel area narrower than two strips: the area still scrolls its strips; the minimum change from six to two
  strips only affects how much width the scroll area demands from the panel.
- Odd strip sizes / style metrics: the minimum is derived from `mixerChannelSize`, not a constant.
- Inspector + Browser + Mixer open on a 1024x768 screen: window minimum width is <= 1024 and Add effect is
  wholly inside the window.
- Sibling controls that share the add column (New mixer channel, Return) and the other `Mixer*` tests: pass in
  the family run below.

Supported-platform hazards: `ModeBarScroll` only uses public Qt 5.15/6 APIs (`QScrollArea`, `QScrollBar`,
`QSizePolicy`, `QSize` overrides whose signatures are identical in Qt 5 and Qt 6); there is no platform-specific
call, no `#ifdef`, no new dependency and no changed header. It therefore compiles on the supported Linux,
macOS and MSVC/Windows CI builds.

| Step | Command (from the worktree unless noted) | Result |
| --- | --- | --- |
| identity with accepted commit | `git diff --exit-code 096c85c163ad -- src/gui/MixerModeBar.cpp src/gui/MixerView.cpp tests/src/gui/MixerSelectionIdentityTest.cpp tests/upstream-modifications.txt` | EXIT=0, empty |
| build integrated | `cmake --build build --target zene MixerSelectionIdentityTest -j"$(nproc)" > .../logs/01-build-integrated.log 2>&1` | EXIT=0 |
| GREEN | `cd build/tests && ctest -R '^MixerSelectionIdentityTest$' --output-on-failure > .../logs/02-test-green-integrated.log 2>&1` | EXIT=0, 1/1 passed |
| fix patch | `git diff HEAD -- src/gui/MixerView.cpp src/gui/MixerModeBar.cpp > .../logs/fix.patch` | EXIT=0, 80 lines |
| reverse fix | `git apply -R .../logs/fix.patch` | EXIT=0 |
| build reverted | `cmake --build build --target zene MixerSelectionIdentityTest -j"$(nproc)" > .../logs/03-build-reverted.log 2>&1` | EXIT=0 |
| RED | `cd build/tests && ctest -R '^MixerSelectionIdentityTest$' --output-on-failure > .../logs/04-test-red-integrated.log 2>&1` | EXIT=8, `window minimum width 2000` |
| re-apply | `git apply .../logs/fix.patch` | EXIT=0 |
| rebuild | `cmake --build build --target zene MixerSelectionIdentityTest -j"$(nproc)" > .../logs/05-build-restored.log 2>&1` | EXIT=0 |
| GREEN again | `cd build/tests && ctest -R '^MixerSelectionIdentityTest$' --output-on-failure > .../logs/06-test-green-restored.log 2>&1` | EXIT=0, 1/1 passed |
| family build | `cmake --build build --target <all 21 Mixer* targets> -j"$(nproc)" > .../logs/07-build-mixer-family.log 2>&1` | EXIT=0 |
| family | `cd build/tests && ctest -R '^Mixer' --output-on-failure > .../logs/08-test-mixer-family.log 2>&1` | EXIT=0, 21/21 passed |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/09-feature-check.log 2>&1` | EXIT=0, RESULT: PASS |

## Integration re-run (round 3)

Source `integration/gui-1030-tab-mixer-add-effect-af010bc3cc` @ `98bfe3b063`, target pinned at `050/m0-train-26`
@ `af010bc3cc`. The round-2 QA review (`runs/gui-1030-tab-mixer-add-effect.r2/qa-session-2.log`) returned
`ACCEPT-FIXED` with empty `blocking`, `notes` and `rework_instructions`, and its only change was the report
correction `98bfe3b063`. That leaves no review points to address. The target is already an ancestor of the source
(`git merge-base --is-ancestor af010bc3cc HEAD` EXIT=0), and `096c85c163^` is `af010bc3cc` itself. So the conflict
the job names in `src/gui/MixerModeBar.cpp`/`src/gui/MixerView.cpp` has no hunk left to resolve on this tree. The
product code and test match both the accepted commit and the round-2 tip: `git diff --exit-code 096c85c163 --
src/gui/MixerModeBar.cpp src/gui/MixerView.cpp tests/src/gui/MixerSelectionIdentityTest.cpp
tests/upstream-modifications.txt` EXIT=0, and `git diff --exit-code 98bfe3b063 -- src tests` EXIT=0. No code change
in this round. Every failure path and platform hazard listed for round 2 is unchanged. The proof was repeated from
scratch on a fresh build, with logs in `runs/gui-1030-tab-mixer-add-effect.r3/logs` (`L`):

| Step | Command (from the worktree unless noted) | Result |
| --- | --- | --- |
| build | `cmake --build build --target zene MixerSelectionIdentityTest -j4 > L/01-build-green.log 2>&1` | EXIT=0 |
| GREEN | `cd build/tests && ctest -R '^MixerSelectionIdentityTest$' --output-on-failure > L/02-test-green.log 2>&1` | EXIT=0, 1/1 passed |
| fix patch + reverse | `git diff af010bc3cc HEAD -- src/gui/MixerModeBar.cpp src/gui/MixerView.cpp > L/fix.patch && git apply -R L/fix.patch` | EXIT=0, 80 lines |
| build reverted | `cmake --build build --target zene MixerSelectionIdentityTest -j4 > L/03-build-reverted.log 2>&1` | EXIT=0 |
| RED | `cd build/tests && ctest -R '^MixerSelectionIdentityTest$' --output-on-failure > L/04-test-red.log 2>&1` | EXIT=8, `FAIL! : ...theAddEffectButtonStaysOnAThousandPixelScreen() 'win->minimumSizeHint().width() <= 1024' returned FALSE. (window minimum width 2000)` |
| re-apply | `git apply L/fix.patch` | EXIT=0, `git status` clean |
| rebuild | `cmake --build build --target zene MixerSelectionIdentityTest -j4 > L/05-build-restored.log 2>&1` | EXIT=0 |
| GREEN again | `cd build/tests && ctest -R '^MixerSelectionIdentityTest$' --output-on-failure > L/06-test-restored.log 2>&1` | EXIT=0, 1/1 passed |
| family build | `CCACHE_TEMPDIR=build/ccache-tmp cmake --build build --target zene <the 22 targets matching ^(Mixer\|VcaStripLifetimeTest$)> -j4 > L/12-build-family.log 2>&1` | EXIT=0 |
| family | `cd build/tests && ctest -R '^(Mixer\|VcaStripLifetimeTest$)' --output-on-failure > L/13-test-family.log 2>&1` | EXIT=0, 22/22 passed (21 `Mixer*` + `VcaStripLifetimeTest`) |
| gates | `python3 L/feature_check_logdir.py <worktree> > L/14-feature-check.log 2>&1` | EXIT=0, all 8 rows PASS, RESULT: PASS |

Two problems with the environment, not the code, came up during the run. Neither is hidden:

- The first family build attempts failed. `L/07-build-family.log` (EXIT=2) failed because zsh did not split the
  target list. `L/10-build-family.log` (EXIT=2) failed with `ccache: error: failed to create temporary file for
  .../dsh/home/ccache-tmp-r6/...: Permission denied`, because the lane's write-jail excludes that directory. Their
  ctest runs `L/08`/`L/11` show only "Not Run" for unbuilt targets. Pointing `CCACHE_TEMPDIR` into the build tree
  fixed the build (`L/12`, EXIT=0).
- The unmodified `qwen/feature_check.py` (`L/09-feature-check.log`, EXIT=1) raised `PermissionError` while writing
  its per-gate logs to `<worktree>/../<name>-feature-check/`, which is outside the write-jail. No gate result was
  reported. `L/feature_check_logdir.py` is a byte copy with two lines changed: `ROOT` is pinned to the original
  `qwen/` directory (for `FIX-BASE` and `gate-baselines`), and the log directory is `L/feature-check-gates`
  (`diff` recorded in this session). The gate commands, baselines and pass rules are unchanged.

## Self-check

- **Regression fails reverted, passes restored**: integration round - `git diff HEAD -- src/gui/MixerView.cpp
  src/gui/MixerModeBar.cpp > logs/fix.patch` EXIT=0, `git apply -R logs/fix.patch` EXIT=0, rebuild EXIT=0,
  then RED `ctest -R '^MixerSelectionIdentityTest$'` EXIT=8 with `window minimum width 2000`
  (`logs/04-test-red-integrated.log`); `git apply logs/fix.patch` EXIT=0, rebuild EXIT=0, then the same
  command EXIT=0, 1/1 passed (`logs/06-test-green-restored.log`). Accepted round: RED EXIT=8
  (`logs/test-2-red.log`), GREEN EXIT=0 (`logs/test-3-green.log`). The product patch itself is `logs/fix.patch`.
- **Deterministic**: no sleeps or wall-clock timing. The test drives the real GUI path (`shell()->setView`,
  `revealPanel`, `resize`) and waits only on Qt's posted `LayoutRequest` events; the assertion is on
  `minimumSizeHint()` and on mapped rects after those layout passes.
- **Cause and completeness**: the mixer's minimum width had two independent contributors - the mode bar
  (`MixerModeBar.cpp`, measured 1576 px) and the channel area's six-strip minimum (`MixerView.cpp:203`,
  measured 448 px with the add column). Both are reduced; the body's other minimums (master 64, rack 245)
  were already below a 1024 screen's centre column. The sibling region paths that use the same scroll area
  (`relayoutZones`, add-channel rebuilds, `refreshDisplay`) only re-add widgets and do not set widths, so the
  smaller minimum is the only layout change. The visible sibling controls New mixer channel and Return use the
  same add column and remain inside the window (family run).
- **No collateral damage**: no saved property, project field, zone width or command changed; the channel
  scroll area still scrolls its strips, and the mode bar is unchanged at normal widths (no scroll bar, same
  height). The mode bar uses a frameless scroll area and row container around its existing controls; the
  controls remain descendants of `mixerModeBar` for the accessibility walk. Old projects load through the same
  `mixer_zones` / shell layout fields, untouched.
- **Not ALREADY-FIXED / FALSE-POSITIVE**: the bug reproduces on this branch; the reverted build's test run
  records the 2000 px minimum, so the verdict is REAL and the fix is in this commit.
- **Integration preserved the accepted design**: the integrated tree is byte-identical to accepted commit
  `096c85c163ad` for the four code, test and registration files (`git diff --exit-code 096c85c163ad --
  src/gui/MixerModeBar.cpp src/gui/MixerView.cpp tests/src/gui/MixerSelectionIdentityTest.cpp
  tests/upstream-modifications.txt` EXIT=0, empty); this report includes the round-2 evidence. The pinned
  target `af010bc3cc` is the accepted commit's parent, so the driver's three-way application had no conflict
  to resolve; the RED/GREEN reversal was repeated on the integrated tree and gave the same 2000 px failure
  (EXIT=8) and pass (EXIT=0). The 21-test `Mixer` family and all feature gates pass.
- **Round 3**: no review change requests (round-2 QA `ACCEPT-FIXED`, empty `blocking`) and no code change. The
  RED/GREEN reversal was repeated on a fresh build: `L/04-test-red.log` EXIT=8 with `window minimum width 2000`,
  then `L/06-test-restored.log` EXIT=0. The family run `L/13-test-family.log` passed 22/22 and the gates
  `L/14-feature-check.log` returned EXIT=0 PASS. The round-3 section above covers the environment workarounds.
