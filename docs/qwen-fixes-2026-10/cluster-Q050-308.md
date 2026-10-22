IMPLEMENTED

Both acceptance criteria are met and proven by checks I ran in this worktree (branch
`feat/050-Q050-308-oc`, base `b9ab28248`, train 050/m0-train-12). The unit is docs-only: no command,
menu, undo class, snapshot, canary figure or project format changed.

Notes on paths. The queue's `KNOWN-LIMITATIONS.md:918` pointer is stale in this checkout; the stale
sentence had moved to `:1020` and sits among the MIDI-clock bullets at `:1015`-`:1064`. The drift-bound
form is **Project tools ▸ Sync ▸ Follow MIDI Clock...**, NOT an Edit-menu field; the Edit menu's
**Clock Source** picker carries `source_port` only. Both facts are stated in the corrected text.

| Criterion | Command / test | What I saw | EXIT |
|---|---|---|---|
| `KNOWN-LIMITATIONS.md` describes the Edit ▸ MIDI Clock forms for selecting a port and drift bound and removes the stale socket-only claim | `ctest -R '^MidiClockDocsGate$' --output-on-failure` from `build/tests` (`test-docs-2.log`) | Passed. The MIDI-clock region names **Edit ▸ MIDI Clock ▸ Clock Source**, **Project tools ▸ Sync ▸ Follow MIDI Clock...**, `source_port`, `drift_bound_ms`, `0`/`100`/`MaxDriftBoundMs`, `clock.get_state` and undo; and the three old sentences are asserted absent | 0 |
| A documentation search finds no remaining statement that MIDI Clock source-port or drift-bound selection is socket-only | same ctest, its sweep of every tracked `.md`/`.html` | Passed, zero unmarked hits. Historical hits are accepted only on a dated superseded/pre-menu marker; a bare "still socket-only" is never excused. Reverting the four docs makes the same sweep name four unmarked claims (below) | 0 |
| The documented Sync route is a real GUI path reaching the engine (Definition of done 2) | `ctest -R '^LibraryMenusTest$' --output-on-failure` from `build/tests` (`test-docs-2.log`) | `theSyncMenuOpensTheFollowClockForm` triggers the menu action, gets a `CommandForm` with `source_port`/`drift_bound_ms`/`enabled`, sets `source_port` = "128:0 Sequencer:Ableton Push" and `drift_bound_ms` = 40, accepts, and `clock.get_state` reports both back; one `control.undo` then reports source `""` and bound `10.0` (`MidiClock::DefaultDriftBoundMs`). 17 passed / 0 failed | 0 |
| The gate is a real red/green control (not tautological) | the four docs restored to the base commit's text, then `python3 tests/midi-clock-docs-gate.py`; then restore and rerun | Reverted: **19 FAIL lines** / EXIT 1, naming "Choosing the port and the drift bound is still socket-only", "MIDI clock is the engine and the socket, and there is no interface for it", "nothing in `src/gui/` offers a clock port selector", the 0.3.0-alpha "not from the interface" bullet, the missing region and the missing manual paragraph. Restored: `RESULT: PASS`, 0 FAIL | 1 then 0 |
| The gate's own four mutations fail it | `ctest -R '^MidiClockDocsGateSelfTest$'` (`test-docs-2.log`) / `python3 tests/midi-clock-docs-gate.py --self-test` | `SELF-TEST PASS`: stale sentence back, Sync route dropped, snapshot bound widened to 1000, manual paragraph gone - each exit 1 naming the lost claim | 0 |
| Existing clock GUI test stays green | `ctest -R '^(MidiClockDocsGate|MidiClockDocsGateSelfTest|LibraryMenusTest|MidiClockMenuTest)$'` from `build/tests` (`test-docs-2.log`) | 4/4 passed, 0 failed | 0 |
| Build | `cmake --build build --target zene LibraryMenusTest MidiClockMenuTest -- -j4` (`build-2.log`, `build-3.log`) | linked clean | 0 |
| Gate 1 / 9 registration and scope | `bash tests/fork-sources-gate.sh` | "every tracked source in scope is registered (1839 fork-NEW, 1212 inherited, 48 tooling), 0 stale"; `all-sources.txt` REPRODUCES | 0 |
| Gate 6 upstream divergence | `bash tests/no-upstream-regression-gate.sh` | "1523 changed path(s) declared; ledger holds 2318 entries" | 0 |
| Gate 3 no tautology | `bash tests/no-tautology-gate.sh` | no violation | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 errors | 0 |
| Doc figures | `python3 tests/doc-figures-gate.py` | "snapshot measures 833 ids / 76 groups", README canary matches | 0 |
| Gates 4 / 7 ratchets | `bash tests/complexity-gate.sh --check`, `bash tests/file-length-gate.sh --check` | both red on the train BY DESIGN; neither lists a file I touched (`feature_check.py`: "no new item in your files") | 1 / 1 (pre-existing) |
| All gates in one table | `python3 feature_check.py <worktree>` (log dir rerouted to `/tmp` because the workspace parent is not writable in this sandbox) | every row PASS, `RESULT: PASS` | 0 |

