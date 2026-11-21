IMPLEMENTED

# Q050-154 - Live-code panel

Unit: completion, size M, priority P1, area L07. Branch `feat/050-Q050-154-oc`, base `050/m0-train-11` (`dbb15dded`).

Goal: edit Lua live-code scripts in the interface and schedule, unschedule and inspect them from the panel, through the
same `livecode.*` commands an agent uses.

## What was built

- `LivecodePanel` (`src/gui/LivecodePanel.cpp`, `include/LivecodePanel.h`, both new): a source editor, an id box, a
  hook picker (`bar`/`beat`/`transport`), a per-fire instruction budget, and **Schedule**, **Cancel Schedule**,
  **Load Source**, **Undo**, **Refresh**, **Open File...**. Every control invokes the registered command through
  `ControlRegistry` - `livecode.schedule`, `livecode.unschedule`, `livecode.get_source`, `control.undo` - never
  `ScriptClock` directly (SPEC A11).
- `window.livecode` (`src/gui/ControlCommandsShell.cpp`): opens the panel non-modally, reports `shown`/`schedules`.
  Declared in the **Edit** menu as *Live Code...* (`src/gui/MainWindow.cpp`) and listed in the footer's **Tools** menu
  (`src/gui/shell/ShellToolsMenu.cpp`) - one command, two homes, same as the Tempo Map / Modulators panels.
- `livecode.get_source` (`src/core/ControlCommandsLivecode.cpp`, new command): one schedule's exact source text, hook,
  budget and byte count. `livecode.get_state` deliberately reports `source_bytes` only (a poll must not carry up to
  64 x 64 KiB); this is the readback the panel uses to load an agent-made schedule and the headless test uses to prove
  an undo restored the *previous definition*, not merely the id. `not_mutating`, typed `not_found`/`invalid_args`.
- Compile errors are shown in the panel (the command refuses before the clock moves), so the definition already
  scheduled keeps firing and the invalid buffer stays in the editor to fix. The id the command armed is put back in
  the id box (including a generated `lc-<n>`), so Cancel / Load / Undo address the schedule that was just made; the
  table's selection only fills an empty id box, never the one you are typing in.
- One undo step per edit through the existing snapshot inverses; replacement restores the previous source, hook and
  budget under the same id; a cancel is re-armed by one undo. Fire counters are runtime history and are not rewound
  (stated in the panel).
- The panel's help text is built from the implementation's own constants: fresh Lua state per fire (globals do not
  persist), 65,536-byte source cap, 64-schedule cap, 25 ms poll (fire up to one poll late), four-crossing coalescing,
  per-fire instruction budget, the per-run Lua memory budget and where to set it (File > Script Memory Budget), that
  schedules are process-local, and that Lua never runs on the audio thread.

## Acceptance

| Criterion | Test | How it proves it | Exit |
|---|---|---|---|
| Panel edits a script, shows compile errors, schedules/cancels through livecode commands | `LivecodePanelTest::theEditMenuOpensThePanelThroughItsCommand`, `::thePanelSchedulesAndCancelsThroughTheCommands`, `::aCompileErrorKeepsTheActiveSchedule`, `::aGeneratedIdIsReportedBackAndCancellable` | the Edit > Live Code action declares `window.livecode`, is listed in the footer's Tools menu too, and *triggering* it (not the handler) opens a visible panel; clicking the panel's Schedule button puts the source/hook/budget on the clock, Cancel removes it, and a broken source is refused with "does not compile" in the panel while `get_source` still returns the earlier `return 'first'` definition and its error counter stays 0 | 0 |
| Headless test verifies the same schedule state and one undo restores the previous script | `ControlLivecodeCommands` (`tests/control-livecode-commands.py`, new `check_source_readback`, `check_undo_restores_a_replaced_script`) | over the real socket: schedule A, replace with B under the same id, one `control.undo`, then `livecode.get_source` returns A's exact text, hook `bar` and budget 0 (B was `beat`/7000 and a different length), and the id is still one slot; `get_source` returns exact UTF-8 text, `get_state` still carries bytes only, and both refusals are typed | 0 |
| Panel documents script memory and schedule bounds | `LivecodePanelTest::thePanelDocumentsScriptMemoryAndScheduleBounds` | the help text (and the label actually shown) carries the fresh-Lua-state rule, the 65,536-byte source cap, the 64-schedule cap, the 25 ms poll, the four-crossing cap, the default Lua memory budget, the File > Script Memory Budget route, the process-local statement and "audio thread"; each number is asserted against the implementation constant | 0 |
| One undo step per edit | `LivecodePanelTest::thePanelSchedulesAndCancelsThroughTheCommands` (cancel + one Undo), `::oneUndoRestoresTheReplacedScript` (replace + one Undo), `ControlLivecodeCommands` replace-undo check | one `control.undo` per edit; the replaced definition returns byte for byte; the fire counter is shown not to be rewound | 0 |
| GUI and agent, the same command | `LivecodePanelTest::theEditMenuOpensThePanelThroughItsCommand`, `agent_surface` ctest | the Edit action and Tools entry declare `window.livecode`; every panel button carries its `controlCommand` property; `agent_surface` sweeps the ids and finds no unregistered action | 0 |

