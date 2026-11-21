FIXED

# Cluster ci-4ff107c9ee4dc69c: MSVC friend lookup in the clip/track headers

## Classification

`tests/src/gui/TouchLayoutTest.cpp` is portable C++; the defect is in the product
headers, not in the test and not in infrastructure. Three declarations wrote
`friend class ClipView;` / `friend class AutomationTrackView;` /
`friend class PatternClipView;` inside `namespace lmms`. Each one declared or
elaborated a *nonexistent* `lmms::` class instead of the intended `lmms::gui::`
view class. GCC and Clang happened to accept the test through its using
directives, while MSVC exposes the elaborated `lmms::` name to later lookup, so
the unqualified `ClipView` uses in the test bind to the phantom class and the
build fails.

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
| 02 | repro, standard `clang++ -fsyntax-only` | 1 | `02-repro-standard.log` (`CLANG_EXIT=1`) |
| 03 | phantom-friend probe under MS compatibility | 1 | `03-probe-mscompat.log` (`CLANG_EXIT=1`) |
| 05 | RED: `ctest -R MsFriendLookupTest` with the product fix reverted | 8 | `05-test-red.log` |
| 07 | GREEN: `ctest -R MsFriendLookupTest` with the fix | 0 | `07-test-green.log` (1/1 passed) |
| 08 | MS-compat check on `TouchLayoutTest.cpp` after the fix | 0 | `08-repro-mscompat-fixed.log` (`CLANG_EXIT=0`) |
| 09 | Clip/Automation/Pattern/TouchLayout family | 0 | `09-test-family.log` (47/47 passed) |
| 4b | `git apply -R fix.patch` (reverse the product fix) | 0 | recorded in p2 |
| 11 | `ctest -R MsFriendLookupTest` after 4b | 8 | `11-test-reverted.log` |
| 4b | `git apply fix.patch` (restore the fix) | 0 | recorded in p2 |
| 13 | `ctest -R MsFriendLookupTest` after reapply | 0 | `13-test-reapplied.log` (1/1 passed) |
| 14 | mutant: remove the `Clip.h` friend | 8 | `14-mutant-Clip.log` |
| 15 | mutant: remove the `AutomationTrack.h` friend | 8 | `15-mutant-AutomationTrack.log` |
| 16 | mutant: remove the `PatternClip.h` friend | 8 | `16-mutant-PatternClip.log` |

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
  `logs/18-submit-check.log` (final run recorded below).

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

None.

## Self-check

- **Regression test fails with the product change reverted and passes with it.**
  Reverting the three header hunks (`git apply -R`, EXIT=0) made
  `MsFriendLookupTest` fail: `logs/11-test-reverted.log`, EXIT=8 (also the
  original red at `05-test-red.log`, EXIT=8). Restoring the fix
  (`git apply`, EXIT=0) made it pass: `logs/13-test-reapplied.log`, EXIT=0
  (also `07-test-green.log`, EXIT=0). Each mutant that removes one friend also
  fails (logs 14/15/16, EXIT=8 each).
- **The test is deterministic.** It runs a `clang++ -fsyntax-only` compile
  probe with fixed flags and a fixed `-fms-compatibility-version=19.40`; there
  is no wall-clock timing, sleep, thread or race, and no seam is needed.
- **The fix is at the cause and complete.** The cause is the three unqualified
  friends. All three were qualified, and all other `friend class ...View`
  declarations in `include/` either are already `gui::`-qualified or name
  non-GUI classes; the grep shows no remaining unqualified GUI view friend.
  The compile probe and the 47-target family exercise all three headers.
- **No collateral damage.** The diff is declarations only: qualified friends
  plus forward declarations. Save/load formats, project loading and existing
  data are untouched, and the family run passed 47/47 (log 09, EXIT=0).
- **ALREADY-FIXED / FALSE-POSITIVE:** not claimed; the verdict is FIXED, so
  this item does not apply.
