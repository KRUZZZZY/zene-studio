# Cluster new-p11-81-port

## Verdict: FIXED

Port of the accepted fix `ab4eb68ae` (cluster new-p11-81) onto the newest line
(`050/m0-train-5`, HEAD `ded355440`), written by hand - no cherry-pick, merge or rebase.

Half of `ab4eb68ae` is already on this line and needed no work: the four delay templates
already initialise their declared members (`include/Delay.h:71-75`, `:148-152`,
`:221-227`, `:304-308`, landed by new-p9-107 / new-p10-105). The bug itself was still
present, so the cursor half was re-applied.

Before, each template advanced one shared `int m_position` on every `update(in, ch)`
call, so a stereo frame walked the ring twice: the two channels read different delay
positions and answered at different times. After, every template keeps one cursor per
channel and `setMaxDelay` folds each of them into the new size.

## What changed

| File:line (after) | Change |
| --- | --- |
| `include/Delay.h:137`, `:211`, `:294`, `:371` | `int m_position` -> `std::array<int, CHANNELS> m_positions{}` in `CombFeedback`, `CombFeedfwd`, `CombFeedbackDualtap`, `AllpassDelay`. |
| `include/Delay.h:123`, `:197`, `:276`, `:279`, `:356` | `update` reads and advances `m_positions[ch]` instead of the shared cursor. |
| `include/Delay.h:96`, `:175`, `:251`, `:334` | `setMaxDelay` folds every channel cursor (`for (int& position : m_positions) position %= m_size;`). |
| `include/Delay.h:29` | `#include <array>`, which the header already relied on for `std::array`. |

The cursor is a fixed `std::array` member: no allocation, no mutex and no logging on the
audio path, and mono (`CHANNELS == 1`) behaviour is bit-identical to before.

Regression: `tests/src/core/DeviceDspTest.cpp:391` (`stereoDelayPrimitivesAdvanceOneCursorPerChannel`)
with the helpers at `:282`-`:340`. It drives an impulse pair (1 on the left, 10 on the right,
both in frame 0) through all four stereo primitives, requires each channel to answer the
same frames with ten times the amplitude, and requires the answer to land exactly at the
configured delay. It also checks that a shrinking `setMaxDelay` folds the cursors.

## Evidence

Every command below was run from the worktree root unless it says `build/tests`.
`logdir` is `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-81-port.r1/logs`.

