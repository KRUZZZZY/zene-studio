FIXED

# Cluster gui-1030-tab-mixer-pick-the-channels-to-show - the Mixer mode bar clipped its right-hand controls

## Verdict: REAL

The mode bar (`src/gui/MixerModeBar.cpp`) laid every control (View, Density, Show, **Pick...**, Follow Selection,
five mode chips, Routing Matrix...) in one `QHBoxLayout` with no wrap and no overflow path. Its minimum width was the
sum of all of them, measured at **1610 px** on this tree (test log `test-4b-red3.log`). A Mixer panel narrower than
that clipped everything past its right edge, and Pick... ("Pick the channels to show") was among the first to go.

Evidence on the pre-fix tree: `MixerModeBar::MixerModeBar` built `auto* layout = new QHBoxLayout(this)` and added
every widget to it, plus an `addStretch(1)` (the removed lines in `git diff -- src/gui/MixerModeBar.cpp`).

## Fix

`src/gui/MixerModeBar.cpp`, `include/MixerModeBar.h` (nothing else in product code):
- a private `WrapLayout` (an anonymous-namespace `QLayout`, `hasHeightForWidth`) places the bar's items left to right
  and starts a new row when the next item would pass the right edge. Wide enough, it is one row, as before.
  Its minimum width is its widest single item, not the sum.
- a caption and the controls it names stay together as one wrapping item (`addGroup`): View + Mix/Routing,
  Density + Compact/Expanded, Show: + the filter box. Everything else (Pick..., Follow Selection, the chips,
  Routing Matrix...) wraps on its own.
- the original stretch is retained as a layout marker: when the whole bar fits on one row, it keeps the mode chips
  and Routing Matrix at the right edge; when the panel narrows, the controls wrap onto additional rows.
- `addSegment` / `addChip` take a `QLayout*` instead of `QHBoxLayout*` (private members; only the constructor calls
  them). Object names, accessible names, tooltips, signals, `controlCommand` properties and the public accessors
  (`viewButton`, `filterBox`, `followButton`, ...) are unchanged. `MixerView` puts the bar in a `QVBoxLayout`, which
  honours height-for-width, so the bar simply gets taller when narrow.

No new control command, nothing on the audio thread, no saved-data or undo effect (pure widget layout).

## Siblings checked

- `include/MixerModeBar.h` is included by `src/gui/MixerView.cpp` and three test files only; all rebuilt (`zene`,
  `MixerModesTest`, `MixerZonesTest`, `MixerMeteringTest`: `build-12.log` EXIT=0).
- `fillZones` (public, used by `MixerZonesTest`) and `refresh` / `refreshChips` do not touch the layout type.
- Other users of `QHBoxLayout` in `MixerView.cpp` are the channel body, not the mode bar; not part of this finding.

## Regression test

`tests/src/gui/MixerModesTest.cpp::theModeBarWrapsInsteadOfClippingInANarrowPanel`. It builds a fresh
`MixerModeBar`, then:
1. asserts the bar's `minimumSizeHint().width()` is at most 420 px (it was 1610 px);
2. lays the bar out at 3000 px (every button/combo box inside the bar) and at 420 px (the same), driving the layout's
   `setGeometry` directly, so there is no show, event loop, timer or wall-clock;
3. asserts the bar has height-for-width, that 420 px needs more rows than 3000 px, and that 3000 px is back to one row.

## Commands and results (all logs in the run's `logs/` folder)

| Step | Command | EXIT / result |
|---|---|---|
| RED (first version of the test, old code) | `cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure > test-red.log` | EXIT=8, `probe.hasHeightForWidth()` returned FALSE |
| 4b revert fix | `git diff -- src include > fix.patch && git apply -R fix.patch`; `cmake --build build --target MixerModesTest -j4 > build-11.log` | build EXIT=0 |
| 4b RED (final test, fix reverted) | `cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure > test-4b-red3.log` | EXIT=8, `probe.minimumSizeHint().width() <= narrow` returned FALSE (the bar needs 1610px at least) |
| 4b re-apply fix | `git apply fix.patch`; `cmake --build build --target zene MixerModesTest MixerZonesTest MixerMeteringTest -j4 > build-12.log` | build EXIT=0 |
| GREEN + family | `cd build/tests && ctest -R '^(MixerModesTest\|MixerZonesTest\|MixerMeteringTest)$' --output-on-failure > test-4b-green3.log` | EXIT=0, 100% tests passed out of 3 |
| gates (see note) | `feature_check_local.py` (six simple gates) | fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS (0 errors), no-tautology PASS, doc-figures PASS |
| complexity | `bash tests/complexity-gate.sh --check > gate-complexity.log` | EXIT=1 (red on the train: 7 REGRESSION lines, none in `MixerModeBar.cpp` or `MixerModesTest.cpp`) |
| file-length | `bash tests/file-length-gate.sh --check > gate-filelen.log` | EXIT=1 (red on the train: 26 lines, none in my files; `MixerModeBar.cpp` is 405 lines) |

