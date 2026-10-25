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

Files in the new-p20-21 commit: `src/core/ControlCommandsTrackPresets.cpp`, `tests/src/core/TrackPresetCommandsTest.cpp`, and `docs/qwen-fixes-2026-10/cluster-new-p23-32.md`.
