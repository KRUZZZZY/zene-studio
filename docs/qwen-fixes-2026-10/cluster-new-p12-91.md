# Cluster new-p12-91

Verdict: **BLOCKED**

## Finding and evidence

The finding is real. Before the attempted fix, `src/gui/HelpCenter.cpp:326-329` refreshed the tour only for a resize event on the parent window. `GuidedTour::place()` derives the hole from the target's current mapped position, so moving the target without resizing the window left the stored hole and bubble stale.

A temporary regression in `tests/src/gui/HelpAccessTest.cpp` moved the tour target and expected the bubble x coordinate to change to 311. The original code failed with actual 121 and expected 311. A candidate fix observed the target and its ancestors and connected scroll-area scrollbar changes to `place()`. It passed the test; reversing only the production patch made the same assertion fail again. The candidate patch is preserved outside the worktree at `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r1/logs/fix.patch`.

The candidate cannot be committed under the required pre-commit gates. The final complexity check still reports six unrelated functions above CCN 10; it does not report `GuidedTour::eventFilter`. The file-length check reports 14 unrelated new files above 500 code lines. These files are outside this bug's scope, and the gate baselines and limits were left unchanged. The attempted product and regression changes were removed from the worktree; this report is the only file committed.

## Verification evidence

All ctest commands below ran from `build/tests`; build and gate commands ran from the worktree root. The affected family executed one ctest (`HelpAccessTest`) per run.

| Check | Exact command | Exit |
|---|---|---:|
| Original-code RED build | `cmake --build build --target HelpAccessTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r1/logs/build-1-red.log' 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Original-code RED test | `ctest -R '^HelpAccessTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r1/logs/test-2-red.log' 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Final candidate build | `cmake --build build --target zene HelpAccessTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r1/logs/build-13-final.log' 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Candidate GREEN test | `ctest -R '^HelpAccessTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r1/logs/test-14-final-green.log' 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Save production patch | `git diff -- include/HelpCenter.h src/gui/HelpCenter.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r1/logs/fix.patch'; rc=$?; echo EXIT=$rc` | 0 |
| Remove patch for proof | `git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r1/logs/fix.patch'; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild without fix | `cmake --build build --target zene HelpAccessTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r1/logs/build-15-proof-red-final.log' 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Removal-proof RED test | `ctest -R '^HelpAccessTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r1/logs/test-16-proof-red-final.log' 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Restore patch | `git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r1/logs/fix.patch'; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild with fix | `cmake --build build --target zene HelpAccessTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r1/logs/build-17-proof-green-final.log' 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Removal-proof GREEN test | `ctest -R '^HelpAccessTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r1/logs/test-18-proof-green-final.log' 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r1/logs/gate-11-complexity-final.log' 2>&1; rc=$?; echo EXIT=$rc` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-91.r1/logs/gate-12-file-length-final.log' 2>&1; rc=$?; echo EXIT=$rc` | 1 |

The complexity gate's six new regressions are `dotCentreAlong` (CCN 16), `readdetail::parseNotes` (13), `adjust` (12), `setStrip` (11), `rotateProjectRevision` (11), and `importedOverrides` (11). The file-length gate's 14 new-file regressions are `src/core/SmfInterchangeReader.cpp` (504 lines), `src/core/TakeLane.cpp` (520), `tests/src/core/CollabTransportTest.cpp` (541), `ControlAutomationScriptTest.cpp` (521), `ControlDeviceCatalogueTest.cpp` (567), `ControlEditCommandsTest.cpp` (511), `CustomAccessTest.cpp` (556), `DeviceDspTest.cpp` (752), `HardwareIoTest.cpp` (556), `NotePlaybackTest.cpp` (624), `RackMacrosTest.cpp` (535), `RevisionTimelineTest.cpp` (600), `VideoTrackTest.cpp` (589), and `tests/src/gui/ArrangerTimelineTest.cpp` (689). The test files without a directory prefix in that list are under `tests/src/core/`.

## Commit

Explicit files committed: `docs/qwen-fixes-2026-10/cluster-new-p12-91.md` only. The product fix was not committed because both mandatory gates exit 1 for unrelated worktree violations. No gate threshold or baseline was changed.
