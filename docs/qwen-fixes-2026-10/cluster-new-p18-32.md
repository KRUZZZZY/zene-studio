FALSE-POSITIVE

## Decision and evidence

The reported Mute-route behavior is the intended contract, not a defect. In `plugins/FrequencyShifter/FrequencyShifterEffect.cpp` the Mute/Pass dry input enters the delay (`:227-228`, `delayInL/R = outL/R + (parallelFB ? inL/R : 0.f)`), is read back after the delay time, and reaches the shifter only through the feedback tap: `:155-156` is `fxIn = parallelFB ? dly*feedback : in + dly*feedback`, so in Mute and Pass (`parallelFB` true) the shifter reads only the delay tap scaled by Feedback. In Send (mode 0) the dry input goes straight into the shifter. The dry input is removed from the output only for Mute: the dry term at `:254` is `routeMute ? 0.f : 1.f - mix`, so `inOut` receives `mix * outL` and no dry signal.

This matches the documented Mute contract at `plugins/FrequencyShifter/FrequencyShifterControlDialog.cpp:227`: Mute is "Like Pass routing, except the input signal isn't sent to the output, so all you hear is the output from the delay line."

The upstream reference has the same shifter input at `tests/reference/FrequencyShifter/FrequencyShifterEffect.cpp:154-155`, the same delay write at `:226-227`, and its Add output at `:246-249`; its dry term at `:253` is `const float dry = 1.f - mix;`. The fork's only delta is the `routeMute` term, i.e. the fork adds the Mute behavior described by the dialog rather than changing existing routing.

The existing regression at `tests/src/core/DeviceDspTestParts1.h:349` (`theFrequencyShifterMuteRouteDoesNotPassDryInput`) sets Route Mode 2 and Mix 0.5, feeds a constant 0.8/-0.4 frame, and asserts both output channels are below `1.0e-6`. It passes.

## Probe results

The probe run in `probe-run-2.log` (QtTest totals: 3 passed, 0 failed, 0 skipped, 574ms) recorded:

- `mute feedback frame 0: 0 0`
- `mute feedback delayed wet: 1.20195 after 1024 frames (delay 441)`
- `mute without feedback peak: 0 after 1024 frames`
- `add route first block peak: 0.8 0.8`

So Mute with feedback produces delayed wet output, Mute without feedback is silent (see Other findings), and Add routing still passes the dry input. The temporary probe slot (`theFrequencyShifterMuteWithFeedbackEmitsDelayedWet`) is not present in the current tree or the rebuilt test binary; its evidence is the log above. The permanent regression that remains in the tree was re-run green (below).

## Commands and results

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p18-32.r1/logs`.

| Command | Exit | Evidence |
|---|---:|---|
| `cmake --build build --target DeviceDspTest` (probe build) | 0 | `build-2.log`; ends `[100%] Built target DeviceDspTest`. The log has no EXIT line: the 0 is the shell echo from the session. |
| `ctest --test-dir build/tests -R DeviceDspTest` | 0 | `test-2.log`; `Start 381: DeviceDspTest`, `100% tests passed out of 1`, `Total Test time (real) = 1.53 sec`. The log has no EXIT line: the 0 is the shell echo from the session. |
| `cmake --build build --target DeviceDspTest` (rebuild after removing the temporary probe slot) | 0 | `build-3.log`; ends `[100%] Built target DeviceDspTest`. The log has no EXIT line: the 0 is the shell echo from the session. |
| direct probe run: `QT_QPA_PLATFORM=offscreen ./DeviceDspTest theFrequencyShifterMuteWithFeedbackEmitsDelayedWet` (from `build/tests`; temporary probe slot) | not recorded in `probe-run-2.log` | The log carries no shell command or EXIT line; its QtTest summary is `Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 574ms`, with the probe values above. |
| `QT_QPA_PLATFORM=offscreen ./DeviceDspTest theFrequencyShifterMuteRouteDoesNotPassDryInput` (from `build/tests`) | 0 | `002-probe-4.log`; existing regression passed: `Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 601ms`. |
| `QT_QPA_PLATFORM=offscreen ./DeviceDspTest theFrequencyShifterMuteWithFeedbackEmitsDelayedWet` (from `build/tests`, re-run in current tree) | 1 | `001-probe-3.log`; `FAIL!: Function not found` because the temporary probe slot is absent from the current binary, not a code failure. |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p18-32` | 0 | `gates-1.log`; `RESULT: PASS`, all gates PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length). Complexity and file-length report no new item in the changed files. |

