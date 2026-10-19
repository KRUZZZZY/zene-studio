# Cluster new-p11-44-port — `reference.copy_devices` can silently choose the wrong source

**Verdict: FIXED**

Finding: `src/core/ControlCommandsReference.cpp` `deviceOwner()` — took the mixer channel whenever
`channel` was present and ignored `track`; a call with neither source field failed as a missing
reference track -1.

## Change

- `src/core/ControlCommandsReference.cpp` — `deviceOwner()` now refuses a call naming zero or both
  source fields with `ControlErrorKind::InvalidArgs` before any device is read; a single field
  behaves as before. Command description updated to state "exactly one of the two".
- `tests/src/core/ArrangingCatchUpTest.cpp` — regression `copyDevicesNamesExactlyOneSource()`.

## Evidence

### RED — regression fails on original code
1. Test added, product untouched:
   `cd build/tests && ctest -R ArrangingCatchUpTest --output-on-failure > logs/test-red.log 2>&1`
   → EXIT=8 — `FAIL! ... '!both.ok' returned FALSE` at `ArrangingCatchUpTest.cpp`, Totals 12 passed, 1 failed.
2. Proof test needs the fix (reverse-apply):
   `git diff -- src/core/ControlCommandsReference.cpp > logs/fix.patch && git apply -R logs/fix.patch` → ok
   `cmake --build build --target zene ArrangingCatchUpTest > logs/build-rev.log 2>&1` → EXIT=0
   `cd build/tests && ctest -R ArrangingCatchUpTest --output-on-failure > logs/test-rev.log 2>&1` → EXIT=8 (same assertion).
   `git apply logs/fix.patch` → ok; rebuild → EXIT=0; ctest → EXIT=0.

### GREEN — after the fix
`cmake --build build --target zene ArrangingCatchUpTest > logs/build-green.log 2>&1` → EXIT=0
`cd build/tests && ctest -R ArrangingCatchUpTest --output-on-failure > logs/test-green.log 2>&1` → EXIT=0
Detail: Totals 13 passed, 0 failed, 0 skipped.

### Affected family
`cd build/tests && ctest -R "ArrangingCatchUpTest|ControlCommandsSnapshot|ControlRegistryTest|ControlSurfaceReferenceTest|ReversibilityContractTest|ReversibilityUndoTest" --output-on-failure > logs/test-family.log 2>&1`
→ EXIT=0, 6 of 6 tests passed.

### Gates
`bash tests/complexity-gate.sh --check` → EXIT=1 (pre-existing regressions unrelated to this change; no new regression lines introduced).
`bash tests/file-length-gate.sh --check` → EXIT=1 (pre-existing regressions unrelated to this change; no new regression lines introduced).

## Files committed
- `src/core/ControlCommandsReference.cpp`
- `tests/src/core/ArrangingCatchUpTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-44-port.md`
