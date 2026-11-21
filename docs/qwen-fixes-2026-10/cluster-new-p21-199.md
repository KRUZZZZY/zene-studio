FIXED

The finding is real, though “full linear scan” overstates the cost. In `mixerIndexOf`, the guard at `src/core/InstrumentOutputBuses.cpp:248` rejected cached index 0. The fallback at lines 255-263 checks the master first, returns immediately, and stores 0 again. `send` calls `mixerIndexOf` for every unmuted bus send at line 295, so the cache miss and repeated store occur on the render worker. The cache-hit guard now accepts `cached >= 0`; the `-1` invalid sentinel remains rejected by the nonnegative and channel-count checks.

The deterministic regression `InstrumentRoutingTest::outputBusMasterIndexIsCached` primes a route to the master, resets test-only hit/fallback counters, and checks the second lookup uses the cache. It fails on the original condition, passes with the fix, fails again with only that condition reversed, then passes after restoration. The counters are compiled only under `LMMS_TESTING`; the shipping path adds no allocation, lock, wait, or log.

Commands and results (logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-199.r1/logs`):

| Evidence | Command | Result |
| --- | --- | --- |
| Build regression before product fix | `cmake --build build --target zene InstrumentRoutingTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-199.r1/logs/qa-001-red-build.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Regression before product fix | `ctest -R '^InstrumentRoutingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-199.r1/logs/qa-002-red-test.log 2>&1; echo EXIT=$?` (from `build/tests`) | EXIT=8; `outputBusMasterIndexIsCached` observed 0 cache hits, expected 1 |
| Build with fix | `cmake --build build --target zene InstrumentRoutingTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-199.r1/logs/qa-003-green-build.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Regression with fix | `ctest -R '^InstrumentRoutingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-199.r1/logs/qa-004-green-test.log 2>&1; echo EXIT=$?` (from `build/tests`) | EXIT=0; 1/1 registered test passed |
| Build after reversing only the cache condition | `cmake --build build --target zene InstrumentRoutingTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-199.r1/logs/qa-005-reversed-build.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Regression with condition reversed | `ctest -R '^InstrumentRoutingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-199.r1/logs/qa-006-reversed-test.log 2>&1; echo EXIT=$?` (from `build/tests`) | EXIT=8; same cache-hit assertion failed |
| Build after restoring fix | `cmake --build build --target zene InstrumentRoutingTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-199.r1/logs/qa-007-restored-build.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Regression after restoring fix | `ctest -R '^InstrumentRoutingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-199.r1/logs/qa-008-restored-test.log 2>&1; echo EXIT=$?` (from `build/tests`) | EXIT=0; 1/1 registered test passed |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-199; echo EXIT=$?` | EXIT=0; all gates pass, with no new complexity or file-length item |

Changed files: `src/core/InstrumentOutputBuses.cpp`, `tests/src/core/InstrumentRoutingTest.cpp`, and this report. `InstrumentRoutingTest` was already registered in `tests/CMakeLists.txt`.

## Self-check

- **Regression proof:** original condition fails; fixed condition passes; reversing only the cache condition fails again; restoring it passes. No timing or sleep is used.
- **Cause and callers:** only `mixerIndexOf` reads the cached index. `send` and `routedIndex` call it; the same nonnegative-index validity check covers both. The master is mixer index 0.
- **Compatibility:** the return index is unchanged. Routes remain stored by persistent channel id; serialization and load behavior are untouched.
- **Realtime:** production adds no work beyond accepting a valid cached index instead of entering the fallback and storing it again. Test counters are guarded by `LMMS_TESTING` and use relaxed atomics.
- **Gates and registration:** `feature_check.py` passed; the existing test registration is unchanged.
