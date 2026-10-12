# Cluster new-p10-310

**Verdict: FALSE-POSITIVE**

The alleged integer division at `src/gui/clips/PatternClipView.cpp:120` does not occur. `ClipView::pixelsPerBar()` returns `float` (`include/ClipView.h:180`), while `TimePos::ticksPerBar()` returns `tick_t` (`include/TimePos.h:127`). The usual arithmetic conversions therefore make the division floating-point. `TimePos` converts to `int` (`include/TimePos.h:97`), so the offset ticks are converted for the multiply, whose other operand is the floating-point ratio. The cast to `int` in line 120 happens after that multiply.

At low zoom, an offset that maps to less than one pixel can still truncate to zero in the final cast. That is pixel-level rounding; the claimed integer division that would zero the ratio is absent. No production code change is warranted.

## Evidence and verification

| Item | Command / result |
|---|---|
| Type and expression evidence | `rg -n "offset = static_cast<int>|startTimeOffset\\(\\) const|float pixelsPerBar\\(\\) const|ticksPerBar\\(\\)|operator int\\(\\) const" src/gui/clips/PatternClipView.cpp include/Clip.h include/ClipView.h include/TimePos.h; echo EXIT=$?` — `EXIT=0`; declarations and expression are at the lines cited above. |
| RED regression | Not run (`N/A`): the source evidence disproves the reported defect, so the procedure's false-positive path skips regression creation and execution. |
| GREEN regression | Not run (`N/A`): no regression was created for a disproven behavior. |
| Affected-family test count | 0 executed; `ctest` was not invoked. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-310.r1/logs/gate-5.log 2>&1; echo EXIT=$?` — `EXIT=0`. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-310.r1/logs/gate-4.log 2>&1; echo EXIT=$?` — `EXIT=0`. |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p10-310.md` only. |

No build or test command was run: no production code or test source changed, and the false-positive path skips those steps.
