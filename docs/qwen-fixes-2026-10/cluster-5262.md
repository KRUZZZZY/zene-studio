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

The tests passed before any edit, so RED is not reproducible on this branch; under procedure step 2 this is ALREADY-FIXED. There is no product patch, so step 4b (revert/reapply) does not apply. I did not re-run the tests against the pre-fix source in this session; the earlier fix commit's message records its own red run (EXIT=8) but I did not verify that.

## Other findings
None.
