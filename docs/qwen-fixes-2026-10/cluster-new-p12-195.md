# cluster new-p12-195 — BUG 7: an invalid crossover count can read past the crossover array

**Verdict: FIXED**

## What was wrong

`RackSplit` holds a fixed `std::array<float, MaxBands - 1>` (`crossovers`, five elements,
`include/RackSplit.h:70`) plus a free-standing `int crossoverCount` (`include/RackSplit.h:71`).
Nothing enforced `crossoverCount <= MaxBands - 1` where the value is stored:

- `Rack::setSplit()` (`src/core/Rack.cpp:234-243`) copied a caller's `RackSplit` verbatim
  (`m_split = split;`), so a caller that sets `crossoverCount = 6` put an impossible count into the
  rack.
- `RackSplit::save()` (`src/core/RackSplit.cpp:70-78`) then looped `k < crossoverCount` and read
  `crossovers[5]` — one past the end of the five-element array (UB) — whenever the rack was saved
  (`Rack::saveSettings()` → `m_split.save(rack)`, `src/core/Rack.cpp:356`).

Every in-tree *producer* of a split already caps the count (`readCrossovers` refuses more than
`MaxBands - 1` values, `src/core/ControlCommandsDeviceTools.cpp:103`; `RackSplit::load` stops at
`MaxBands - 1`, `src/core/RackSplit.cpp:89`), but the public store and the public saver did not, so
any API caller could still create the out-of-bounds read.

## The fix (smallest change, both ends the finding names)

- `src/core/Rack.cpp` (`Rack::setSplit`): clamp the stored count —
  `m_split.crossoverCount = std::clamp(m_split.crossoverCount, 0, RackSplit::MaxBands - 1);`
- `src/core/RackSplit.cpp` (`RackSplit::save`): bound the read —
  `const int count = std::clamp(crossoverCount, 0, MaxBands - 1);` and iterate `k < count`.

Behaviour of every valid split (count 0..5) is unchanged; no headers changed; no new control
commands; no allocation, locking or logging on any audio path (both edits run on the control /
save thread).

## Regression test

`tests/src/core/DeviceToolsTest.cpp` → `anOverlongCrossoverCountIsClampedAndSavedSafely()`
(placed after `aRackSplitIsSetSavedAndUndone`). It sets `crossoverCount = MaxBands` (6) with five
real frequencies, stores it through `Rack::setSplit()`, and asserts (a) the stored count is clamped,
(b) a raw `RackSplit::save()` writes no more than five values, (c) the rack's own saved
`crossovers` attribute holds exactly the stored count.

## Evidence (commands exactly as run; cwd = worktree unless stated)

| Step | Command | EXIT |
|---|---|---|
| Gates before the change | `bash tests/complexity-gate.sh --check > logs/gate-pre-1.log 2>&1` | 0 |
| Gates before the change | `bash tests/file-length-gate.sh --check > logs/gate-pre-2.log 2>&1` | 0 |
| Build test on original code | `cmake --build build --target DeviceToolsTest > logs/build-1.log 2>&1` | 0 |
| **RED** (original code + new test) | `ctest -R '^DeviceToolsTest$' --output-on-failure > logs/test-1-red.log 2>&1` (cwd `build/tests`) | **8** |
| Build with the fix | `cmake --build build --target zene DeviceToolsTest > logs/build-2.log 2>&1` | 0 |
| **GREEN** (fixed code) | `ctest -R '^DeviceToolsTest$' --output-on-failure > logs/test-2-green.log 2>&1` (cwd `build/tests`) | **0** |
| GREEN, QtTest detail | `ZENE_CONTROL_CHECK_RESULTS=1 ./DeviceToolsTest > logs/test-2b-green-qt.log 2>&1` (cwd `build/tests`) | 0 |
| Family (rack + device + command snapshot) | `ctest -R '^(DeviceToolsTest\|RackTest\|RackMacrosTest\|RackZonesTest\|RackZoneRoutingTest\|RackMenuTest\|ControlCommandsSnapshot)$' --output-on-failure > logs/test-3-family-green.log 2>&1` (cwd `build/tests`) | 0 |
| Reverse proof: strip the fix | `git diff -- src/core/Rack.cpp src/core/RackSplit.cpp > logs/fix.patch` then `git apply -R logs/fix.patch` | 0, 0 |
| Reverse proof: rebuild | `cmake --build build --target zene DeviceToolsTest > logs/build-4-nofix.log 2>&1` | 0 |
| Reverse proof: **test must fail without the fix** | `ctest -R '^DeviceToolsTest$' --output-on-failure > logs/test-4-nofix-red.log 2>&1` (cwd `build/tests`) | **8** |
| Reverse proof: restore the fix | `git apply logs/fix.patch` then `cmake --build build --target zene DeviceToolsTest > logs/build-5-reapplied.log 2>&1` | 0, 0 |
| Reverse proof: **test passes with the fix** | `ctest -R '^DeviceToolsTest$' --output-on-failure > logs/test-5-reapplied-green.log 2>&1` (cwd `build/tests`) | **0** |
| Family, final state | `ctest -R '<same family regex>' > logs/test-6-family-final.log 2>&1` (cwd `build/tests`) | 0 |
| Ratchet gate | `bash tests/complexity-gate.sh --check > logs/gate-1-complexity.log 2>&1` | **0** |
| Ratchet gate | `bash tests/file-length-gate.sh --check > logs/gate-2-filelength.log 2>&1` | **0** |

(`logs/` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-195.r1/logs`.)

- **Failing assertion, recorded before the fix** (`logs/test-1-red.log:16` and
  `logs/test-4-nofix-red.log:16`, identical): `FAIL! : DeviceToolsTest::anOverlongCrossoverCountIsClampedAndSavedSafely() 'rack.split().crossoverCount <= RackSplit::MaxBands - 1' returned FALSE.`
- **Affected-family test count:** 7 ctest tests
  (`RackMacrosTest`, `RackTest`, `RackZonesTest`, `RackZoneRoutingTest`, `DeviceToolsTest`,
  `RackMenuTest`, `ControlCommandsSnapshot`) — 7/7 passed in the final state
  (`logs/test-6-family-final.log`: `100% tests passed, 0 tests failed out of 7`).
- **DeviceToolsTest QtTest totals:** RED `Totals: 5 passed, 1 failed, 7 skipped`
  (`logs/test-1-red.log:34`); GREEN `Totals: 6 passed, 0 failed, 7 skipped`
  (`logs/test-2b-green-qt.log:30`). The 7 skips are the pre-existing `QSKIP`s for a build without
  the amplifier module — unchanged before and after.
- **Gate exit codes:** complexity-gate 0, file-length-gate 0 (both also 0 before the change).

## Files committed

- `src/core/Rack.cpp` (clamp in `Rack::setSplit`)
- `src/core/RackSplit.cpp` (bounded read in `RackSplit::save`)
- `tests/src/core/DeviceToolsTest.cpp` (regression test)
- `docs/qwen-fixes-2026-10/cluster-new-p12-195.md` (this report)

No new product files, so `tests/fork-sources.txt` needed no entry (all three changed files are
already listed there: lines 1523, 1526, 1944). No header changed, so no declaration/definition
split and no extra target rebuilds beyond `zene` and the tests that link it. No gate baseline was
re-anchored and no gate limit was touched.
