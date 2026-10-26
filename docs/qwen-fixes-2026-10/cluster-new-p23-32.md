FIXED
new-p23-32: FIXED

Decision: REAL. `src/core/ControlCommandsTrackPresets.cpp:62` rejects a path when `QFileInfo(path).isAbsolute()` is false, but the refusal said the path *is* absolute. The message now says it must be absolute.

Regression: `TrackPresetCommandsTest::saveRefusalExplainsTheAbsolutePathRequirement` checks both `InvalidArgs` and the exact refusal text. `LibraryProjectsTest::aTrackPresetRoundTrips` still covers the normal absolute-path save.

| Proof | Exact command | Exit |
|---|---|---:|
| Build without the fix | `cmake --build build --target zene TrackPresetCommandsTest -j4` | 0 |
| RED, reversed product patch | `cd build/tests && ./TrackPresetCommandsTest saveRefusalExplainsTheAbsolutePathRequirement` | 1 |
| Reverse patch | `git diff -- src/core/ControlCommandsTrackPresets.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-32.r1/logs/fix.patch`; `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-32.r1/logs/fix.patch` | 0; 0 |
| Reapply patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-32.r1/logs/fix.patch` | 0 |
| Build with the fix | `cmake --build build --target zene TrackPresetCommandsTest -j4` | 0 |
| GREEN | `cd build/tests && ./TrackPresetCommandsTest saveRefusalExplainsTheAbsolutePathRequirement` | 0 |
| Registration and feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-32` | 0 |

The exact build and test transcripts are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-32.r1/logs/026-build-preset-test-reverse.log`, `027-red-proof-p23-32-final-test.log`, `028-apply-final-test.log`, `029-build-preset-test-restore.log`, `030-green-proof-p23-32-final-test.log`, and `035-gate-p23-precommit-final.log`.

The added focused test source is registered in CMake and both scope manifests. `tests/all-sources.txt` was re-derived from its published recipe to remove stale vendored sfizz entries and make the gate pass.

Commit: `e0c7e6ea8` (`cluster new-p23-32: correct track preset save refusal`). Files committed: `docs/qwen-fixes-2026-10/cluster-new-p23-32.md`, `src/core/ControlCommandsTrackPresets.cpp`, `tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/fork-sources.txt`, `tests/src/core/TrackPresetCommandsTest.cpp`, `tests/upstream-modifications.txt`.

new-p20-21: FIXED

Decision: REAL. `presetLoad` checkpoints and changes the track at `src/core/ControlCommandsTrackPresets.cpp:91-95`, but previously supplied no handler transaction payload. The registry synthesized only its generic reversible record; the new payload records the target track, `control.undo`, and the Track checkpoint mechanism. The registry intentionally strips private `__transaction` from the direct reply (`include/ControlResultCheck.h:61`); the test checks the public `control.transactions` record.

Regression: `TrackPresetCommandsTest::loadReturnsItsTrackCheckpointTransaction` verifies the transaction row carries the target id, `control.undo`, `reversible: true`, and the Track checkpoint mechanism, then verifies one undo restores the target's earlier mute state.

| Proof | Exact command | Exit |
|---|---|---:|
| Build without the payload | `cmake --build build --target zene TrackPresetCommandsTest -j4` | 0 |
| RED, reversed product patch | `cd build/tests && ./TrackPresetCommandsTest loadReturnsItsTrackCheckpointTransaction` | 1 |
| Reverse patch | `git diff -- src/core/ControlCommandsTrackPresets.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-32.r1/logs/fix.patch`; `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-32.r1/logs/fix.patch` | 0; 0 |
| Reapply patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-32.r1/logs/fix.patch` | 0 |
| Build with the payload | `cmake --build build --target zene TrackPresetCommandsTest -j4` | 0 |
| GREEN | `cd build/tests && ./TrackPresetCommandsTest loadReturnsItsTrackCheckpointTransaction` | 0 |

The build and test transcripts are in `042-build-p20-reverse.log`, `043-red-proof-p20-21.log`, `044-apply-p20.log`, `045-build-p20-restore.log`, and `046-green-proof-p20-21.log` under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-32.r1/logs`.

Affected family: `ctest -R '^TrackPresetCommandsTest$' --output-on-failure` from `build/tests` — 1/1 CTest target passed (the target runs both preset regressions). Feature gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-32` — EXIT=0; table in `051-gate-p20-precommit-final.log`.

Commit: `dcda67a2c` (`cluster new-p20-21: report track preset checkpoint transaction`). Files committed: `src/core/ControlCommandsTrackPresets.cpp`, `tests/src/core/TrackPresetCommandsTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p23-32.md`.