Logs. The requested log directory `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-308.r1/logs`
is **not writable** in this session (mkdir/touch both return EACCES although the directory is owned by the
user; the parent `.../Q050-308.r1` is equally refused), so the same numbered logs were written to
`/home/kruzzzzy/.cache/zene-q050-308-logs/` (`build-1..3.log`, `test-docs-1.log`, `test-docs-2.log`,
`gate-reverted.log`, `data/figures/gates logs`). No claim above depends on a path I could not write.

What changed: `docs/KNOWN-LIMITATIONS.md` (Edit-menu bullet corrected; a new bullet documents the Sync
`clock.slave_set` form and its 0-100 ms integer bound; a dated correction bullet covers the old
"no interface" paragraph, whose title and closing line are also corrected), `docs/FEATURE-LIST-0.3.0.md`
row 74, the `docs/RELEASE-NOTES-v0.3.0-alpha.md` 0.3.0-alpha bullet (marked superseded, measurement kept),
`data/manual/index.html` (new MIDI Clock paragraph), the new gate `tests/midi-clock-docs-gate.py` plus its
two ctest registrations and `tests/fork-sources.txt` entry, the new
`tests/src/gui/LibraryMenusTest.cpp::theSyncMenuOpensTheFollowClockForm`, and the feature record
`docs/050-features/Q050-308.md`.

## Self-check

- **? Each new test FAILS with the product change reverted (reverse the non-test hunks, rebuild, run:
  exit code?), then PASSES restored.** The product change here is documentation, so the "non-test hunks"
  are the four doc edits (the gate and the GUI test are the tests). `git show <base>:<path>` over those
  four files (that is, the pre-change text), then `python3 tests/midi-clock-docs-gate.py` -> **EXIT 1, 19 FAIL lines** naming the stale
  sentences and the lost routes (`gate-reverted.log`). Restoring the four files -> **EXIT 0**
  (`RESULT: PASS`). The GUI test is unaffected by the docs revert (it tests the product's menu path,
  which is unchanged); the gate is the acceptance proof for this docs unit, and it is red without the
  change.
- **? Every acceptance criterion is proven by a test I ran myself (name the test and its exit code per
  criterion).** Criterion 1: `MidiClockDocsGate` EXIT 0 (region/route/bound checks, and the old sentences
  absent). Criterion 2: the same ctest's documentation sweep EXIT 0 on the corrected tree and EXIT 1 with
  four unmarked claims when reverted. The GUI half of the documented route: `LibraryMenusTest` EXIT 0,
  slot `theSyncMenuOpensTheFollowClockForm` observed `PASS` in the binary's own output (17 passed).
- **? Each edit is ONE undo step, and every new or changed command has an honest reversibility class.**
  No command was added or changed. `clock.slave_set` remains `snapshot`
  (`src/core/ControlReversibilityTableSnapshot.cpp:310`), and its inverse restores mode, `follow_tempo`,
  `source_port` and `drift_bound_ms` in one `addUndoStep`
  (`src/core/ControlCommandsClock.cpp:191`-`:201`). The new KNOWN-LIMITATIONS bullet states exactly that.
  The GUI test proves one form Run is one `control.undo` step: after undo the source is `""` and the
  bound is back to `10.0`.
- **? Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a
  signal.** The diff touches no audio-path code at all: four Markdown/HTML documents, one Python
  file-reading gate, one QtTest file, two test-registration files. `tests/rt-safety-sweep.py`'s scope is
  untouched (no `src/` file changed).
- **? Old projects still load and render exactly as before; new data round-trips through save and load.**
  No project format, serialisation, model or render code changed; the diff is documentation plus a
  read-only gate and one GUI test. `git diff --stat` shows no `src/` or `include/` file.
- **? Qt 5.15 / MSVC / macOS < 10.15 portability.** The only C++ added is a QtTest slot using
  `QApplication::topLevelWidgets()`, `qobject_cast`, `QAction::trigger`, `QJsonObject` and `QCOMPARE` -
  all Qt 5.15-and-earlier APIs with no Qt-6-only call and no guard needed. No `windows.h` macro names, no
  `std::filesystem`. The gate is plain Python 3 with `shutil.copy2` (no symlinks, so it runs on the
  Windows ARM/MSVC ctest jobs, as its Q050-014/035 neighbours do).
- **? Sibling commands in the command files you changed.** No command file was changed, so there is no
  sibling set to reconcile; `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py` prints the same
  WARN for that ("no command file changed - is the action on the agent surface?"), which is the expected
  answer for a docs unit: the agent surface already carries `clock.slave_set` (snapshot entry verified
  in the committed snapshot with `source_port` as a string and `drift_bound_ms` as an integer 0..100).
- **? If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract
  that shows it.** Not applicable - the verdict is IMPLEMENTED, and the stale claims were real in the base
  tree (reproduced by the reverted run above).

Extra self-check, the submit script's mechanical rows (run with the log directory rerouted, because the
default log path is not writable here): commits on the branch PASS, working tree clean PASS (checked
after committing), report verdict PASS, feature record `docs/050-features/Q050-308.md` PASS, acceptance
table covers both criteria PASS, GUI path PASS (`tests/src/gui/LibraryMenusTest.cpp`), GUI test PASS,
user manual PASS, tests added/changed PASS, gates PASS.
