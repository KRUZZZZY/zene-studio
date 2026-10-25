new-p18-43: FIXED

`src/tracks/InstrumentTrackNotes.cpp:389` was REAL: the existing loader called `InstrumentOutputBuses::loadSettings()` for every load, and that method clears routes before reading its optional node. The test `InstrumentRoutingTest::presetWithoutOutputBusNodeKeepsRoutes` exercises the real `Track::loadPreset` path: a preset without `<outputbuses>` keeps the route, an explicit node replaces it, and a project load without the node still clears it.

The mode-aware `Track::loadTrackSpecificSettings` hook passes preset/project mode to InstrumentTrack. The default hook delegates to the existing one-argument method for other track types. InstrumentTrack skips bus loading only for a preset that lacks `<outputbuses>`; preview mode and project reset behavior remain intact.

| Evidence | Command | Exit |
| --- | --- | ---: |
| Original production code and regression target built | `cmake --build build --target InstrumentRoutingTest -j12` | 0 |
| RED: preset route became `-1`, expected mixer channel id `7` | `ctest -R '^InstrumentRoutingTest$' --output-on-failure` from `build/tests` | 8 |
| GREEN build of app and affected test | `cmake --build build --target zene InstrumentRoutingTest -j12` | 0 |
| GREEN: 12 QtTest cases passed; 1/1 CTest target passed | `ctest -R '^InstrumentRoutingTest$' -V` from `build/tests` | 0 |
| Step 4b: saved the product diff and reversed it | `git diff -- include/Track.h include/InstrumentTrack.h src/core/Track.cpp src/tracks/InstrumentTrack.cpp src/tracks/InstrumentTrackNotes.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-43.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-43.r1/logs/fix.patch` | 0 |
| Reverse proof: rebuilt original app and test | `cmake --build build --target zene InstrumentRoutingTest -j12` after reversing the production patch | 0 |
| Reverse proof RED: the same preset route assertion failed | `ctest -R '^InstrumentRoutingTest$' --output-on-failure` from `build/tests` | 8 |
| Reapplied the saved production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-43.r1/logs/fix.patch` | 0 |
| Reverse proof GREEN: rebuilt app and test | `cmake --build build --target zene InstrumentRoutingTest -j12` | 0 |
| Reverse proof GREEN: focused test passed | `ctest -R '^InstrumentRoutingTest$' --output-on-failure` from `build/tests` | 0 |
| Initial feature gate: the source manifest did not reproduce | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-43` | 1 |
| Final feature gate table: all rows PASS | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-43` | 0 |

The first gate run exposed the existing 1,148-entry `plugins/SfzPlayer/sfizz` mismatch in `tests/all-sources.txt`; it was regenerated from that file's published recipe. The next gate run passed `fork-sources` and `all-sources-reproduce` as well as every other row. The CTest family executed 1 registered target (12 QtTest cases).

Files in this fix commit: `include/Track.h`, `include/InstrumentTrack.h`, `src/core/Track.cpp`, `src/tracks/InstrumentTrack.cpp`, `src/tracks/InstrumentTrackNotes.cpp`, `tests/src/core/InstrumentRoutingTest.cpp`, `tests/all-sources.txt`, `tests/upstream-modifications.txt`, and this report.
