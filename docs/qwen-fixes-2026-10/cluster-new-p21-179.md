Verdict: FIXED (the sibling overflow is REAL and is fixed here; the cited expression is ALREADY-FIXED)

# Cluster new-p21-179: GrooveTemplate target tick arithmetic overflows before clamping

## Decision

The finding cites `GrooveTemplate.cpp:~184` (`targetTickFor`: `grid + timing` added in `tick_t` before `clampedTick`).

- **Cited expression: ALREADY-FIXED.** `src/core/GrooveTemplate.cpp:187-188` now reads
  `const long long grid = gridTickFor(pos);` then `return clampedTick(grid + stepForPosition(pos).timing);`.
  The sum is in `long long` and is narrowed through `clampedTick` (`GrooveTemplate.cpp:58-65`). Commit
  `83bb21f22` ("fix(groove): the grid arithmetic wrapped at the end of the timeline", Luna cluster 3175) made this
  change, and it is an ancestor of this branch's HEAD.
- **Sibling with the same defect: REAL, fixed here.** `jitteredTick` (`GrooveTemplate.cpp:418-429`, the random
  timing draw used by `applyGroove`) added the moved position and a positive jitter in `tick_t`
  (`moved + static_cast<tick_t>(...)`) before `clampedTick`. For a note at `INT32_MAX` with a positive draw the sum
  wraps negative; `clampedTick` keeps it negative; the `jittered < 0 ? 0 : jittered` floor then moves the note to
  tick 0. This is reached from `applyGroove` (`GrooveTemplate.cpp:479`), called by the control command
  `groove.apply` (id at `src/core/ControlCommandsGrooveEdit.cpp:387`; handler `grooveApply` at `:211`, call at `:251`). It is a control-thread path, not the
  audio thread.

## The change

`src/core/GrooveTemplate.cpp:426-427`: the offset is computed as `long long` and added to `static_cast<long long>(moved)`
before `clampedTick`:

```cpp
const long long offset = std::lround((unit * 2.0 - 1.0) * static_cast<double>(randomTicks));
const tick_t jittered = clampedTick(static_cast<long long>(moved) + offset);
```

For every value that did not overflow, the result is identical. The only change is at the overflowing edge, where the
old code produced undefined behaviour and a wrong tick 0.

## Siblings checked and left alone

- `applyGroove` `GrooveTemplate.cpp:467`: `pos + static_cast<tick_t>(lround(amount * (target - pos)))`. The travel is
  bounded by `|target - pos|` with `amount` in [0, 1], so the sum lies between `pos` and `target`, and both are
  tick values. No overflow is reachable, so it is not changed.
- `gridTickFor` / `absoluteSlotOf` (`GrooveTemplate.cpp:76-79`, `:177-181`): already wide and clamped (the commit above).
- `jitteredVolume` (`:433-441`): an int velocity, not a tick; no tick arithmetic.

## Tests

- `tests/src/core/GrooveTemplateTest.cpp` gains two private slots next to
  `theGridArithmeticDoesNotOverflowAtTheEndOfTheTimeline`:
  - `theTimingOffsetDoesNotWrapAtTheEndOfTheTimeline`: a guard for the cited path (step timing +5 at `INT32_MAX`
    must stay at `INT32_MAX`). It passes on the original code too, which is expected: the cited line is already fixed.
  - `theRandomJitterDoesNotWrapAtTheEndOfTheTimeline` (the RED test): a neutral groove, a note at `INT32_MAX`,
    `randomTicks` 6, seeds 1..64 (fixed, deterministic). Each seed must leave the note in `[last - 6, last]`.
    On the original code seed 1 moves it to 0.

## Commands and results

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-179.r1/logs`. All commands run from the worktree
root unless noted. `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-bunny-tmp-p21-179` was set on every build (see Notes).

| Step | Command | Log | EXIT |
| --- | --- | --- | --- |
| RED build (first attempt, ccache temp dir not writable) | `cmake --build build --target GrooveTemplateTest -j4` | build-1-red.log | 2 |
| RED build (ccache temp dir set) | `CCACHE_TEMPDIR=... cmake --build build --target GrooveTemplateTest -j4` | build-2-red.log | 2 (a test-source compile error: `vector::first`; fixed to `front`) |
| RED build | `CCACHE_TEMPDIR=... cmake --build build --target GrooveTemplateTest -j4` | build-3-red.log | 0 |
| RED run (original product code, new tests) | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^GrooveTemplateTest$' --output-on-failure` | test-1-red.log | 8 |
| GREEN build | `CCACHE_TEMPDIR=... cmake --build build --target GrooveTemplateTest -j4` | build-4-green.log | 0 |
| GREEN run | same ctest | test-2-green.log | 0 |
| 4b: save fix patch | `git diff -- src/core/GrooveTemplate.cpp > logs/fix.patch` | (none) | 0 |
| 4b: revert fix | `git apply -R logs/fix.patch` | (none) | 0 |
| 4b: rebuild reverted | `CCACHE_TEMPDIR=... cmake --build build --target GrooveTemplateTest -j4` | build-5-revert.log | 0 |
| 4b: run reverted (must FAIL) | same ctest | test-3-revert-red.log | 8 |
| 4b: reapply fix | `git apply logs/fix.patch` | (none) | 0 |
| Final build | `CCACHE_TEMPDIR=... cmake --build build --target zene GrooveTemplateTest ControlGrooveCommandsTest -j4` | build-6-final.log | 0 |
| Final family run | `cd build/tests && ctest -R '^(GrooveTemplateTest\|ControlGrooveCommandsTest)$' -V` | test-4-final-family.log | 0 |
| Gate, direct call | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | gate-1-feature-check.log | 1 (crash, see Notes; not a gate result) |
| Gate, same logic, logs in run folder | `python3 logs/gate-driver.py <worktree> logs/gates` | gate-2-feature-check.txt | 0 |

