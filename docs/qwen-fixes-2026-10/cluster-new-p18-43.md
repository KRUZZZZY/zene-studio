FIXED

new-p18-43: FIXED

`src/tracks/InstrumentTrackNotes.cpp:389` was REAL: the existing loader called `InstrumentOutputBuses::loadSettings()` for every load, and that method clears routes before reading its optional node. The test `InstrumentRoutingTest::presetWithoutOutputBusNodeKeepsRoutes` exercises the real `Track::loadPreset` path: a preset without `<outputbuses>` keeps the route, an explicit node replaces it, and a project load without the node still clears it.

The mode-aware `Track::loadTrackSpecificSettings` hook passes preset/project mode to InstrumentTrack. The default hook delegates to the existing one-argument method for other track types. InstrumentTrack skips bus loading only for a preset that lacks `<outputbuses>`; preview mode and project reset behavior remain intact.

| Evidence | Command | Exit |
| --- | --- | ---: |
| Original production code and regression target built | `cmake --build build --target InstrumentRoutingTest -j12` | 0 |
| RED: preset route became `-1`, expected mixer channel id `7` | `ctest -R '^InstrumentRoutingTest$' --output-on-failure` from `build/tests` | 8 |
| GREEN build of app and affected test | `cmake --build build --target zene InstrumentRoutingTest -j12` | 0 |
| GREEN: 12 QtTest cases passed; 1/1 CTest target passed | `ctest -R '^InstrumentRoutingTest$' -V` from `build/tests` | 0 |
| Step 4b: saved the product diff and reversed it | `git diff -- include/Track.h include/InstrumentTrack.h src/core/Track.cpp src/tracks/InstrumentTrack.cpp src/tracks/InstrumentTrackNotes.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-43.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-43.r1/logs/fix.patch` | 0 |
| Reverse proof: rebuilt original app and test | `cmake --build build --target zene InstrumentRoutingTest -j12` after reversing the production patch | 0 |
| Reverse proof RED: the same preset route assertion failed | `ctest -R '^InstrumentRoutingTest$' --output-on-failure` from `build/tests` | 8 |
| Reapplied the saved production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-43.r1/logs/fix.patch` | 0 |
| Reverse proof GREEN: rebuilt app and test | `cmake --build build --target zene InstrumentRoutingTest -j12` | 0 |
| Reverse proof GREEN: focused test passed | `ctest -R '^InstrumentRoutingTest$' --output-on-failure` from `build/tests` | 0 |
| Initial feature gate: the source manifest did not reproduce | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-43` | 1 |
| Final feature gate table: all rows PASS | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-43` | 0 |

The first gate run exposed the existing 1,148-entry `plugins/SfzPlayer/sfizz` mismatch in `tests/all-sources.txt`; it was regenerated from that file's published recipe. The next gate run passed `fork-sources` and `all-sources-reproduce` as well as every other row. The CTest family executed 1 registered target (12 QtTest cases).

Files in this fix commit: `include/Track.h`, `include/InstrumentTrack.h`, `src/core/Track.cpp`, `src/tracks/InstrumentTrack.cpp`, `src/tracks/InstrumentTrackNotes.cpp`, `tests/src/core/InstrumentRoutingTest.cpp`, `tests/all-sources.txt`, `tests/upstream-modifications.txt`, and this report.

new-p18-44: FIXED

`src/tracks/InstrumentTrackNotes.cpp:147` was REAL: ordinary multi-tick advancement was treated as a jump, so the held Program Change was resent. `InstrumentRoutingTest::controllerLaneDoesNotResendAcrossMultiTickAdvances` checks changed values, song seeks, clip-local rewinds and lane reconfiguration. `controllerLaneResendsAfterClipUnmute` protects the resume path: `InstrumentTrack::play` resets the sent-value sentinel when a previously muted clip resumes, so its held state is restored without misclassifying normal tick advances.

| Evidence | Command | Exit |
| --- | --- | ---: |
| Original implementation/test target built | `cmake --build build --target InstrumentRoutingTest -j12` | 0 |
| RED: second identical event observed after a multi-tick advance (actual 2, expected 1) | `ctest -R '^InstrumentRoutingTest$' --output-on-failure` from `build/tests` | 8 |
| RED: unmute preservation assertion before the resume reset | `ctest -R '^InstrumentRoutingTest$' --output-on-failure` from `build/tests` | 8 |
| GREEN build of the focused target | `cmake --build build --target InstrumentRoutingTest -j12` | 0 |
| GREEN: 14 QtTest cases passed; 1/1 CTest target passed | `ctest -R '^InstrumentRoutingTest$' -V` from `build/tests` | 0 |
| Step 4b: saved the production diff and reversed it | `git diff -- src/tracks/InstrumentTrackNotes.cpp src/tracks/InstrumentTrack.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-43.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-43.r1/logs/fix.patch` | 0 |
| Reverse proof: rebuilt original app and test | `cmake --build build --target zene InstrumentRoutingTest -j12` | 0 |
| Reverse proof RED: the same unchanged-value assertion failed | `ctest -R '^InstrumentRoutingTest$' --output-on-failure` from `build/tests` | 8 |
| Reapplied the saved production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-43.r1/logs/fix.patch` | 0 |
| Reverse proof GREEN: rebuilt app and test | `cmake --build build --target zene InstrumentRoutingTest -j12` | 0 |
| Reverse proof GREEN: focused test passed | `ctest -R '^InstrumentRoutingTest$' --output-on-failure` from `build/tests` | 0 |
| Feature gate table: all rows PASS | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-43` | 0 |

Files in this fix commit: `src/tracks/InstrumentTrackNotes.cpp`, `src/tracks/InstrumentTrack.cpp`, `tests/src/core/InstrumentRoutingTest.cpp`, `tests/upstream-modifications.txt`, and this report.

new-p14-123: FIXED

`src/tracks/InstrumentTrackNotes.cpp:128-137` was REAL: its `thread_local bool inside` suppressed every track's transform while any one track forwarded a transformed external event. `MidiInputTransformTest::nestedLayerAppliesItsOwnInputTransform` creates owner A and layer B with transposes +1 and +2, sends external key 60 through the real layer forwarding path, and verifies B starts key 63 rather than 61. The corresponding external note-off releases B's key 63.

The thread-local recursion guard is now a linked stack of active track contexts. It blocks re-entry for the same track while allowing nested layer tracks to apply their transforms. The context is stack-owned and uses no heap allocation, lock or logging.

| Evidence | Command | Exit |
| --- | --- | ---: |
| Original implementation/test target built | `cmake --build build --target MidiInputTransformTest -j12` | 0 |
| RED: layer transform skipped; key 63 handle was absent | `ctest -R '^MidiInputTransformTest$' --output-on-failure` from `build/tests` | 8 |
| GREEN build of app and affected test | `cmake --build build --target zene MidiInputTransformTest -j12` | 0 |
| GREEN: 17 QtTest cases passed; 1/1 CTest target passed | `ctest -R '^MidiInputTransformTest$' -V` from `build/tests` | 0 |
| Step 4b: saved the production diff and reversed it | `git diff -- src/tracks/InstrumentTrackNotes.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-43.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-43.r1/logs/fix.patch` | 0 |
| Reverse proof: rebuilt original app and test | `cmake --build build --target zene MidiInputTransformTest -j12` | 0 |
| Reverse proof RED: the same layer-key assertion failed | `ctest -R '^MidiInputTransformTest$' --output-on-failure` from `build/tests` | 8 |
| Reapplied the saved production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-43.r1/logs/fix.patch` | 0 |
| Reverse proof GREEN: rebuilt app and test | `cmake --build build --target zene MidiInputTransformTest -j12` | 0 |
| Reverse proof GREEN: focused test passed | `ctest -R '^MidiInputTransformTest$' --output-on-failure` from `build/tests` | 0 |
| Final feature gate table: all rows PASS | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-43` | 0 |

Files in this fix commit: `src/tracks/InstrumentTrackNotes.cpp`, `tests/src/core/MidiInputTransformTest.cpp`, and this report.

## Build audit

The broad build request `cmake --build build -j12` was rejected by the local build guard with exit 97 (`build-19.log`): full builds are assigned to the parent. The explicit dependency audit found changed-header dependents under `lmmsobjs`, `zene`, `zene_api`, `InstrumentRoutingTest`, and `MidiInputTransformTest`. I rebuilt those named targets with `cmake --build build --target zene zene_api InstrumentRoutingTest MidiInputTransformTest -j12` (exit 0, `build-20.log`). The combined affected-family run `ctest -R '^(InstrumentRoutingTest|MidiInputTransformTest)$' -V` from `build/tests` passed both targets (exit 0, `test-20.log`): 14 + 17 QtTest cases. The final `feature_check.py` command shown in the gate rows passed with exit 0 (`gate-5.log`).

## Self-check

- **Reverted test fails; restored test passes:** For `new-p18-43`, the step 4b reverse command recorded above exited 0; the reversed build `cmake --build build --target zene InstrumentRoutingTest -j12` exited 0; the same `ctest -R '^InstrumentRoutingTest$' --output-on-failure` from `build/tests` failed with exit 8. Reapplying the patch exited 0, the build exited 0, and the same test passed with exit 0. For `new-p18-44`, the reverse command exited 0, reversed build exited 0, and `InstrumentRoutingTest` failed with exit 8 on the repeated-value assertion; restore, build, and the same test passed with exit 0. For `new-p14-123`, the reverse command exited 0, reversed build exited 0, and `MidiInputTransformTest` failed with exit 8 because the layer key-63 voice was absent; restore, build, and the same test passed with exit 0. The focused target runs each executed one CTest target; the final combined run executed 2/2 targets and 31 QtTest cases.
- **Determinism:** `controllerLaneDoesNotResendAcrossMultiTickAdvances` injects explicit ticks (0, 12, 24, 200, 220, 225, 205) and explicit song playback ticks/seeks; `controllerLaneResendsAfterClipUnmute` drives mute state directly. The preset regression builds and loads explicit QDom preset/project elements. `nestedLayerAppliesItsOwnInputTransform` sends explicit external note-on/off events through the layer chain and synchronously renders one period for note-off cleanup. These tests use no sleeps, wall-clock timing, or races.
- **Cause and caller/sibling audit:** `Track::loadTrack` dispatches preset mode at `src/core/Track.cpp:821` and project mode at `:895`; the default mode-aware virtual at `:997-1000` delegates to the old one-argument hook, and `InstrumentTrack`'s legacy overload at `src/tracks/InstrumentTrack.cpp:1186-1188` forwards `false`. InstrumentTrack's mode-aware loader at `:1193` passes mode to `loadZeneInstrumentSettings`; the only guarded route read is `src/tracks/InstrumentTrackNotes.cpp:428-430`. The controller sender is called from the clip playback loop at `src/tracks/InstrumentTrack.cpp:1022`; the loop resets the held-value sentinel on clip resume at `:1018-1020`, and the sender still emits changed values and on actual chase/rewind. Input transform entry remains in `InstrumentTrack::processInEvent` at `src/tracks/InstrumentTrack.cpp:489`; the stack guard suppresses re-entry only for the same track, leaving nested tracks eligible. Tests cover these paths and their relevant siblings.
- **Collateral and compatibility:** The route regression verifies three cases: a preset missing `<outputbuses>` preserves the current route, an explicit preset node replaces it, and a project load missing the node clears it as before. Controller tests verify held-value suppression, changed values, song seek, clip rewind, lane reconfiguration, and unmute resend; no controller serialization changed. The transform test verifies A's +1 and nested B's +2 both apply and that note-off releases the transformed B voice. No project format, command, or undo implementation changed. All three verdicts are FIXED, supported by the named code paths and red/green tests above; the ALREADY-FIXED/FALSE-POSITIVE-only evidence question does not apply.
