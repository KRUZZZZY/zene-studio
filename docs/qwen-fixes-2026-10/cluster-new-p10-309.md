FALSE-POSITIVE (for the stated trigger; the code-order hazard at lines 121-122 is real but unreachable, so no fix was made)

# Cluster new-p10-309: Empty pattern previews can divide by zero

Finding: `src/gui/clips/PatternClipView.cpp:121` (historical line 121; the current tree has the divisions at 119-122 and the guard at 206).
Claim: a zero-length pattern (`lengthOfPattern()` returns 0) makes `pixelsPerPattern` / `ticksPerPattern` zero, and the modulo on the paint path faults.

## Evidence

1. **A pattern's length cannot be zero.** `PatternStore::lengthOfPattern()` (`src/core/PatternStore.cpp:85-97`) seeds `maxLength` with `TimePos::ticksPerBar()`, takes the max over the instrument-track clips, and returns `maxLength.nextFullBar()`. `nextFullBar()` (`include/TimePos.h:93`) is `ceil(ticks / ticksPerBar)`, which is at least 1 when the input is at least one bar. So `ticksPerPattern` (`PatternClipView.cpp:120`) is always at least `ticksPerBar > 0`, and the divisor at line 121 can never be zero from an empty pattern. An empty pattern gives a 1-bar length, not 0.
   - The existing callers already rely on this floor: `PatternClip.cpp:41-42` guards `t > 0` and `PatternStore.cpp:52` guards `lengthOfPattern(clipNum) <= 0` before dividing. Both guards are dead code for the same reason.

2. **The only other way `pixelsPerPattern` (`PatternClipView.cpp:119`) can be zero is `pixelsPerBar() * lengthBars < 1`.** That needs `pixelsPerBar()` below 1, and it cannot happen on the song editor, the only host of `PatternClipView`:
   - `PatternClip.cpp:113-115` creates `PatternClipView` only for `PatternClip`, and `Song.cpp:1436-1437` creates pattern tracks at song level, so the view lives under `SongEditor`.
   - `SongEditor.cpp:81-83`: `MIN_PIXELS_PER_BAR = 4`, `MAX_PIXELS_PER_BAR = 400`, `ZOOM_STEPS = 200`.
   - `SongEditor.cpp:751-762` (`calculatePixelsPerBar`) maps the zoom model value to `exp2(...)`, so the result is at least 4.
   - The zoom model is an `IntModel` over `0..ZOOM_STEPS` (`SongEditor.cpp:95`), and `AutomatableModel::setValue` and `setInitValue` clamp through `fittedValue` (`src/core/AutomatableModel.cpp:462-466` and `:1061`). The config-restored initial value is clamped too.
   - `PatternEditor` (`src/gui/editors/PatternEditor.cpp:241-243`) can compute a zero or negative `pixelsPerBar` from `width() - m_trackHeadWidth`, but it hosts the pattern's inner tracks (`PatternStore` tracks), not `PatternClip`s, so it never paints `PatternClipView`.

3. **Latent hazard (real by code, unreachable today):** the modulo at `PatternClipView.cpp:122` runs before the `pixelsPerPattern > 0` guard at `:206`. If `pixelsPerPattern` truncated to 0 it would fault. With item 2, that cannot happen. This is not the finding's trigger and changing it would not change any reachable behaviour, so it is not fixed here.

## Commands and exit codes

| Command | EXIT | Log |
| --- | --- | --- |
| `python3 .../qwen/feature_check.py <worktree>` | 1 | `feature-check-1.log`: `PermissionError` creating `<worktree>/../<name>-feature-check` (outside the write-jail). The script never reached its table. |
| `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources-1.log` |
| `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-1.log` |
| `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-1.log` |
| `tests/scripted/check-namespace` | 0 | `gate-check-namespace-1.log` |
| `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology-1.log` |
| `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures-1.log` |
| `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity-1.log`: 6 `REGRESSION` lines, all in files this branch does not change (for example `src/wasm/WasmSandbox.cpp`, `tests/src/gui/XyPadPairTest.cpp`, `src/core/TakeLane.cpp`). The branch diff against its base is empty (0 files), so the wrapper's "changed files" filter yields no failing items. |
| `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length-1.log`: 23 `REGRESSION` lines, same reasoning (none in `PatternClipView.cpp`). |
| `python3 .../qwen/submit_check.py <worktree> new-p10-309` | 1 | `submit-check-1.log` (run before this report existed) |

No build or ctest was run: no product file was changed, so there was nothing to build and no regression to prove.

## Why no RED/GREEN

There is no product change. A regression test that passes on the unchanged tree would not prove the fix, and the finding's trigger is unreachable, so writing one would be a test without a bug behind it.

## Self-check

- **Test fails with the change reverted and passes with it (exit codes):** not applicable. No product change, no regression test. Verdict is not-real (see items 1-2 above).
- **Test is deterministic:** not applicable (no new test).
- **Fix at the cause and complete:** not applicable (no fix). The callers and siblings checked are listed in items 1-2: `PatternClip.cpp:41-42`, `PatternStore.cpp:52`, `Song.cpp:1436-1437`, `SongEditor.cpp:81-95`, `:751-762`, `AutomatableModel.cpp:462-466`, `:1061`, and `PatternEditor.cpp:241-243`.
- **No collateral damage:** nothing changed, so old projects and saved data behave exactly as before.
- **Not-real verdict evidence:** given above with file:line (`PatternStore.cpp:85-97`, `TimePos.h:93`, `PatternClip.cpp:113-115`, `Song.cpp:1436-1437`, `SongEditor.cpp:81-95`, `:751-762`, `AutomatableModel.cpp:462-466`, `:1061`).
- **Gates:** feature_check.py could not run in the jail (exit 1, `PermissionError`, see table). The gate commands it lists were run directly from the worktree: six PASS (exit 0); complexity and file-length exit 1 with regressions only in files this branch does not touch. The wrapper's PASS/FAIL table is therefore not produced; this is a limitation, not a pass.
- **submit_check:** exit 1 before this report existed. Its "tests added or changed" item will stay FAIL, because a not-real verdict has no fix and no regression test. The gates item stays FAIL because the wrapper could not run in the jail.
