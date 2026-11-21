# Cluster new-p12-91

Verdict: **BLOCKED**

## Finding and fix

The finding is real. `GuidedTour::eventFilter()` in `src/gui/HelpCenter.cpp:344-355` used to reposition only for a resize of the parent window. The updated filter also reacts to target and ancestor move, resize, show, hide, layout and scroll events. `GuidedTour::place()` at `src/gui/HelpCenter.cpp:387-407` installs filters on the target chain and connects any ancestor scroll area's scrollbars, then recomputes the hole and bubble position. The declaration comment and private slot are in `include/HelpCenter.h:113-117`.

The regression `HelpAccessTest::aTourTracksTargetMovementAndScrollingWithoutResizingWindow()` in `tests/src/gui/HelpAccessTest.cpp:176-210` moves the target and scrolls its containing area while asserting the main window remains 800×600. Against the original code it failed at line 202: actual bubble position `(141, 30)`, expected `(161, 110)`. The test passed with the fix. Removing only the production patch reproduced the same failure; restoring it passed again.

The report remains **BLOCKED** because both required ratchet gates fail on unrelated regressions already present in the branch. The gate thresholds and baselines were not changed.

## Verification evidence

Build commands ran from the worktree root. Every ctest command ran from `build/tests`. The affected ctest family executed one test, `HelpAccessTest`, per run.

| Check | Exact command | Exit |
|---|---|---:|
| Original-code RED build | `cmake --build build --target zene HelpAccessTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r2/logs/build-1-red.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Original-code RED test | `ctest -R '^HelpAccessTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r2/logs/test-2-red.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Fixed GREEN build | `cmake --build build --target zene HelpAccessTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r2/logs/build-3-green.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Fixed GREEN test | `ctest -R '^HelpAccessTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r2/logs/test-4-green.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Save product patch | `git diff -- include/HelpCenter.h src/gui/HelpCenter.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r2/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Reverse product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r2/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild without fix | `cmake --build build --target zene HelpAccessTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r2/logs/build-5-proof-red.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Removal-proof RED test | `ctest -R '^HelpAccessTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r2/logs/test-6-proof-red.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r2/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild with fix restored | `cmake --build build --target zene HelpAccessTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r2/logs/build-7-proof-green.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Removal-proof GREEN test | `ctest -R '^HelpAccessTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r2/logs/test-8-proof-green.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r2/logs/gate-13-complexity-final.log 2>&1; rc=$?; echo EXIT=$rc` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r2/logs/gate-14-file-length-final.log 2>&1; rc=$?; echo EXIT=$rc` | 1 |

The complexity gate reports six unrelated new functions above CCN 10: `dotCentreAlong` (16), `readdetail::parseNotes` (13), `adjust` (12), `setStrip` (11), `rotateProjectRevision` (11), and `importedOverrides` (11). It does not report the tour functions. The file-length gate reports 14 unrelated new files over 500 code lines: `src/core/SmfInterchangeReader.cpp`, `src/core/TakeLane.cpp`, `tests/src/core/CollabTransportTest.cpp`, `ControlAutomationScriptTest.cpp`, `ControlDeviceCatalogueTest.cpp`, `ControlEditCommandsTest.cpp`, `CustomAccessTest.cpp`, `DeviceDspTest.cpp`, `HardwareIoTest.cpp`, `NotePlaybackTest.cpp`, `RackMacrosTest.cpp`, `RevisionTimelineTest.cpp`, `VideoTrackTest.cpp`, and `tests/src/gui/ArrangerTimelineTest.cpp`. The core test paths in the list are under `tests/src/core/`.

The session did not expose the requested `fff grep` tool or a shell `fff` executable. Build, test and gate logs were searched with `rg -n` for `error`, `FAILED`, `Assertion`, and the relevant summaries.

## Explicit files committed

- `include/HelpCenter.h`
- `src/gui/HelpCenter.cpp`
- `tests/src/gui/HelpAccessTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-91.md`
