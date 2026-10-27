FIXED

Cluster new-p14-444: "Consolidation can overflow while adding a clip offset" (src/core/ClipConsolidate.cpp, base line 56).

## What was wrong

- `mixClip` computed `clipStart + startTimeOffset` and `clip->endPosition()` (`sp + m_length`) in `tick_t`, which is `std::int32_t` ([include/LmmsTypes.h:35](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-444/include/LmmsTypes.h:35)). Signed overflow is undefined behaviour, and in practice the sum wraps.
- The finding's exact example (clip at tick 1, offset `INT_MAX`) does overflow at that line. But I checked the consequence and it does **not** reproduce as described: `SampleClip::sourceFrameAt` computes the same origin sum at [src/core/SampleClip.cpp:838](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-444/src/core/SampleClip.cpp:838) and wraps the same way, so the sample window comes out empty and the render is silent, before and after this change. The start-side regression test therefore passes on the unfixed code too (see the RED/GREEN table). It is a guard, not the proof.
- The overflow that does change the output is the clip-end sibling. A clip that ends past the largest tick wraps `endPosition()` negative, `begin >= to` drops the whole clip, and the audio before the largest tick is lost. This is reproduced RED below.

## The fix

[src/core/ClipConsolidate.cpp:55-80](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-444/src/core/ClipConsolidate.cpp:55):

- `clipStart`, `from`, `to`, `begin` are `std::int64_t` (lines 60-67). The offset and length sums are exact.
- `to` is clamped to the largest tick (lines 62-63). An end past the largest tick has no tick the mix can hold.
- `begin < to <= INT_MAX` is checked before narrowing (lines 68-70), so both `sourceFrameAt` calls get in-range ticks (`TimePos(beginTick)`, `TimePos(endTick)`, line 71). The old code passed `clip->endPosition()`, which is the same tick when the sum does not overflow.
- `span` is computed from `to - begin` in `int64` before the double conversion (line 78).

Behaviour is unchanged for every clip that does not pass the largest tick: `from`, `to`, `begin` and the window arguments take the same values as before. The only output change is that a clip ending past the largest tick now renders up to it instead of disappearing.

Callers checked: `ClipConsolidate::render` is called from `src/core/ControlCommandsConsolidate.cpp:212` (`clip.consolidate`) and `src/core/ControlCommandsLibraryClips.cpp:167` (`browser.save_clip`, via `writeClip`). Both go through `mixClip`, and both families were run (below).

No allocation, lock, log or new control command was added. `mixClip` is offline render code (control-command thread), not an audio-thread path. Undo is untouched.

## Not changed (same pattern, other files; outside this cluster)

Each of these does `startPosition + startTimeOffset` or `sp + length` in `tick_t`. I did not change them: they are shared with playback and editing, and the finding is about this function.

- [src/core/SampleClip.cpp:838](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-444/src/core/SampleClip.cpp:838) and [SampleClip.cpp:892](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-444/src/core/SampleClip.cpp:892) (`sourceFrameAt` origin; playback path).
- [src/core/ClipAudioLoop.cpp:82](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-444/src/core/ClipAudioLoop.cpp:82), [ClipPreFades.cpp:59](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-444/src/core/ClipPreFades.cpp:59) and [:110](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-444/src/core/ClipPreFades.cpp:110), [ClipTransients.cpp:54](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-444/src/core/ClipTransients.cpp:54), [SampleFrameEdits.cpp:50](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-444/src/core/SampleFrameEdits.cpp:50) and [:179](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-444/src/core/SampleFrameEdits.cpp:179), [ArrangementEdits.cpp:328](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-444/src/core/ArrangementEdits.cpp:328).
- [include/Clip.h:128-132](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-444/include/Clip.h:128): `endPosition()` returns `sp + m_length` in `int`.
- [src/core/SampleClip.cpp:575](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-444/src/core/SampleClip.cpp:575): `m_startFrameOffset = startTimeOffset * framesPerTick` stores a float into an `int`. For a large offset the conversion is out of range (undefined). The start-side regression test goes through this setter, because a loaded project does too.
- The warp branch of `SampleClip::sourceFrameAt` was not examined.

## Regression tests

[tests/src/core/ClipConsolidateTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-444/tests/src/core/ClipConsolidateTest.cpp:254) (two new cases, no timing, no sleeps):

- `aClipEndingPastTheLargestTickIsRenderedUpToIt` (line 254): a 48-tick take at `INT_MAX - 20`, rendered over `[INT_MAX-20, INT_MAX)`. Its first 20 ticks must read 0.5. **This is the RED test:** it fails on the unfixed code.
- `aClipOffsetPastTheLargestTickContributesNothing` (line 275): a take at tick 1 with offset `INT_MAX`, rendered over `[0, 2*length)`. The mix must be silent. **This passes on the unfixed code too**, for the reason above, so it guards the new behaviour without proving it.

