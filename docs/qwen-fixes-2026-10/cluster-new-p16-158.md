Verdict: FALSE-POSITIVE

new-p16-158: Empty patterns can divide by zero during PatternTrack playback

Evidence: the divisor cannot be zero. `PatternTrack::play()` computes `patternLength = lengthOfPattern(...) * TimePos::ticksPerBar()` and then `startTimeOffset() % patternLength` at [PatternTrack.cpp:119-121](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-158/src/tracks/PatternTrack.cpp:119). The finding's line 116 is the `if (!clip->isMuted() && ...)` line in the current tree; the modulo is at 121.

Three facts make the divisor positive:

1. **`lengthOfPattern()` is floored at one bar.** [PatternStore.cpp:87](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-158/src/core/PatternStore.cpp:87) starts `maxLength` at `TimePos::ticksPerBar()`, [PatternStore.cpp:96](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-158/src/core/PatternStore.cpp:96) only raises it with `std::max`, and [PatternStore.cpp:100](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-158/src/core/PatternStore.cpp:100) returns `maxLength.nextFullBar()`. An empty pattern, or one whose clips are zero length, therefore still returns 1 bar. A zero-length pattern is not reachable through this function.
2. **A bar is never below 6 ticks.** [TimePos.h:149](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-158/include/TimePos.h:149) gives `ticksPerBar(sig) = 192 * numerator / denominator`. The time-signature models are clamped to 1..32 ([MeterModel.cpp:35-36](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-158/src/core/MeterModel.cpp:35), clamp at [AutomatableModel.cpp:606](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-158/src/core/AutomatableModel.cpp:606) and [616](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-158/src/core/AutomatableModel.cpp:616)), so the minimum is `192 * 1 / 32 = 6`. The static is written from `Song::ticksPerBar()` only ([Song.cpp:413](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-158/src/core/Song.cpp:413), value from [Song.h:249-251](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-158/include/Song.h:249)).
3. **No overflow path reaches zero.** `bar * ticksPerBar` is `int32` ([LmmsTypes.h:34-35](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-158/include/LmmsTypes.h:34)). The product is about the clip length rounded up to a whole bar, so it cannot reach 2^32 and wrap to 0.

The finding's premise that `PatternStore::play` cannot protect the calculation is correct about ordering: the guard at [PatternStore.cpp:52](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-158/src/core/PatternStore.cpp:52) runs after line 121. But the divisor is already positive, so the guard is defensive and the calculation is safe. The same floor covers the other `lengthOfPattern` callers I read or saw in grep output: [PatternStore.cpp:57](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-158/src/core/PatternStore.cpp:57), [PatternStore.cpp:240](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-158/src/core/PatternStore.cpp:240) and [TrackContainer.cpp:602](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-158/src/core/TrackContainer.cpp:602) (seen in grep output only, not read in full).

Decision: no product change. The finding does not reproduce on this branch, so there is no fix to make. The added test pins the invariant the finding depends on; it is not a red-green regression for a fix (see Self-check).

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-158.r1/logs/`.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Gate, first attempt | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 (PermissionError: the script writes logs to `<worktree>/../<name>-feature-check`, outside the write-jail; nothing ran) | none |
| Gate, rerun | `python3 -I logs/feature_check_logdir.py <worktree> logs` | 0 | `feature-check-1.log` (8 rows PASS, `RESULT: PASS`) |
| Build, first attempt | `cmake --build build --target AutomationTrackTest -j2` | 2 (ccache could not write its temp dir under `~/.local/share/zene-bunny-fix/dsh/...`) | `build-1.log` |
| Build, rerun | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p16-158 cmake --build build --target AutomationTrackTest -j2` | 0 | `build-2.log` |
| Test (ctest) | `cd build/tests && ctest -R '^AutomationTrackTest$' --output-on-failure` | 0 (1/1 passed) | `test-1.log` |
| Test (direct, verbose) | `cd build/tests && ./AutomationTrackTest -v2` | 0 (`Totals: 16 passed, 0 failed`; new function `PASS` at line 145) | `test-2-direct.log` |
| Pre-submit check (before this report) | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p16-158` | 1 (missing report, no commit, no self-check yet) | `submit-check-0.log` |

The gate is the shared script, unmodified except that its log directory is redirected into the LOG FOLDER (only `ROOT` and the `logs` path change in the copy). Its checks and limits are the same.

Affected-family count: `ctest -R '^AutomationTrackTest$'` executed 1 test, which contains 16 test functions (all passed).

## Self-check

1. **Regression FAILS with the product change reverted and PASSES with it.** Not applicable: there is no product change. The test passes on the unchanged code (`build-2.log` / `test-1.log` / `test-2-direct.log`, EXIT 0). This is an invariant pin, not a red-green proof. It would turn red if `lengthOfPattern` ever returned 0 or if `ticksPerBar` could reach 0.
2. **Deterministic.** No wall-clock, sleeps or races. Inputs are fixed: the time signature is set to 1/32 through `MeterModel::numeratorModel()/denominatorModel()` and restored by `qScopeGuard`; the clip is one bar at position 0; `play()` is given 4410 frames and offset 0. The test asserts `TimePos::ticksPerBar() == 6` (passed), so the 1/32 path is really exercised.
3. **Fix at the cause and complete.** No fix. Callers of `lengthOfPattern` share the same floor (see Evidence). Nothing else changed.
4. **No collateral damage.** No product code changed, so no project load, save or limit behaviour changes. The test is in an existing file (`tests/src/tracks/AutomationTrackTest.cpp`), so no new registration is needed. The file already sits in `tests/CMakeLists.txt`, and `#include <QScopeGuard>` is the only added include.
5. **FALSE-POSITIVE evidence.** The code (file:line) is in the Evidence section: `PatternStore.cpp:87,96,100`, `TimePos.h:149`, `MeterModel.cpp:35-36`, `AutomatableModel.cpp:606,616`, `Song.cpp:413`, `Song.h:249-251`, `LmmsTypes.h:34-35`.

Not checked: no full-tree build or full ctest run (the lane is -j4 and the build is 45–50 GB). No audio-thread path was touched, so no realtime review applies.
