FIXED: `FilterHolder::reset()` now clears `description`, and a new holder initializes it to null. A holder processed before its next `setup()` no longer tests an indeterminate or stale pointer or runs the previous filter.

Cluster: new-p21-158 (Luna patrol: “reset() leaves a dangling description pointer”, FilterPool.cpp:18). Branch `bugs/qwen-new-p21-158`, base `cf3618e61`.

## Evidence

- `FilterPool.h:47` previously declared `description` without an initializer. `FilterPool.cpp:68-72` reads it in a null check before choosing either bypass or filter processing; an uninitialized pointer read is undefined behavior.
- `FilterPool.cpp:29` assigns the pointer only in `setup()`. `Voice.cpp:480-484` can return before the filter setup loop at `Voice.cpp:510-512`, after `Synth.cpp:1313-1315` has reset the selected voice. `Voice::reset()` previously left the pointer unchanged (`Voice.cpp:1719-1720` calls `FilterHolder::reset()`, which did not clear it).
- The report was right that `process()` does not dereference `description` itself. That does not make the state safe: `process()` reads it, and a non-null value selects processing with the previous filter configuration. The existing null branch is the explicit bypass behavior.
- The deterministic regression sets up a low-pass filter, resets the holder, then processes an alternating input. Before the product change the old filter still changed the input; after reset clears the pointer the block is copied unchanged.
- `FilterPool.h:47` now initializes `description` to null, and `FilterPool.cpp:20-23` clears it on reset. Both assignments are allocation-free and do not add work beyond the existing reset on the audio path.

## Test and build evidence

One test (`FilterPoolTest`) was executed per focused ctest run, from `build/tests`.

| State | Command | EXIT | Result |
| --- | --- | ---: | --- |
| Original product, regression added | `cmake --build build/tests --target FilterPoolTest -j4` | 0 | Test built |
| Original product, regression added | `ctest -R '^FilterPoolTest$' --output-on-failure` | 8 | Failed: reset did not bypass the previous filter (`FilterPoolTest.cpp:46`) |
| Fix applied | `cmake --build build --target zene -j4` | 0 | Built |
| Fix applied | `cmake --build build/plugins/SfzPlayer --target sfzplayer -j4` | 0 | Built touched sfizz code and module |
| Fix applied | `cmake --build build/tests --target FilterPoolTest -j4` | 0 | Built |
| Fix applied | `ctest -R '^FilterPoolTest$' --output-on-failure` | 0 | 1/1 passed |
| Product hunks reversed | `cmake --build build/plugins/SfzPlayer --target sfzplayer -j4` | 0 | Built |
| Product hunks reversed | `cmake --build build/tests --target FilterPoolTest -j4` | 0 | Built |
| Product hunks reversed | `ctest -R '^FilterPoolTest$' --output-on-failure` | 8 | Failed at the same assertion |
| Fix restored with `git checkout -- plugins/SfzPlayer/sfizz/src/sfizz/FilterPool.cpp plugins/SfzPlayer/sfizz/src/sfizz/FilterPool.h` | `cmake --build build --target zene -j4` | 0 | Built |
| Fix restored | `cmake --build build/plugins/SfzPlayer --target sfzplayer -j4` | 0 | Built |
| Fix restored | `cmake --build build/tests --target FilterPoolTest -j4` | 0 | Built |
| Fix restored | `ctest -R '^FilterPoolTest$' --output-on-failure` | 0 | 1/1 passed |

The reversal patch was limited to `FilterPool.cpp` and `FilterPool.h`; the test and registrations remained unchanged. It is saved as `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-158.r1/logs/qa-new-p21-158-reverse.patch`.

## Gates and files

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-158`: EXIT 0. All eight checks passed; complexity and file-length reported no new item in changed files.
- Regression registration: `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt`.
- Files in the QA fix commit: `plugins/SfzPlayer/sfizz/src/sfizz/FilterPool.cpp`, `plugins/SfzPlayer/sfizz/src/sfizz/FilterPool.h`, `tests/src/plugins/FilterPoolTest.cpp`, `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt`, and this report.

## Self-check

1. `FilterPoolTest` fails on the original code (ctest EXIT 8), passes with the fix (EXIT 0), and fails again with only the product changes reversed (EXIT 8). After restoring the product changes, it passes again (EXIT 0).
2. The test is deterministic: it supplies a fixed alternating 64-frame block synchronously; it uses no clock, sleep or race.
3. The pointer is initialized at construction and cleared at the reset cause. `Synth::Impl::startVoice()` calls `Voice::reset()` before `Voice::startVoice()`; the early-return-before-setup path and the `FilterHolder::process()` consumer were checked. `Voice::reset()` is the production caller of `FilterHolder::reset()`.
4. The change affects only filter-holder state after reset and before the next setup. It does not touch project data, save/load code or commands.
5. The prior FALSE-POSITIVE verdict was incorrect. `FilterPool.h:47` was uninitialized and `FilterPool.cpp:68` read it before setup; `FilterPool.cpp:20-23` now initializes the holder state safely on reset.
