FIXED

new-p17-180: FIXED
Evidence: `src/gui/SurroundPanner.cpp:243` now sends only the values changed by a control. Slider commits include the current enabled state read from `mixer.surround_get`, so a stale checkbox value cannot replace a newer enable edit. `tests/src/gui/SurroundPannerTest.cpp:117` opens the panner, changes position, spread and enabled state through another command, then edits LFE and checks every field.

- RED, fix reverted: `cmake --build build --target zene SurroundPannerTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-180.r1/logs/build-023.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0.
- RED test: `ctest -R '^SurroundPannerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-180.r1/logs/test-023.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=8; `aSliderEditKeepsNewerPanValues` failed.
- GREEN, fix restored: `cmake --build build --target zene SurroundPannerTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-180.r1/logs/build-024.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0.
- GREEN test: `ctest -R '^SurroundPannerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-180.r1/logs/test-024.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0.

new-p17-179: FIXED
Evidence: `src/gui/SurroundPanner.cpp:72` adds a pad format setter; `:276` rereads the channel's live sends and refreshes the label and pad; `:351` starts a dialog-owned refresh timer. `tests/src/gui/SurroundPannerTest.cpp:140` reroutes from 5.1 to 7.1, invokes the timer signal directly, checks the destination and speaker layout, then checks a format change back to 5.1 without waiting.

- RED, fix reverted: `cmake --build build --target zene SurroundPannerTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-180.r1/logs/build-025.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0.
- RED test: `ctest -R '^SurroundPannerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-180.r1/logs/test-025.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=8; `aRouteAndFormatChangeRefreshThePanner` failed because the refresh hook was absent.
- GREEN, fix restored: `cmake --build build --target zene SurroundPannerTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-180.r1/logs/build-026.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0.
- GREEN test: `ctest -R '^SurroundPannerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-180.r1/logs/test-026.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0.

new-p14-324: FIXED
Evidence: `src/gui/SurroundPanner.cpp:117`, `:122` and `:127` place only for left-button press, drag and release events. `tests/src/gui/SurroundPannerTest.cpp:186` right-clicks the pad and verifies the position remains unchanged and the panner remains off.

- RED, fix reverted: `cmake --build build --target zene SurroundPannerTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-180.r1/logs/build-021.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0.
- RED test: `ctest -R '^SurroundPannerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-180.r1/logs/test-021.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=8; `rightClickDoesNotMoveOrEnableThePanner` failed.
- GREEN, fix restored: `cmake --build build --target zene SurroundPannerTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-180.r1/logs/build-022.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0.
- GREEN test: `ctest -R '^SurroundPannerTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-180.r1/logs/test-022.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0.

## Self-check

- Final affected-family command: `ctest -R '^(MixerWindowsTest|SurroundPannerTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-180.r1/logs/test-027.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0, 2/2 CTest targets passed.
- Final feature gate: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-180 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-180.r1/logs/gate-002.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0. All eight rows PASS; complexity and file-length report no new item in changed files.
- Completion check: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-180 new-p17-180 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-180.r1/logs/submit-002.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0 (`RESULT: PASS - submit`). The five generic prompts printed by the checker are answered in this section.
- The regression target is a new, registered file under 500 lines. `tests/CMakeLists.txt` is recorded in `tests/upstream-modifications.txt`. `tests/all-sources.txt` was regenerated from its documented recipe to repair the pre-existing manifest mismatch found by the first gate run (EXIT=1); the final `fork-sources` and `all-sources-reproduce` gates both exit 0.
- Reverted-fix proof: new-p17-180 is RED/GREEN in `test-023.log` (EXIT=8) / `test-024.log` (EXIT=0); new-p17-179 is RED/GREEN in `test-025.log` (EXIT=8) / `test-026.log` (EXIT=0); new-p14-324 is RED/GREEN in `test-021.log` (EXIT=8) / `test-022.log` (EXIT=0). Each RED log names only its own regression failure; each GREEN run passes the full `SurroundPannerTest` target.
- Determinism: tests set pan values through `mixer.surround_pan`, route and format through their control commands, and send right-click with `QTest::mouseClick`. The route case calls `QMetaObject::invokeMethod(refresh, "timeout", Qt::DirectConnection)` as a synchronous refresh seam. There are no sleeps, clock checks or races.
- Cause and sibling paths: pad press, drag and release all filter on the left button; left-button placement and keyboard arrows still use the same pad release callback. LFE and spread use the changed-field slider path, and the enabled checkbox sends only its own field. Route refresh rereads `mixer.surround_get`; no command or audio-thread path was added.
- Compatibility: changes are confined to dialog controls and their GUI tests. No project serialization, saved values, DSP path, limits or command schema changed, so existing project data continues through the same load path. All three verdicts are FIXED; the not-real-verdict evidence question does not apply.
- Files committed across the three bug commits and the test/report registration commit: `src/gui/SurroundPanner.cpp`, `tests/src/gui/MixerWindowsTest.cpp`, `tests/src/gui/SurroundPannerTest.cpp`, `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt`, `tests/upstream-modifications.txt`, and `docs/qwen-fixes-2026-10/cluster-new-p17-180.md`.
