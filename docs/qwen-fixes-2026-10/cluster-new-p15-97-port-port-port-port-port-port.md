FIXED
# Cluster new-p15-97-port-port-port-port-port-port — Large audio periods violate `std::clamp`'s bounds

## Verdict and code evidence

**REAL.** Before the fix, `LinkAudioStream::receivePeriod()` passed `period * 2` as the lower bound and `m_rx.capacity() / 4` as the upper bound to `std::clamp` (`src/core/LinkAudioStream.cpp:253` in the final tree). The RX ring is constructed with `AudioRingFrames` (`src/core/LinkAudioStream.cpp:108`, `include/LinkAudioStream.h:77`), so the upper bound is 16384. A period of 9000 made the old lower bound 18000, violating the clamp precondition on the audio thread. `process()` reaches this code when receive is enabled (`src/core/LinkAudioStream.cpp:165-171`).

`jitterBounds()` now computes `maximum = capacity / 4` and `minimum = std::min(period, maximum / 2) * 2` (`src/core/LinkAudioStream.cpp:212-217`), and `receivePeriod()` clamps with those ordered bounds (`:253-254`). For every period through 8192 frames the minimum remains exactly `period * 2`; above that, it is capped at the ceiling. Capping before multiplication also prevents overflow for extreme period values.

The helper has one caller (`receivePeriod()`), and `receivePeriod()` has one caller (`process()`). `rg -n 'std::clamp|jitterBounds\(|receivePeriod\(|void LinkAudioStream::process|AudioRingFrames|JitterBounds' include/LinkAudioStream.h src/core/LinkAudioStream.cpp` found this as the only clamp and the only receive path. The helper performs fixed-width arithmetic only; no allocation, lock, wait, I/O, logging, or Qt call was added to the audio thread.

## Regression and registration

`LinkAudioStreamTest::largePeriodsKeepTheJitterTargetInsideItsSupportedRange()` (`tests/src/core/LinkAudioStreamTest.cpp:226`) checks ordering and the 16384 ceiling at period 9000, preserves the 512-frame minimum of 1024, and checks an extreme `SIZE_MAX`-adjacent period. Its deterministic receive-path section injects a 5 ms latency and exactly 17000 ring frames, then processes one 9000-frame period and verifies priming, audible output, and no underrun or trim. It uses no clock, sleep, thread, socket, or race.

The existing test source is already registered in `tests/CMakeLists.txt:1394` and `tests/fork-sources.txt:2421`; no new test file was needed. The changed inherited header and implementation are recorded in `tests/upstream-modifications.txt`.

## Build and test evidence

Commands below are copied from the runs. Build and test output is in the named files under the run log directory. Build commands ran from the worktree root. `ctest` commands without an explicit `cd` ran with working directory `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-97-port-port-port-port-port-port/build/tests`.

| Phase | Exact command | Result |
|---|---|---|
| RED build | `cmake --build build --target zene LinkAudioStreamTest -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/build-1-red.log 2>&1` | EXIT=0 |
| RED run, first attempt | `ctest -R '^LinkAudioStreamTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/test-1-red.log 2>&1` | EXIT=8; the new bound assertion failed; this run also reported the existing realtime stress assertion and a test-process segfault. |
| RED run, repeat | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/test-2-red-repeat.log 2>&1` | EXIT=8; only the new assertion failed (`19 passed, 1 failed`). This is the clean RED result. |
| GREEN build | `cmake --build build --target zene LinkAudioStreamTest ControlMidiReconnectSharedTest SessionTuningTest ControlLinkCommandsTest -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/build-3-green.log 2>&1` | EXIT=0 |
| Affected family | `ctest -R '^(LinkAudioStreamTest|ControlMidiReconnectSharedTest|SessionTuningTest|ControlLinkCommandsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/test-3-green.log 2>&1` | EXIT=0; 4/4 passed. |

The required 4b negative control retained the pure helper and test seam, and reversed only the minimum-bound calculation. Reversing the entire production diff would remove the helper the regression calls and turn the proof into a compile failure.

| Negative-control step | Exact command | Result |
|---|---|---|
| Capture product diff | `git diff -- include/LinkAudioStream.h src/core/LinkAudioStream.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/fix.patch` | EXIT=0; narrowed to the single behavioral hunk. |
| Check reverse patch | `git apply --check -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/fix.patch` | EXIT=0 |
| Reverse fix hunk | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/fix.patch` | EXIT=0 |
| Reverted build | `cmake --build build --target zene LinkAudioStreamTest -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/build-4-reverted.log 2>&1` | EXIT=0 |
| Reverted test | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/test-4-red-proven.log 2>&1` | EXIT=8; the new assertion failed, `19 passed, 1 failed`. |
| Restore fix hunk | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/fix.patch` | EXIT=0 |
| Restored build | `cmake --build build --target zene LinkAudioStreamTest -j8 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/build-5-restored.log 2>&1` | EXIT=0 |
| Restored test | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/test-5-green-proven.log 2>&1` | EXIT=0; 1/1 passed. |

