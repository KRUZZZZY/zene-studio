# Cluster new-p14-113: hardware output reads past the initialized frame range

Verdict: **FIXED**

Latent: no production caller reaches a frame outside the period today. The device feed (`src/core/AudioEngine.cpp:2154`) passes frames below `m_framesPerPeriod`, `renderNextPeriod()` returns a span of `framesPerPeriod()` frames (`src/core/AudioEngine.cpp:1238`), and the bus is initialised with that same count (`src/core/AudioEngine.cpp:537`). `monoSample()` has no production caller at all. The defect is in the bus's own contract: a direct call with `frame >= frames()` read the next channel's plane, or past the storage on the last channel.

## Evidence

- `src/core/HardwareOutputBus.cpp:91` (`sample()`) and `:101` (`monoSample()`) indexed the planes with no frame bound. `sample(0, 256, …)` after `init(256)` read `data[256]`, which is channel 1's first sample; on channel 127 the same index runs past the vector.
- Fix: both methods now return silence for `frame < 0 || frame >= m_frames` (`src/core/HardwareOutputBus.cpp:91`, `:101`). The bound matches the storage: `m_frames` is what `init()` allocated per plane (`src/core/HardwareOutputBus.cpp:32-37`).
- Siblings checked: `add()` and `addPlane()` already clamp `count` to `m_frames` (`src/core/HardwareOutputBus.cpp:57`, `:76`); `swap()` clears `used * m_frames` (`:48`); the channel bounds of `sample()` (`channel >= 0 && channel < side.used`) were already present. No other reader of the planes exists (`rg` over `src/` and `include/`).
- Header contract updated to match: `include/HardwareOutputBus.h:85-90`.
- Regression: `tests/src/core/HardwareIoTest.cpp:199` (`outOfRangeFrameReadsAsSilence`). It is in the `HardwareIoTest` family, which already tests this bus. It was first written in `SurroundTest.cpp`; it was moved because `SurroundTest.cpp` crossed the 500-line file-length limit (see the gate note below).