Matrix: normal path and the six existing test functions (two-clip consolidate and undo, muted track and clip gain, composite, range-start overlap, refusals, default path); exhaustion (end past the largest tick, offset past the largest tick); audio-thread (none, offline path); platform (no platform code; the build here is Linux/Qt6 offscreen).

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-444.r1/logs` (file names below).

| Step | Command (run from the worktree unless noted) | EXIT | Log |
|---|---|---|---|
| build (unfixed, first try) | `cmake --build build --target ClipConsolidateTest -j4` | 2 | build-1.log: ccache could not create its temp file (permission denied in the default temp dir). Environment problem, not code. |
| build (unfixed) | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target ClipConsolidateTest -j4` | 0 | build-2.log |
| **RED** | `cd build/tests && ctest -R '^ClipConsolidateTest$' --output-on-failure` | **8** | test-red-1.log: `FAIL! aClipEndingPastTheLargestTickIsRenderedUpToIt() 'std::fabs(levelAt(fresh, 2, 18) - 0.5) < 1e-4' returned FALSE`; `Totals: 9 passed, 1 failed` |
| fix written; build | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target ClipConsolidateTest -j4` | 0 | build-3.log (0 errors) |
| **GREEN** | `cd build/tests && ctest -R '^ClipConsolidateTest$' -V` | **0** | test-green-2.log: `Totals: 10 passed, 0 failed` |
| 4b: save product diff | `git diff -- src/core/ClipConsolidate.cpp > .../logs/fix.patch` | 0 | fix.patch |
| 4b: unfix | `git apply -R .../logs/fix.patch` | 0 | |
| 4b: build (unfixed) | `cmake --build build --target ClipConsolidateTest -j4` (ccache env as above) | 0 | build-4b-unfixed.log |
| 4b: **RED again** | `cd build/tests && ctest -R '^ClipConsolidateTest$' -V` | **8** | test-4b-unfixed.log: `FAIL!` on `aClipEndingPastTheLargestTickIsRenderedUpToIt`; `Totals: 9 passed, 1 failed` |
| 4b: re-apply fix | `git apply .../logs/fix.patch` | 0 | |
| 4b: build (fixed) | `cmake --build build --target ClipConsolidateTest -j4` (ccache env as above) | 0 | build-4b-fixed.log |
| 4b: **GREEN again** | `cd build/tests && ctest -R '^ClipConsolidateTest$' -V` | **0** | test-4b-fixed.log: `Totals: 10 passed, 0 failed` |
| collateral family (browser.save_clip) | `cmake --build build --target LibraryBrowserTest -j4`, then `cd build/tests && ctest -R '^LibraryBrowserTest$' -V` | 0, 0 | test-5-lib.log: `Totals: 23 passed, 0 failed` |
| gates | `python3 -I .../logs/gates/driver.py <worktree>` (see note) | 0 | gates-1.log: `RESULT: PASS` |

Test family size: `ClipConsolidateTest` ran 10 QTest results: 6 existing test functions, 2 new ones, and `initTestCase` and `cleanupTestCase`. `LibraryBrowserTest` ran 23 QTest results, same counting. Zero-test check: both non-zero.

Gate note: the unmodified `qwen/feature_check.py` could not run here. Its first act is `mkdir` of `<worktree>-feature-check` in the projects folder, which the write-jail blocks (`PermissionError: Permission denied`, exit 1; the output was not kept as a log). Its ratchet baseline also runs `git worktree add` inside the shared repo. I ran `gates/driver.py` instead. It imports the same `SIMPLE`/`RATCHET` lists and the same `run`/`key`/`item_file` functions from `feature_check.py` without modifying the file. The only difference is the source of the base listings: `git archive c6ee4fc7b` into the run folder, rather than a worktree in the shared repo. Base = HEAD here, so the baseline is the tree before this change. Limits and baselines are untouched.

Gate rows (driver): fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures: all exit 0. complexity and file-length: exit 1 on the base and on this tree, with no new item in the two changed files (`ClipConsolidate` is not listed in either). `RESULT: PASS`.

## Files committed

- `src/core/ClipConsolidate.cpp` (fix, two includes)
- `tests/src/core/ClipConsolidateTest.cpp` (two regression cases, one include)
- `docs/qwen-fixes-2026-10/cluster-new-p14-444.md` (this report)

No registration needed: `src/core/ClipConsolidate.cpp` is already in `tests/fork-sources.txt`, and the test file is already in `tests/CMakeLists.txt`.
