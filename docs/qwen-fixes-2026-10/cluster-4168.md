Verdict: FIXED
4168: FIXED
new-p10-171: FIXED

Evidence: `src/gui/shell/InspectorClipFields.cpp:61-63` returns the complete `ControlResult`; the Inspector's clip and note handlers use `ReadoutField::bindTextResult` (`src/gui/shell/InspectorClipFields.cpp:229-241,277-284`). `ReadoutField::commitTyped()` displays `errorMessage` and keeps the existing generic message when no reason is supplied (`src/gui/shell/TransportReadouts.cpp:221-251`). The regression at `tests/src/gui/DevicePanelTest.cpp:445-537` stubs the registered `note.transpose` handler with a fixed refusal and checks the visible tooltip. The existing numeric Inspector test covers a successful typed note edit. The new binding is UI-thread-only; it does not touch audio processing, allocation constraints, or undo behavior.

RED/GREEN proof: reversed only the production patch, rebuilt `zene` and all direct `TransportReadouts.h` test consumers, and reran the same two-test ctest family. Without the fix, the Inspector test failed because it showed `Key: "D4" was not accepted`; with the patch restored, both tests passed (2/2).

| Phase | Command | Exit |
|---|---|---:|
| Reverse product patch | `git diff -- include/TransportReadouts.h src/gui/shell/TransportReadouts.cpp src/gui/shell/InspectorClipFields.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4168.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4168.r1/logs/fix.patch` | 0 |
| Build without fix | `cmake --build build --target zene DevicePanelTest RecordRemainingTest` | 0 |
| RED | `ctest -R 'DevicePanelTest|RecordRemainingTest' --output-on-failure` (from `build/tests`) | 8 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4168.r1/logs/fix.patch` | 0 |
| Build with fix | `cmake --build build --target zene DevicePanelTest RecordRemainingTest` | 0 |
| GREEN | `ctest -R 'DevicePanelTest|RecordRemainingTest' --output-on-failure` (from `build/tests`; 2/2 passed) | 0 |
| Gates, initial branch state | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-4168` | 1 |
| Repair manifest from its published recipe | `{ awk '/^[^#[:space:]]/{exit} {print}' tests/all-sources.txt; { git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)' ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } | LC_ALL=C sort -u; } > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4168.r1/logs/all-sources.txt.new && mv /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4168.r1/logs/all-sources.txt.new tests/all-sources.txt` | 0 |
| Gates after recipe repair | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-4168` | 0 |

The first gate run exposed a train-15 snapshot regression already described in `tests/all-sources.txt`: 1148 vendored `plugins/SfzPlayer/sfizz/**` entries contradicted the manifest's own exclusion recipe. The manifest was regenerated from that recipe; `tests/upstream-modifications.txt` records this gate repair. Gate 02 passed all eight rows, including source registration, no-upstream-regression, namespace, tautology, doc figures, complexity, and file length. The tested target is a Qt GUI target and uses the configured offscreen platform; only the configured Linux build was available in this worktree.

Evidence: `src/gui/shell/InspectorClipFields.cpp:126-155` adds the clip and note ticks in `qint64`, checks the supported `tick_t` range, and rejects only typed lengths whose selected unit needs an unrepresentable Time origin. Explicit tick and bar lengths remain usable because those units do not depend on the origin. `setSelectedLength()` applies that check before invoking `note.edit` (`src/gui/shell/InspectorClipFields.cpp:225-236`). `noteTimeText()` validates the note's absolute start and end before calling the Time formatter, and uses origin-free length formatting for Bars|Beats and Ticks (`src/gui/shell/InspectorClipFields.cpp:261-276`). The regression at `tests/src/gui/DevicePanelTest.cpp:539-600` covers a normal one-second edit, a large origin where `96 ticks` still works, and a Time display / typed-seconds refusal at `clip = INT_MAX - 100`, `note = 200`. Existing clip and note serialization is unchanged; no audio-thread path is involved.

RED/GREEN proof: with only the new-p10 product patch reversed, the test failed with `shown length: 0:00.429; note.edit dispatched: 1`. Restoring the patch made the same two-test ctest family pass (2/2).

| Phase | Command | Exit |
|---|---|---:|
| Reverse product patch | `git diff -- src/gui/shell/InspectorClipFields.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4168.r1/logs/fix-new-p10-171-final.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4168.r1/logs/fix-new-p10-171-final.patch` | 0 |
| Build without fix | `cmake --build build --target zene DevicePanelTest RecordRemainingTest` | 0 |
| RED | `ctest -R 'DevicePanelTest|RecordRemainingTest' --output-on-failure` (from `build/tests`) | 8 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4168.r1/logs/fix-new-p10-171-final.patch` | 0 |
| Build with fix | `cmake --build build --target zene DevicePanelTest RecordRemainingTest` | 0 |
| GREEN | `ctest -R 'DevicePanelTest|RecordRemainingTest' --output-on-failure` (from `build/tests`; 2/2 passed) | 0 |
| Final feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-4168` | 0 |

The first p10 gate run returned 1 because `notesSection` reached CCN 14; extracting the checked time formatter into `noteTimeText()` restored the complexity gate. The final feature check passed all eight rows. Only the configured Linux build was available; the test target uses the existing offscreen Qt registration.

## Self-check

- RED/GREEN: 4168's fixed-handler test failed with the generic `Key: "D4" was not accepted` message when only the product patch was reversed (ctest exit 8), then passed with the fix (ctest exit 0). new-p10-171's test failed with `shown length: 0:00.429; note.edit dispatched: 1` when only its product patch was reversed (ctest exit 8), then passed with the fix (ctest exit 0). Both tests ran in the `DevicePanelTest|RecordRemainingTest` family (2 registered ctest tests).
- Determinism: no sleeps or wall-clock values. The tests inject fixed command refusal results, use fixed clip/note tick values (`INT_MAX - 100` and `200`), set the musical time format directly, and synchronously send Qt input events.
- Cause and siblings: the Inspector's clip and note command handlers now preserve `ControlResult`; `ReadoutField` keeps the bool API for other callers. The note length parser's combined origin uses wide checked arithmetic; the note start and length Time display paths are both checked, including the length end passed into `MusicalTime`. Explicit ticks and bar units remain valid without a Time origin.
- Collateral and persistence: no project serialization, command registration, audio processing, or undo transaction code changed. The regression verifies an ordinary time edit and that `96 ticks` still applies at an out-of-range absolute Time origin. No new bound was added to shared callers.
- Not applicable: neither verdict is ALREADY-FIXED nor FALSE-POSITIVE.
- Final `feature_check.py` result: exit 0, all eight gates PASS (see the final feature-gates command and the gate table above).