Test counts:

- RED run (`test-1-red.log`): `GrooveTemplateTest` Totals 18 passed, 1 failed. The failure, verbatim:
  `FAIL! : GrooveTemplateTest::theRandomJitterDoesNotWrapAtTheEndOfTheTimeline() 'moved >= last - randomTicks && moved <= last' returned FALSE. (seed 1 moved the last-tick note to 0)`
- Reverted-fix run (`test-3-revert-red.log`): the same single failure, EXIT 8.
- Final family (`test-4-final-family.log`): `GrooveTemplateTest` 19 passed, 0 failed (EXIT 0); `ControlGrooveCommandsTest` 16 passed, 0 failed (EXIT 0). 35 tests executed in the family; 0 failures.

Gates (`gate-2-feature-check.txt`, EXIT 0, `RESULT: PASS`):

| Gate | Result |
| --- | --- |
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS (no new item in changed files) |
| file-length | PASS (no new item in changed files) |

## Files committed

- `src/core/GrooveTemplate.cpp`
- `tests/src/core/GrooveTemplateTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p21-179.md`

No new source or test files were added, so no registration file changed.

## Notes (environment deviations, stated plainly)

- ccache's default temp directory (under `~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`) is not writable from
  this sandbox, so the first two builds failed in ccache, not in the compiler (`build-1-red.log`). I set
  `CCACHE_TEMPDIR` to a directory under `~/.cache/ccache-bunny-tmp-p21-179` for the builds. The compiler and flags are
  unchanged.
- `feature_check.py` could not run as written: it creates `<worktree>/../<name>-feature-check/` for its logs, and that
  folder is in the shared projects directory, which is outside this lane's write-jail (`gate-1-feature-check.log`,
  `PermissionError`). I did not change the gate or its baselines. I ran the same `SIMPLE` and `RATCHET` gates and the
  same comparison, using the functions from `feature_check.py`, with the per-gate logs written to the run folder
  (`logs/gate-driver.py`). The baseline for the fix base `639e1d23e` was already cached, so the ratchet comparison did not
  touch git.

## Self-check

- **Each new test FAILS with the product change reverted and PASSES restored (exit codes)?** Yes. The RED test fails on
  the original product code (`test-1-red.log`, EXIT 8). The 4b revert (`git apply -R`, build-5-revert, EXIT 0) also
  fails (`test-3-revert-red.log`, EXIT 8). The reapplied fix passes (`test-4-final-family.log`, EXIT 0). The guard test
  `theTimingOffsetDoesNotWrapAtTheEndOfTheTimeline` passes on the original code too; that is expected, because the cited
  line was already fixed, and it is a guard, not regression proof for this change.
- **Every acceptance criterion is proven by a test I ran (name and exit code)?** The finding's criterion ("a grid near the
  end of the timeline plus a timing offset must not overflow") is `theTimingOffsetDoesNotWrapAtTheEndOfTheTimeline`
  (passes, in the 19 of `test-4-final-family.log`). The sibling criterion ("the random jitter must not wrap a note at the
  last tick") is `theRandomJitterDoesNotWrapAtTheEndOfTheTimeline` (RED then GREEN, above).
- **Each edit is one undo step; every new or changed command has an honest reversibility class?** Not applicable. No
  command was added or changed; the edit is inside the pure arithmetic of `applyGroove`'s `jitteredTick`. How the caller
  records undo is unchanged (`ControlCommandsGrooveEdit.cpp:211-251` is not edited).
- **Nothing added runs on the audio thread (allocates, frees, locks, waits, does I/O, signals)?** Yes, and nothing
  runs there. `jitteredTick` is `static` in `GrooveTemplate.cpp` and is reached only from `applyGroove`, which is called
  from the control command `grooveApply` (`ControlCommandsGrooveEdit.cpp:211`, `:251`). The added code is one `long long`
  add and one `lround`, both already used in that function. No allocation, lock, log or I/O.
- **Old projects still load and render exactly as before; new data round-trips?** Yes. No saved-data path is touched.
  Every value that did not overflow gives the same result, so the saved data and old projects produce identical ticks.
- **Qt 5.15 / MSVC / macOS portability?** Yes: only `long long` and `std::lround` (already used in this file). No Qt API,
  no `std::filesystem`, no platform macros.
- **Siblings of the changed code checked and consistent?** Yes. The siblings are listed in "Siblings checked and left
  alone" above. `jitteredTick` is called only from `applyGroove` (`GrooveTemplate.cpp:479`), and `applyGroove` is called
  only from `ControlCommandsGrooveEdit.cpp:251` (the `groove.apply` handler, id at `:387`); no other groove command reaches it.
- **Limit or check added that could hit other callers?** None added. The change only widens an intermediate sum.
- **Gates pass?** Yes (`gate-2-feature-check.txt`, EXIT 0, `RESULT: PASS`), with the Notes above on how they were run.
