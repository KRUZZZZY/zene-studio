# Cluster 1530 — Step values lose precision on save

Verdict: **ALREADY-FIXED** on this branch (`bugs/qwen-1530`).

## Evidence

The finding pointed at `src/core/ModulationLayer.cpp:68` (`saveSource`). The current
file already serializes steps with `std::numeric_limits<float>::max_digits10`
significant digits, exactly as the finding prescribes:

- `src/core/ModulationLayer.cpp:69-75` — comment plus
  `QString::number(source.steps[...], 'g', std::numeric_limits<float>::max_digits10)`
  (the `#include <limits>` at line 33 supports it).
- The only other step site is the read-back at `src/core/ModulationLayer.cpp:141-143`
  (`loadSource`), which parses the saved text; no other step serialization exists
  (grep for `steps` in the file shows only these sites).

The fix landed in commit `98852c433 fix(modulation): step values lost precision on save`
(ancestor of this branch's HEAD; it touched `src/core/ModulationLayer.cpp` and
`tests/src/core/ModulationLayerTest.cpp`). Its diff replaced
`QString::number(value, 'g', 6)` with the `max_digits10` form.

The regression test already exists and is exact, not fuzzy:

- `tests/src/core/ModulationLayerTest.cpp:427-470` —
  `stepValuesSurviveSaveAndReload` saves a Steps modulator with steps
  `0.12345678f, -0.87654321f, 0.03141592f, 1.0f`, reloads it, and asserts
  exact `float == float` round-trip via `QVERIFY2` (line 463), plus a
  bit-identical `outputAt` comparison (line 469).

## Verification commands and results

All logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/1530.r1/logs`.

1. First run (test binary not yet built — ctest reported "Not Run", not a real red):

   ```
   cd build/tests && ctest -R '^ModulationLayerTest$' --output-on-failure > LOGS/test-1.log 2>&1; echo EXIT=$?
   EXIT=8
   ```

   `test-1.log` shows `Unable to find executable ... ModulationLayerTest (Not Run)`.

2. Build the test target:

   ```
   cmake --build build --target ModulationLayerTest > LOGS/build-1.log 2>&1; echo EXIT=$?
   EXIT=0
   ```

3. Rerun with the current (fixed) sources — GREEN:

   ```
   cd build/tests && ctest -R '^ModulationLayerTest$' --output-on-failure > LOGS/test-2.log 2>&1; echo EXIT=$?
   EXIT=0
   ```

   `test-2.log`: `1/1 Test #86: ModulationLayerTest .............. Passed 1.28 sec`,
   `100% tests passed, 0 tests failed out of 1`.

4. Gates:

   ```
   bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1; echo EXIT=$?   # EXIT=0
   bash tests/file-length-gate.sh --check >> LOGS/gate-1.log 2>&1; echo EXIT=$? # EXIT=0
   ```

## Action taken

No code change: the defect described by the finding is already fixed on this branch
(commit `98852c433`), and the exact round-trip test passes. Only this report was
committed.
