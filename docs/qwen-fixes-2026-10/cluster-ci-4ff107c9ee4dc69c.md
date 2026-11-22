FIXED

# Cluster ci-4ff107c9ee4dc69c: MSVC friend lookup in the clip/track headers

## Classification

`tests/src/gui/TouchLayoutTest.cpp` is portable C++; the defect is in the product
headers, not in the test and not in infrastructure. Three declarations wrote
`friend class ClipView;` / `friend class AutomationTrackView;` /
`friend class PatternClipView;` inside `namespace lmms`. Each one declared or
elaborated a *nonexistent* `lmms::` class instead of the intended `lmms::gui::`
view class. GCC and Clang follow the standard, which makes a name first
introduced by a friend declaration invisible to ordinary lookup, while MSVC
makes it visible, so the unqualified `ClipView` uses in the test bind to the
phantom class and the build fails.

The defective declarations were at `include/Clip.h:437`,
`include/AutomationTrack.h:63` and `include/PatternClip.h:61` (the latter two
shift to lines 65 and 63 after the fix adds the required forward declarations).
The repair qualifies the friends as `gui::ClipView`, `gui::AutomationTrackView`
and `gui::PatternClipView` and adds forward declarations in `AutomationTrack.h`
and `PatternClip.h`. `Clip.h` already forward-declares its GUI class.

## Evidence

`tests/src/gui/TouchLayoutTest.cpp`:

- lines 78-79: `using namespace lmms;` and `using namespace lmms::gui;`
- line 86: `ClipView* viewOf(const Clip* clip)` - an unqualified GUI type
- line 88: `findChildren<ClipView*>()` - the same unqualified type in the lookup
- line 344: `ClipView* lowerView = viewOf(lower);` - another unqualified use

There is no platform-specific code in the test. The error was the header
declaring a second, phantom `lmms::ClipView` that MSVC's lookup can see.

## Commands and EXIT codes