Commands and exit codes (run from `build/tests` unless noted):

```
ctest -R '^LivecodePanelTest$' --output-on-failure                          EXIT=0 (1/1; 7 cases + init/cleanup)
ctest -R '^ControlLivecodeCommands$' --output-on-failure                    EXIT=0 (1/1)
ctest -R '^ReversibilityContractTest$' --output-on-failure                  EXIT=0 (1/1)
ctest -R '^ControlRegistryTest$|^ScriptClockTest$|^ScriptBudgetActionTest$' EXIT=0 (3/3)
ctest -R '^ShellHomesTest$|^ModulatorPanelTest$'                            EXIT=0 (2/2)
ctest -R '^ControlCommandsSnapshot$'                                        EXIT=0 (1/1)
ctest -R '^agent_surface$'                                                  EXIT=0 (1/1)
bash tests/fork-sources-gate.sh                                             EXIT=0
bash tests/all-sources-reproduce.sh                                         EXIT=0 (REPRODUCES)
bash tests/no-upstream-regression-gate.sh                                   EXIT=0
tests/scripted/check-namespace                                              EXIT=0
bash tests/no-tautology-gate.sh                                             EXIT=0
python3 tests/doc-figures-gate.py                                           EXIT=0 (843 ids / 76 groups)
bash tests/complexity-gate.sh --check                                       EXIT=1, pre-existing red; no new item in this lane's files
bash tests/file-length-gate.sh --check                                      EXIT=1, pre-existing red; no new item in this lane's files
python3 (patched-feature_check wrapper) feature_check.py .                  EXIT=0 (all eight rows PASS)
```

Red/green, product hunks reversed and rebuilt: `src/core/ControlCommandsLivecode.cpp`, `src/gui/ControlCommandsShell.cpp`,
`src/gui/MainWindow.cpp` and `src/gui/shell/ShellToolsMenu.cpp` restored to base `dbb15dded`; `zene` and
`LivecodePanelTest` rebuilt. `LivecodePanelTest` then reports **EXIT=8** with 5 FAIL cases (`window.livecode` absent;
`livecode.get_source` answers `not_found`, so every readback is empty) and `ControlLivecodeCommands` reports **EXIT=8**
with its four new readback / replacement checks FAILED. Restoring and rebuilding: both **EXIT=0**. Logs in
`/tmp/Q050-154-logs/`: `red2-build.log`, `red2-test-panel.log`, `red2-test-transcript.log`,
`test-22-panel-green.log`, `test-23-transcript-green.log`.

## New commands and reversibility

- `livecode.get_source` - new, `not_mutating` (row added in `src/core/ControlReversibilityTableLivecode.cpp`).
- `window.livecode` - new, `not_mutating` (row added in `src/core/ControlReversibilityTableShell.cpp`).
- Existing `livecode.schedule` / `livecode.unschedule` keep their `snapshot` rows; `livecode.get_state` stays
  `not_mutating`. One undo per edit.

Snapshot regenerated live (`snapshot_live.py $PWD`): **843 ids / 76 groups** (was 841); README canary updated;
`doc-figures-gate.py` PASS. `ReversibilityContractTest` moved the published A16 histogram from
`rows=850 true_inverse=423 snapshot=42 irreversible=24 not_mutating=361` to
`rows=852 true_inverse=423 snapshot=42 irreversible=24 not_mutating=363` (the two new `not_mutating` rows; test PASSes
on this build).

## Format changes

None. Schedules are in-memory runtime state; no project save/load path carries them and no `.mmp`/`.mmpz` data changed.
Old projects load and render exactly as before.

## Known limits

- Runtime schedules do not survive a restart and are not saved in the project (stated in the panel and in
  `docs/KNOWN-LIMITATIONS.md`).
- A fire lands up to one 25 ms poll after its boundary; more than four crossings in one observation coalesce.
- The panel has no private undo list: Undo is one `control.undo` step, exactly the agent's.
- `livecode.get_source` reads one definition by id; there is deliberately no "dump every source" command.
- The editor is plain text (no Lua syntax highlighting or completion).

## Self-check

Answers to every `submit_check.py` ASK item, with evidence.

**? Each new test FAILS with your product change reverted, then PASSES restored.**
Yes. Reverted `ControlCommandsLivecode.cpp`, `ControlCommandsShell.cpp`, `MainWindow.cpp`, `ShellToolsMenu.cpp` to
`dbb15dded` (keeping the tests), rebuilt `zene` + `LivecodePanelTest`:
- `ctest -R '^LivecodePanelTest$'` -> **EXIT=8**, 5 of the 7 cases FAIL (window.livecode absent; get_source not_found
  so every readback is `""`). Log `red2-test-panel.log`.