Gate note, for the reviewer: `feature_check.py` could not run as shipped. It creates its log folder next to the
worktree (outside the write area, `PermissionError`), and then needs a `git checkout` in the shared baseline tree for
this base commit, which the lane guard refuses. I ran a copy (`logs/feature_check_local.py`) that differs from the
original only in two path lines (`ROOT` fixed to the real directory, logs written to the run's log folder), and ran
the two ratchet gates directly. The ratchet's "new compared with the base" comparison was therefore not done; instead
I checked that no item of either ratchet gate names a file I changed. Registration: `MixerModeBar.{h,cpp}` are already
in `tests/fork-sources.txt`; `MixerModesTest.cpp` is registered in `tests/upstream-modifications.txt` by commit 02230a9.
Independent review reran the required `feature_check.py` command and all checks passed, including both ratchets.

Environment note: ccache could not create its temp files in its default directory (`Permission denied`); builds
used `CCACHE_TEMPDIR` in the log folder. This does not change the compiled output.

## Not done / limits

- I did not open the real application and narrow the Mixer panel by hand; the proof is the layout-level test.
- `ctest` was run for the three Mixer GUI test families only, not the whole suite.

## Self-check

- **Fails reverted, passes fixed:** with `fix.patch` (product files only) reverted, `ctest -R '^MixerModesTest$'`
  EXIT=8, "the bar needs 1610px at least" (`test-4b-red3.log`); with the fix re-applied the three-test family
  EXIT=0, 100% passed out of 3 (`test-4b-green3.log`).
- **Deterministic:** no timers, sleeps, `show()` or event loop. The widget is never shown; the layout is driven through
  `QLayout::setGeometry` with fixed widths (420 and 3000 px), and sizes come from the offscreen style's size hints
  (fixed `tokens::targetSize()` minimum heights, button texts), which do not change between runs.
- **Complete at the cause:** the cause is the single non-wrapping `QHBoxLayout`; the whole bar now uses `WrapLayout`
  (`MixerModeBar.cpp:99`, installed at the constructor), so every control, not only Pick..., is covered. Callers of
  the changed private helpers (`addSegment`, `addChip`) are the constructor only (`rg`); the public accessors and
  `fillZones` are unchanged; `MixerView.cpp:114-118` embeds the bar in a `QVBoxLayout`, which honours height-for-width.
- **No collateral damage:** layout only. No saved data, project format, command, undo step or audio-thread code is
  touched, no limit was added. At 3000 px the bar remains on one row and the mode chips stay right-aligned, both
  asserted by the regression; at 420 px the controls wrap onto more rows.
- **Verdict is FIXED**, not ALREADY-FIXED / FALSE-POSITIVE, so no not-real evidence applies.
- **Gates:** independent review ran the required `feature_check.py` command successfully; all eight checks passed,
  including complexity and file-length with no new items in changed files.

## Integration onto 050/m0-train-26 (round 2)

Verdict unchanged: **FIXED**. This is an integration of the accepted patch (source `63cbed3a21`) onto
`050/m0-train-26` @ `af010bc3cc`; no new design. The driver's three-way merge reported a conflict in
`src/gui/MixerModeBar.cpp`, but its fallback direct application applied all four patch files cleanly
(`integration-apply.log`); no conflict marker is in any file. The staged diff is exactly the accepted patch; base to
tip (`git diff --shortstat af010bc3cc HEAD`) is 5 files, 279 insertions, 11 deletions, this report included
(corrected in round 3: an earlier version said 247). Semantic difference from the accepted patch: none. The only other `layout->` users in
`MixerModeBar.cpp` are `addWidget`/`addStretch`/`setContentsMargins`/`setSpacing`, all supported by `WrapLayout`, and
`MixerModeBar.h` is included by `MixerView.cpp`, `MixerZonesTest.cpp`, `MixerMeteringTest.cpp`, `MixerModesTest.cpp`
only (all rebuilt).

| Step | Command | EXIT / result |
|---|---|---|
| build (integrated tree) | `cmake --build build --target zene MixerModesTest MixerZonesTest MixerMeteringTest -j4 > logs/build-1.log` | EXIT=0 |
| GREEN family | `cd build/tests && ctest -R '^(MixerModesTest\|MixerZonesTest\|MixerMeteringTest)$' --output-on-failure > logs/test-green-1.log` | EXIT=0, 100% passed, 3 of 3 |
| 4b revert | `git diff HEAD -- src/gui/MixerModeBar.cpp include/MixerModeBar.h > logs/fix.patch && git apply -R logs/fix.patch` | EXIT=0 |
| 4b RED | `cmake --build build --target MixerModesTest -j4 > logs/build-2-red.log` (EXIT=0); `ctest -R '^MixerModesTest$' --output-on-failure > logs/test-red-1.log` | EXIT=8: `'probe.minimumSizeHint().width() <= narrow' returned FALSE. (the bar needs 1610px at least)` |
| 4b re-apply | `git apply logs/fix.patch`; `cmake --build build --target zene MixerModesTest MixerZonesTest MixerMeteringTest -j4 > logs/build-3.log` | apply EXIT=0, build EXIT=0 |
| GREEN family after re-apply | `ctest -R '^(MixerModesTest\|MixerZonesTest\|MixerMeteringTest)$' --output-on-failure > logs/test-green-2.log` | EXIT=0, 100% passed, 3 of 3 |
| gates | `python3 logs/feature_check_local.py <worktree> > logs/gates-2.log` | EXIT=0, all 8 PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length), base `af010bc3c`, baseline cache present so the new-item comparison ran |

