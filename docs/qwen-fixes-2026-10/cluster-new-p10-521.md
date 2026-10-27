Verdict: FALSE-POSITIVE

new-p10-521: FALSE-POSITIVE. The filter `update(x, ch)` methods index fixed `CHANNELS`-sized arrays without a check, so a call with `ch >= CHANNELS` is out of range. No product path makes that call: every production caller passes a literal 0 or 1 on a two-channel filter, or a loop index bounded by 2. No code change was made.

## Evidence

The unchecked indexing is real, but only for a caller that passes an index of at least `CHANNELS`:

- The state arrays are sized by the template parameter: `include/BasicFilters.h:146-147` (`frame` in `LinkwitzRiley`), `:192` (`BiQuad`), `:227` (`OnePole`). `CHANNELS` defaults to `DEFAULT_CHANNELS`, which is `2` (`include/lmms_constants.h:38`).
- The indexed methods are `LinkwitzRiley::update` at `include/BasicFilters.h:125-127`, `BiQuad::update` at `:182-187`, `OnePole::update` at `:219-222`, and `BasicFilters::update` at `:357`. The finding's line 116 is now blank; the methods are at the lines above.
- `BasicFilters::update` forwards its `_chnl` unchanged to `m_biQuad` and `m_subFilter` (`include/BasicFilters.h:708`, `:714`), so it has the same contract as its sub-filters.

Every production caller passes a channel in `[0, CHANNELS)`:

| Caller | Filter (CHANNELS) | Channel argument | Lines |
| --- | --- | --- | --- |
| `src/core/InstrumentSoundShaping.cpp` | `BasicFilters<>` (2) | literal `0`, `1` | `:176-177`, `:193-194`, `:209-210`, `:219-220` |
| `plugins/DualFilter/DualFilter.cpp` | `BasicFilters<2>` | literal `0`, `1` | `:159-160`, `:183-184` |
| `plugins/Bitcrush/Bitcrush.cpp` | `StereoLinkwitzRiley` | literal `0`, `1` | `:200-201`, `:212-213` |
| `plugins/CrossoverEQ/CrossoverEQ.cpp` | `StereoLinkwitzRiley` | literal `0`, `1` | `:142-145`, `:153-154`, `:163-164`, `:173-174`, `:183-184` |
| `plugins/SlewDistortion/SlewDistortion.cpp` | `StereoLinkwitzRiley` | literal `0`, `1` | `:162-163` |
| `plugins/LOMM/LOMM.cpp` | `StereoLinkwitzRiley`, `BasicFilters<2>` | loop `i`, bounded by `i < 2` | loop `:208`; calls `:218-222` |
| `plugins/Eq/EqEffect.cpp` | `StereoBiQuad` via `EqFilter` | literal `0`, `1` | `:207-208`, `:212-213`, `:218-219`, `:221-222`, `:228-229`, `:234-235`, `:240-241`, `:246-247`, `:252-253`, `:258-259`, `:263-264` |

The Eq chain is the only indirect path. `EqFilter::update(in, ch, frameProgress)` forwards `ch` unchanged to `StereoBiQuad::update` (`plugins/Eq/EqFilter.h:137-140`), and every caller passes a literal 0 or 1 (`plugins/Eq/EqEffect.cpp` above).

Other checks:

- `OnePole<CHANNELS>` / `StereoOnePole` have no production user. `OnePoleLowPassNode` (`include/RoutingNodes.h:64`, `src/core/RoutingNodes.cpp`) is a separate class.
- The filter template arguments are fixed at compile time and channel indices come from literals or constant loops. No project data, user input or runtime channel count reaches these calls.
- Tests use literal 0 or 1 as well: `tests/src/core/BasicFiltersSampleRateTest.cpp:49,63,65,74`, `tests/reference/DualFilter/DualFilter.cpp:159-184`, `tests/src/plugins/MultitapEchoBoundsTest.cpp:167,175`.

So the out-of-range call in the finding is a caller error that no product path makes. It has no user-visible effect. Adding a guard in the per-sample path would change behaviour that nothing reaches, which the task rules do not allow, so no fix was made and no regression test was written (there is no defect to prove RED against).

What would change this verdict: a caller that computes the channel index from data, or passes a channel count larger than the filter's `CHANNELS`.

Optional, not done: a debug-only assert at the call boundary, if the owner wants hardening. It touches the per-sample hot path, so it needs its own decision.

## Commands and results

| Evidence | Command | Result |
| --- | --- | --- |
| Caller audit (filter `update` call sites, LOMM loop bound, definitions) | `{ echo "# caller audit for filter update() channel argument"; rg -n "(m_filter1\|m_filter2\|n->m_filter\|m_lp1\|m_lp2\|m_lp3\|m_hp1\|m_hp2\|m_hp3\|m_hp4\|m_ap\|m_lp\|m_hp\|m_filter\|m_hp12\|m_lp12\|m_hp24\|m_hp480\|m_hp481\|m_lowShelf\|m_highShelf\|m_para[1-4]\|m_biQuad\w*\|m_biQuadFrame\w*)\s*(->\|\.)\s*update\(" src include plugins/DualFilter plugins/LOMM plugins/CrossoverEQ plugins/SlewDistortion plugins/Bitcrush plugins/Eq --glob '*.cpp' --glob '*.h'; echo "# LOMM loop bound"; rg -n "for \(int i = 0; i < 2" plugins/LOMM/LOMM.cpp; echo "# definitions"; rg -n "DEFAULT_CHANNELS = \|class LinkwitzRiley\|class BiQuad\|class OnePole\|class BasicFilters\|::update\(" include/BasicFilters.h include/lmms_constants.h; } > logs/001-caller-audit.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Direct gate run, as specified | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/002-feature-check-pre-report.log 2>&1` | EXIT=1. The script tried to create its log directory `…/zene-qwen-new-p10-521-feature-check` next to the worktree, outside the write-jail (`PermissionError`). No product check ran. |
| Gate run with redirected log directory | Copy of `feature_check.py` in `logs/gate-copy/`, differing from the original only in `ROOT` (kept pointing at the bunny directory, so the same `FIX-BASE` and baselines are read) and the log directory (`logs/feature-check-out`). Diff in `logs/003-gate-copy.diff`. Run as `python3 -I logs/gate-copy/feature_check.py <worktree> > logs/004-feature-check-pre-report.log 2>&1` | EXIT=0; base `639e1d23e`, 0 changed files; all 8 rows PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

Not run:

- RED and GREEN regression runs: not applicable, since there is no product change.
- Family tests (`BasicFiltersSampleRateTest`, `DualFilter`, `MultitapEchoBounds`): not built or run in this lane. `build/tests` has no binaries for them, and the verdict does not depend on them.

## Self-check

Pending: the completion check (`submit_check.py`) runs after this commit. Its gate step calls `feature_check.py` from the bunny directory, so it is expected to hit the same out-of-jail log directory error. That will be reported as-is below.
