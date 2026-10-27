FALSE-POSITIVE

# Cluster new-p14-263 — FALSE-POSITIVE

Verdict: **FALSE-POSITIVE**

Finding: "Sample-rate scaling can overflow before the range check" (`src/core/RingBuffer.cpp:94`).
The finding's premise is that `f_cnt_t` is a 32-bit unsigned type. On this branch it is 64-bit, and
the remainder product cannot overflow it. No product code was changed.

## Evidence (file:line)

- `include/LmmsTypes.h:42` `using sample_rate_t = std::uint32_t;`
- `include/LmmsTypes.h:43` `using f_cnt_t = std::uint64_t;` (the finding assumed 32-bit; it is 64-bit here)
- `src/core/RingBuffer.cpp:91-92` `wholeFrames = frames / oldRate; remainderFrames = frames % oldRate;`
  so `remainderFrames < oldRate <= UINT32_MAX`.
- `src/core/RingBuffer.cpp:94-97` guards the whole-frame product before it is computed:
  `if (newRate != 0 && wholeFrames > frameLimit / newRate) throw std::length_error`.
- `src/core/RingBuffer.cpp:100` `remainderNumerator = remainderFrames * newRate` is the product the finding names.
  Its largest value is `(UINT32_MAX - 1) * UINT32_MAX = 2^64 - 3*2^32 + 2 < 2^64 - 1`, so it fits `std::uint64_t`.
  The bound is checked at compile time by the probe below.
- `src/core/RingBuffer.cpp:103-106` checks the final sum against `frameLimit` before returning.
- `src/core/RingBuffer.cpp:385-407` `updateSamplerate()` calls `checkedRateScaledFrames` in a `try`,
  and returns early without touching the buffer on any throw.

The finding's example (1 s at 44.1 kHz rescaled to 192 kHz) gives `remainderFrames * newRate < 44100 * 192000 ≈ 8.5e9`.
That exceeds 2^32, so it would overflow a 32-bit `f_cnt_t`. It does not overflow the 64-bit type on this branch.

History: the checked rescale path was introduced by commit `9bab096b4` (cluster new-p10-334-port, verdict FIXED),
which is an ancestor of this branch (`git merge-base --is-ancestor 9bab096b4 HEAD` → ancestor).

Existing coverage of the rescale path: `tests/src/core/RecordRingBufferTest.cpp:184-205`
`SamplerateUpdate_PreservesScaledFrameCount` (44.1 kHz → 48 kHz, exact ceil-rounded frame count).

## Commands run (copied from my log folder; EXIT as printed)

Run folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-263.r1/logs`

| # | Command | EXIT | Log |
|---|---|---:|---|
| 1 | `cmake --build build --target zene RecordRingBufferTest -j4` | 2 | `build-1.log` — ccache could not create its temp file in the sandbox path (`ccache: error: failed to create temporary file … Permission denied`); not a code error |
| 2 | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^RecordRingBufferTest$' --output-on-failure` | 8 | `test-1.log` — `RecordRingBufferTest (Not Run)`, because the target did not build |
| 3 | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p14-263 cmake --build build --target RecordRingBufferTest -j4` | 0 | `build-2.log` (0 ccache errors) |
| 4 | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^RecordRingBufferTest$' --output-on-failure` | 0 | `test-2.log` — `1/1 Test #187: RecordRingBufferTest ... Passed`, `100% tests passed out of 1` |
| 5 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `gates-1.log` — `PermissionError` creating `<worktree>-feature-check` (outside the write-jail); no gate ran through this script |
| 6 | gate commands run directly in the worktree (see table below) | — | `gates-2.summary`, `gate-<name>-2.log` |
| 7 | `bash -c …` loop over the six simple gates and the two ratchets | 0 / 0 / 0 / 0 / 0 / 0 / 1 / 1 | `gates-2.summary` |
| 8 | probe: `c++ -std=c++20 -Wall -Wextra` on a `static_assert` of the remainder-product bound (`$TMPDIR/p14-263-probe/bound.cpp`) | compile 0, run 0 | the `static_assert` holds at compile time |

Gate results (run directly, because `feature_check.py` could not create its log directory):

| Gate | EXIT | Notes |
|---|---:|---|
| fork-sources | 0 | |
| all-sources-reproduce | 0 | |
| no-upstream-regression | 0 | |
| check-namespace | 0 | |
| no-tautology | 0 | |
| doc-figures | 0 | |
| complexity (ratchet) | 1 | 7 `REGRESSION:` lines, all in files this branch does not change (for example `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`). The branch changes only this report, so no item is "NEW in your files"; red on the train on purpose. |
| file-length (ratchet) | 1 | 22 `REGRESSION:` lines, all in files this branch does not change (for example `src/core/TakeLane.cpp`). Same reason. |

## Self-check

- Verdict and evidence: the verdict is supported by `include/LmmsTypes.h:42-43`, `src/core/RingBuffer.cpp:91-106`, and the
  static bound in command 8. Nothing in the product changed, so no regression test was written. A test that passes on
  the unchanged code is not a regression for a non-bug; the existing `SamplerateUpdate_PreservesScaledFrameCount` (command 4,
  EXIT 0) covers the rescale path.
- Gates: `feature_check.py` could not run in this write-jail (command 5, EXIT 1). The six simple gates exited 0. The two
  ratchets exited 1 and their `REGRESSION:` lines are all in files this branch does not change, so they cannot count against it.
  The owner should re-run `feature_check.py` from outside the jail to confirm the table.
- `submit_check.py`: see the result recorded below.
- Not checked: whether any other caller relies on `f_cnt_t` being 32-bit. A grep for `f_cnt_t` as 32-bit would only find
  the `uint32_t` return types in `include/InstrumentLayers.h:180-188`, which are unrelated to `RingBuffer`.
