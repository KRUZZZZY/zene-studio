FIXED

# Cluster new-p8-42 — route changes do not mark the project modified

## Decision

REAL. The finding is confirmed: the four route-mutating handlers in `src/core/ControlCommandsMixerRoutes.cpp`
changed the mixer and recorded an undo step, but never marked the song modified. A created or removed send
therefore left `Song::isModified()` false, and closing the project without a manual save skipped the prompt.

## Evidence

- Before the fix, `src/core/ControlCommandsMixerRoutes.cpp` contained no `setModified` call at all. The handlers
  that change routes were `handleRouteWrite` (127), `handleSidechainWrite` (191), `removeSidechainSend` (251),
  `removeRegularSend` (300). `handleSendMany` (368) calls `handleRouteWrite` once per channel.
- Why the undo step does not mark the song: `control::addUndoStep` (`src/core/ControlReversibility.cpp:147-151`)
  forwards to `ProjectJournal::addJournalAction`, which pushes the checkpoint and does not call
  `Song::setModified` (`src/core/ProjectJournal.cpp:277-292`). Recording undo is not the same as dirtying the project.
- Sibling commands mark the song after a successful write, e.g. `src/core/ControlCommandsMixerStrip.cpp:151` and
  `src/core/ControlCommandsMixerTools.cpp:192`.
- Undo and redo already mark the song: `src/core/ProjectJournal.cpp:148` (undo) and `:198` (redo). Only the forward
  route mutations were missing the call.

## Fix

One `Engine::getSong()->setModified();` line after each successful mutation, in `src/core/ControlCommandsMixerRoutes.cpp`:

| Line | Function | Commands covered |
| --- | --- | --- |
| 156 | `handleRouteWrite` (after the refusal check) | `mixer.route_to`, `mixer.send_to`, `mixer.send_many` (via `handleRouteWrite`) |
| 222 | `handleSidechainWrite` (after the refusal check) | `mixer.sidechain_to` |
| 274 | `removeSidechainSend` (after `deleteSidechainSend`) | `mixer.route_remove` with `sidechain: true` |
| 338 | `removeRegularSend` (after `deleteChannelSend`) | `mixer.route_remove` |

Refused changes return before the call, so a refused change does not dirty the song (asserted by the test).
No new control command, no save-format change, no new limit. The call is on the control thread (command handler),
the same place as the sibling calls.

## Regression test

