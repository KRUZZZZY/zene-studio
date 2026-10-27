Verdict: FALSE-POSITIVE

new-p21-64: `swapIn` can build a reversed frame range — not reachable. Every writer of a clip's stored `SampleWindow` keeps it well formed (`sourceIn <= sourceOut <= buffer frames`, and non-empty whenever a window is stored), so `swapIn` never receives `from > to`. No product change; no regression test added (there is no reachable state for it to fail on).

Evidence (file:line in this worktree):

- The finding's precondition is a stored window with `sourceIn > sourceOut`. The only stored window is `SampleClip::m_window` ([include/SampleClip.h:284](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-64/include/SampleClip.h:284)), and it is written only by `installWindow()` ([src/core/SampleClip.cpp:737-739](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-64/src/core/SampleClip.cpp:737)), called from four places:
  1. `setSampleWindow()` ([src/core/SampleClip.cpp:694-705](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-64/src/core/SampleClip.cpp:694)): clamps with `SampleWindow::clamped` (`in = min(sourceIn, frames)`, `out = clamp(sourceOut, in, frames)`, so `out >= in` by construction), then refuses `clamped.empty()` (I4: "a window that is not well formed is a rejected edit"). A reversed request such as `{5, 3}` becomes `{5, 5}` and is refused.
  2. `replaceSample()` ([src/core/SampleClip.cpp:377-379](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-64/src/core/SampleClip.cpp:377)): the same clamp, and an empty result falls back to `SampleWindow::full(frames)`. This is the path `swapIn` itself uses ([src/core/ControlCommandsSampleTools.cpp:73-75](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-64/src/core/ControlCommandsSampleTools.cpp:73)).
  3. `resetWindowToFullBuffer()` ([src/core/SampleClip.cpp:729-732](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-64/src/core/SampleClip.cpp:729)): `SampleWindow::full(...)` = `{0, frames}`, always well formed.
  4. Copy and construction ([src/core/SampleClip.cpp:52](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-64/src/core/SampleClip.cpp:52), [:96](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-64/src/core/SampleClip.cpp:96)): copies or `full(...)`.
- Project load ([src/core/SampleClip.cpp:942-944](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-64/src/core/SampleClip.cpp:942)) goes through `setSampleWindow()`, so a saved `srcin > srcout` is clamped and refused rather than stored. Saved data cannot introduce the state either.
- The buffer under the window changes only in `replaceSample` (lines 349, 373), `setSampleFile` (line 399, followed by `resetWindowToFullBuffer` at line 400) and the project load of embedded data (line 923, followed by `resetWindowToFullBuffer` at line 924). `Sample::setBuffer` ([include/Sample.h:135](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-64/include/Sample.h:135)) is called only on play-handle samples ([src/core/SamplePlayHandle.cpp:476](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-64/src/core/SamplePlayHandle.cpp:476), [:499](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-64/src/core/SamplePlayHandle.cpp:499)), not on a clip's `m_sample`, so the window cannot outlive a buffer it was clamped against.
- The two `swapIn` callers pass valid ranges: `crop` passes `window.sourceIn`, `window.sourceOut` after the whole-source refusal ([src/core/ControlCommandsSampleTools.cpp:85-87, 95](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-64/src/core/ControlCommandsSampleTools.cpp:85)), and `makeUnique` passes `0, sampleSize()` ([src/core/ControlCommandsSampleTools.cpp:107](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-64/src/core/ControlCommandsSampleTools.cpp:107)). `crop`'s own refusal only covers the whole-source case, which is correct because a stored window is never reversed.
- The existing regression for the reachable crop path, `ControlSampleOperatorTest::aSourceEditKeepsTheClipPlaybackSettings` ([tests/src/core/ControlSampleOperatorTest.cpp:359](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-64/tests/src/core/ControlSampleOperatorTest.cpp:359), which authors its window through `setSampleWindow`), passes on this branch (see the log table below).

Optional hardening, NOT applied: a `from < to` guard at the top of `swapIn` would be dead code under the invariant above. Adding it would be a judgement call for a reviewer who wants defence in depth, not a fix for a reachable bug.