`test-2.log` also records CTest registration 381 `DeviceDspTest` as `Passed` with `100% tests passed out of 1`.

## Self-check

- **Regression FAILS reverted / PASSES with the change (exit codes).** This packet makes no product change; the regression predates it and guards the existing fork term. The term is `const float dry = routeMute ? 0.f : 1.f - mix;` at `plugins/FrequencyShifter/FrequencyShifterEffect.cpp:254`, which the upstream reference lacks (`tests/reference/FrequencyShifter/FrequencyShifterEffect.cpp:253` is `const float dry = 1.f - mix;`). `tests/src/core/DeviceDspTestParts1.h:349-358` sets Route Mode 2 (Mute) and Mix 0.5, feeds a constant `0.8/-0.4` frame, and asserts both outputs `< 1.0e-6`. Reverting only the `routeMute` term to the upstream line would output `0.5*0.8 = 0.4` and `0.5*-0.4 = -0.2`, failing both asserts; that reversion was not run because this packet forbids product-code changes, so the fail-side evidence is analytical at the lines cited. The pass side was run: `002-probe-4.log` records `Totals: 3 passed, 0 failed`, exit 0, for `theFrequencyShifterMuteRouteDoesNotPassDryInput` (row above).
- **Deterministic, no wall-clock timing.** The test body (`tests/src/core/DeviceDspTestParts1.h:349-358`) uses only fixed injected values: the seam is the injected frame lambda `[](long) { return SampleFrame(0.8f, -0.4f); }` fed through `runBlocks(chainOf(1), ..., 1, &at)` with a fixed block count and `at` counter, and fixed settings `Route Mode 2.0`, `Mix 0.5`. There is no sleep, `QTest::qWait`, timer, thread or clock read; the `601ms` in `002-probe-4.log` is QtTest's wall-clock report, not an input.
- **Fix is at the cause and complete; callers and siblings checked.** No fix was applied: the verdict is FALSE-POSITIVE, so there is no code delta whose callers could drift. The only changed path on the branch is this report (`git log --oneline -3`: `e9827300de`, `238017d447`; `git diff --stat` empty before p4). The Mute term is a single expression read only by the output write at `FrequencyShifterEffect.cpp:255-256`; the dialog contract and the upstream comparison are in the Decision section above, so no caller or sibling is left inconsistent by this packet.
- **No collateral damage.** The branch changes only `docs/qwen-fixes-2026-10/cluster-new-p18-32.md`; no product code, test, project format, preset, or command code changed, so old projects and saved data load exactly as before. No limit or validation check was added.
- **FALSE-POSITIVE proof.** The intended Mute contract is documented at `plugins/FrequencyShifter/FrequencyShifterControlDialog.cpp:227` ("Like Pass routing, except the input signal isn't sent to the output"), and the code matches it: Mute/Pass feed the shifter only from the feedback-scaled delay tap (`FrequencyShifterEffect.cpp:155-156`), the dry input is written only into the delay (`:227-228`), and the dry output term is zeroed only for Mute (`:254`). The upstream reference is identical apart from the fork's Mute term (`tests/reference/FrequencyShifter/FrequencyShifterEffect.cpp:154-155`, `:226-227`, `:253`).

## Other findings

- The Feedback=0 plus Mute case is silent by design. The shifter input is the delay tap times Feedback (`fxInL/R = parallelFB ? dly*feedback : ...` at `FrequencyShifterEffect.cpp:155-156`), so at Feedback=0 the shifter input is 0, the shifter output is 0, and the dry term is dropped, which gives silence. This is the upstream design: the reference at `tests/reference/FrequencyShifter/FrequencyShifterEffect.cpp:154-155` and `:226-227` is identical, and its output stage at `:253-255` uses `dry = 1.f - mix` with no mute term. Feedback defaults to 0 (`plugins/FrequencyShifter/FrequencyShifterControls.cpp:39`), so Mute is silent at defaults. Not a regression (probe peak 0 after 1024 frames).
- A UI hint explaining the silent Feedback=0 + Mute combination could be considered separately; it is outside this verification packet.