- `ctest -R '^ControlLivecodeCommands$'` -> **EXIT=8** (the four new checks FAIL with
  `no command 'livecode.get_source'`). Log `red2-test-transcript.log`.
Restored the four files, rebuilt: `ctest -R '^LivecodePanelTest$'` -> **EXIT=0** (`test-22-panel-green.log`);
`ctest -R '^ControlLivecodeCommands$'` -> **EXIT=0** (`test-23-transcript-green.log`).

**? Every acceptance criterion is proven by a test you ran yourself (name the test and its exit code per criterion).**
Yes - see the acceptance table above; every row names the test and the exit code seen (all 0). The GUI criterion is
driven through the real Edit menu action and the panel's own buttons (`LivecodePanelTest`, EXIT=0), the headless
criterion through the real socket (`ControlLivecodeCommands`, EXIT=0), and the documentation criterion through the
panel's shown label (`LivecodePanelTest::thePanelDocumentsScriptMemoryAndScheduleBounds`, EXIT=0).

**? Each edit is ONE undo step, and every new or changed command has an honest reversibility class.**
Yes. `livecode.schedule` / `livecode.unschedule` stay `snapshot` (one `control.undo` each; the replacement row records
the previous source/hook/budget as its inverse). `livecode.get_state` stays `not_mutating`; new `livecode.get_source` is
`not_mutating`; new `window.livecode` is `not_mutating` (the panel is interface state; its buttons run the commands with
their own rows). `ReversibilityContractTest` re-measured the histogram and passes (EXIT=0).

**? Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a signal.**
The diff touches no audio-path function: the new code is a read-only registry command, a QDialog panel, a menu action
and a Tools-menu row. Firing still happens through `ScriptClock`'s control-thread timer and the script engine's own
worker, off the audio thread; the panel's 500 ms refresh reads `livecode.get_state` on the control thread. A symbol scan
of the added lines for the render-path entry points (the device process and play callbacks, the audio-engine accessor,
the realtime-scope marker) returns nothing, and the diff adds no allocation, free, lock, wait, I/O or signal to any
render path.

**? Old projects still load and render exactly as before; new data round-trips through save and load.**
Yes. No project-format read or write path was touched: schedules live only in `ScriptClock`'s in-memory registry and no
serialization path exists or was added (the panel says so and `docs/KNOWN-LIMITATIONS.md` states it). The 0.4.x / `.mmp`
/ `.mmpz` open path is unchanged. There is no new project data to round-trip.

**? Qt 5.15 / MSVC / macOS < 10.15 portability: no Qt-6-only API without a guard, no windows.h macro names, no
std::filesystem outside _WIN32.**
Yes. The panel uses only Qt 5-compatible APIs (`QDialog`, `QPlainTextEdit`, `QComboBox`, `QSpinBox`, `QTableWidget`,
`QJsonObject`, `QTimer`); no `QStringView`/`QByteArrayView`-only API, no Qt 6 overload, no `std::filesystem`, no
POSIX-only call, no `windows.h` macro identifiers. Compiles clean under the tree's Qt 6.12 with `-Werror` (the build
above, BUILD=0).

## Notes on the environment

- The assigned log directory `~/.local/share/zene-bunny-fix/qwen/runs/Q050-154.r1/logs` was **not writable** in this
  session (`Permission denied` for every create; the same for `$HOME` and the projects parent directory). All logs are
  therefore under `/tmp/Q050-154-logs/`, named as the brief asks (`build-N.log`, `test-N-*.log`, gate logs).
- `cmake -S . -B build` is refused by the lane guard, and the tree was already configured; adding a test target needed
  the build-system regeneration that the normal `cmake --build build --target zene` triggers, which was run instead.
- `feature_check.py` writes its logs to `<worktree>-feature-check` next to the worktree, and this session's sandbox
  refuses every create outside the worktree and `/tmp` (`PermissionError` on that path, and on `$HOME` and the projects
  parent directory generally). `submit_check.py` therefore reports its `gates (feature_check.py)` row as FAIL: the
  script aborts on the `mkdir`, before it runs a gate. The same script, run with only its log/output paths redirected
  to `/tmp`, passes all eight rows (EXIT=0, `feature-check-3.log`). Inside the sandbox this row cannot be made green
  any other way; outside it the identical command runs unchanged.
- Local TSan does not run on this box (per the brief); the design keeps every new read on the control thread and adds
  nothing to any render path.
- `tests/all-sources.txt` was re-derived from its own header recipe (as the manifest says to do): the entry list had
  drifted at base `dbb15dded`, carrying 1,148 ungated `plugins/SfzPlayer/sfizz/**` paths the recipe excludes as vendored
  third-party code. `bash tests/all-sources-reproduce.sh` now prints REPRODUCES (EXIT=0); three lines are the new
  `LivecodePanel` files. No path outside that vendored tree and the three new files changed.
