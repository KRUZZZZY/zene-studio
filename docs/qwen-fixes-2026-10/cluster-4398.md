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

## Fix (round 1, then reworked per Luna's 4398.r1 review)

Round 1 answered from the disk before `beginSession()` and from `markerWasPresent`
after it. Luna's review found that snapshot mutable: `clear()` and `endSession()`
zero `markerWasPresent` for safe-start state, so clearing safe-start mid-session
flipped the reported history of the previous run, and `install()` reset
`sessionBegun` only on the success path, so a failed install left a stale session
snapshot. The rework keeps the previous-run result in a **latched** field:

`src/core/SafeStart.cpp` (header unchanged — `State` is in the .cpp anonymous namespace):

1. New `State` member `bool previousRunMarkerFound = false;` — the previous-run
   STATUS, latched at `beginSession()`, separate from `markerWasPresent` (which
   `clear()`/`endSession()` keep mutating for safe-start state).
2. `install()` resets `s.sessionBegun = false;` at the START of the function,
   before the early returns — a fresh session context whether the install
   succeeds or fails, so a failed install cannot leave a stale snapshot.
3. `beginSession()` latches `s.previousRunMarkerFound = s.previous.present;`
   alongside the existing `markerWasPresent` snapshot.
4. `endSession()` resets `s.sessionBegun = false;` — the session is over, so the
   disk answers the previous-run question for the NEXT launch again (and the
   disk is clean: the marker was just unlinked).
5. `previousRunExitedCleanly()` reads the latch, not the mutable snapshot:

   ```cpp
   const State& s = state();
   if (!s.sessionBegun) { return !markerExists(); }
   return !s.previousRunMarkerFound;
   ```

Behaviour-preserving otherwise: pre-`beginSession()` disk semantics unchanged;
`clear()` still lifts safe-start state (`safeStart`, `skipEnabled`, marker
unlink) exactly as before — it just no longer rewrites history.

## Regression tests

Round 1: `previousRunStatusSurvivesThisSessionsMarker()` (section "8. THE STATUS QUESTION") —
plants a crashed-run marker, asserts the pre-`beginSession()` disk half, then the clean-predecessor
half: after `beginSession()` the status must still say the previous run exited cleanly despite
this session's own marker.

Round 2 (Luna's rework), two new tests in `tests/src/core/SafeStartTest.cpp`:

- `clearingSafeStartDoesNotRewritePreviousRunHistory()` (section 9): crashed predecessor,
  `beginSession()`, then `clear()` mid-session — the status must STILL report the previous run
  as unclean for the whole session. RED with the round-1 fix (it read the mutable
  `markerWasPresent`, which `clear()` zeroes).
- `failedInstallLeavesNoStaleSessionSnapshot()` (section 10): crashed predecessor, `beginSession()`,
  then a FAILED `install()` into a non-existent path — the module owns no working directory, so the
  status must answer from the disk (clean), not from the stale snapshot of a session that no longer
  exists. RED with the round-1 fix (`install()` reset `sessionBegun` only on the success path).

## Commands and EXIT codes (round 2, as run; logs in runs/4398.r2/logs)

RED first — the two new regressions against the round-1 code (latch absent,
`install()` reset only on success):

```
$ cmake --build build --target SafeStartTest > .../logs/build-1.log 2>&1; echo EXIT=$?
EXIT=0
$ (cd build/tests && ctest -R SafeStartTest --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?)
EXIT=8
```

```
FAIL!  : SafeStartTest::clearingSafeStartDoesNotRewritePreviousRunHistory() '!previousRunExitedCleanly()' returned FALSE.
   Loc: [../../tests/src/core/SafeStartTest.cpp(560)]
FAIL!  : SafeStartTest::failedInstallLeavesNoStaleSessionSnapshot() 'previousRunExitedCleanly()' returned FALSE. (the stale snapshot ...)
   Loc: [../../tests/src/core/SafeStartTest.cpp(591)]
Totals: 10 passed, 2 failed, 0 skipped, 0 blacklisted, 1387ms
```

GREEN after the rework — rebuild `zene` and both SafeStart test targets, whole family:

```
$ cmake --build build --target zene SafeStartTest SafeStartLoadPathTest > .../logs/build-2.log 2>&1; echo EXIT=$?
EXIT=0
$ (cd build/tests && ctest -R SafeStart --output-on-failure > .../logs/test-2.log 2>&1; echo EXIT=$?)
EXIT=0
```

```
1/2 Test #53: SafeStartTest ....................   Passed    1.43 sec
2/2 Test #54: SafeStartLoadPathTest ............   Passed    0.22 sec
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

No new product files this round: `src/core/SafeStart.cpp` is already registered in
`tests/fork-sources.txt` (line 1549); `tests/src/core/SafeStartTest.cpp` is a fork-new test source
outside the manifest recipe's awk allowlist and was never registered — unchanged state, nothing to
add. No inherited file changed, so `tests/upstream-modifications.txt` needs no entry.
