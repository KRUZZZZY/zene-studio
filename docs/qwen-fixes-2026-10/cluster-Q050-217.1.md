IMPLEMENTED

# Q050-217.1 - MIDI bindings for built-in commands (stage 1/4)

Cluster Q050-217, stage 1 of 4: "Bind MIDI notes and CCs to built-in commands that require no arguments."
Built in `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-feat-q050-217.1`
on `feat/050-Q050-217.1-oc`, commit `6b499a9aa` (plus the doc/report commit).

The binding surface is the controller surface that already existed, extended rather than duplicated: one
MIDI port, one 15 ms UI-thread drain, the MIDI thread allocating nothing. A **second, additive** config store
(`midi/commandbindings`) holds command bindings; the launcher's `launcher/bindings` keeps its format and every
binding an old config holds. Three new commands on the agent surface and three new entries under
Launcher > Controller, both through `ControlRegistry`.

## Integration onto `050/m0-train-16` (2026-10-26, run r4)

Accepted patch `9603448074` (base `07d6d02bd`) three-way applied onto `cf3618e61`. Every code and test file
applied cleanly, so the product and test code is byte-identical to the accepted patch - **no semantic
difference**. The four conflicts were all derived figures or append-only registers, resolved without a design
change:

- `tests/upstream-modifications.txt` - both sides appended rows at the end; kept the train's rows, then this
  unit's six (the README row's figure updated to 849 -> 852).
- `README.md` canary - neither 849 (train) nor 848 (patch): re-measured from a live snapshot, **852 ids / 76 groups**.
- `tools/mcp-zene-control/zene_control/commands_snapshot.json` - not hand-merged: regenerated from a live
  instance of the integrated build (`snapshot_live.py`, exit 0: 852 commands, `0.4.0-alpha.4776+cf3618e`).
- `docs/RELEASE-NOTES-v0.3.0-alpha.md` - kept the train's history (trains 15/16, Q050-090, Q050-125), added this
  unit's paragraph, re-took the A16 block as `rows=861 true_inverse=427 snapshot=42 irreversible=24
  not_mutating=368` (train 16's `858/425/42/24/367` plus this unit's 2 `true_inverse` + 1 `not_mutating`);
  `ReversibilityContractTest` confirms it.

Affected failure paths re-checked on the integrated tree: MIDI decode -> queue -> UI-thread dispatch (note and
CC), the launcher's own bindings and their precedence, bind/replace/undo across the two config stores, the
Launcher > Controller menu entries, collab routing of the two editing commands, the command snapshot, the A16
histogram. Platform hazards: unchanged from the accepted patch (no new API; Qt 5.15 / MSVC / macOS < 10.15
reasoning below still holds; CI not run locally).

Evidence (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-217.1.r4/logs`):

