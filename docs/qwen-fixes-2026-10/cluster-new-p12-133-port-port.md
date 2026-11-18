# Cluster new-p12-133-port-port

Verdict: **FIXED**

The finding was REAL on this line: `MidiClock::setSlaveEnabled(false)` still cleared the
follow-tempo preference, and the follower's poll timer still could not be restarted after
a disable/re-enable cycle. The fix was re-applied by hand from the accepted branch
(`bugs/qwen-new-p12-133-port`: `784aa798b`, `4d4c2249d`, `127fff8c5`; originally
`bugs/qwen-new-p12-133`: `550b31f45`, `cba37d803`, `15f1ff58d`) - no merge, no
cherry-pick, no rebase. Both regressions are RED without the product hunks and GREEN
with them, and the job's gate (`feature_check.py`) is green.

## What was re-applied, and what was already on this line

| Accepted commit | Product change | State on this line | Action here |
| --- | --- | --- | --- |
| `550b31f45` Fix MIDI clock slave follow preference | `src/core/MidiClock.cpp`: `setSlaveEnabled(false)` must not clear `m_slaveFollowTempo` | **MISSING** (the store was at `src/core/MidiClock.cpp:283`) | re-applied by hand |
| `cba37d803` Fix MIDI clock follow preference command flow | `src/core/ControlCommandsClock.cpp`: `restoreSlaveState(enabled, follow, ...)` | **ALREADY PRESENT** - the newest line carries the same change from `ed62a4c46` (`src/core/ControlCommandsClock.cpp:135-149,184-187`) | none needed |
| `15f1ff58d` Fix MIDI clock follow timer restart | `src/core/MidiClock.cpp`: `startFollowTimer()` creates the timer once and restarts it when inactive | **MISSING** (`if (m_followTimer != nullptr) { return; }`) | re-applied by hand |

The conflict that stopped the earlier port was in `src/core/ProjectRevisions.cpp` and
five test files - all of them part of the ratchet-drift sweep in `4d4c2249d`, not part
of the product fix. This port re-applies the PRODUCT fix and its regressions only; the
drift sweep is not repeated here (see "The ratchet gates" below).

## File and line evidence

- `src/core/MidiClock.cpp:278-292` - `setSlaveEnabled()`: the disable path resets the
  tracker only; the follow PREFERENCE is configuration and survives the toggle.
  Before the fix the body also ran `m_slaveFollowTempo.store(false);`.
- `src/core/MidiClock.cpp:421-436` - `startFollowTimer()`: the timer is created and
  connected once and started whenever it is not active, so a disable/re-enable cycle
  resumes the poll. Before the fix the function returned early whenever the timer
  object already existed, and `stopFollowTimer()` only stops it - so the second
  `clock.slave_set` with `enabled:true` left the follower with a stopped timer that
  never wrote the measured tempo to the Song again.
- `src/core/ControlCommandsClock.cpp:135-149` - `restoreSlaveState()`: the one caller of
  `setSlaveEnabled()` / `setSlaveFollowTempo()` / `startFollowTimer()` /
  `stopFollowTimer()`; it sets the follow flag explicitly after the enable/disable, so
  the command path already carried the preference across a disable on this line
  (`ed62a4c46`). The engine-level store was the hole the finding names.
- `tests/src/core/MidiClockTest.cpp:611-631` - `slaveEnableTogglePreservesFollowTempoPreference`:
  the engine-level claim (the preference survives the toggle and the measurement still
  reaches the Song).