## Verification

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-113.r1/logs`. Each build/test was run with stdout and stderr redirected to its listed log, and its exit code is listed.

Two environment workarounds were needed; neither changes a test, gate or limit:

- ccache's configured temp directory (`/home/kruzzzzy/.cache/ccache-bunny/ccache.conf`) is outside the write-jail, so builds failed with `Permission denied` (`build-1.log`, EXIT=2). Every later build used `CCACHE_TEMPDIR=$TMPDIR cmake --build build …`.
- `feature_check.py` writes its logs to `<worktree>/../<name>-feature-check`, outside the write-jail (`feature-check-1.log`, EXIT=1). The gates were run from a scratch copy in `$TMPDIR` (`fcgate.OGJH/feature_check_logdir.py`) that differs from the original only in the two path lines: `ROOT` (kept pointing at the real qwen folder, so the same gate baselines are read) and the log directory (moved into the run folder). The checks, limits and baselines are unchanged.

| Stage | Exact command | Working directory | Exit | Log / result |
|---|---|---|---:|---|
| First SurroundTest build (ccache permission) | `cmake --build build --target SurroundTest -j4` | worktree root | 2 | `build-1.log` (ccache temp dir, not a code error) |
| Build with new test, before fix | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target SurroundTest -j4` | worktree root | 0 | `build-2.log` |
| **RED in SurroundTest, before fix** | `ctest -R '^SurroundTest$' --output-on-failure` | `build/tests` | 8 | `test-red-1.log`: `FAIL! : …outOfRangeFrameReadsAsSilence() 'outputs.sample(0, 256, master) == 0.f' returned FALSE`, `SurroundTest.cpp(252)`; 15 passed, 1 failed |
| **GREEN in SurroundTest, with fix** | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target SurroundTest -j4`; `ctest -R '^SurroundTest$' --output-on-failure` | worktree root; `build/tests` | 0; 0 | `build-green-1.log`; `test-green-1.log` |
| 4b: save product diff | `git diff -- src/core/HardwareOutputBus.cpp include/HardwareOutputBus.h > …/logs/fix.patch` | worktree root | 0 | `fix.patch` |
| 4b: revert fix | `git apply -R …/logs/fix.patch` | worktree root | 0 | — |
| 4b: rebuild without fix | `cmake --build build --target SurroundTest -j4` (with ccache override) | worktree root | 0 | `build-prove-red.log` |
| 4b: **RED without fix** | `ctest -R '^SurroundTest$' --output-on-failure` | `build/tests` | 8 | `test-prove-red.log`: `FAIL!` at `SurroundTest.cpp(252)` |
| 4b: reapply fix | `git apply …/logs/fix.patch` | worktree root | 0 | — |
| 4b: rebuild with fix | `cmake --build build --target SurroundTest -j4` (with ccache override) | worktree root | 0 | `build-prove-green.log` |
| 4b: GREEN with fix | `ctest -R '^SurroundTest$' --output-on-failure` | `build/tests` | 0 | `test-prove-green.log` |
| Test moved to `HardwareIoTest.cpp` (gate fix); GREEN with fix | `cmake --build build --target HardwareIoTest -j4`; `ctest -R '^HardwareIoTest$' --output-on-failure` | worktree root; `build/tests` | 0; 0 | `build-hwio-green.log`; `test-hwio-green.log` |
| **RED in HardwareIoTest, fix reverted** | `git apply -R …/logs/fix.patch`; rebuild; `ctest -R '^HardwareIoTest$' --output-on-failure` | `build/tests` | 0 (revert), 0 (build), **8** (test) | `build-hwio-red.log`; `test-hwio-red.log`: `FAIL!` at `HardwareIoTest.cpp(214)`; 37 passed, 1 failed |
| GREEN in HardwareIoTest, fix reapplied | `git apply …/logs/fix.patch`; rebuild; `ctest -R '^HardwareIoTest$' --output-on-failure` | `build/tests` | 0, 0, 0 | `build-hwio-green2.log`; `test-hwio-green2.log`; `HardwareIoTest` verbose totals: 38 passed, 0 failed (`test-final2-hwio-verbose.log`) |
| Baseline for the unrelated `ControlRenderSurround` failure (fix .cpp reverted only) | `git apply -R …/logs/fix-cpp-only.patch`; `cmake --build build --target zene -j4`; `ctest -R '^ControlRenderSurround$' --output-on-failure` | worktree root; `build/tests` | 0, 0, 8 | `build-baseline.log`; `test-baseline-controlrendersurround.log` (same two CLI-text items as with the fix); fix reapplied, exit 0 |
| Final build of the affected targets | `cmake --build build --target zene SurroundTest HardwareIoTest SurroundExportTest SurroundPannerTest -j4` | worktree root | 0 | `build-final.log` |
| Affected family, final | `ctest -R 'Surround\|HardwareIo' --output-on-failure` | `build/tests` | 8 | `test-final2-family.log`: 5 of 6 pass; the one failure is `ControlRenderSurround` (see below) |
| Gates (first attempt, log dir outside jail) | `python3 …/feature_check.py <worktree>` | worktree root | 1 | `feature-check-1.log` (PermissionError on the log directory) |
| Gates (scratch copy, first run) | `python3 -I …/fcgate.OGJH/feature_check_logdir.py <worktree>` | worktree root | 1 | `feature-check-2.log`: all gates pass except `file-length`: `NEW: new file over 500 lines: tests/src/core/SurroundTest.cpp (515)`. The test was then moved to `HardwareIoTest.cpp`. |
| **Gates (final)** | `python3 -I …/fcgate.OGJH/feature_check_logdir.py <worktree>` | worktree root | **0** | `feature-check-3.log`: `RESULT: PASS` (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

Test counts: `HardwareIoTest` 38 of 38 pass with the fix (`test-final2-hwio-verbose.log`). The `Surround|HardwareIo` family runs 6 tests: `HardwareIoTest`, `SurroundTest`, `SurroundExportTest`, `SurroundPannerTest`, `ControlRenderSurround`, `ControlHardwareIo`. Five pass; `ControlRenderSurround` fails.

### The `ControlRenderSurround` failure is pre-existing

The two items it reports are CLI refusal-text checks (`--channels takes`, `--channels surround is for`). The CLI prints nothing for those refusals in this environment, so the check's captured text is empty. The source strings are present (`src/core/main.cpp:1116`, `:1281`); the refusal goes through `usageError()`, which writes with `qCritical()` (`src/core/main.cpp:331-336`), so the output depends on Qt message routing, which this change does not touch. It fails in the same way with the fix reverted (`test-baseline-controlrendersurround.log`, EXIT=8), so it is not caused by this change and was not hidden or edited.

## Self-check

Completion check run: `python3 …/submit_check.py <worktree> new-p14-113` → `submit-check-1.log`, EXIT=1.

| Item | Result |
|---|---|
| commits on the branch | PASS (`6cfdc5f57`) |
| working tree clean | PASS |
| report present | PASS |
| tests added or changed | PASS (`tests/src/core/HardwareIoTest.cpp`) |
| **gates (feature_check.py)** | **FAIL, environmental.** `submit_check.py` runs `feature_check.py` unmodified, which must create `<worktree>/../<name>-feature-check`, outside the write-jail: `PermissionError`. This is the same blocker as `feature-check-1.log`. The gate logic itself passed from the scratch copy with only the log path changed: `feature-check-3.log`, EXIT=0, `RESULT: PASS`. Needs the check to run outside the jail, or a log directory allowed in it. |

Answers to the `?` items:

1. **The regression fails with the change reverted and passes with it.** RED: `test-hwio-red.log`, EXIT=8, `FAIL! … sample(0, 256, master) == 0.f` at `HardwareIoTest.cpp(214)`. GREEN: `test-hwio-green2.log`, EXIT=0; `test-final2-hwio-verbose.log` shows 38 of 38 passing. The same cycle held in `SurroundTest` before the move (`test-prove-red.log` EXIT=8, `test-prove-green.log` EXIT=0).
2. **Deterministic.** No clock, sleep, thread or engine period. The test builds a local `HardwareOutputBus`, initialises it to 256 frames, writes constant planes (0.5 on channel 0, 0.25 on channel 1), sets the master off the outputs, swaps, and reads at frames 255 and 256.
3. **Fix at the cause; callers and siblings checked.** Frame bound added to both read methods (`src/core/HardwareOutputBus.cpp:91`, `:101`), matching the storage `init()` allocates (`:32-37`). Callers: the device feed (`src/core/AudioEngine.cpp:2154`) reads frames below `m_framesPerPeriod`, the bus's own size (`:537`), so the in-range path is unchanged. `monoSample` has no production caller. Siblings `add()` and `addPlane()` already clamp to `m_frames` (`:57`, `:76`). No other reader of the planes exists.
4. **No collateral damage.** The bound only changes results for frames outside `[0, frames())`, which no current caller produces. In-range reads are asserted unchanged (`sample(0, 255)` and `monoSample(255)` both return 0.5 in the test). `HardwareIoTest` (38), `SurroundExportTest`, `SurroundPannerTest` and `ControlHardwareIo` pass. The one red in the family, `ControlRenderSurround`, fails identically with the fix reverted (`test-baseline-controlrendersurround.log`, EXIT=8), so it is pre-existing and was not edited. No limit, saved format or control command was added; the realtime path gained two integer comparisons and nothing else.