`tests/src/core/MixingRoutingTest.cpp:465` `routeChangesMarkTheProjectModified` — a new slot in the existing
`MixingRoutingTest` (already registered at `tests/CMakeLists.txt:761`), so no new file and no registration change.
It drives `mixer.route_to`, `mixer.send_to`, `mixer.route_remove`, `mixer.sidechain_to`, and sidechain
`mixer.route_remove` through `ControlRegistry::invoke`, checks `Song::isModified()` after each, and checks that a
refused `route_remove` leaves the flag clear. Between steps it resets the flag with `Song::noteProjectSaved(...)`
(the product's own way back to clean, used by `tests/src/gui/BrowserDragUrlTest.cpp`). No timing, sleeps or races.

Note on RED coverage: `QVERIFY` stops at the first failure, so the RED run reports only the first assertion
(`mixer.route_to`). The other assertions are proven by the same mechanism, not by separate RED runs.

## Commands and exit codes

`LOGS` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-42.r1/logs`. Build commands were run with
`CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p8-42` (see Environment notes).

| Step | Command (abridged) | Log | EXIT |
| --- | --- | --- | --- |
| RED build | `cmake --build build --target zene MixingRoutingTest -j4` (test edit, fix not yet applied) | `build-3.log` | 0 |
| **RED test** | `cd build/tests && ctest -R '^MixingRoutingTest$' --output-on-failure` | `test-red-1.log` | **8** |
| Fix applied | edit `ControlCommandsMixerRoutes.cpp` (4 lines) | — | — |
| GREEN build | `cmake --build build --target zene MixingRoutingTest -j4` | `build-4.log` | 0 |
| **GREEN test** | `cd build/tests && ctest -R '^MixingRoutingTest$' --output-on-failure` | `test-green-1.log` | **0** (1/1 passed) |
| 4b save fix | `git diff -- src/core/ControlCommandsMixerRoutes.cpp > $LOGS/fix.patch` | — | 0 |
| 4b revert | `git apply -R $LOGS/fix.patch` | — | 0 |
| 4b rebuild | `cmake --build build --target zene -j4` | `build-6.log` | 0 |
| 4b rebuild | `cmake --build build --target MixingRoutingTest -j4` | `build-7.log` | 0 |
| **4b RED (no fix)** | `cd build/tests && ctest -R '^MixingRoutingTest$' --output-on-failure` | `test-red-2-no-fix.log` | **8** (`FAIL! ... mixer.route_to created a send without marking the song modified`, `MixingRoutingTest.cpp(481)`) |
| 4b re-apply | `git apply $LOGS/fix.patch` | — | 0 |
| 4b rebuild | `cmake --build build --target zene -j4` | `build-8.log` | 0 |
| 4b rebuild | `cmake --build build --target MixingRoutingTest -j4` | `build-9.log` | 0 |
| **4b GREEN (fix)** | `cd build/tests && ctest -R '^MixingRoutingTest$' --output-on-failure` | `test-green-2-with-fix.log` | **0** |
| Family build | `cmake --build build --target <T> -j4` for each of the 28 family targets not yet built, one invocation per target | `build-t1..t28-*.log` | 0 for all 28 |
| **Family test** | `cd build/tests && ctest -R "Mixer\|Rout\|Send\|Sidechain\|Channel" -j2 --output-on-failure` | `test-family-2.log` | **0** (31/31 passed; 31 tests executed) |
| Gate: fork-sources | `bash tests/fork-sources-gate.sh` | `gate-fork-sources.log` | 0 |
| Gate: all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | `gate-all-sources-reproduce.log` | 0 |
| Gate: no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-regression.log` | 0 |
| Gate: check-namespace | `tests/scripted/check-namespace` | `gate-check-namespace.log` | 0 |
| Gate: no-tautology | `bash tests/no-tautology-gate.sh` | `gate-no-tautology.log` | 0 |
| Gate: doc-figures | `python3 tests/doc-figures-gate.py` | `gate-doc-figures.log` | 0 |
| Gate: complexity (ratchet) | `bash tests/complexity-gate.sh --check` | `gate-complexity.log` | 1 (raw; see note) |
| Gate: file-length (ratchet) | `bash tests/file-length-gate.sh --check` | `gate-file-length.log` | 1 (raw; see note) |
| `feature_check.py` | `python3 …/feature_check.py <worktree>` | — | could not run: PermissionError (see Environment notes) |

Gate note: the two ratchet scripts exit 1 because the train has many regressions, but none of the listed items are in
the two files this change touches (`ControlCommandsMixerRoutes.cpp`, `MixingRoutingTest.cpp`). The listed items are in
`WasmSandbox.cpp`, `ControlCommandsMixerTools.cpp`, `TakeLane.cpp`, and other files. Under feature_check's rule
("fail only on items new in your files") both rows pass. I did not get a `feature_check.py` table, because it could
not create its log folder (see below).

## Environment notes

- The default ccache temp directory (under `dsh/home/ccache-tmp-r6`) returned `Permission denied`, so the first
  two builds failed (`build-1.log`, `build-2.log`). `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p8-42` fixed it.
  `build-2.log` also shows the first compile error (`Song::setModified(bool)` is private); the test was changed to
  use `Song::noteProjectSaved` instead.
- `feature_check.py` writes its logs to `<worktree>/../<name>-feature-check`, which is outside the write-jail, so
  it failed with `PermissionError` (`gates-1.log`, from `feature_check.py:71`). I ran each of its
  gate commands directly with logs in `LOGS`, as recorded above.
- The build guard refuses several `--target` arguments in one call (`build-5.log`, EXIT 97), so the family targets
  were built one at a time.
- In the first family run (`test-family-1.log`, before the family binaries existed) 29 tests were "Not Run" and
  `ControlRoutingCommands` failed with `IndexError` at `tests/control-routing-commands.py:213` (`loaded[0]`, an
  empty catalogue list). The same binary passed on the rerun (`test-family-2.log`). I did not root-cause that
  first failure. It is a catalogue/plugin-dependent check, and the rerun was clean.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Yes. RED: `test-red-1.log`
  (EXIT 8) and `test-red-2-no-fix.log` (EXIT 8), both at `MixingRoutingTest.cpp:481`. GREEN: `test-green-1.log`
  and `test-green-2-with-fix.log` (EXIT 0). See the commands table above.
- **The test is deterministic.** Yes. No timing, sleeps, or races. The clean state is set by
  `Song::noteProjectSaved` (`src/core/Song.cpp:2663`), a public seam, and the sequence of commands is fixed.
- **The fix is at the cause and complete.** Yes. Every route-mutating handler in the file now marks the song:
  `handleRouteWrite` (covers `route_to`, `send_to`, and `send_many`, which calls it per channel),
  `handleSidechainWrite`, `removeSidechainSend`, `removeRegularSend`. The only other mutation path is undo/redo,
  which already marks (`ProjectJournal.cpp:148`, `:198`). Refused paths return before the call.
- **No collateral damage.** Yes. The change only sets the dirty flag. It does not change the save format, load
  paths, limits, or any command group. No new control command (the snapshot is not affected). The call is on the
  control thread, not the audio thread.
- **Verdict FIXED, not ALREADY-FIXED or FALSE-POSITIVE.** Not applicable: the code evidence above shows the defect is
  real and was fixed.