Gate note: the shipped `feature_check.py <worktree>` exits 1 here with `PermissionError` creating
`<worktree>-feature-check` (outside the write area; `logs/gates-1.log`). `logs/feature_check_local.py` differs only in
the `ROOT` and `logs =` lines (log folder moved into the run's logs); the checks are identical.

Files committed: `docs/qwen-fixes-2026-10/cluster-gui-1030-tab-mixer-pick-the-channels-to-show.md`,
`include/MixerModeBar.h`, `src/gui/MixerModeBar.cpp`, `tests/src/gui/MixerModesTest.cpp`,
`tests/upstream-modifications.txt`.

Not done: no manual run of the real app; ctest ran for the three Mixer GUI families only. Luna's round-1 review asked
for the wide-panel alignment to be preserved and tested: when the whole bar fits on one row, the mode chips and
Routing Matrix... stay at the right edge (the stretch marker in `WrapLayout`). That alignment is asserted in the
regression at 3000 px (corrected in round 3: an earlier version said that review had no itemised points).

## Integration re-verification (round 3)

Verdict unchanged: **FIXED**. Luna's round-2 QA said ACCEPT-NOTE and listed two report-only notes: the stale
"no itemised request" wording and the stale 247-insertion figure. Both are corrected above. No product or test code
changed in this round. The branch tip already has the accepted patch on `050/m0-train-26` @ `af010bc3cc`. No file has
a conflict marker (`grep` for `<<<<<<<`/`>>>>>>>` in the three code files found nothing). The code diff is identical
to the accepted patch: the `+`/`-` lines of `git diff af010bc3cc HEAD -- include src tests` match the non-doc hunks
of `r2/accepted.patch` (`diff` EXIT=0, `logs/semantic-diff.log`). Also, `git apply --check -R --exclude='docs/*'
accepted.patch` returned EXIT=0.

Failure paths and platform hazards considered: wide panel (one row, chips right-aligned: asserted); narrow panel
(wraps, minimum width = widest item: asserted); parent layout must honour height-for-width (`MixerView` uses a
`QVBoxLayout`); header change (`MixerModeBar.h`) means every includer has to be rebuilt (zene + three tests, built
below). The fix is pure Qt widget layout with no platform-specific API, and it does not touch the audio thread,
saved data, undo or commands.

| Step | Command (logs in the r3 run's `logs/`) | EXIT / result |
|---|---|---|
| build | `cmake --build build --target zene MixerModesTest MixerZonesTest MixerMeteringTest -j4 > build-1.log` | EXIT=0 |
| GREEN family | `cd build/tests && ctest -R '^(MixerModesTest\|MixerZonesTest\|MixerMeteringTest)$' --output-on-failure > test-green-1.log` | EXIT=0, 100% passed out of 3 |
| 4b revert | `git diff af010bc3cc HEAD -- src/gui/MixerModeBar.cpp include/MixerModeBar.h > fix.patch && git apply -R fix.patch` | EXIT=0 |
| 4b RED | `cmake --build build --target MixerModesTest -j4 > build-2-red.log` (EXIT=0); `ctest -R '^MixerModesTest$' --output-on-failure > test-red-1.log` | EXIT=8: `'probe.minimumSizeHint().width() <= narrow' returned FALSE. (the bar needs 1610px at least)` |
| 4b re-apply | `git apply fix.patch`; `cmake --build build --target zene MixerModesTest MixerZonesTest MixerMeteringTest -j4 > build-3.log` | apply EXIT=0, build EXIT=0 |
| GREEN after re-apply | `ctest -R '^(MixerModesTest\|MixerZonesTest\|MixerMeteringTest)$' --output-on-failure > test-green-2.log` | EXIT=0, 100% passed out of 3 |
| gates (shipped) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > gates-1.log` | EXIT=1, `PermissionError` creating `<worktree>-feature-check` (outside the write area); no gate ran |
| gates (log path moved) | `python3 logs/feature_check_local.py <worktree> > gates-2.log` | EXIT=0, all 8 PASS, base `af010bc3c`, complexity and file-length "no new item in your files" |

`feature_check_local.py` changes only two lines of the shipped script (`feature_check_local.diff`): `ROOT` points at the
real directory, and logs are written to the run's log folder. The gate commands are the same.

Files committed this round: `docs/qwen-fixes-2026-10/cluster-gui-1030-tab-mixer-pick-the-channels-to-show.md` only.
