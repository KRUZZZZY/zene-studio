NEEDS-CONTEXT

Cluster new-p16-241: "A zero sample rate leaves the delay ring unusable" (plugins/Delay/StereoDelay.cpp, historical line 70).

Branch: `integration/new-p16-241-c6ee4fc7bc` (the worktree is on this branch, not on `bugs/qwen-new-p16-241`). Base: `c6ee4fc7bc`.

## Summary

The finding has three parts. Each has its own verdict:

| Part of the finding | Verdict | Where |
| --- | --- | --- |
| Zero or negative rate: `tick()` takes a modulo by zero, and a negative size reaches `new[]` | ALREADY-FIXED on this line by `64edb6ed29` (new-p10-398, clamp to one frame) | `plugins/Delay/StereoDelay.cpp` (`std::max(1, …)` in `setSampleRate`), tests `tests/src/core/StereoDelay.cpp:70-105` |
| Out-of-range rate: `maxTime * sampleRate` overflows an int in the constructor, and `setSampleRate` converts a float of 2^31 or more to int | REAL on this line, FIXED in this commit | `plugins/Delay/StereoDelay.cpp:42-44, 100-114`; tests `tests/src/core/StereoDelay.cpp:107-133` |
| Prescribed contract: "reject nonpositive rates before deleting or replacing the current buffer" | NEEDS-CONTEXT (owner decision). It contradicts the shipped clamp contract of `64edb6ed29` and that commit's four test slots. Not implemented. | see "The open decision" below |

The reported crash does not occur on this branch. The out-of-range part is fixed here. The reject-versus-clamp choice for zero and negative rates is left to the owner.

## Evidence (file:line on this branch)

- Clamp for zero and negative rates (shipped, `64edb6ed29`): `plugins/Delay/StereoDelay.cpp:114` in the current tree has `bufferSizeF >= 1.0f ? static_cast<int>(bufferSizeF) : 1`. The HEAD version has `std::max(1, static_cast<int>(sampleRate * m_maxTime))`. Either way a zero or negative product gives a one-frame ring, so `tick()` never divides by zero.
- Constructor overflow (real before this commit): HEAD `plugins/Delay/StereoDelay.cpp:39` is `m_maxLength = maxTime * sampleRate;` (signed int overflow, undefined behaviour). Now `:42-44` sizes the ring in 64 bits and keeps it only when it is in `[1, INT_MAX]`.
- Out-of-range refusal: `plugins/Delay/StereoDelay.cpp:100-103` refuses `bufferSizeF >= 2^31` before `delete[] m_buffer`, so a live delay keeps its last good ring.
- Pass-through with no ring: `plugins/Delay/StereoDelay.cpp:50-54` resets the length when `setSampleRate` refused, and `:75-78` returns from `tick()` when there is no ring. Neither adds allocation, locking or logging on the audio path.
- Behaviour for in-range rates is unchanged. For any product in `[1, 2^31)` the float value and the `static_cast<int>` result are the same as before (`:100`, `:114`). `<algorithm>` was dropped because its only use (`std::max`) is gone.
- Realistic inputs are far from the limit. A 20 s ring at 192 kHz is 3,840,000 frames, against the 2^31 limit of 2,147,483,648.

## The open decision (for the owner)

The integration brought in `5d396fba70` (the accepted patch from `bugs/qwen-new-p16-241`) with conflicts. Its contract refuses zero and negative rates and leaves the ring untouched. The shipped contract from `64edb6ed29` clamps them to a one-frame ring. These cannot both hold.

Evidence:
- `run-5-accepted-product-vs-HEAD-tests.log`: the `5d396fba70` product file against this branch's tests gives `Totals: 3 passed, 4 failed`. The four failures are `zeroMaxTimeClampsToOneFrameRing`, `zeroSampleRateClampsToOneFrameRing`, `negativeArgumentsClampToOneFrameRing` and `zeroSampleRateReconfigurationKeepsRingUsable`.
- `64edb6ed29`'s own commit message says those four slots "fail on the previous code and pass with this change".

