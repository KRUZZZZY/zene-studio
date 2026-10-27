ALREADY-FIXED

## Evidence and decision

The finding is about `ExportNoiseShaper::quantise16` indexing `m_error` with a caller-supplied channel, which the finding says is undefined behaviour when the shaper is built for one channel and asked for channel 1. That defect is **not present on this branch**: the fix is already in history as commit `a33cdf90c` ("fix(export): noise shaper indexed past its error-history buffer", Luna cluster 5048). `git merge-base --is-ancestor a33cdf90c HEAD` exits 0, so it is in `bugs/qwen-5048` at `639e1d23e`.

Current code, `src/core/ExportNoiseShaper.cpp`:

- `:46-51` `quantise16` checks `channel < 0 || channel >= m_error.size()` and returns `0` (silence) before any index, so an out-of-range channel never reads or writes `m_error`. The write-back at `:61` is only reached for an in-range channel.
- `:65-78` `quantiseFrames` calls `quantise16(in[frame][chnl], chnl)` at `:72`. Its `channels` argument is not checked against the history size, but the guard in `quantise16` covers it: any `chnl >= m_error.size()` gets silence. The finding's example (`ExportNoiseShaper(1)` then `quantiseFrames(..., 2, ...)`) therefore writes silence to channel 1 and shapes channel 0 as before.
- The constructor sizes the history with `std::max(channels, 1)` at `:36`, so a zero or negative count still has one slot.

The regression is `ExportNoiseShaperTest::aChannelOutsideTheErrorHistoryIsSilence` in `tests/src/core/ExportNoiseShaperTest.cpp:191`. It drives a mono shaper with channels `{-1, 1, 2, 99}` and then `quantiseFrames(..., 2, ...)`, and asserts silence for every out-of-range channel while the in-range channel 0 is still written. It has no timing, sleeps or races.

Production callers are safe in the way the finding needs. `src/core/audio/AudioFileWave.cpp:106` constructs the shaper with the file's channel count, `:211` calls `quantiseFrames` with the same count, and `:281` calls `quantise16(samples[i], i % channels())`, which stays inside the history.

### Residual sibling, not part of this cluster (latent, unreachable from production)

`quantiseFrames` reads `in[frame][chnl]` at `:72`. `SampleFrame` is a fixed two-sample array (`include/SampleFrame.h:189`, `std::array<sample_t, DEFAULT_CHANNELS>`, `DEFAULT_CHANNELS = 2` in `include/lmms_constants.h:38`). A caller that passed `channels > 2` would read past the frame. Production never does this: `src/core/ProjectRenderer.cpp:317-318` sends multichannel exports to `writeSurround` and only calls `writeBuffer` (and so `quantiseFrames`) when `m_channels <= DEFAULT_CHANNELS`. Surround output uses `writeInterleaved` (`AudioFileWave.cpp:262`), which calls the per-sample `quantise16` and stays in range. I did not change this path: fixing it means deciding what a >2-channel `SampleFrame` contract is, which is a design question, and it is not what the finding reports. Flagging it for a follow-up cluster.

## Evidence table

