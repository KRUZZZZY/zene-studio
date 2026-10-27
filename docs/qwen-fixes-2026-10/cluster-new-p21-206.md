FALSE-POSITIVE

## Evidence and decision

The finding is **not real** on `bugs/qwen-new-p21-206`. The crash it describes needs `m_knobPixmap` to be null when `drawFlatKnob` runs on a knob with a non-null model. That state cannot be reached.

- **The pointer is always set for every knob type that reaches `drawFlatKnob`.** `onKnobNumUpdated()` builds `m_knobPixmap` with `std::make_unique<QPixmap>(...)` unconditionally for every non-Styled knob (`src/gui/widgets/Knob.cpp:140`). The constructor always runs it (`src/gui/widgets/Knob.cpp:82`), and `setknobNum` runs it on every type change (`src/gui/widgets/Knob.cpp:267-274`). Nothing resets the pointer: the only assignment to `m_knobPixmap` in the file is line 140, and `grep` finds no `reset` or `nullptr` assignment (see `logs/evidence-1.log`).
- **Only non-Styled knobs reach `drawFlatKnob`.** `Knob::drawKnob` returns right after the call for non-Styled knobs (`src/gui/widgets/Knob.cpp:388-394`). The Styled branch (`:409-435`) always returns before any `drawFlatKnob` call. The call at `src/gui/widgets/Knob.cpp:443` is therefore unreachable, because the Styled branch has already returned and non-Styled knobs returned at `:393`. The only two callers are `:392` and `:443` (`logs/evidence-3.log`), and the declaration is private (`include/Knob.h:203`).
- **A failed pixmap load does not null the pointer.** `embed::getIconPixmap` returns a `QPixmap` value (`src/gui/embed.cpp:120-137`), and it is wrapped in `make_unique` at `src/gui/widgets/Knob.cpp:140` whatever the load result. A pixmap whose load failed is still a non-null `m_knobPixmap` pointer, so `m_knobPixmap->width()` at `src/gui/widgets/Knob.cpp:451` does not dereference null. I did not verify Qt's `QPixmap::width()` on a null-data pixmap against the Qt source in this sandbox (only the declaration is in `/usr/include/qt6/QtGui/qpixmap.h:54-55`). The verdict does not depend on it, because the crash the finding describes needs a null `unique_ptr`, and that does not occur.
- **Line number moved.** The finding's line 390 is now a comment inside `drawKnob` (`src/gui/widgets/Knob.cpp:390`). The dereference the finding means is `src/gui/widgets/Knob.cpp:451` (`side = std::min(m_knobPixmap->width(), m_knobPixmap->height())`).
- **The model check the finding mentions is present.** `drawFlatKnob` returns when `model() == nullptr` (`src/gui/widgets/Knob.cpp:448`), before the pixmap is read.

No product code changes, so no regression test was written and no `ctest` family was run for this closure (a not-real verdict has no red state to prove). The existing tests `KnobKeyboardTest`, `StereoEnhancerControlDialog` and `StereoMatrixControlDialog` were not run for this report.

## Commands and exit codes

| # | Command (run from the worktree) | Log | EXIT |
|---|---|---|---|
| 1 | `rg -n "drawFlatKnob\|m_knobPixmap\s*=\|make_unique<QPixmap>\|m_knobPixmap.reset\|m_knobNum\s*=\s" src/gui/widgets/Knob.cpp include/Knob.h` | `logs/evidence-1.log` | 0 |
| 2 | `sed -n 386,394p src/gui/widgets/Knob.cpp` | `logs/evidence-2.log` | 0 |
| 3 | `rg -n "drawFlatKnob\(" src include` | `logs/evidence-3.log` | 0 |
| 4 | `python3 -I $TMPDIR/fc-p21-206/feature_check.py <worktree>` (copy of `qwen/feature_check.py`, see Gates) | `logs/gates-1.log` | 0 |
| 5 | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-206` | `logs/submit-1.log` | see Self-check below |

Commands 1-3 are the evidence for the verdict. No build was run, because nothing in `src/` or `include/` changed.

## Gates

`qwen/feature_check.py` writes its per-check logs to the sibling folder `<worktree>-feature-check`, which is outside this session's write-jail, so the first run failed with `PermissionError` on that `mkdir` (before any check ran). I re-ran a copy of the same script, changing only two lines: `ROOT` points at the original `qwen/` folder (for `FIX-BASE` and the cached baseline `gate-baselines/639e1d23e4b84a751f6ed90bd626d35d24727cc4.json`, which exists and was only read), and the log folder points at this run's `logs/gates-feature-check/`. The checks, limits and baselines are unchanged.

```
feature-check zene-qwen-new-p21-206 (base 639e1d23e, 1 changed files)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

The one changed file is this report. Gate exit code: 0 (`logs/gates-1.log`). The first, unmodified run exited 1 on the `PermissionError` above and is recorded as `logs/gates-0-permission.log` in the run folder.

## Self-check

- **The regression test fails with the product change reverted and passes with it.** N/A: this is a not-real verdict and the diff contains no product change. Nothing was written to reverse.
- **Every acceptance criterion is proven by a test I ran.** N/A: no acceptance criterion; the verdict rests on the code evidence above (commands 1-3, EXIT 0).
- **The test is deterministic.** N/A: no test was added.
- **The fix is at the cause and complete.** N/A: no fix. The sibling `drawKnob` branch and both `drawFlatKnob` callers were checked (`:392`, `:443`), and both are consistent with the non-null invariant.
- **No collateral damage.** The branch is unchanged apart from this report.
- **Verdict evidence.** The file:line evidence above is the verdict evidence.
