ALREADY-FIXED

# Cluster new-p10-349: tail trim past the source end

## Verdict

The defect is already fixed on branch `bugs/qwen-new-p10-349`. No source, include, plugin, or test change is needed. This report is the only change.

## Code evidence

- `src/core/SampleFrameEdits.cpp:233` clamps the requested frame to the source length before anything else uses it:
  `const f_cnt_t at = std::min(frame, static_cast<f_cnt_t>(clip->sample().sampleSize()));`
- `src/core/SampleFrameEdits.cpp:240` computes the tail tick from the clamped frame:
  `const tick_t tail = tailTick(*clip, at);`
- `tests/src/core/ArrangingCatchUpTest.cpp:236` is the regression slot `aTailTrimPastTheSourceEndsWhereTheSourceEnds()`. It trims a clip to the source end, then trims a second clip to a frame far past the end, and requires the second clip's `sourceOut` and `endPosition().getTicks()` to equal the first clip's.

## Cause

Before the fix, `trimTailToFrame` passed the raw requested frame into `tailTick`. A frame past the source end produced a tail tick past the end of the source, so the clip's end moved beyond where the source stops. The clamp at line 233 runs before `tailTick` at line 240, so the tick is always computed from a frame inside the source.

In-range frames are unchanged. For any `frame <= sampleSize()`, `min(frame, sampleSize())` returns `frame`, so behaviour for valid edits is identical to the pre-fix code.

## History

- `49e9bb16c0` (`fix(arrange): bound tail trim to the source before its tail tick`) introduced the clamp and the regression slot. It is an ancestor of `HEAD` (`git merge-base --is-ancestor 49e9bb16c0 HEAD` exit 0). Its subject names cluster `new-p12-214`, so this fix is shared with that cluster.
- `git blame` on `src/core/SampleFrameEdits.cpp:233-235` attributes those lines to `49e9bb16c0`. Lines 230-232 come from the earlier commit `009f591b27`. Blame log: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-349.r1/logs/blame-01.log`.
- The ancestry log `ancestry-01.log` is empty (0 lines). Ancestry was checked by the command above, not by that log.
- The diff of the fix is in `logs/fix.patch`.

## Test evidence (recorded logs)

The exact p2 command lines are not recorded in the logs, so only the results and log paths are given here. All logs are under `/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-349.r1/logs/`.

- RED (fix reverted, `logs/test-07-red.log`): `ArrangingCatchUpTest` ctest entry 377 failed. The failing assertion is `aTailTrimPastTheSourceEndsWhereTheSourceEnds()`: `beyond->endPosition().getTicks()` was 324 and the expected value `endAtSource` was 224. The ctest wrapper's exit code is not in the log.
- GREEN build (`logs/build-07-green.log`): `p2-build-green: exit 0`, 36 lines of output.
- GREEN test (`logs/test-07-green.log`): `p2-test-green: exit 0`, 100% tests passed, 1 of 1.
- GREEN tail slot (`logs/test-07-green-tail.log`): `aTailTrimPastTheSourceEndsWhereTheSourceEnds()` PASS. Totals: 3 passed, 0 failed.

## Canonical test command and the default-environment failure

- Default environment (`logs/test-01.log`): `ctest -R '^ArrangingCatchUpTest$' --output-on-failure` reports entry 377 as failed. The only failing check is `newDocumentsCarryTheirTypesTemplate()`. It fails because `mastering.mpt` cannot be written to `/home/kruzzzzy/.cache/ArrangingCatchUpTest/document-templates/`, which is not writable here. The tail slot `aTailTrimPastTheSourceEndsWhereTheSourceEnds()` passes in that run.
- So the default-environment failure comes from the unwritable cache path, not from the clip-trim code.
- Canonical command: run ctest with `XDG_CACHE_HOME` pointed at a writable directory under the run's log folder. `logs/test-03-xdg-override.log` and `logs/test-04-ctest-xdg-override.log` show the XDG override run passing (`100% tests passed out of 1`, entry 377).

## Self-check

The packet names the `submit_check` ASK items to answer, but the list of those items is not in the packet, and I could not find it in the worker prompt or session log. I cannot answer each ASK by number. The answers below cover the topics the packet names and the evidence for each.

- **Why `checkTrim`'s raw comparison is harmless.** `src/core/ControlCommandsClipFrames.cpp:91-100` compares the raw `frames.tail <= frames.head` before clamping. `trimHeadToFrame` and `trimTailToFrame` clamp with `std::min` against the same source length. `min` is monotone, so a raw `tail <= head` implies the clamped `tail <= head`. The clamped tail then falls under the existing `at <= sourceIn` refusal in `trimTailToFrame`. The raw check can only refuse earlier, with a more specific message, for inputs the clamped path already refuses. This reasoning is from reading the code. No test isolates this case.
- **Does the fix change valid edits?** No. For `frame <= sampleSize()` the clamp returns `frame`, so valid edits are unchanged.
- **Is the regression covered?** Yes. RED fails at 324 vs 224, GREEN passes (see Test evidence).
- **Is the default-environment ctest failure related?** No. It is the unwritable `mastering.mpt` template path under `~/.cache`. The tail slot passes there.