| Step | Command | Exit |
| --- | --- | --- |
| Build | `cmake --build build --target zene MidiCommandBindingsTest MidiCommandBindingsGuiTest CollabPolicyTest ReversibilityContractTest -- -j4` (`build-2.log`; `build-1.log` failed only on a ccache temp-dir permission, fixed with `CCACHE_TEMPDIR`) | 0 for every target |
| Snapshot | `snapshot_live.py $PWD` (`snapshot-1.log`) | 0 |
| Doc figures | `python3 tests/doc-figures-gate.py` (`docfig-1.log`) | 0 |
| Red | dispatch `invoke` calls in `LaunchBindings.cpp` and the three Launcher > Controller entries in `RecordLaunchMenus.cpp` removed, tests rebuilt (`build-3-neg.log`, 0), `ctest -R '^(MidiCommandBindingsTest\|MidiCommandBindingsGuiTest)$'` (`ctest-2-neg.log`) | **8** (0% of 2 passed; 5 core slots and 3 GUI slots fail) |
| Green | restored, rebuilt (`build-4.log`, 0), `ctest -R '^(MidiCommandBindingsTest\|MidiCommandBindingsGuiTest\|RecordingPassTest\|PerformViewTest\|ReversibilityContractTest\|CollabPolicyTest\|ControlCommandsSnapshot)$'` (`ctest-3.log`) | **0** (100% of 7) |
| Gates | `feature_check.py` (`feature-check-2.log`; run from a /tmp copy whose only change is the log directory, because the stock script writes beside the worktree, outside this lane's write-jail) | 0 - all 8 rows PASS |

The acceptance table below is the accepted round's; every test it names was re-run green on the integrated tree
in the rows above (the GUI and core harnesses, `RecordingPassTest`, `PerformViewTest`, `CollabPolicyTest`,
`ReversibilityContractTest`, `ControlCommandsSnapshot`).

## Acceptance

| Criterion | Test (command as run from `build/tests`) | What it proves | Exit |
| --- | --- | --- | --- |
| 1. "Bind a note and a CC to a built-in command and verify one dispatch per activation in a synthetic MIDI harness." | `ctest -R '^MidiCommandBindingsTest$'` - `aBoundNoteDispatchesOncePerPress`, `aBoundControlChangeDispatchesOncePerSweep` (`tests/src/core/MidiCommandBindingsTest.cpp`) | Synthetic `MidiEvent`s go through the product's own `MidiSurfaceInput::processInEvent` -> `decode()` -> `MpscQueue` -> UI-thread `drainPending()`. A note press runs the bound command's handler exactly once; the release, and a velocity-0 note-on, run it none; the next press runs it once. A CC sweep 1/2/40/127 runs it exactly once; 0 runs it none; the next sweep runs it once; another controller and another channel run it none. The count is the command handler's own, i.e. dispatches through the registry. | **0** with the change, **8** reverted |
| 1 (channel rule; CC value not an argument) | `theChannelRuleIsTheLaunchers`, `theControlChangeValueIsNotPassedAsAnArgument` | -1 answers any channel, an exact channel only itself, a malformed channel (-2, absent) is skipped rather than read as "any". A command with an OPTIONAL argument is bindable and runs with an empty argument object - the CC value does not reach it. | **0** with the change, **8** reverted |
| 1 (the refusal that keeps it honest) | `aCommandThatNeedsArgumentsIsRefused` | `session.bind_command` on a command with a required argument, on an unknown command, and on a note binding without its key are each a typed `InvalidArgs` and store nothing. | **0** |
| 1 through the interface | `ctest -R '^MidiCommandBindingsGuiTest$'` - `theMenuBindsANoteThatThenRunsTheCommand` (`tests/src/gui/MidiCommandBindingsGuiTest.cpp`) | The **built** Launcher > Controller action is triggered, the real `CommandForm` it opens is filled and its Run pressed, the real store read back, and then live note-on / note-off through the connected surface add exactly one mixer channel, the release adding none. | **0** with the change, **8** reverted |
| 2. "Read back, replace, and undo a binding; existing launcher bindings still work." | `MidiCommandBindingsTest::aBindingReadsBackIsReplacedAndUndoes` | `session.command_bindings` reads the binding back field by field; a second bind on the same control leaves exactly one binding naming the new command; **one** `control.undo` restores the displaced one; the next undo removes the original. | **0** with the change, **8** reverted |
| 2 (unbind, typed miss) | `unbindRemovesOneAndUndoes` | One unbind removes only that control, one undo puts it back, and unbinding nothing is `NotFound`, not a silent success. | **0** |
| 2 (existing launcher bindings still work) | `theLaunchersBindingsStillWork`, `anExactCommandWinsAnEqualSpecificityLauncher`; `ctest -R '^RecordingPassTest$'` (`theLauncherIsBoundToNotes`, `learningANoteDoesNotReleaseAPlayingGateCell`, `aBindingWithAnInvalidChannelIsSkipped`); `ctest -R '^PerformViewTest$'` (`learnMidiBindsTheControllersNextNote`) | `session.bind`, `session.bind_learn`, `session.unbind` and launch-on-a-note behave as before. A channel-0 command leaves an any-channel launcher available on channel 1; an exact launcher beats a wildcard command; the synthetic equal-specificity case confirms an exact command wins. Moving a note from the launcher to a command and back is ONE undo step across both stores. | **0** |
| 2 through the interface | `MidiCommandBindingsGuiTest::theMenuReadsBackReplacesAndUndoesOneStep` | The list entry declares `session.command_bindings` and reads the binding back; the menu's form replaces it; **Edit > Undo**, opened and triggered as a user does, puts the displaced binding back in one step. | **0** with the change, **8** reverted |
| A16: classification and published histogram | `MidiCommandBindingsTest::theContractClassifiesTheNewCommands`; `ctest -R '^ReversibilityContractTest$'` | Explicit rows exist for all three new commands (`true_inverse`, `true_inverse`, `not_mutating`), and the live histogram equals the published block. | **0** / **0** |
| The bindings stay on the machine that made them | `ctest -R '^CollabPolicyTest$'` - `peerRouting` | `session.bind_command` / `session.unbind_command` route `Local` for an editor, like `session.bind` / `session.unbind`, so a peer's binding is never streamed to the host as a project edit | **0** |
| Snapshot and docs | `ctest -R '^ControlCommandsSnapshot$'`; `python3 tests/doc-figures-gate.py` | The committed snapshot equals the binary of this build; the README canary matches the snapshot. | **0** / **0** |

### Red / green, recorded

- **Green** (with the change): `ctest -R '^(MidiCommandBindingsTest|MidiCommandBindingsGuiTest)$'` -> exit **0**,
  "100% tests passed out of 2".
- **Red** (product change reverted, tests kept): the dispatcher's registry call and the two menu/command
  registrations removed, rebuilt, run -> `MidiCommandBindingsTest` exit **8** with 3 failing slots
  (`aBoundNoteDispatchesOncePerPress`, `aBoundControlChangeDispatchesOncePerSweep`, `theChannelRuleIsTheLaunchers`
  - every count 0 where 1 was expected), `MidiCommandBindingsGuiTest` exit **8** with
  `theMenuBindsANoteThatThenRunsTheCommand` failing (0 mixer channels added).
- **Restored**: exit **0** again, "100% tests passed out of 2".
- Full green run of the touched and neighbouring suites:
  `ctest -R '^(RecordingPassTest|ReversibilityContractTest|PerformViewTest|ControlCommandsSnapshot|CollabPolicyTest|MidiCommandBindingsTest|MidiCommandBindingsGuiTest)$'`
  -> exit **0**, "100% tests passed out of 7".
- Independent review added a regression assertion for a channel-specific command displacing a wildcard launcher.
  Against the submitted product code `ctest -R '^MidiCommandBindingsTest$'` exited **8** at the assertion
  that the wildcard binding should remain; after the small fix, rebuilding the touched targets exited **0** and
  the seven-test focused CTest run exited **0** (100% passed).

The builder's product-reversal evidence is under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-217.1.r1/negctl/`. The independent review's
build, CTest, feature-check, and manifest-reproduction logs are under that run's `logs/` directory.

## New commands

- `session.bind_command` - bind a note (`trigger:"note"`, `key` 0..127) or a control change (`trigger:"cc"`,
  `controller` 0..127) on `channel` 0..15 or -1 (any) to `command`, a registered command that requires no
  arguments. Replaces a binding on the same control, and takes the note off the launcher's store in the same
  edit. One undo step. `true_inverse`.
- `session.unbind_command` - remove that binding. One undo step. `true_inverse`.
- `session.command_bindings` - read them back, with whether a controller is connected. `not_mutating`.

GUI: **Launcher > Controller > Bind a Command to a Note or Control Change...**, **Unbind a Command...**,
**Command Bindings** - each action declares the command it runs, so the menu and an agent take the identical
path through `ControlRegistry`.

## Format changes

None to the project. Config only: a new `midi/commandbindings` array (a section nothing wrote before, so an
old config simply has no command bindings) and, only when a note moves between the two kinds of binding, the
existing `launcher/bindings`. Old projects load and render unchanged; a new binding round-trips as plain JSON.

## Files

Product: `include/LaunchBindings.h`, `src/core/LaunchBindings.cpp` (note + CC decode, the two stores, the
dispatch), `src/core/ControlCommandsLaunchBindings.cpp` (the three commands), `include/MidiSurfaceInput.h`
(`drainPending()`, the harness seam), `src/gui/RecordLaunchMenus.cpp` (the menu entries),
`src/core/ControlReversibilityTableDevices.cpp` (three rows),
`src/core/CollabPolicy.cpp` (the two editing commands are machine-local, as the launcher's are).
Tests: `tests/src/core/MidiCommandBindingsTest.cpp`, `tests/src/gui/MidiCommandBindingsGuiTest.cpp`,
`tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt`.
Derived/docs (as integrated onto train 16): `tools/mcp-zene-control/zene_control/commands_snapshot.json` (852 ids / 76 groups),
`README.md` canary 849 -> 852, `docs/RELEASE-NOTES-v0.3.0-alpha.md` A16 block
(`rows=861 true_inverse=427 snapshot=42 irreversible=24 not_mutating=368`), `data/manual/index.html`,
`docs/050-features/Q050-217.1.md`, `tests/upstream-modifications.txt`.

## Self-check

**Integration run r4 (2026-10-26), on commit `9cd0b8f7a`.** `submit_check.py $PWD Q050-217.1` exit 1
(`submit-check-1.log`): every row PASS except "gates (feature_check.py)", with an empty detail. The cause is the
lane's write-jail, not a gate: the stock `feature_check.py` exits 1 with `PermissionError` creating its log
directory `../zene-integrate-Q050-217.1-cf3618e61a-feature-check` beside the worktree (`feature-check-3-stock.log`).
The same script run from a /tmp copy with only the log directory moved into this run's `logs/` exits **0**, all
eight gate rows PASS, on the committed tree (`feature-check-4.log`). The "?" items, for the integrated tree:

- **Red/green:** the `Red` and `Green` rows of the integration table above - exit **8** reverted, **0** restored.
- **Each criterion proven:** criterion 1 - `MidiCommandBindingsTest` and `MidiCommandBindingsGuiTest`, exit 0;
  criterion 2 - the same two plus `RecordingPassTest` and `PerformViewTest` for the existing launcher bindings,
  exit 0 (`ctest-3.log`, 7/7).
- **One undo step, honest classes / realtime / projects / portability:** code is byte-identical to the accepted
  patch, so the answers below are unchanged; `ReversibilityContractTest` exit 0 on the re-taken histogram.

### Independent red/green confirmation on the integrated tree

**? Each new test FAILS with your product change reverted (reverse the non-test hunks, rebuild, run: exit
code?), then PASSES restored.**
Yes. Reverting the dispatch call in `LaunchBindings::applyTrigger` and the registrations in
`ControlCommandsLaunchBindings.cpp` / `RecordLaunchMenus.cpp` (tests and the new API kept, so the harness still
compiles): `MidiCommandBindingsTest` exit **8** with 9 failing slots and `MidiCommandBindingsGuiTest` exit
**8** with 3 failing slots. Restored and rebuilt: both exit **0**. A full revert of the header too cannot compile
the tests at all, which is why the reversal used is the behavioural one - the assertions exercise commands,
the store and the menu entries, all of which the reverted product lacks.

**? Every acceptance criterion is proven by a test you ran yourself (name the test and its exit code per
criterion).**
Yes - the table above, row by row, with the ctest names and exit codes; both criteria have a core-harness
proof and a GUI proof.

**? Each edit is ONE undo step, and every new or changed command has an honest reversibility class.**
Yes. `session.bind_command` writes both stores and records ONE `addUndoStep` whose undo restores both previous
arrays and whose redo writes both new ones; `session.unbind_command` records one step for one list.
`aBindingReadsBackIsReplacedAndUndoes`, `unbindRemovesOneAndUndoes` and `theLaunchersBindingsStillWork` drive
it (a second bind is undone by a single `control.undo`). Classes: `true_inverse`, `true_inverse`,
`not_mutating`, rows added and `ReversibilityContractTest` green with the histogram re-taken.

**? Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a
signal.**
Yes. The only MIDI-thread code touched is `LaunchBindings::decode()`: it fills a `LaunchTrigger` of four ints
and a bool and returns - no allocation, no lock, no I/O, no signal - and the existing `MpscQueue` carries it.
`ControlRegistry::invoke`, the config write, the undo step and the latch `QSet`s are all in `applyTrigger` /
`applyNote`, on the UI thread from the 15 ms timer. The diff touches no audio-path function (`AUDIO_HINT`
finds nothing in it), and `src/core/LaunchBindings.cpp` holds no audio state.

**? Old projects still load and render exactly as before; new data round-trips through save and load.**
Yes for projects: nothing was added to the project format, so every old project loads and renders unchanged.
Bindings are preferences, not project data (as the launcher's already were), so they round-trip through the
CONFIG file: `LaunchBindings::setCommandBindings` writes a compact JSON array and `commandBindings()` reads it
back - which the read-back/replace test covers, including a section an older config does not have.

**? Qt 5.15 / MSVC / macOS < 10.15 portability: no Qt-6-only API without a guard, no windows.h macro names, no
std::filesystem outside _WIN32.**
Yes. The additions use only `QJsonArray`/`QJsonObject`/`QSet`/`QString`, `std::floor` and `std::unique_ptr`;
no Qt 6 API (no `qsizetype` arithmetic assumed, no `QStringView`), no `windows.h` names, no
`std::filesystem`. `MidiSurfaceInput::drainPending()` is a plain inline member calling the existing private
`drain()`, so both Qt versions compile it identically. The build here is Qt 6.12; the Qt 5.15 path is
untouched by these calls.

**Other gates, and two that cannot pass in this checkout:**

- `tests/no-upstream-regression-gate.sh` exit **0**; `tests/scripted/check-namespace` exit **0**;
  `tests/no-tautology-gate.sh` exit **0**; `python3 tests/doc-figures-gate.py` exit **0**.
- `tests/complexity-gate.sh --check` exit 1 and `tests/file-length-gate.sh --check` exit 1 - both on
  pre-existing train regressions only: neither lists a line in any file this unit touched
  (`LaunchBindings.cpp` 324 lines, `ControlCommandsLaunchBindings.cpp` 328, `RecordLaunchMenus.cpp` 192,
  `MidiCommandBindingsTest.cpp` 452, `MidiCommandBindingsGuiTest.cpp` 238).
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py $PWD` exit **0** after the review
  fix: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology and
  doc-figures all pass; complexity and file-length report "no new item in your files".
- `tests/fork-sources-gate.sh` exit **0** after re-deriving `tests/all-sources.txt` with the recipe in its own
  header. It failed at the base, before this unit: train-12 commit `07d6d02bd` put 1,148 vendored
  `plugins/SfzPlayer/sfizz` paths back into the list while the recipe - and the manifest's own "vendored
  third-party trees are excluded on purpose" preamble - still excludes them, so the manifest did not reproduce
  from its own recipe and `all-sources-reproduce.sh` exited 1 on `HEAD:tests/all-sources.txt` alone. The
  committed list is now the recipe's byte-exact output (the two new fork-NEW test files are the only non-sfizz
  difference from the previous entries), recorded in `tests/upstream-modifications.txt` because the file is
  inherited.
- Local TSan does not run on this machine, so the thread-safety argument above is reasoning, not a measured
  race report: the queue is the existing single-producer/single-consumer `MpscQueue`, and every latch set is
  touched only on the UI thread.

**Out of scope, deliberately.** Stages 2-4 (workflows, scripts, workflow header buttons) are not built; no
choice here forces them to be undone - the store is an additive array of self-describing entries, the dispatch
is one registry call, and a later stage can pass an argument or add another trigger kind without changing the
format this stage writes.
