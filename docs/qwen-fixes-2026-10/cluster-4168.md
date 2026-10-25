4168: FIXED

Evidence: `src/gui/shell/InspectorClipFields.cpp:61-63` returns the complete `ControlResult`; the Inspector's clip and note handlers use `ReadoutField::bindTextResult` (`src/gui/shell/InspectorClipFields.cpp:229-241,277-284`). `ReadoutField::commitTyped()` displays `errorMessage` and keeps the existing generic message when no reason is supplied (`src/gui/shell/TransportReadouts.cpp:221-251`). The regression at `tests/src/gui/DevicePanelTest.cpp:445-496` stubs the registered `note.transpose` handler with a fixed refusal and checks the visible tooltip. The existing numeric Inspector test covers a successful typed note edit. The new binding is UI-thread-only; it does not touch audio processing, allocation constraints, or undo behavior.

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