new-p23-37: FIXED

Decision: REAL. `pluginReplace` at `src/core/ControlCommandsTrackPresets.cpp:104-127` replaces the effect and creates its undo record, but previously did not mark the song modified. The command now calls `Engine::getSong()->setModified()` after a successful replacement; refusals still return before that mutation.

Regression: `DeviceToolsChainTest::replacingADeviceMarksTheProjectModified` saves and marks a clean baseline, confirms a refused replacement leaves it clean, then verifies a successful replacement dirties the project and `control.undo` succeeds. The target now depends on the amplifier module so a targeted build executes this test rather than skipping it.

| Proof | Exact command | Exit |
|---|---|---:|
| Build without the fix | `cmake --build build --target zene DeviceToolsChainTest -j4` | 0 |
| Reverse product patch | `git diff -- src/core/ControlCommandsTrackPresets.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-32.r1/logs/fix.patch`; `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-32.r1/logs/fix.patch` | 0; 0 |
| RED | `cd build/tests && ./DeviceToolsChainTest replacingADeviceMarksTheProjectModified` | 1; `song->isModified()` was false after replacement |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-32.r1/logs/fix.patch` | 0 |
| Build with the fix | `cmake --build build --target zene DeviceToolsChainTest -j4` | 0 |
| GREEN | `cd build/tests && ./DeviceToolsChainTest replacingADeviceMarksTheProjectModified` | 0 |

Proof logs: `052-reverse-p23-37.log` through `057-green-proof-p23-37.log`; the dependency build and final green run are `058-build-p23-37-dependency.log` and `059-green-p23-37-final.log`. Final batch build: `cmake --build build --target zene TrackPresetCommandsTest DeviceToolsChainTest -j4` — EXIT=0 (`060-build-batch-final-tests.log`). Final affected CTest families: from `build/tests`, `ctest -R '^TrackPresetCommandsTest$' --output-on-failure` — 1/1 passed (`061-ctest-preset-final.log`), and `ctest -R '^DeviceToolsChainTest$' --output-on-failure` — 1/1 passed (`062-ctest-device-final.log`). Direct test totals: TrackPresetCommandsTest 4 passed, 0 skipped; DeviceToolsChainTest 9 passed, 0 skipped (`063-preset-direct-final.log`, `064-device-direct-final.log`). Final feature check: EXIT=0, all gates PASS (`066-feature-check-final-report.log`).

Commit message: `cluster new-p23-37: mark plugin replacements modified`. Files committed: `src/core/ControlCommandsTrackPresets.cpp`, `tests/CMakeLists.txt`, `tests/src/core/DeviceToolsChainTest.cpp`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p23-32.md`.

## Self-check

- The regressions fail with only their corresponding production patch reversed and pass when restored: new-p23-32 `027-red-proof-p23-32-final-test.log` EXIT=1 / `030-green-proof-p23-32-final-test.log` EXIT=0; new-p20-21 `043-red-proof-p20-21.log` EXIT=1 / `046-green-proof-p20-21.log` EXIT=0; new-p23-37 `054-red-proof-p23-37.log` EXIT=1 / `059-green-p23-37-final.log` EXIT=0. The red failures are respectively the wrong refusal text, absent target track in the public transaction row, and a clean project after successful replacement.
- The tests are deterministic: they invoke the registered control commands synchronously with fixed arguments and assert returned state. No clock timing, sleeps, races, or audio-thread seam is used. The preset tests use a temporary preset file; the device test uses the built amplifier module and checks a clean saved baseline before and after a refused command.
- The fixes are at the command handlers in `src/core/ControlCommandsTrackPresets.cpp:56-157`. `track.preset_save`, `track.preset_load`, and `plugin.replace` are registered once in this file; the save path retains its existing absolute-path guard, load uses the existing Track checkpoint, and plugin replacement marks modified only after the validated replacement succeeds. The public transaction test accounts for the registry stripping private `__transaction` from direct replies (`include/ControlResultCheck.h:61`).
- No project serialization format or load path changed. The save fix changes only refusal wording; the load fix adds transaction reply metadata; replacement changes only the in-memory modified flag after success. Tests assert refusal leaves the project clean, exercise undo, and the full affected families pass: TrackPresetCommandsTest 4/4 and DeviceToolsChainTest 9/9, with zero skips in each direct run (`063-preset-direct-final.log`, `064-device-direct-final.log`).
- The ALREADY-FIXED / FALSE-POSITIVE evidence question does not apply: all three verdicts are FIXED, each with the RED/GREEN evidence above.
