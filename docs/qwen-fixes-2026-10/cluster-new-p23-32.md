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

The exact build and test transcripts are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-32.r1/logs/026-build-preset-test-reverse.log`, `027-red-proof-p23-32-final-test.log`, `028-apply-final-test.log`, `029-build-preset-test-restore.log`, `030-green-proof-p23-32-final-test.log`, and `034-gate-p23-after-manifest.log`.

The added focused test source is registered in CMake and both scope manifests. `tests/all-sources.txt` was re-derived from its published recipe to remove stale vendored sfizz entries and make the gate pass.