| Check | Exact command | Result |
| --- | --- | --- |
| Gates before any edit | `bash tests/file-length-gate.sh --check` (`gate-1-filelength-pre.log`) | `EXIT=1` - 14 pre-existing REGRESSION keys, already failing on this base commit. |
| Gates before any edit | `bash tests/complexity-gate.sh --check` (`gate-2-complexity-pre.log`) | `EXIT=1` - 6 pre-existing REGRESSION keys, already failing on this base commit. |
| RED build | `cmake --build build --target DeviceDspTest` (`build-1.log`) | `EXIT=0` |
| RED test | `ctest -R '^DeviceDspTest$' --output-on-failure` from `build/tests` (`test-1-red.log`) | `EXIT=8`; the new slot failed, plus a pre-existing failure of `theFrequencyShifterMuteRouteDoesNotPassDryInput` because no plugin `.so` had been built in this worktree. |
| Pre-existing failure cleared | `cmake --build build --target frequencyshifter` (`build-2-fs-plugin.log`) | `EXIT=0` (builds `build/plugins/libfrequencyshifter.so`; no product change). |
| RED, isolated | `ctest -R '^DeviceDspTest$' --output-on-failure` from `build/tests` (`test-2-red-with-plugin.log`) | `EXIT=8`; `DeviceDspTest::stereoDelayPrimitivesAdvanceOneCursorPerChannel() 'alignedImpulse(comb, 3.0)' returned FALSE` - the only failure in the family. |
| GREEN build | `cmake --build build --target zene DeviceDspTest AudioResamplerRatioTest StereoDelay` (`build-3-green.log`) | `EXIT=0` |
| GREEN family | `ctest -R '^(DeviceDspTest\|AudioResamplerRatioTest\|StereoDelay)$' --output-on-failure` from `build/tests` (`test-3-green.log`) | `EXIT=0`; `100% tests passed out of 3`. |
| Test reshaped for the complexity gate | `cmake --build build --target DeviceDspTest` (`build-8-reshaped.log`); `ctest -R '^DeviceDspTest$' --output-on-failure` from `build/tests` (`test-8-reshaped-green.log`) | `EXIT=0`; `EXIT=0`, `100% tests passed out of 1`. New functions: CCN 6 / 4 / 3 / 4 / 1, all within the target of 10. |
| Removal proof | `git diff -- include/Delay.h > "$logdir/fix.patch"; git apply -R "$logdir/fix.patch"` | `EXIT=0` for both. |
| Removal proof build | `cmake --build build --target zene DeviceDspTest` (`build-12-final-proof-red.log`) | `EXIT=0` |
| Removal proof test | `ctest -R '^DeviceDspTest$' --output-on-failure` from `build/tests` (`test-12-final-proof-red.log`) | `EXIT=8`; `FAIL! ... 'channelsStayAligned(comb, 3.0)' returned FALSE. (StereoCombFeedback must keep one cursor per channel)` - the test does catch the bug. |
| Restore | `git apply "$logdir/fix.patch"` | `EXIT=0` |
| Restore build | `cmake --build build --target zene DeviceDspTest` (`build-13-final-proof-green.log`) | `EXIT=0` |
| Restore GREEN family | `ctest -R '^(DeviceDspTest\|AudioResamplerRatioTest\|StereoDelay)$' --output-on-failure` from `build/tests` (`test-13-final-proof-green.log`) | `EXIT=0`; `100% tests passed out of 3`. |
| GREEN before the proof, same tree | `cmake --build build --target zene DeviceDspTest AudioResamplerRatioTest StereoDelay` (`build-11-final-green.log`), then the same ctest (`test-11-final-green.log`) | `EXIT=0`; `EXIT=0`, `100% tests passed out of 3`. |
| Complexity gate after the fix | `bash tests/complexity-gate.sh --check` (`gate-7-complexity-final.log`) | `EXIT=1` - pre-existing base failure. The REGRESSION list is byte-identical to the pre-edit list: this change adds no new key (diff of the two logs is empty). |
| File length gate after the fix | `bash tests/file-length-gate.sh --check` (`gate-8-filelength-final.log`) | `EXIT=1` - pre-existing base failure. Same 14 keys as before the edit; the only difference is the size already printed for the already-flagged `tests/src/core/DeviceDspTest.cpp` (752 -> 812). No new file crossed the limit and no baseline was re-anchored. |

Affected family: 3 tests in `ctest -R '^(DeviceDspTest|AudioResamplerRatioTest|StereoDelay)$'`,
3/3 passed (`test-13-final-proof-green.log`).

### Note on the two gates

Both ratchets already fail on this branch before any edit of mine (`ded355440`): the
file-length gate reports 14 files over the 500-code-line limit that are absent from
`tests/file-length-baseline.tsv` (among them `tests/src/core/DeviceDspTest.cpp`, the file
the existing `Delay.h` regressions already live in), and the complexity gate reports 6
pre-existing over-target functions. Neither baseline was re-anchored and no gate limit was
changed; the evidence above shows the change adds no new gate key to either.

## Files committed

- `include/Delay.h`
- `tests/src/core/DeviceDspTest.cpp`
- `tests/upstream-modifications.txt` (inherited file, reason recorded)
- `tests/fork-sources.txt` (new report registered)
- `docs/qwen-fixes-2026-10/cluster-new-p11-81-port.md` (this report)