- `tests/src/gui/MidiClockMenuTest.cpp:177-215` -
  `slaveSetRetainsFollowPreferenceAcrossDisable`: the command-level claim (the menu's
  `enabled`-only re-enable keeps the preference, the poll timer is restarted, and the
  Song's tempo follows again).

## The ratchet gates

`bash tests/complexity-gate.sh --check` and `bash tests/file-length-gate.sh --check`
are RED on this line - on drift the newest train carries: six functions over the CCN
target and fifteen file-length lines, in files this fix does not touch. The lists are
BYTE-IDENTICAL before and after this change (`logs/gate-complexity-0.log` vs
`logs/gate-complexity-1.log`: 6 items each; `logs/gate-filelength-0.log` vs
`logs/gate-filelength-1.log`: 15 items each), so nothing here is new.

The job's gate is `feature_check.py`, whose contract is exactly that: the complexity
and file-length rows PASS when no item they list is NEW compared with the base commit
in a file the branch changed. `logs/feature-check-2.log`: every row PASS, RESULT: PASS.
The drift sweep of `4d4c2249d` - 35 files, 14 new test sources, six extractions - is
NOT repeated here: it is what conflicted with the newest line, and the newest line
carries its own drift, which the merge train owns.

No gate limit, no baseline, no exemption and no test assertion was weakened or
weakened-by-relocation.

## Determinism

Both regressions use INJECTED timestamps and no wall-clock sleep:

- The engine-level test feeds 48 pulses at exactly 120 BPM through
  `MidiClock::handleInputMessage()` with synthetic timestamps
  (`MidiClock::nowNs() + 60000000000ULL` as the start), so the measured tempo is 120
  whatever the machine's speed. The stream starts a minute AHEAD of the monotonic
  clock because the tracker drops its lock when no pulse arrived for
  `LockTimeoutMs` (500 ms) and the age it measures is the distance from the LAST pulse
  to the audio period's own clock - a pulse from the future is never aged out
  (`src/core/MidiClockTracker.cpp:196-202`).
- The command-level test does the same, and its only remaining timing is the
  `QTRY_COMPARE_WITH_TIMEOUT(..., 1500)` bound, which is six polls' worth of the
  250 ms poll - it waits for the timer's callback, it does not depend on it.
- `ctest --repeat until-fail:5 -R '^MidiClock(Test|MenuTest)$'` passed five
  consecutive runs (`logs/test-repeat-1.log`, EXIT=0).

## Evidence

All logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-133-port-port.r1/logs`.

| Stage | Exact command (working directory) | EXIT | Result |
| --- | --- | ---: | --- |
| Build the two regression targets on the ORIGINAL code | `cmake --build build --target MidiClockTest MidiClockMenuTest -j12 > LOGS/build-red-1.log 2>&1` | 0 | Built. |
| Build after adding the `cleanup()` slot (still no product change) | `cmake --build build --target MidiClockMenuTest -j12 > LOGS/build-red-2.log 2>&1` | 0 | Built. |
| **RED** on the original code | `ctest -R '^MidiClock(Test\|MenuTest)$' --output-on-failure > LOGS/test-red-2.log 2>&1` (from `build/tests`) | **8** | 2/2 failed: `MidiClockTest` 20 passed / 1 failed - `slaveEnableTogglePreservesFollowTempoPreference() 'clock->slaveFollowTempo()' returned FALSE. (disabling the slave cleared its follow preference)`; `MidiClockMenuTest` 7 passed / 1 failed - `slaveSetRetainsFollowPreferenceAcrossDisable() Actual (song->getTempo()): 100, Expected (measuredTempo): 120`. |
| Build with the fix | `cmake --build build --target zene MidiClockTest MidiClockMenuTest -j12 > LOGS/build-green-1.log 2>&1` | 0 | Built. |
| **GREEN** | `ctest -R '^MidiClock(Test\|MenuTest)$' --output-on-failure > LOGS/test-green-1.log 2>&1` (from `build/tests`) | **0** | 2/2 passed. |
| Build the rest of the clock family | `cmake --build build --target MidiClockSourceTest -j12 > LOGS/build-green-2.log 2>&1`; `cmake --build build --target ScriptClockTest -j12 > LOGS/build-green-3.log 2>&1` | 0 / 0 | Built. |
| Affected family, final state | `ctest -R '^(MidiClockTest\|MidiClockSourceTest\|MidiClockMenuTest\|ControlClockCommands\|ScriptClockTest)$' --output-on-failure > LOGS/test-green-family2.log 2>&1` (from `build/tests`) | **0** | 5/5 passed. (An earlier run of the same family, `logs/test-green-family.log`, was EXIT=8 only because `ScriptClockTest` was not built yet - `***Not Run`, 0.00 sec - not a failure of a test.) |
| Stability of the deterministic regressions | `ctest --repeat until-fail:5 -R '^MidiClock(Test\|MenuTest)$' --output-on-failure > LOGS/test-repeat-1.log 2>&1` (from `build/tests`) | 0 | 5 consecutive runs, 2/2 passed each time. |
| Command-surface canary (no new command, no changed schema) | `ctest -R '^ControlCommandsSnapshot$' --output-on-failure > LOGS/test-snapshot-1.log 2>&1` (from `build/tests`) | 0 | 1/1 passed. |
| Reverse the PRODUCT hunks only | `git diff -- src/core/MidiClock.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch > LOGS/proof-1-applyrev.log 2>&1` | 0 / 0 | The patch was written and the reversal applied; the test files stayed unchanged. |
| Build the reversed product | `cmake --build build --target zene MidiClockTest MidiClockMenuTest -j12 > LOGS/proof-1-build.log 2>&1` | 0 | Built. |
| **RED** with the product hunks reversed | `ctest -R '^MidiClock(Test\|MenuTest)$' --output-on-failure > LOGS/proof-1-test.log 2>&1` (from `build/tests`) | **8** | 2/2 failed - the same two assertions as the RED run above. |
| Restore the product hunks | `git apply LOGS/fix.patch > LOGS/proof-2-apply.log 2>&1` | 0 | The product fix is back. |
| Build and rerun | `cmake --build build --target zene MidiClockTest MidiClockMenuTest -j12 > LOGS/proof-2-build.log 2>&1`; `ctest -R '^MidiClock(Test\|MenuTest)$' --output-on-failure > LOGS/proof-2-test.log 2>&1` (from `build/tests`) | 0 / **0** | 2/2 passed. |
| Job gate (all rows) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-133-port-port > LOGS/feature-check-2.log 2>&1` | **0** | RESULT: PASS - fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity (`no new item in your files`), file-length (`no new item in your files`). |
| Complexity ratchet, raw | `bash tests/complexity-gate.sh --check > LOGS/gate-complexity-1.log 2>&1` | **1** | RED on six items inherited from the train (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`) - none of them in a file this change touches; the list is identical to the pre-change run (`logs/gate-complexity-0.log`). |
| File-length ratchet, raw | `bash tests/file-length-gate.sh --check > LOGS/gate-filelength-1.log 2>&1` | **1** | RED on fifteen items inherited from the train (e.g. `TakeLane.cpp`, `DeviceDspTest.cpp`, `CollabTransportTest.cpp`) - none of them in a file this change touches; the list is identical to the pre-change run (`logs/gate-filelength-0.log`). `tests/src/core/MidiClockTest.cpp` sits at 500 code lines, AT the limit and not over it. |
| Registration gate | `bash tests/fork-sources-gate.sh > LOGS/gate-forksources-2.log 2>&1` | 0 | PASS: every tracked source in scope is registered, 0 stale entries. |

## Files committed

- `src/core/MidiClock.cpp` - the product fix (the preference store removed from the
  disable path; the follow timer restarted when it is inactive).
- `tests/src/core/MidiClockTest.cpp` - the engine-level regression.
- `tests/src/gui/MidiClockMenuTest.cpp` - the command-level regression and a `cleanup()`
  slot so a slot that stops early cannot leak a following slave into the next slot.
- `tests/fork-sources.txt` - the fork-NEW clock menu test is registered here (the
  accepted branch registered it in the same file; the newest line had lost the entry),
  and this report.
- `docs/qwen-fixes-2026-10/cluster-new-p12-133-port-port.md` - this report.

No header changed (no declaration changed), no new product file, no new test file, no
new command.

## Self-check

Every item the pre-submit check asks about, with the evidence.

1. **The regression test FAILS with your product change reverted and PASSES with it
   (exit codes).** `ctest -R '^MidiClock(Test|MenuTest)$' --output-on-failure` from
   `build/tests`: EXIT=8 (2/2 failed) with `git apply -R LOGS/fix.patch`
   (`logs/proof-1-test.log`), EXIT=0 (2/2 passed) after `git apply LOGS/fix.patch`
   (`logs/proof-2-test.log`). The same pair was RED before the fix was written at all
   (EXIT=8, `logs/test-red-2.log`) and GREEN after it (EXIT=0,
   `logs/test-green-1.log`). The failing assertions are named in the log:
   `'clock->slaveFollowTempo()' returned FALSE. (disabling the slave cleared its
   follow preference)` and `Actual (song->getTempo()): 100, Expected (measuredTempo):
   120`.
2. **The test is deterministic: no wall-clock timing, sleeps or races.** The pulses
   carry INJECTED timestamps with exact 120 BPM intervals, starting a minute ahead of
   the monotonic clock so the tracker's 500 ms lock timeout can never age them out
   (`src/core/MidiClockTracker.cpp:196-202`: the age is 0 for a pulse from the
   future). The engine-level test calls `applyMeasuredTempo()` directly; the
   command-level test waits with `QTRY_COMPARE_WITH_TIMEOUT(..., 1500)`, a bound of
   six polls (the poll runs every 250 ms), not a sleep that decides the result.
   `ctest --repeat until-fail:5` passed five consecutive runs (`logs/test-repeat-1.log`).
3. **The fix is at the cause and complete: every caller and sibling of what you
   changed was checked and stays consistent.** `setSlaveEnabled()` has exactly one
   product caller - `restoreSlaveState()` in `src/core/ControlCommandsClock.cpp:135-149`
   - which sets the follow flag explicitly right after it, so the command path is
   unchanged; the engine-level store was the hole. `setSlaveFollowTempo()` has the
   same single caller. `startFollowTimer()` / `stopFollowTimer()` have the same single
   caller, and the timer is created once on the control thread
   (`include/MidiClock.h:381-384`) and restarted from that same thread, so no new
   thread affinity is introduced. The GUI menu (`src/gui/MidiClockMenu.cpp:212-219`)
   sends `{enabled}` alone when the user stops following and
   `{enabled:true, follow_tempo:on}` when the user turns the tempo-follow on; both
   reach the same `restoreSlaveState()` and are covered by the command-level
   regression. The state surface (`src/core/MidiClockState.cpp:99`) reports the
   preserved preference, and the menu's "Follow Its Tempo" check mark therefore keeps
   the user's choice across a stop - which is the point of the finding.
4. **No collateral damage.** No saved data, project file or command schema changed
   (`ControlCommandsSnapshot` passes, EXIT=0), no new command was added, the undo
   step is the same one recorded before (the inverse calls the same
   `restoreSlaveState()` with the before-state's own flag), and nothing runs on the
   audio thread: the two changed functions are called from the control thread only
   (`MidiClock::setSlaveEnabled` and `MidiClock::startFollowTimer`, the latter the
   existing poll timer's start, not a new timer). No limit or check was added that
   another caller could hit. The change of behaviour is confined to the follower's
   configuration and to the poll that writes the measured tempo - which is what the
   finding asks for.
5. **Verdict evidence.** The finding is REAL, not ALREADY-FIXED: the store was
   present at `src/core/MidiClock.cpp:283` before this change
   (`git show HEAD:src/core/MidiClock.cpp` lines 278-289 contain
   `m_slaveFollowTempo.store(false);`), and the accepted fix's other half
   (`startFollowTimer()` returning early on an existing timer) was present at
   `src/core/MidiClock.cpp:420`. The command-path half of the accepted fix
   (`restoreSlaveState(enabled, follow, ...)`) was ALREADY on this line through
   `ed62a4c46`, which is why the command-level "preference survives a disable"
   assertion already passed before this change; the engine-level store was the hole.
