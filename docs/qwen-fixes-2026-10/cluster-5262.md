ALREADY-FIXED

5262: ALREADY-FIXED

Finding: `RackZoneVoice::render` stopped at `index + 1 >= size`, dropping the final source frame (a one-frame sample was silent).

## Evidence (code)
- `src/core/RackZoneInputs.cpp:49-60`: the loop now ends only when `index >= size` (line 50). The last frame is rendered with itself as the interpolation partner (`next = index + 1 < size ? index + 1 : index`, line 54). The voice ends once `position >= size` (line 60). This is the fix the finding asks for.
- History: commits `6b0f58b0d5` ("a zone voice ended before writing the sample's last frame", Luna cluster 5262) and `9b02a1992c` (fractional-step follow-up) are on this branch and touch exactly this loop (`git log -L47,61:src/core/RackZoneInputs.cpp`).
- Regression tests already in `tests/src/core/RackZoneRoutingTest.cpp:260-315`: `aVoiceRendersTheLastSourceFrame` (one-frame sample sounds; three-frame sample's frame 2 is output), `aFractionalStepHoldsTheLastSourceFrame`, `aFractionalStepSoundsEveryFinalFrameVisit`.
- No audio-thread change: no product file was edited in this session.

## Commands
| Step | Command | Result |
| --- | --- | --- |
| Build | `cmake --build build --target zene RackZoneRoutingTest -j4 > logs/build-1.log 2>&1` | EXIT=0 |
| Family test (from build/tests) | `ctest -R '^RackZoneRoutingTest$' --output-on-failure > logs/test-1.log 2>&1` | EXIT=0, 1/1 passed |
| The three named tests directly | `./RackZoneRoutingTest aVoiceRendersTheLastSourceFrame aFractionalStepHoldsTheLastSourceFrame aFractionalStepSoundsEveryFinalFrameVisit > logs/test-2.log 2>&1` | EXIT=0, each PASS (5 passed, 0 failed incl. init/cleanup) |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/feature-1.log 2>&1` | EXIT=0, all 8 rows PASS |

The tests passed before any edit, so RED is not reproducible on this branch; under procedure step 2 this is ALREADY-FIXED. There is no product patch, so step 4b (revert/reapply) does not apply. Extra check (not a product change): I temporarily put the buggy condition back on line 50 (`index + 1 >= size`), rebuilt (`logs/build-2-buggy.log`, EXIT=0) and ran the three tests: `./RackZoneRoutingTest <3 names> > logs/test-3-buggy.log` gave EXIT=3, all three FAIL ("Compared floats are not the same"). I then ran `git checkout -- src/core/RackZoneInputs.cpp`, rebuilt (EXIT=0) and ran the whole binary: `logs/test-4-restored.log` EXIT=0, 12 passed, 0 failed. `git status` was clean afterwards.

## Self-check
- Test fails with the fix reverted, passes with it: yes. The revert was the one-line buggy condition above, not a `git apply -R` of a patch, because there is no product diff this session. Buggy: EXIT=3 (3 FAIL). Restored: EXIT=0 (12 passed).
- Deterministic: yes. The tests drive `RackZoneVoice::render` directly with fixed in-memory `SampleBuffer`s and fixed steps (1.0, 0.5, 0.25) and compare exact expected floats. There is no clock, sleep, thread or device.
- Cause and completeness: the only place that reads the sample tail is `RackZoneVoice::render` (`src/core/RackZoneInputs.cpp:42-63`). Its end test (line 50), interpolation partner (line 54) and end-of-sample rule (line 60) are consistent. The other voice code (`restart` line 34, `voiceFor` line 109 onward) does not touch the end condition.
- No collateral damage: no product, save-format or limit change this session, so old projects load as before.
- ALREADY-FIXED evidence: `src/core/RackZoneInputs.cpp:50` and `:54`, plus commits `6b0f58b0d5` and `9b02a1992c`.
- Sibling commands (`app.*`, `arrangement.*`): not affected. I changed no command files. The submit check lists them because the branch carries unrelated merged changes. My only commit adds `docs/qwen-fixes-2026-10/cluster-5262.md`.
- The submit check's "tests added or changed" row lists `tests/control-*.py` files. They come from the merged train history, not from me.

## Other findings
None.
