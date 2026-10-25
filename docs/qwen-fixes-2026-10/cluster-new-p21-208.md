FIXED

## Decision

REAL. In the original [LeakDetector.h](../../plugins/SfzPlayer/sfizz/src/sfizz/utility/LeakDetector.h:57), `auto residualCount = count.load() > 0` assigned the comparison result, so the diagnostic streamed a boolean. With three leaked test objects, the original code printed `Leaked 1 instance(s)`. The initializer now stores `count.load()` as an integer and checks `residualCount > 0` in the `if` condition. The existing diagnostic at line 58 therefore prints the actual residual count.

The shared `LeakDetector<Owner>::ObjectCounter` is the common reporting path for the 15 `LEAK_DETECTOR` users under `plugins/SfzPlayer/sfizz/src/sfizz` (including FilePool, Buffer, Synth, Voice, Region and both Wavetable types). No sibling counter or caller-specific reporting path needs a separate change.

## Regression and test matrix

`tests/src/plugins/SfizzLeakDetectorTest.cpp` exercises the header directly in a child process so the existing shutdown assertion cannot terminate the QtTest runner. The clean child constructs and destroys three tracked objects. The leaking child leaves three tracked objects alive; the test captures stderr and requires the exact diagnostic text and the existing assertion's child-process crash status. `QProcess::waitForStarted(-1)` and `waitForFinished(-1)` synchronize on process completion; there are no sleeps, timing measurements or races.

| Path | Coverage and result |
|---|---|
| Normal shutdown | Three objects are destroyed; child exits normally and emits no `Leaked` diagnostic. |
| Residual leak | Three objects remain; fixed diagnostic must contain `Leaked 3 instance(s) of class TestOwner`. Reverting the product hunk makes this assertion fail because the diagnostic says `1`. |
| Exhaustion/reconfiguration | This counter has no capacity or reconfiguration API. The regression covers zero and positive residual counts; no limit or new branch was added. |
| Audio thread | The test does not create an engine or render audio. Only the static shutdown report changed; the existing atomic constructor/destructor accounting is untouched. |
| Supported builds | The test enables the debug-only macro with `/UNDEBUG` on MSVC and `-UNDEBUG` on other compilers. It uses QtTest/QProcess APIs and C++17-compatible syntax. Local validation was Linux x86_64, GCC 16.2.1, Qt 6.12; other platform builds were not run here. |

## Evidence

| Check | Exact command | Exit | Evidence |
|---|---|---:|---|
| Original behavior, red | `ctest -R '^SfizzLeakDetectorTest$' --output-on-failure` | 8 | `test-9.log`: the clean case passes; the three-object case fails on `Leaked 1 instance(s)`. |
| Final fix, green | `ctest -R '^SfizzLeakDetectorTest$' --output-on-failure` | 0 | `test-10.log`: focused test passes, 1/1. |
| Affected SFZ family | `ctest -R '^(SfzPlayerTest|SfzPlayerRealtimeTest|SfizzLeakDetectorTest)$' --output-on-failure` | 0 | `test-11.log`: 3/3 tests pass. |
| Reverse-patch proof | `git diff -- plugins/SfzPlayer/sfizz/src/sfizz/utility/LeakDetector.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-208.r1/logs/fix.patch` | 0 | Saved only the product-header hunk as `fix.patch`. |
| Reverse-patch proof | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-208.r1/logs/fix.patch` | 0 | Reversed the product hunk; rebuilt `sfzplayer`, `SfizzLeakDetectorTest` and `zene` with commands below. |
| Reverse-patch proof | `ctest -R '^SfizzLeakDetectorTest$' --output-on-failure` | 8 | `test-9.log`: the identical final test failed again with the original `Leaked 1` output. |
| Restore | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-208.r1/logs/fix.patch` | 0 | Restored the product hunk; rebuilt all affected targets and reran focused and family tests. |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-208` | 0 | Final PASS table is in `gate-3.log`; all eight gate rows pass, including scope reproduction, complexity and file length. |
| Completion gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-208 new-p21-208` | 0 | `submit-1.log`: commits, clean status, report, test registration and embedded feature gate pass; each ASK item is answered in the self-check below. |

The product-header, test and application builds used `cmake --build build --target sfzplayer`, `cmake --build build --target SfizzLeakDetectorTest` and `cmake --build build --target zene`; the final reverse and restored builds exited 0 (`build-22.log` through `build-27.log`). The existing `SfzPlayerTest` and `SfzPlayerRealtimeTest` targets also built with exit 0 (`build-8.log` and `build-9.log`). Every build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-208.r1/logs`.

The first feature-gate run exited 1 because the inherited `tests/all-sources.txt` contained 1,148 vendored sfizz paths excluded by its own header recipe. I regenerated that list from the published recipe: 1,148 stale vendor entries were removed and the new regression source was added. The rerun exited 0. This inventory correction changes no product code and is recorded in `tests/upstream-modifications.txt`.

Two early combined-target build attempts, `cmake --build build --target zene SfizzLeakDetectorTest`, exited 2 during CMake registration/regeneration (`build-1.log`, `build-2.log`). I moved the Qt test property after `add_test()` and then built each target separately; all final standalone target builds passed.

## Self-check

- **Regression proof:** the exact final test failed with the product hunk reversed (`test-9.log`, CTest exit 8) and passed after restoration (`test-10.log`, exit 0). The final SFZ family passed 3/3 (`test-11.log`, exit 0). Both `git apply -R` and `git apply` exited 0.
- **Determinism:** the two child modes use fixed object counts of zero residual and three residual; process completion synchronizes the observation. There are no timed sleeps, clocks, or concurrent workers.
- **Cause and callers:** the changed initializer is the shared `ObjectCounter` shutdown report. All 15 `LEAK_DETECTOR` call sites use that template; construction/destruction counters and their atomic operations are unchanged.
- **Collateral behavior:** the only product change is the shutdown diagnostic's value. No project load/save code, serialized data, limits, commands, undo behavior, audio rendering or realtime path changed. The inventory update restores its documented recipe and includes the new test source.
- **Verdict evidence:** this is a REAL bug, demonstrated by the original `Leaked 1` output for three residual instances and the exact-count regression. An ALREADY-FIXED or FALSE-POSITIVE contract does not apply.

## Files committed

- `plugins/SfzPlayer/sfizz/src/sfizz/utility/LeakDetector.h`
- `tests/src/plugins/SfizzLeakDetectorTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p21-208.md`
