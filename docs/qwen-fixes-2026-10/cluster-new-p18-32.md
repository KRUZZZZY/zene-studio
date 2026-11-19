FALSE-POSITIVE

## Decision and evidence

The reported Mute-route behavior is the intended contract, not a defect. In `plugins/FrequencyShifter/FrequencyShifterEffect.cpp` the Mute/Pass input is fed into the delay (`:227-228`, `delayInL/R = outL/R + (parallelFB ? inL/R : 0.f)`) and into the shifter (`:155-156`, `fxInL/R` includes `inL/R` when not in Send routing). The dry input is removed from the output only for Mute: the dry term at `:254` is `routeMute ? 0.f : 1.f - mix`, so `inOut` receives `mix * outL` and no dry signal.

This matches the documented Mute contract at `plugins/FrequencyShifter/FrequencyShifterControlDialog.cpp:227`: Mute is "Like Pass routing, except the input signal isn't sent to the output, so all you hear is the output from the delay line."

The upstream reference has the same shifter-input and delay-write structure at `tests/reference/FrequencyShifter/FrequencyShifterEffect.cpp:154-155` and `:248-249`; its dry term at `:253` is `1.f - mix`. The fork's only delta is the `routeMute` term, i.e. the fork adds the Mute behavior described by the dialog rather than changing existing routing.

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
| `QT_QPA_PLATFORM=offscreen ./DeviceDspTest theFrequencyShifterMuteWithFeedbackEmitsDelayedWet` (from `build/tests`; temporary probe slot) | not recorded in `probe-run-2.log` | The log has no shell command or EXIT line; its QtTest summary is `Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 574ms`, with the probe values above. |
| `QT_QPA_PLATFORM=offscreen ./DeviceDspTest theFrequencyShifterMuteRouteDoesNotPassDryInput` (from `build/tests`) | 0 | `002-probe-4.log`; existing regression passed: `Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 601ms`. |
| `QT_QPA_PLATFORM=offscreen ./DeviceDspTest theFrequencyShifterMuteWithFeedbackEmitsDelayedWet` (from `build/tests`, re-run in current tree) | 1 | `001-probe-3.log`; `FAIL!: Function not found` because the temporary probe slot is absent from the current binary, not a code failure. |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p18-32` | 0 | `gates-1.log`; `RESULT: PASS`, all gates PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length). Complexity and file-length report no new item in the changed files. |

`test-2.log` also records CTest registration 381 `DeviceDspTest` as `Passed` with `100% tests passed out of 1`.

## Self-check

To be completed by p4.

## Other findings

- The Feedback=0 plus Mute case is silent by design: Mute removes the dry input (`FrequencyShifterEffect.cpp:254`) and there is no feedback tail to fill the delay, so no output is expected after the initial delay content drains (probe peak 0 after 1024 frames). This is not a regression.
- A UI hint explaining the silent Feedback=0 + Mute combination could be considered separately; it is outside this verification packet.
