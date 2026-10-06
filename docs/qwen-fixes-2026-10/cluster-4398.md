# Cluster 4398 — Previous-run status reports this session's marker

**Verdict: FIXED**

## Finding

`src/core/SafeStart.cpp` — `previousRunExitedCleanly()` returned `!markerExists()`.
`beginSession()` writes THIS session's marker, so any client that calls
`beginSession()` and then asks the status gets `false` ("previous run did not
exit cleanly") whether the previous run was clean or crashed. Confidence: high.

## Evidence (file:line)

- `src/core/SafeStart.cpp:338` (pre-fix): `bool previousRunExitedCleanly() { return !markerExists(); }` — a live disk check.
- `src/core/SafeStart.cpp:274` `beginSession()` snapshots the predecessor at line 289 (`s.markerWasPresent = s.previous.present;`) and then writes this session's marker at line 305 (`writeMarker(...)`), so after `beginSession()` the disk check always reports "unclean".
- `include/SafeStart.h:190-193` documents the contract: answerable before `beginSession()` too, where it means "is a marker on disk". The fix preserves both halves.

## Fix

`src/core/SafeStart.cpp` (header unchanged — `State` is in the .cpp anonymous namespace):

1. New `State` member `bool sessionBegun = false;` (with the other defaults).
2. `install()` resets `s.sessionBegun = false;` — a fresh working directory is a fresh session context (this also keeps the process-global state honest between sessions).
3. `beginSession()` sets `s.sessionBegun = true;` alongside the other per-session resets.
4. `previousRunExitedCleanly()` now answers from the disk **before** `beginSession()` and from the snapshot **after** it:

   ```cpp
   const State& s = state();
   if (!s.sessionBegun) { return !markerExists(); }
   return !s.markerWasPresent;
   ```

Behaviour-preserving otherwise: pre-`beginSession()` disk semantics unchanged; `endSession()`/`clear()` already zero `markerWasPresent`, so post-session status still reads clean.

## Regression test

Added `previousRunStatusSurvivesThisSessionsMarker()` to `tests/src/core/SafeStartTest.cpp`
(section "8. THE STATUS QUESTION"): plants a crashed-run marker, asserts the pre-`beginSession()`
disk half (preservation guard), then the clean-predecessor half — after `beginSession()` the status
must still say the previous run exited cleanly despite this session's own marker (the RED leg,
line 525).

## Commands and EXIT codes (as run)

```
$ cmake --build build --target SafeStartTest > .../logs/build-1.log 2>&1; echo EXIT=$?
EXIT=0
$ (cd build/tests && ctest -R SafeStartTest --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?)
EXIT=8
```

RED before the fix — exactly the new assertion, all 8 existing tests pass:

```
FAIL!  : SafeStartTest::previousRunStatusSurvivesThisSessionsMarker() 'previousRunExitedCleanly()' returned FALSE. (the previous run exited cleanly; this session's own marker must not change that)
   Loc: [../../tests/src/core/SafeStartTest.cpp(525)]
Totals: 9 passed, 1 failed, 0 skipped, 0 blacklisted, 1357ms
```

```
$ cmake --build build --target zene SafeStartTest > .../logs/build-2.log 2>&1; echo EXIT=$?
EXIT=0
$ (cd build/tests && ctest -R SafeStart --output-on-failure > .../logs/test-2.log 2>&1; echo EXIT=$?)
EXIT=8
```

Intermediate run: the pre-`beginSession()` leg then failed at line 508 because the QtTest process
shares the global `State` across tests and `install()` did not reset `sessionBegun`; `SafeStartLoadPathTest`
had no binary yet ("Not Run"). Fixed by the `install()` reset and building that target:

```
$ cmake --build build --target zene SafeStartTest SafeStartLoadPathTest > .../logs/build-3.log 2>&1; echo EXIT=$?
EXIT=0
$ (cd build/tests && ctest -R SafeStart --output-on-failure > .../logs/test-3.log 2>&1; echo EXIT=$?)
EXIT=0
```

GREEN — whole family:

```
1/2 Test #53: SafeStartTest ....................   Passed    1.45 sec
2/2 Test #54: SafeStartLoadPathTest ............   Passed    0.21 sec
100% tests passed, 0 tests failed out of 2
```

## Gates

```
$ bash tests/complexity-gate.sh --check   # .../logs/gate-1.log
GATE_COMPLEXITY_EXIT=0
$ bash tests/file-length-gate.sh --check  # .../logs/gate-2.log
GATE_LENGTH_EXIT=0
```

## Bookkeeping

No new product files: `src/core/SafeStart.cpp` and `tests/src/core/SafeStartTest.cpp` are already
registered in `tests/fork-sources.txt`; no manifest changes needed.
