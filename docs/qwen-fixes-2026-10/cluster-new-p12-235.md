# Cluster new-p12-235

Verdict: **FIXED**

`sessionaudio::load()` now tracks the engine output sample rate the held clips were cached at. When the rate changes, the held map is drained into the retired vector (so the audio thread can finish with the old buffers) and the clips are re-resampled from their source files at the new rate. `retire()` resets the cached rate for the next project.

## File evidence

- `src/core/SessionAudioCells.cpp:151-156` — `Clips` struct gained `int cachedRate = 0`.
- `src/core/SessionAudioCells.cpp:181-194` — `load()` compares `cachedRate` with the current engine rate; on mismatch, moves held clips to retired and clears held before looking up or loading.
- `src/core/SessionAudioCells.cpp:268` — `retire()` resets `cachedRate = 0`.
- `tests/src/core/SessionAudioCellPlaybackTest.cpp:345-367` — `cachedClipsRebuildWhenEngineRateChanges()` writes a clip at the default rate, prepares it, changes the engine rate via `setSampleRateForTesting`, prepares again, and asserts the clip's sample rate matches the new engine rate.

## Evidence

The affected ctest family is `SessionAudioCellPlaybackTest`: one registered CTest test (13 cases including init/cleanup) was run per invocation.

| Evidence | Command (working directory) | Exit |
| --- | --- | ---: |
| RED on original code (`test-1.log`) | `ctest -R '^SessionAudioCellPlaybackTest$' --output-on-failure` (`build/tests`) | 8 |
| GREEN with fix (`test-2.log`) | `ctest -R '^SessionAudioCellPlaybackTest$' --output-on-failure` (`build/tests`) | 0 |
| RED after reversing `fix.patch` (`test-3.log`) | `ctest -R '^SessionAudioCellPlaybackTest$' --output-on-failure` (`build/tests`) | 8 |
| GREEN after reapplying `fix.patch` (`test-4.log`) | `ctest -R '^SessionAudioCellPlaybackTest$' --output-on-failure` (`build/tests`) | 0 |
| Complexity gate (`gate-ccn.log`) | `bash tests/complexity-gate.sh --check` (worktree root) | 1 (pre-existing) |
| File length gate (`gate-len.log`) | `bash tests/file-length-gate.sh --check` (worktree root) | 1 (pre-existing) |

Both gates report pre-existing regressions from other clusters; neither `SessionAudioCells.cpp` nor `SessionAudioCellPlaybackTest.cpp` appears in any regression list. The new `load()` CCN is 4 (well under 10) and the test file remains under 500 lines.

Each build used `cmake --build build --target SessionAudioCellPlaybackTest` (or `zene` for the final build) from the worktree root and exited 0. Logs are `build-1.log` through `build-5.log`.

The fix-removal command was `git diff -- src/core/SessionAudioCells.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-235.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-235.r1/logs/fix.patch` (exit 0). The restoration command was `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-235.r1/logs/fix.patch` (exit 0).

The RED test log shows: `FAIL!: Compared values are not the same — Actual (static_cast<int>(p2.clip->sampleRate())): 44100, Expected (newRate): 48000`. The GREEN test logs show: `Totals: 13 passed, 0 failed`.

## Files committed

- `src/core/SessionAudioCells.cpp`
- `tests/src/core/SessionAudioCellPlaybackTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-235.md`
