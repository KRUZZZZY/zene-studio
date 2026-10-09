# Integration Bar 3 blocker fixes

Branch `bugs/m0-fix-integration-bar3`, based on train 2 tip `c405233f8`.

| Cluster | Verdict | Fix commit | Regression test and red / green proof | Gate exits |
| --- | --- | --- | --- | --- |
| 3982 — landing content | FIXED | `cfa3dccab` | `ControlSessionRecordLandTest` plus `ControlSuccessPaths`: reversed landing implementation failed with ctest exit **8**; restored implementation passed all 3 matching tests, exit **0**. Added assertions that landed MIDI clips contain the slot pattern's notes and landed sample clips own the audio buffer. | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |
| render — audible diff after landing | FIXED | `cfa3dccab` | `ControlSuccessPaths`: with landing reversed, the land assertion failed, `project.audible_diff`'s child render exited 1, and self-comparison failed; ctest exit **8**. With landing restored, the land, audible-diff, and self-comparison checks passed; matching ctest exit **0**. | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |
| 3508 — take-lane writer ownership | FIXED | `c2fc2d695` | `WriteRefusalGateTest::oneWriterPerPersistedValue`: direct `SampleClip::setLaneIndex` failed the gate (ctest exit **8**); routing through `control::assignTakeLane` passed (exit **0**). | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |
| 13.8 — rebuilt chain identity | FIXED | `42b939247` | `DeviceMovesUndoTest::thePointerOverloadFindsARebuiltChain`: the original raw-pointer assertion failed in the supplied train-2 full-suite log (ctest exit **8**, assertion at line 166). Reversing the test edit locally passed once (exit **0**), showing the allocator-dependent failure is intermittent; the `QPointer` identity check then passed **50 consecutive ctest runs** (each exit **0**). | complexity 0; file-length 0; fork-sources 0; no-upstream-regression 0 |

## Evidence

- Landing reverse / green logs: `/tmp/claude-1000/-home-kruzzzzy-Documents-AI-KOS-PROJECT-projects-zene-studio-program/719627a5-c809-46ec-b4a4-231af5d313ec/scratchpad/luna-integration-bar3/landing-final-red.log` and `landing-final-green-recheck.log`.
- Lane-owner reverse / green logs: `lane-owner-red.log` and `lane-owner-green-recheck.log` in the same scratch directory.
- Chain-identity stress summary: `device-50-run-summary.log` in the same scratch directory. The original failing assertion is in the supplied train-2 log at `/tmp/claude-1000/-home-kruzzzzy-Documents-AI-KOS-PROJECT-projects-zene-studio-program/719627a5-c809-46ec-b4a4-231af5d313ec/scratchpad/m0/train2bar3/ctest.log:542`.