Every line below was run in this worktree. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5048.r1/logs/`.

| Step | Command (run from `build/tests` where it is a test) | Result | EXIT |
| --- | --- | --- | --- |
| Build attempt 1 | `cmake --build build --target ExportNoiseShaperTest -j4` (`build-1-green.log`) | ccache could not create a temp file under `~/.local/share/zene-bunny-fix/dsh/...` (sandbox permission); test not compiled | 2 |
| Test attempt 1 | `ctest -R '^ExportNoiseShaperTest$' --output-on-failure` (`test-1-green.log`) | `Not Run`, 0 tests executed | 8 |
| Build 2 (ccache temp dir redirected, see note) | `CCACHE_TEMPDIR=$HOME/.cache/zene-tmp/ccache-5048 cmake --build build --target ExportNoiseShaperTest -j4` (`build-2-green.log`) | built | 0 |
| GREEN on HEAD, direct | `./ExportNoiseShaperTest` (`direct-2-green.log`) | `Totals: 9 passed, 0 failed` | 0 |
| GREEN on HEAD, ctest | `ctest -R '^ExportNoiseShaperTest$' --output-on-failure` (`test-2-green.log`) | `1/1 Test #104: ExportNoiseShaperTest ... Passed` | 0 |
| RED step 1: revert the fix | `git diff a33cdf90c^ a33cdf90c -- src/core/ExportNoiseShaper.cpp > fix.patch` (15 lines), then `git apply -R fix.patch` | working tree reverted to the pre-fix code | diff 0, apply -R 0 |
| RED build | `CCACHE_TEMPDIR=... cmake --build build --target zene ExportNoiseShaperTest -j4` (`build-3-red.log`) | rebuilt with the reverted source | 0 |
| RED direct | `QT_QPA_PLATFORM=offscreen ./ExportNoiseShaperTest` (`direct-3-red.log`) | `FAIL! : ExportNoiseShaperTest::aChannelOutsideTheErrorHistoryIsSilence() 'value == 0' returned FALSE. (channel -1 outside a 1-channel history quantised to 16383, not silence)`, then `double free or corruption (out)` and `Received signal 6 (SIGABRT)` | no clean exit (see note) |
| RED ctest | `ctest -R '^ExportNoiseShaperTest$' --timeout 60 --output-on-failure` (`test-4-red.log`) | `Timeout 60.08 sec`, `0% tests passed, 1 tests failed out of 1` | 8 |
| GREEN restore | `git apply fix.patch` (forward), `git diff --quiet HEAD -- src/core/ExportNoiseShaper.cpp` (source matches HEAD) | tree back to HEAD, `git status` clean | 0 |
| GREEN build | `CCACHE_TEMPDIR=... cmake --build build --target zene ExportNoiseShaperTest -j4` (`build-4-green-restored.log`) | rebuilt with the fix | 0 |
| GREEN direct, restored | `QT_QPA_PLATFORM=offscreen timeout 60 ./ExportNoiseShaperTest` (`direct-5-green-restored.log`) | `Totals: 9 passed, 0 failed, 0 skipped, 0 blacklisted` | 0 |
| GREEN ctest, restored | `ctest -R '^ExportNoiseShaperTest$' --output-on-failure` (`test-5-green-restored.log`) | `1/1 ... Passed`, `100% tests passed, 0 tests failed out of 1` | 0 |

Affected family: `ExportNoiseShaperTest` is the only ctest entry in the family (1 ctest test executed, 9 QTest cases passed). The historical `Red:` record in commit `a33cdf90c` (abort with `double free or corruption (out)`) matches the RED run here.

### Note on the RED exit

With the fix reverted, the test prints the assertion and then aborts with SIGABRT. The process then hangs in the crash handler: ctest times it out at 60 s (exit 8), and the bounded direct run needed a manual kill (exit 137 from my own SIGKILL after the SIGTERM from `timeout` was ignored). So the RED result is a clean assertion failure plus abort, not a clean non-zero exit. Both fail; neither passes.

### Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` **could not run from this lane**: it writes its logs to `<worktree parent>/<worktree name>-feature-check`, which is `projects/zene-studio-program/zene-qwen-5048-feature-check`, outside the write-jail (`PermissionError`, `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5048.r1/logs/feature-check-1.log`). I did not modify the script. I ran each gate command the script runs, in the worktree, with its own log:

| Gate (command from feature_check.py) | EXIT | Note |
| --- | --- | --- |
| fork-sources (`bash tests/fork-sources-gate.sh`) | 0 | |
| all-sources-reproduce (`bash tests/all-sources-reproduce.sh`) | 0 | |
| no-upstream-regression (`bash tests/no-upstream-regression-gate.sh`) | 0 | |
| check-namespace (`tests/scripted/check-namespace`) | 0 | |
| no-tautology (`bash tests/no-tautology-gate.sh`) | 0 | |
| doc-figures (`python3 tests/doc-figures-gate.py`) | 0 | |
| complexity (`bash tests/complexity-gate.sh --check`) | 1 | 7 `REGRESSION:` items, all in untouched files (`src/wasm/WasmSandbox.cpp`, `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsSessionRecordLand.cpp`, …); none in `ExportNoiseShaper*` |
| file-length (`bash tests/file-length-gate.sh --check`) | 1 | 22 `REGRESSION:` items, all in untouched files; none in `ExportNoiseShaper*` |

Why the two red gates do not block this report: `feature_check.py` marks a complexity or file-length item as FAIL only when it sits in a file changed on this branch. `git diff --name-only $(git merge-base HEAD 050/m0-train-17)..HEAD` is empty here (the branch is at `050/m0-train-17`'s tip), and this change touches only `docs/qwen-fixes-2026-10/cluster-5048.md`. So by the script's own rule these would show `no new item in your files`. That reasoning is mine, not the script's output; the script itself was not run.

No product file is changed by this cluster. The only commit is this report.

## Self-check

Pending: `submit_check.py` is run after this commit and its result is appended below.
