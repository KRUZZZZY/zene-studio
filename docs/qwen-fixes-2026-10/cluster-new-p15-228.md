ALREADY-FIXED — cluster new-p15-228: an unknown saved icon no longer leaves the previous chosen icon on the header

Finding: "BUG 10: An unknown saved icon can leave the previous chosen icon displayed", `src/gui/TrackIcons.cpp:63`.
Claim: after icon A is shown on a Sample track, a chosen name unavailable in this build leaves A on the header.

## Verdict evidence (code)

The fix the finding asks for is in the tree on this branch:

- `src/gui/TrackIcons.cpp:63-64`: an unknown name fails `!shellicons::svg(chosen).isEmpty()`, so the function does not take the chosen-icon branch and falls through.
- `src/gui/TrackIcons.cpp:74-76`: if `shown` still holds a `zene:` name, it is cleared, so the previously displayed icon's record is dropped.
- `src/gui/TrackIcons.cpp:77-80`: the type glyph is set again (`sample_track` for a Sample track, via `typeIconName`); a type with no glyph (folder) gets `QIcon()`, so the removed icon is not left on the button.
- Lineage (`git blame`): the clear branch (`cb056c3d5`, 2026-10-03 10:19) was there from the start; the unknown-name check (`b34f580c1`, 12:48) was added before the bug-hunt; the type-glyph restore (`cc5495125`, 16:51) is the 0.5.0 bug hunt commit. Before `b34f580c1`, an unknown name took the first branch and set a blank icon; the current code has neither defect.
- Caller path: `src/gui/tracks/TrackLabelButton.cpp:199` is the only caller of `applyTrackIcon` in `src` (grep). With `false`, `paintEvent` continues to the instrument-logo path, which only runs for Instrument tracks and compares `m_iconName`, which was just cleared.
- Load path: `src/core/TrackLooks.cpp:97-108` (`load`) calls `changed()`; `src/gui/WorkspaceMenus.cpp:203` installs `repaintTrackHeaders` (`src/gui/TrackIcons.cpp:84-90`) as the change hook, so a project load repaints the header and calls `applyTrackIcon` with the unknown name.

## Regression coverage

Existing test file `tests/src/gui/TrackIconsLevelBarTest.cpp`, test `aChosenIconReplacesTheTypeGlyphAndClearingPutsItBack` (line 116). The exact finding sequence is at lines 118-120 (icon shown) and 136-141 (unknown name `no-such-icon`). This report adds one assertion there (lines 140-141, plus the `sameGlyph` helper at line 88 and the `QIcon` / `embed.h` includes): the button must paint the same pixels as the `sample_track` glyph. Before, the test only checked that `shown` is empty and the icon is non-null, which would not catch a header still showing A with `shown` cleared.

No product code was changed. The test is a regression test for behaviour that is already correct, and it was run against two mutants to show it fails when the behaviour regresses.

## Commands and exit codes