Commands run (EXIT codes are the ones shown; logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-64.r1/logs/`):

| Step | Command | EXIT |
| --- | --- | --- |
| First target build, failed in ccache (`Permission denied` on the ccache temp dir under `zene-bunny-fix/dsh`), not in compilation | `cmake --build build --target ControlSampleOperatorTest -j4 > .../build-1.log 2>&1` | 2 |
| Same target, ccache disabled for this environment only | `CCACHE_DISABLE=1 cmake --build build --target ControlSampleOperatorTest -j4 > .../build-2.log 2>&1` | 0 |
| Family run from `build/tests` (1 test executed) | `ctest -R ControlSampleOperator --output-on-failure > .../test-1.log 2>&1` | 0 (100% passed, 1/1) |
| `feature_check.py` on the worktree | `python3 .../feature_check.py <worktree> > .../gates-1.log 2>&1` | 1, before any gate ran: it creates `<worktree>-feature-check/` next to the worktree (`PermissionError`, outside the write-jail). The same gates were run directly instead (below). |
| fork-sources | `bash tests/fork-sources-gate.sh > .../gate-fork-sources.log 2>&1` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh > .../gate-all-sources-reproduce.log 2>&1` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > .../gate-no-upstream-regression.log 2>&1` | 0 |
| check-namespace | `tests/scripted/check-namespace > .../gate-check-namespace.log 2>&1` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh > .../gate-no-tautology.log 2>&1` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py > .../gate-doc-figures.log 2>&1` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check > .../gate-complexity.log 2>&1` | 1 (7 `REGRESSION` lines; the gate is red on the train by design. None names a file this branch changes: the branch changes only this report, and no `REGRESSION` line names `ControlCommandsSampleTools.cpp`, `SampleClip.cpp` or `SampleWindow.h`) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check > .../gate-file-length.log 2>&1` | 1 (19 `REGRESSION` lines; same reasoning) |

The six simple gates all pass. The two ratchet gates are red on the train by design. I did not compare their listings against the base checkout, which `feature_check.py` does in its cache, so the PASS for those rows rests on this branch changing no source file, not on a baseline diff.

Product files changed: none. Committed files: this report only.

## Self-check

Submit-check (`submit_check.py ... new-p21-64`, output in `.../logs/submit-check-1.log`, EXIT=1 before this section existed):

- **Report has a Self-check section answering every ASK item:** this section (added in a follow-up commit).
- **Gates (`feature_check.py`) FAIL, not a product failure:** the script cannot run in this sandbox. It creates `<worktree>-feature-check/` beside the worktree, which is outside the write-jail (`PermissionError` at `feature_check.py:71`, `.../logs/gates-1.log`, EXIT=1). The six simple gates were run directly from the worktree and all exit 0. The two ratchet gates exit 1 on the train by design, with no `REGRESSION` line naming a file this branch changes. Re-run `feature_check.py` from an unrestricted shell to confirm the table.
- **Regression test fails with the product change reverted and passes with it (exit codes):** not applicable. There is no product change, and no new test, because the reported state cannot be produced. The nearest existing test, `ControlSampleOperatorTest` (`ctest -R ControlSampleOperator`), passes on this branch: EXIT=0, 1/1 (`test-1.log`).
- **Test is deterministic (seam or injected values):** not applicable, no new test. The existing test uses no timing; it authors its window via `setSampleWindow` at `tests/src/core/ControlSampleOperatorTest.cpp:359`.
- **Fix is at the cause and complete, every caller and sibling checked:** not applicable, no product change. Callers of `swapIn` (`crop`, `makeUnique`) and every writer of `SampleClip::m_window` were checked (`src/core/SampleClip.cpp:377-379, 694-705, 729-732, 737-746, 942-944`, `src/core/ControlCommandsSampleTools.cpp:85-87, 95, 107`).
- **No collateral damage (saved data, limits, other callers):** no product code changed, so nothing that loads or calls the code changes behaviour. Saved projects enter the window through `setSampleWindow()` (`SampleClip.cpp:942-944`), which clamps and refuses empty windows, so a saved `srcin > srcout` cannot be stored.
- **FALSE-POSITIVE justification (file:line or contract):** `SampleClip.cpp:377-379` (`replaceSample`, clamp then full-buffer fallback), `SampleClip.cpp:694-705` (`setSampleWindow`, clamp then reject empty; I4 contract in the comment at 700-701), `include/SampleWindow.h:64-68` (`clamped` makes `out >= in`). Full list in the Evidence section above.