The first feature gate found the train's stale `tests/all-sources.txt`: it listed 1148 `plugins/SfzPlayer/sfizz/**` vendor files that its own header recipe excludes. `tests/all-sources.txt` was regenerated from that recorded recipe; the 1148 removals are all in the excluded vendor tree, and no first-party source path was removed. This makes the scope gates reproduce without changing product behavior.

The regeneration command (EXIT=0) preserved the header and ran the recipe from `tests/all-sources.txt` verbatim:

```sh
set -o pipefail
{
  sed -n '1,129p' tests/all-sources.txt
  { git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)' ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } | LC_ALL=C sort -u
} > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/generated-all-sources.txt
result=$?
if [ "$result" -eq 0 ]; then
  cp /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/generated-all-sources.txt tests/all-sources.txt
  result=$?
fi
if [ -e /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/generated-all-sources.txt ]; then
  rm /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/generated-all-sources.txt
fi
printf 'EXIT=%s\n' "$result"
exit 0
```

| Gate | Exact command | Result |
|---|---|---|
| Initial feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-97-port-port-port-port-port-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/gate-1-feature-check.log 2>&1` | EXIT=1 before the manifest repair (`fork-sources` and `all-sources-reproduce` only). |
| Initial direct scope gates | `bash tests/fork-sources-gate.sh`; `bash tests/all-sources-reproduce.sh` | EXIT=1 for each before the manifest repair. |
| Final feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-97-port-port-port-port-port-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/gate-5-feature-check-post-report.log 2>&1` | EXIT=0; all 8 rows PASS (`gate-5-feature-check-post-report.log`). |
| Completion check after the fix commit | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-97-port-port-port-port-port-port new-p15-97-port-port-port-port-port-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-97-port-port-port-port-port-port.r1/logs/gate-6-submit-check-post-commit.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0` | EXIT=0; every mechanical item passed and the check printed `PASS - submit`; all five ASK items are answered below. |

## Collateral damage

No project field, saved data, setting, or control command changes. The adjustment affects only periods for which the old lower bound exceeded the ring's quarter-capacity ceiling. At or below 8192 frames the minimum is unchanged. The period itself is not capped; only the jitter target bound is. Thus a caller with a legitimate large period is not rejected or shortened by a new limit.

## Files committed

- `include/LinkAudioStream.h`
- `src/core/LinkAudioStream.cpp`
- `tests/src/core/LinkAudioStreamTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/all-sources.txt` (recipe-derived train-state repair described above)
- `docs/qwen-fixes-2026-10/cluster-new-p15-97-port-port-port-port-port-port.md`

## Self-check

- Regression fails with the fix reverted and passes restored: after reversing only the product bounds hunk, the build exited 0 and `ctest -R '^LinkAudioStreamTest$' --output-on-failure` exited 8 on `largePeriodsKeepTheJitterTargetInsideItsSupportedRange()`; after reapplying it, the build exited 0 and the same test exited 0 (1/1 passed). See `build-4-reverted.log`, `test-4-red-proven.log`, `build-5-restored.log`, and `test-5-green-proven.log`.
- Deterministic test: the bounds checks inject periods 9000, 512, and `SIZE_MAX / 2 + 1`; the receive-path check injects latency and ring contents directly through `LinkAudioStreamTestAccess`. The new test has no wall-clock timing, sleeps, threads, sockets, or races.
- Fix at cause and complete: `jitterBounds()` is called only by `receivePeriod()`, which is called only by `process()`. The `std::clamp` search above found no sibling clamp in these files. The four rebuilt dependents passed 4/4 tests.
- No collateral damage: projects and serialized data are untouched. For periods up to 8192, the computed minimum is exactly the old `period * 2`; the test pins the normal 512-frame case. The fix caps only the invalid target bound; it does not limit a caller's period.
- Verdict is REAL: the pre-fix code passed an invalid lower bound at `src/core/LinkAudioStream.cpp:253` for 9000-frame periods; the repeated RED run and the 4b reversed-hunk run both failed on the new ordered-bounds assertion.