All runs from `build/tests` or the worktree as stated; logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-228.r1/logs`.

| # | Command | EXIT | Log | Result |
|---|---|---|---|---|
| 1 | `cmake --build build --target TrackIconsLevelBarTest -j4` | 2 | build-1.log | ccache could not write `/run/user/1000/ccache-tmp` (permission denied); no compile error |
| 2 | same, with `CCACHE_TEMPDIR=/tmp/ccache-tmp-p15` | 0 | build-2.log | built |
| 3 | `ctest -R '^TrackIconsLevelBarTest$' --output-on-failure` (unmodified tree, original test) | 0 | test-1.log | 1/1 passed |
| 4 | `./TrackIconsLevelBarTest` (unmodified tree, original test) | 0 | test-4-binary.log | Totals: 8 passed, 0 failed |
| 5 | strengthened test, unmodified product code: build, then `./TrackIconsLevelBarTest` | 0 / 0 | build-5-strong-green.log, test-5-strong-green.log | **GREEN**: 8 passed, 0 failed |
| 6 | mutant A (`sed`: clear branch disabled, `if (false && ...)`), build + run | 0 / 2 | build-6-strong-mutant.log, test-6-strong-red.log | **RED**: `shown.empty()` fails at test:138; 6 passed, 2 failed |
| 7 | mutant B (clear `shown` but do not restore the glyph: `(void)own;` in place of the `setIcon`), build + run | 0 / 2 | build-8-mutant-noglyph.log, test-8-mutant-noglyph-red.log | **RED**: sameGlyph fails at test:140 ("the header still shows the icon chosen before, not the type glyph"); 6 passed, 2 failed |
| 8 | restore `TrackIcons.cpp` from a copy (`git diff --quiet` shows no diff vs HEAD), build + run | 0 / 0 | build-9-restored.log, test-9-restored.log | **GREEN**: 8 passed |
| 9 | `ctest -R '^TrackIconsLevelBarTest$'` from `build/tests` | 0 | test-10-ctest.log | 1/1 passed (test run 10.29 s) |
| 10 | `python3 feature_check.py <worktree>` | 1 | gate-feature-check-1.log | **not a gate result**: script tries to `mkdir` `<worktree>-feature-check` (a sibling of the worktree, outside the write-jail): PermissionError |
| 11 | `python3 -I /tmp/p15-gates/run_gates.py <worktree>` (same checks and rules as `feature_check.py`, logs redirected into the run folder) | 0 | gate-feature-check-3.log | RESULT: PASS, 1 changed file |

Sequence of the first RED (test before any change, mutant A on the original test, which had the same assertion at line 130): `ctest` exit 8 with `shown.empty()` FAIL at `TrackIconsLevelBarTest.cpp(130)` (test-2-mutant.log). The first GREEN on the original test: exit 0 (row 3).

Gate table (run 11, `gate-feature-check-3.log`):

```
PASS  fork-sources            exit 0
PASS  all-sources-reproduce   exit 0
PASS  no-upstream-regression  exit 0
PASS  check-namespace         exit 0
PASS  no-tautology            exit 0
PASS  doc-figures             exit 0
PASS  complexity              exit 1; no new item in your files
PASS  file-length             exit 1; no new item in your files
RESULT: PASS
```

The complexity and file-length gates exit 1 on the base too (the script treats only new items as failures). The base is `2da2ed356`, cached in `gate-baselines`.

Affected test family: `^TrackIconsLevelBarTest$`, 8 test functions executed (`Totals: 8 passed`), 0 failures.

Files committed: `tests/src/gui/TrackIconsLevelBarTest.cpp` (test only, already registered in `tests/fork-sources.txt:2607`, so no registration change) and this report. No product file and no header changed.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** There is no product change, because the bug is already fixed. The equivalent check is the mutant runs above: with the clear branch disabled (mutant A), the test exits 2 (fails at test:138); with the glyph restore removed while `shown` is still cleared (mutant B), it exits 2 (fails at test:140, the new assertion); on the tree as committed it exits 0 (rows 5 and 8/9). The mutants were applied to a copy of `src/gui/TrackIcons.cpp` and restored; `git diff` on that file is empty at the end.
- **The test is deterministic.** No timers, sleeps or threads. It creates `Track::create(Track::Type::Sample, ...)`, sets the chosen name with `TrackLooks::instance().setIcon` (which calls the change hook synchronously), and compares the `QIcon` pixels of the `sample_track` glyph (`embed::getIconPixmap`) with the button's icon at 24x24. The only event loop call is the existing `QCoreApplication::processEvents()`.
- **The fix is at the cause and complete.** No fix was needed. The checked callers and siblings: the only caller of `applyTrackIcon` is `TrackLabelButton.cpp:199`; the load path (`TrackLooks.cpp:97-108`) and the hook (`WorkspaceMenus.cpp:203`, `repaintTrackHeaders`) reach the header; the folder case (`aTypeWithNoGlyphOfItsOwnShowsNone...`, line 153) is also covered and passes.
- **No collateral damage.** No product code, no data format, no saved-file or `TrackLooks::load` change. A project with unknown icon names loads through `TrackLooks::load` as before and the header falls back to the type glyph as the current code does.
- **ALREADY-FIXED evidence.** `src/gui/TrackIcons.cpp:63-64` (unknown name falls through), `:74-80` (clear `shown`, restore the type glyph), and the existing test that now also checks the header pixels (`TrackIconsLevelBarTest.cpp:136-141`). Mutant B proves the header-pixel assertion is discriminating; mutant A proves the `shown` assertion is.
- **Environment deviations (not hidden):** (1) `feature_check.py` cannot run as shipped from this worktree because it writes its logs to `<worktree>-feature-check`, outside the write-jail (row 10). I ran the same checks with `/tmp/p15-gates/run_gates.py` (it imports the script's own `SIMPLE`, `RATCHET`, `run`, `base_listings`, `key`, `item_file`, and uses its base `2da2ed356` from the cache; only the log directory differs). (2) The ccache temp dir needed `CCACHE_TEMPDIR=/tmp/...` (row 1).
- **Completion check (`submit_check.py`, `submit-check-2.log`, EXIT 1):** PASS on commits on the branch, clean tree, the report, and tests changed. The one FAIL is `gates (feature_check.py)`: the script itself cannot create its sibling log directory (`gate-feature-check-1.log`, PermissionError), so the check sees no gate result. The same gate checks pass through the wrapper (`gate-feature-check-3.log`, EXIT 0). The five "?" judgement items are answered in this section.
- **Not checked here:** the full `ctest` tree and the whole gate suite beyond the feature gates; no other test family was run. The 0.5.0 ratchets (complexity and file-length) are red on the train on purpose; no new item in this worktree's files.