All compile/test commands below are the p1-p4 record; logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/ci-4ff107c9ee4dc69c.r1/logs/`.

| # | Command (summary) | EXIT | Log |
|---|---|---|---|
| 01 | repro, `clang++ -fsyntax-only -fms-compatibility` on `TouchLayoutTest.cpp` | 1 | `01-repro-mscompat.log` (`CLANG_EXIT=1`) |
| 02 | repro, standard `clang++ -fsyntax-only` | 1 | `02-repro-standard.log` (`CLANG_EXIT=1`; only the ungenerated `TouchLayoutTest.moc` include at line 551, no `ClipView` ambiguity) |
| 03 | phantom-friend probe under MS compatibility | 1 | `03-probe-mscompat.log` (`CLANG_EXIT=1`) |
| 05 | RED: `ctest -R MsFriendLookupTest` before the fix (original RED) | 8 | `05-test-red.log` |
| 07 | GREEN: `ctest -R MsFriendLookupTest` with the fix | 0 | `07-test-green.log` (1/1 passed) |
| 08 | MS-compat check on `TouchLayoutTest.cpp` after the fix | 0 | `08-repro-mscompat-fixed.log` (`CLANG_EXIT=0`) |
| 09 | Clip/Automation/Pattern/TouchLayout family | 0 | `09-test-family.log` (47/47 passed) |
| 4b | `git apply -R fix.patch` (reverse the product fix) | 0 | EXIT seen from `; echo EXIT=$?` on the console in p4, not saved to a log; the tree state is corroborated by 11 (RED) and 13 (GREEN) |
| 11 | `ctest -R MsFriendLookupTest` after 4b | 8 | `11-test-reverted.log` |
| 4b | `git apply fix.patch` (restore the fix) | 0 | EXIT seen from `; echo EXIT=$?` on the console in p4, not saved to a log; the tree state is corroborated by 11 (RED) and 13 (GREEN) |
| 13 | `ctest -R MsFriendLookupTest` after reapply | 0 | `13-test-reapplied.log` (1/1 passed) |
| 14 | mutant: revert the `Clip.h` friend to its unqualified form | 8 | `14-mutant-Clip.log` |
| 15 | mutant: revert the `AutomationTrack.h` friend to its unqualified form | 8 | `15-mutant-AutomationTrack.log` |
| 16 | mutant: revert the `PatternClip.h` friend to its unqualified form | 8 | `16-mutant-PatternClip.log` |

Note on 4b: the reverse-apply step used `git diff HEAD~1` because the product
fix was already committed at that point, so `fix.patch` was the fix commit's
diff and `git apply -R` reverted exactly that change.

p5 commands:

- `python3 feature_check.py <worktree>`: first run EXIT=1, with
  `fork-sources` and `all-sources-reproduce` failing because the new
  `tests/src/compile/MsFriendLookupProbe.cpp` was missing from
  `tests/all-sources.txt`. Re-derived that manifest with its own published
  recipe (`git ls-files ... | LC_ALL=C sort -u`), which printed `REPRODUCES`
  for the new list; re-ran feature_check: EXIT=0, all rows PASS. Log:
  `logs/17-feature-check.log`.
- `python3 submit_check.py <worktree> ci-4ff107c9ee4dc69c`: log
  `logs/18-submit-check.log` (the log holds only `RESULT: PASS - submit`; the
  EXIT=0 came from `; echo EXIT=$?` on the console, corroborated by log 19).

## Disclosures

- MSVC was not run locally. `clang -fms-compatibility` with the flags in
  `tests/scripts/ms_friend_lookup_check.py` stood in for it.
- The cmake guard refused the reconfigure (exit 97), and one direct
  `/usr/bin/cmake` configure was run in p2 (not repeated).
- The guard also rejects full builds, so only `zene` and the 43
  Clip/Automation/Pattern/TouchLayout test targets were rebuilt.
- The change is compile-time only: friendship now grants access to the intended
  `gui::` class. There is no runtime, save-format, audio-thread or undo change.
- `MsFriendLookupTest` is registered only where clang exists.

## Other findings

- `ControllerSurfaceTest::TemplatesSaveListReadAndApply` fails on this box:
  `'surface.saveTemplate(kTemplate)' returned FALSE`
  (`tests/src/core/ControllerSurfaceTest.cpp:484`). It fails identically with
  this cluster's PeakController change reversed (`r3/logs/11-surface-reverted.log`,
  EXIT=8), so it predates this work; the save returns false when the template
  file cannot be opened for writing (`ControllerSurface::saveTemplate`), which
  points at the environment's template directory. Not investigated further here.

## Round 2 re-verification (logs in `runs/ci-4ff107c9ee4dc69c.r2/logs/`)

Round 1 QA: "previous session ended without committing anything". This round
re-ran step 4b with every exit code written to a log file, then committed.

| # | Command | EXIT | Log |
|---|---|---|---|
| 01 | `git diff 8571717ae8 HEAD -- include/Clip.h include/AutomationTrack.h include/PatternClip.h > logs/fix.patch` | 0 | `01-patch.log` (`PATCH_EXIT=0`) |
| 01 | `git apply -R logs/fix.patch` | 0 | `01-patch.log` (`APPLY_R_EXIT=0`; 3 headers changed) |
| 02 | `cd build/tests && ctest -R MsFriendLookupTest --output-on-failure` (fix reverted) | 8 | `02-test-red.log` (`reference to 'ClipView' is ambiguous`, same for `AutomationTrackView`, `PatternClipView`; `CTEST_EXIT=8`) |
| 03 | `git apply logs/fix.patch` | 0 | `03-reapply.log` (`APPLY_EXIT=0`, tree clean afterwards) |
| 04 | `cd build/tests && ctest -R MsFriendLookupTest --output-on-failure` (fix applied) | 0 | `04-test-green.log` (1/1 passed, `CTEST_EXIT=0`) |
| 05 | `cmake --build build --target zene TouchLayoutTest -j4` | 0 | `05-build.log` |
| 11 | `cmake --build build -j4 --target <16 automation test targets not yet built>` | 0 | `11-build-family.log` |
| 12 | `ctest -R "MsFriendLookup\|TouchLayout\|Clip\|Automation\|Pattern" -j4` | 0 | `12-test-family.log` (100% of 66 passed) |
| 07 | `python3 feature_check.py <worktree>` | 0 | `07-feature-check.log` (`RESULT: PASS`) |

The probe reads the headers straight from the source tree via clang, so the
red/green runs need no binary rebuild. An earlier family run (`06`, EXIT=8)
showed 16 tests `Not Run` because their executables had never been built;
after building them (`11`) the family passed (`12`). Two build attempts
(`09`, `11`-precursor) failed only from shell word-splitting of the target
list (make: "File name too long"), not from code.

## Round 3 (logs in `runs/ci-4ff107c9ee4dc69c.r3/logs/`)

QA asked for (1) a run of the rebuilt `TouchLayoutTest` slot whose code failed
to compile on MSVC, with its state sequence; (2) before/after grabs of the
`gui::ClipView`; (3) a real MSVC before/after; (4) the PeakController friend.

| # | Command | EXIT | Log / result |
|---|---|---|---|
| 01 | `python3 -I logs/friend_scan.py <worktree>` | 0 | `01-friend-scan.log` (7 hits, see Self-check) |
| 02 | RED: probe extended with `PeakControllerDialog`; `ctest -R MsFriendLookupTest` before the header change | 8 | `02-test-red-peak.log` (`reference to 'PeakControllerDialog' is ambiguous`) |
| 03 | GREEN: `ctest -R MsFriendLookupTest` after qualifying the friend | 0 | `03-test-green-peak.log` |
| 04 | `cmake --build build --target zene TouchLayoutTest -j4` | 0 | `04-build-touch.log` |
| 05 | `cd build/tests && ZENE_EVIDENCE_DIR=logs/grab ./TouchLayoutTest aTouchTapSelectsAnArrangerClip` (offscreen) | 0 | `05-slot-run.log` (3 passed, 0 failed) |
| 06 | MSVC toolchain probe (`command -v cl cl.exe ...`, `find / -iname cl.exe`) | 0 | `06-msvc-probe.log` (no `cl.exe`; only clang-cl and wine) |
| 07 | `cmake --build build -j4 --target zene peakcontrollereffect ControllerSurfaceTest PeakControllerEffectZeroFrameTest TouchLayoutTest` (every includer of PeakController.h) | 0 | `07-build-includers.log` |
| 08 | `ctest -R "MsFriendLookup\|TouchLayout\|Clip\|Automation\|Pattern\|PeakController\|ControllerSurface" -j4` | 8 | `08-test-family.log` (67/68; only `ControllerSurfaceTest`, see Other findings) |
| 09 | `ctest -R ^ControllerSurfaceTest$` alone | 8 | `09-surface-alone.log` (same failure) |
| 10-11 | the same test with the PeakController change reversed and rebuilt | 8 | `11-surface-reverted.log` (same failure, so not caused by this change) |
| 12 | 4b: `git diff -- include/PeakController.h > logs/fix.patch && git apply -R logs/fix.patch` | 0 | `12-4b.log` (`APPLY_R_EXIT=0`) |
| 13 | `ctest -R MsFriendLookupTest` without the fix | 8 | `13-4b-red.log` |
| 12 | `git apply logs/fix.patch` | 0 | `12-4b.log` (`APPLY_EXIT=0`) |
| 14 | `ctest -R MsFriendLookupTest` with the fix | 0 | `14-4b-green.log` |
| 15 | `python3 feature_check.py <worktree>` | 0 | `15-feature-check.log` (`RESULT: PASS`) |

(1) State sequence driven by `TouchLayoutTest::aTouchTapSelectsAnArrangerClip`
(binary `build/tests/TouchLayoutTest`, built from the working tree that is
committed with this report; the slot gained only a read-only
`recordTapState` helper that prints these lines and saves the grabs):

```
STATE before-tap selected=0 geometry=0,0 513x44
STATE after-tap selected=1 geometry=0,0 513x44
STATE rows upperBottom=128 lowerTop=136 gap=8
```

(2) `view->grab()` of the `gui::ClipView` (the TrackGripContrastTest grab
path): `logs/grab/before-tap.png` and `logs/grab/after-tap.png`, both
sha256 `a40b0ebbb916d27e1841f985e49defe3f755653aa017b60f5d68b05a7cb2999f`.
They are byte-identical: the tap sets `ShellSelection` (the shell's
inspector selection, asserted by the slot), while the clip's paint
(`MidiClipView::paintEvent`, `src/gui/clips/MidiClipView.cpp:677`) colours it
through `getColorForDisplay`, which reads the view's own `isSelected()`
(`src/gui/clips/ClipView.cpp:1568`); the identical hashes show the tap does
not set that flag, so the clip's pixels do not change. The selection change is shown by the state sequence,
not by the image.

(3) **Not done: blocked.** This box has no MSVC (`06-msvc-probe.log`: no
`cl.exe`, no Windows SDK, no MSVC Qt). A hosted msvc-x64 run needs a push, and
this run's rules forbid anything leaving the machine and forbid push. Nobody
can approve one inside the session. The real MSVC before/after
(8571717ae8 C2872, fix commit builds) remains for the merge train's CI.

(4) Done: `include/PeakController.h` forward-declares
`gui::PeakControllerDialog` and befriends it; registered in
`tests/upstream-modifications.txt`. The dialog compiled before without the
friendship (it befriended a phantom class under GCC), so granting it changes
no behaviour.

## Self-check

- **Regression test fails with the product change reverted and passes with it.**
  Reverting the three header hunks (`git apply -R`, EXIT seen from
  `; echo EXIT=$?` on the console in p4, not saved to a log) made
  `MsFriendLookupTest` fail: `logs/11-test-reverted.log`, EXIT=8 (also the
  original red at `05-test-red.log`, EXIT=8). Restoring the fix
  (`git apply`, EXIT seen from `; echo EXIT=$?` on the console in p4, not
  saved to a log) made it pass: `logs/13-test-reapplied.log`, EXIT=0
  (also `07-test-green.log`, EXIT=0). Round 2 re-ran this with the apply
  exits logged: `r2/logs/01-patch.log` APPLY_R_EXIT=0, `02-test-red.log`
  CTEST_EXIT=8, `03-reapply.log` APPLY_EXIT=0, `04-test-green.log`
  CTEST_EXIT=0. Round 3, for the PeakController change: `r3/logs/12-4b.log`
  APPLY_R_EXIT=0, `13-4b-red.log` CTEST_EXIT=8 (`PeakControllerDialog` is
  ambiguous), `12-4b.log` APPLY_EXIT=0, `14-4b-green.log` CTEST_EXIT=0. Each mutant that reverted one friend to
  its unqualified form also fails (logs 14/15/16, EXIT=8 each).
- **The test is deterministic.** It runs a `clang++ -fsyntax-only` compile
  probe with fixed flags and a fixed `-fms-compatibility-version=19.40`; there
  is no wall-clock timing, sleep, thread or race, and no seam is needed.
  The round-3 state sequence comes from `TouchLayoutTest::aTouchTapSelectsAnArrangerClip`,
  which is evidence, not the regression test; that slot already contained a
  `QTest::qWait(30)` and `QTRY_VERIFY` polls before this cluster, and round 3
  added only read-only `qInfo` lines and an env-gated `grab()` save to it.
- **The fix is at the cause and complete.** Correction (round 3): rounds 1-2
  said a search found no other defective unqualified friend. That was wrong:
  the round-1 grep only looked at `friend class ...View` names and missed
  `include/PeakController.h:73` `friend class PeakControllerDialog;`, which sits
  in `namespace lmms` while the dialog is `lmms::gui::PeakControllerDialog`
  (`include/PeakController.h:90`, inside `namespace gui` at line 87). Round 3
  qualifies it as `gui::PeakControllerDialog` with a forward declaration. The
  round-3 scan (`r3/logs/friend_scan.py`, output `r3/logs/01-friend-scan.log`)
  lists every unqualified `friend class X;` outside a `gui` namespace whose name
  is also declared after a `namespace gui` line. It printed 7 hits; besides
  PeakController the six are false matches of that crude rule:
  `AudioEngine.h:899`, `ConfigManager.h:319`, `Ladspa2LMMS.h:77` befriend
  `lmms::Engine` (`include/Engine.h:53`, outside its `gui` block), and
  `Oscilloscope.h:55`, `SlewDistortion.h:85`, `VstEffect.h:97` befriend
  `lmms::*Controls` (declared at `OscilloscopeControls.h:40`,
  `SlewDistortionControls.h:59`, `VstEffectControls.h:53` after the `gui`
  forward-declaration block closes). The `...View` friends in
  `InstrumentTrackWindow.h`, `MixerChannelView.h`, `MixerView.h` and
  `TrackOperationsWidget.h` sit inside `namespace lmms::gui` and are correct.
  The compile probe now covers all four fixed headers.
- **No collateral damage.** The diff is declarations only: qualified friends
  plus forward declarations. Save/load formats, project loading and existing
  data are untouched, and the family run passed 47/47 (log 09, EXIT=0).
- **ALREADY-FIXED / FALSE-POSITIVE:** not claimed; the verdict is FIXED, so
  this item does not apply.
- **Sibling commands in changed command files:** this fix changes no command
  file. The `app.*` and `arrangement.*` ids submit_check lists come from the
  branch's pre-existing commits between the base and this cluster; the only
  files this cluster touches are the three friend declarations, the forward
  declarations, the compile probe/test and the scope manifest. They are not
  affected by a compile-time friendship qualification.
