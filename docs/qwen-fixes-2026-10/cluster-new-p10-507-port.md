# cluster new-p10-507-port — large automation point gap

Verdict: **BLOCKED**. The finding is real and the bounded-sampling port is implemented, but the required GREEN test and reverse-apply proof could not be completed. The post-fix test build stayed queued behind the shared build-slot limit for more than 18 minutes and was stopped; the final required gates also fail on unrelated files already present on this branch. No gate baseline or limit was changed.

## Evidence

| File and lines | Evidence |
| --- | --- |
| `include/AutomationClip.h:177` | `valuesAfter` now takes a wide starting offset and requested sample count. |
| `src/core/AutomationClip.cpp:604,634,654,836,850,858` | Curve evaluation computes offsets and segment spans in `int64_t`, including Hermite spans across signed tick endpoints. The sampler validates the requested range and allocation size, then allocates only `count` floats. |
| `src/gui/clips/AutomationClipView.cpp:280,321,327` | Clip view intersects each segment with the visible tick range and skips invalid/null samples. |
| `src/gui/editors/AutomationEditor.cpp:1052,1336,1344,1452` | Editor sampling is bounded to visible ticks and off-screen x-coordinate arithmetic is wide and clamped. |
| `tests/src/tracks/AutomationTrackTest.cpp:184,218` | Regression caps array requests for a million-tick gap and checks a Hermite segment spanning `INT_MIN` to `INT_MAX`. |

The original-tree regression was RED: `testLargeGapViewportSamples` observed one rejected allocation above 4 KiB, and `testExtremeHermiteTickSpan` returned `-1.980704e+28` instead of `0.5`. The ctest run executed one CTest executable; QtTest reported 13 passed and 2 failed slots.

## Commands and outcomes

| Command | Exit | Result |
| --- | ---: | --- |
| `cmake --build build --target AutomationTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-507-port.r1/logs/build-1-red.log 2>&1; code=$?; echo EXIT=$code; exit $code` | 0 | Built the original-code regression target. |
| `ctest -R '^AutomationTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-507-port.r1/logs/test-1-red.log 2>&1; code=$?; echo EXIT=$code; exit $code` | 8 | RED, as above. |
| `git diff -- include/AutomationClip.h src/core/AutomationClip.cpp src/core/AutomationClipTransforms.cpp src/gui/clips/AutomationClipView.cpp src/gui/editors/AutomationEditor.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-507-port.r1/logs/fix.patch; code=$?; echo EXIT=$code; exit $code` | 0 | Saved the production-only patch for the required reverse-apply proof. |
| `cmake --build build --target zene AutomationTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-507-port.r1/logs/build-2-green.log 2>&1; code=$?; echo EXIT=$code; exit $code` | Not captured | Log shows `zene` built, then the test target failed to compile because `testValuesAfterPastEnd` still called the removed one-argument API. That assertion was adapted afterward; no success is claimed for this attempt. |
| `cmake --build build --target zene AutomationTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-507-port.r1/logs/build-3-green.log 2>&1; code=$?; echo EXIT=$code; exit $code` | 143 | Stopped after the build shim reported both shared build slots busy for over 18 minutes; the target compilation did not start. |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-507-port.r1/logs/gate-3-final-complexity.log 2>&1; code=$?; echo EXIT=$code; exit $code` | 1 | Existing branch-base regressions: six unrelated functions exceed CCN 10. The new automation functions do not appear among regressions. |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-507-port.r1/logs/gate-3-final-file-length.log 2>&1; code=$?; echo EXIT=$code; exit $code` | 1 | Existing branch-base regressions: 14 unbaselined files exceed 500 code lines. |

The six complexity regressions are `dotCentreAlong` in `tests/src/gui/XyPadPairTest.cpp`, `parseNotes` in `src/core/DawProjectReadTracks.cpp`, `adjust` in `src/core/ControlCommandsMixerTools.cpp`, `setStrip` in `src/core/ControlCommandsMixerStrip.cpp`, `rotateProjectRevision` in `src/core/ProjectRevisions.cpp`, and `importedOverrides` in `src/gui/ControlCommandsShortcuts.cpp`. The 14 overlength files are `src/core/SmfInterchangeReader.cpp`, `src/core/TakeLane.cpp`, `tests/src/core/CollabTransportTest.cpp`, `tests/src/core/ControlAutomationScriptTest.cpp`, `tests/src/core/ControlDeviceCatalogueTest.cpp`, `tests/src/core/ControlEditCommandsTest.cpp`, `tests/src/core/CustomAccessTest.cpp`, `tests/src/core/DeviceDspTest.cpp`, `tests/src/core/HardwareIoTest.cpp`, `tests/src/core/NotePlaybackTest.cpp`, `tests/src/core/RackMacrosTest.cpp`, `tests/src/core/RevisionTimelineTest.cpp`, `tests/src/core/VideoTrackTest.cpp`, and `tests/src/gui/ArrangerTimelineTest.cpp`.

The reversed-patch build/test proof and post-fix affected-family ctest run were not run because no build slot became available. An earlier post-fix build log shows `zene` compiled and linked, then the test target stopped at an existing one-argument `valuesAfter` assertion; that assertion was adapted to the compatibility helper, but the corrected test target was not rebuilt. The `fff grep` tool was not exposed in this session, so log findings were searched with `rg`.

## Files in this commit

- `include/AutomationClip.h`
- `src/core/AutomationClip.cpp`
- `src/core/AutomationClipTransforms.cpp`
- `src/gui/clips/AutomationClipView.cpp`
- `src/gui/editors/AutomationEditor.cpp`
- `tests/src/tracks/AutomationTrackTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-507-port.md`
