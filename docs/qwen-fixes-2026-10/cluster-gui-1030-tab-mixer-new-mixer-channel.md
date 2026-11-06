FIXED

# cluster gui-1030-tab-mixer-new-mixer-channel - Mixer controls can be clipped off-screen

## Verdict: REAL (measured), fixed

In Mix view on a 1024x768 window with the Inspector and the Browser open, the mixer's minimum width was
**1576 px**. The shell cannot go below that, so the window was forced to **2000 px** wide and the
New mixer channel and Return buttons ended up off a 1024-pixel screen. The numbers come from a probe in the
regression test, before the fix (log `03-test-red.log`, `05-test-probe.log`).

There are two causes. Fixing only one of them still fails the test:

| Contributor (pre-fix) | Minimum width | Where |
| --- | --- | --- |
| The mode bar row (View/Density segments, Show filter, Pick, Follow, five chips, Routing Matrix) | 1576 px | `src/gui/MixerModeBar.cpp:104-150`, added straight to the outer layout at the old `MixerView.cpp:118` |
| The zone splitter: master 64 + channels dock 448 (**six strips = 384** + the add column 64) + VCA 72 + rack 245 + handles | 838 px | the six-strip minimum at the old `MixerView.cpp:203` (the finding's line) |

The finding's root cause (Luna) names the second one. The probe showed the first one is larger. Neither
part can scroll: the only scroll area was the channel strips.

## Fix (`src/gui/MixerView.cpp`)

- `src/gui/MixerView.cpp:100` `ModeBarStrip`: the mode bar now sits inside a frameless `QScrollArea` that
  scrolls sideways (`MixerView.cpp:161`). Its `sizeHint` is the bar's own width, so the mixer asks for the
  same preferred width as before. Its height is the bar's height, plus the scroll bar's height only while
  the bar is too wide to fit (recomputed in `resizeEvent`). On a wide screen it looks the same as before,
  and in a narrow panel every mode-bar control can still be reached by scrolling.
- `src/gui/MixerView.cpp:251`: the channel scroll area's **minimum** is now one strip. It was six.
  `ChannelArea::sizeHint` (`MixerView.cpp:240`) still asks for six strips as its **preferred** width, so the
  default layout at normal widths is unchanged. The strips scroll; the add column and the rack beside them
  no longer get pushed out.

Result: the mixer's minimum width is 518 px. On the 1024x768 window with the Inspector and the Browser open,
the window stays 1024 px wide (its minimum is now 867), and both add buttons are fully on screen inside every
ancestor, measured by the test.

## Regression test

`tests/src/gui/MixerZonesTest.cpp:376` `theAddButtonsStayOnScreenInANarrowMixView`: Mix view, Inspector and
Browser revealed, `resize(1024, 768)`. It asserts that the window is not forced wider than 1024, and that the
parts of `newChannelBtn` and `newReturnBtn` left on screen (clipped by every ancestor up to the window) are
their full width. At the end it restores the Arrange view, the mixer panel and the 1400x900 size the other
cases use.

## Evidence (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1030-tab-mixer-new-mixer-channel.r1/logs/`)

All builds used `CCACHE_DISABLE=1`. ccache's temp dir (`.../dsh/home/ccache-tmp-r6`) is outside this lane's
write-jail, so a build with ccache failed with "Permission denied" (`01-build-red.log`, EXIT=2).

| Step | Command | EXIT |
| --- | --- | --- |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target zene MixerZonesTest -j4 > 02-build-red.log` | 0 |
| RED test (before the fix) | `cd build/tests && ctest -R '^MixerZonesTest$' --output-on-failure > 03-test-red.log` | 8 - `FAIL! theAddButtonsStayOnScreenInANarrowMixView() 'window->width() <= 1024' returned FALSE. (the window is forced 2000 px wide)`; 10 passed, 1 failed |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene MixerZonesTest -j4 > 06-build-fix.log` | 0 |
| GREEN test | `ctest -R '^MixerZonesTest$' --output-on-failure > 07-test-fix-probe.log` | 0 (1/1) |
| Family build | `CCACHE_DISABLE=1 cmake --build build -j4 --target zene <the 22 Mixer test targets> > 10-build-family.log` | 0 |
| Family tests | `ctest -R 'Mixer' -j2 --output-on-failure > 11-test-family.log` | 0 - **100% tests passed out of 22** |
| 4b revert | `git diff -- src/gui/MixerView.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b rebuild + test without the fix | `12-build-4b-reverted.log` / `ctest -R '^MixerZonesTest$' > 13-test-4b-reverted.log` | build 0 / **test 8** (forced 2000 px wide) |
| 4b reapply | `git apply logs/fix.patch` | 0 |
| 4b rebuild + test with the fix | `14-build-4b-reapplied.log` / `15-test-4b-reapplied.log` | build 0 / **test 0** |
| Half fix (mode bar only, six-strip minimum restored) | `16-build-half.log` / `17-test-half.log` | build 0 / test 8 (forced **1262 px** wide). Then the line was restored, and `git diff` was confirmed identical to `fix.patch` |
| Final build + test | `18-build-final.log` / `19-test-final.log` | 0 / 0 (1/1) |
| Gates | `python3 logs/fc-tool/feature_check_logs_redirected.py <worktree> > 22-feature-check-final.log` | **0 - RESULT: PASS** (all 8 rows PASS) |

About the gates: `feature_check.py` as shipped writes its logs to `<worktree>/../<name>-feature-check`, which is
outside the write-jail (`20-feature-check.log`: `PermissionError`, EXIT=1). I ran a copy
that differs only in two lines: `ROOT` points at the original qwen folder, and `logs` points at the LOG
folder. The `diff` is recorded at the top of `21-feature-check.log`. The gates, baselines and checks are
the shipped ones.

Files committed: `src/gui/MixerView.cpp`, `tests/src/gui/MixerZonesTest.cpp`,
`tests/upstream-modifications.txt`, this report.

## Self-check

- **The test fails with the product change reverted and passes with it:** yes. Reverted
  (`git apply -R fix.patch`, rebuilt): `ctest -R '^MixerZonesTest$'` EXIT=8, "the window is forced 2000 px
  wide". Reapplied (`git apply fix.patch`, rebuilt): EXIT=0. With only half of the fix: EXIT=8 (1262 px).
- **Deterministic:** no timers, sleeps or waits. It uses a fixed `resize(1024, 768)` and one
  `processEvents()` to run the layout synchronously. The checks are pure geometry (`mapTo` and rectangle
  intersection with each ancestor). The offscreen platform is set by the test's ctest ENVIRONMENT.
- **At the cause and complete:** both sources of the oversized minimum are fixed: the unscrollable mode bar
  and the six-strip channel minimum. The siblings check out:
  - The add column (`newChannelBtn`/`newReturnBtn`, fixed at one strip width each) and the rack
    (`EffectRackView::DEFAULT_WIDTH`, fixed) are unchanged. They are the columns that must stay visible.
  - The sends banner (382 px minimum, shown only in sends mode) is below the new 518 px floor, so it does
    not raise the minimum.
  - `MixerView::modeBar()` (`include/MixerView.h:138`) still returns the same `MixerModeBar`. It is now a
    descendant through the strip, and every `findChild` and `modeBar()` user in `MixerModesTest` and
    `MixerMeteringTest` passes (family 22/22).
  - `channelArea` has no other caller that sets its width (`grep setMinimumWidth src/gui/MixerView.cpp`).
  - The same `MixerView` is mounted in the bottom region in Arrange view. There its height is unchanged
    while the bar fits (the extra scroll-bar row appears only when it does not).
- **No collateral damage:** layout only. No model, save format, command, undo or audio-thread code was
  touched, so projects load and save as before. The preferred widths are kept (`ModeBarStrip::sizeHint` is
  the bar's own; `ChannelArea::sizeHint` still asks for six strips), so wide windows lay out as before.
  The zone sizes saved by `window.mixer_zones` still apply: the minimums only got smaller, so no stored
  width that used to fit is now refused. All of `MixerZonesTest`'s existing layout, drag and save cases
  pass. No limit or check was added.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable. The verdict is FIXED; the defect was reproduced
  before the fix (2000 px forced width, `03-test-red.log`).