Options:
1. Keep the clamp (shipped `64edb6ed29`). Then the finding's zero-rate part is closed as ALREADY-FIXED and nothing else changes.
2. Adopt refusal (the finding's wording). Then the four `64edb6ed29` slots must be replaced, which is a contract change and is not something a fix lane should do on its own.

I did not pick either option. My lean is option 2, because refusal keeps the audible tail on a live `setSampleRate(0)`, while the clamp replaces it with a one-frame delay. That lean needs the owner's sign-off because it reverses a shipped contract.

## Integration state I found and cleaned up

- `git status` at start: `UU` on `plugins/Delay/StereoDelay.cpp`, `tests/src/core/StereoDelay.cpp` and `tests/upstream-modifications.txt`, with `docs/qwen-fixes-2026-10/cluster-new-p16-241.md` staged. `integration-apply.log` shows the patch was applied "with conflicts" by a fallback.
- I saved the conflicted working copies to `logs/discarded-merge-*.txt`, then ran `git restore --source=HEAD --staged --worktree` on the three files. Nothing committed was touched. The accepted commits `541ab3285d`, `eda19ff6be` and `5d396fba70` remain on `bugs/qwen-new-p16-241`.

## Regression test

The new slots are `overflowingConstructorRateIsRefusedBeforeIntNarrowing` and `outOfRangeSampleRateKeepsTheCurrentRing` in `tests/src/core/StereoDelay.cpp`. They sit next to the shipped `64edb6ed29` slots, which are unchanged.

- RED on HEAD: `run-6-RED-head-new-tests.log`, `Totals: 7 passed, 2 failed`. The failures are `m_maxLength` 1 where 0 is expected (constructor), and `m_maxLength` 1 where 960000 is expected (live ring replaced).
- GREEN with the fix: `run-7-GREEN-fix.log`, `Totals: 9 passed, 0 failed`.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-241.r4/logs`.

| # | Command (run from the worktree unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `git restore --source=HEAD --staged --worktree -- plugins/Delay/StereoDelay.cpp tests/src/core/StereoDelay.cpp tests/upstream-modifications.txt` | 0 | - |
| 2 | `cmake --build build --target StereoDelay -j2` (no `CCACHE_DISABLE`) | 2 | `build-1-head-StereoDelay.log`: ccache could not write its temp file (permission denied) |
| 3 | `CCACHE_DISABLE=1 cmake --build build --target StereoDelay -j2` | 0 | `build-2-head-StereoDelay.log` |
| 4 | `cd build/tests && ctest -R '^StereoDelay$' --output-on-failure` | 0 | `test-1-head-StereoDelay.log` (1 test passed) |
| 5 | `./build/tests/StereoDelay` on HEAD | 0 | `run-2-head-StereoDelay-slots.log` (7 passed) |
| 6 | `git apply -R fix-p10-398.patch` (the `64edb6ed29` product diff), build, run | 4 | `run-3-RED-pre-p10-398.log`: 4 failed, `m_maxLength` 0 |
| 7 | `git apply fix-p10-398.patch`, build, run | 0 | `run-4-GREEN-restored.log` (7 passed) |
| 8 | `5d396fba70` product file over HEAD (`git show … > file`), build, run, then `git restore --worktree` | 4 | `run-5-accepted-product-vs-HEAD-tests.log`: 4 failed |
| 9 | Build with the new slots on HEAD product code, run | 2 | `run-6-RED-head-new-tests.log`: 2 failed (RED) |
| 10 | Apply the fix, build, run | 0 | `run-7-GREEN-fix.log`: 9 passed (GREEN) |
| 11 | `git diff -- plugins/Delay/StereoDelay.cpp > fix.patch` | 0 | `fix.patch` |
| 12 | `git apply -R fix.patch`, build, run | 2 | `run-8-4b-reversed-RED.log`: 2 failed |
| 13 | `git apply fix.patch`, build, run | 0 | `run-9-4b-reapplied-GREEN.log`: 9 passed |
| 14 | `cmake --build build --target StereoDelay partc_ref_delay DelayControlsTest DelayLfoAutomationTest MonoDelayTest -j2` (ccache disabled) | 0 | `build-10-family.log` |
| 15 | `cd build/tests && ctest -R 'StereoDelay\|DelayControlsTest\|DelayLfoAutomationTest\|MonoDelayTest\|partc_ref_delay'` | 0 | `test-2-family.log`: 4/4 passed |
| 16 | `python3 …/qwen/feature_check.py <worktree>` | 1 | `gate-1-feature-check.log`: `PermissionError` creating `<worktree>-feature-check`, which is outside the write-jail. The script did not run any gate. |
| 17 | The six non-ratchet gates run directly from the worktree: `tests/fork-sources-gate.sh`, `tests/all-sources-reproduce.sh`, `tests/no-upstream-regression-gate.sh`, `tests/scripted/check-namespace`, `tests/no-tautology-gate.sh`, `tests/doc-figures-gate.py` | 0, 0, 0, 0, 0, 0 | `gate-2` … `gate-7` |
| 18 | Ratchets run directly: `tests/complexity-gate.sh --check`, `tests/file-length-gate.sh --check` | 1, 1 | `gate-8-complexity.log`, `gate-9-file-length.log`. Neither log has a `REGRESSION` line that names `StereoDelay`, and neither names a file this commit changed. They are red on the train on purpose. |

Notes:
- `ctest -N -R Delay` lists no test named `DelayEffect`, and `partc_ref_delay` is a build target without a registered ctest. The family run above therefore covers the four registered Delay tests.
- The ccache failure in row 2 is a sandbox permission issue, not a code issue. Every build after it used `CCACHE_DISABLE=1`.

## Files committed

- `plugins/Delay/StereoDelay.cpp`
- `tests/src/core/StereoDelay.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p16-241.md`

## Residual risk

- A ring that fits an int but is too big for memory still throws `bad_alloc` from `new[]`. This is unchanged and is not part of this finding.
- For zero or negative rates, `DelayEffect` builds `StereoDelay` with the output sample rate. If that rate were ever zero at construction, the delay would be a one-frame line. I did not check whether the engine can report zero there (unverified).
- No new control commands. No undo or realtime change: the audio path adds one branch and no allocation, locking or logging.
