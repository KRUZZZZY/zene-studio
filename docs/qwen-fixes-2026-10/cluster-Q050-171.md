IMPLEMENTED

# Cluster Q050-171 — VCA/edit-group limitation notes vs the merged tree

Docs unit, L04, member B-ED-034. The VCA bullet in `docs/KNOWN-LIMITATIONS.md` still carried
the pre-M3.7 story ("nothing in the interface creates a group, names one, assigns a member,
locks it or edits through it"), named fourteen `vca.*` ids, described one propagated media edit,
and cited `Mixer.cpp` lines stale by the next renumber. The merged tree has the VCA strip, the
channel's **VCA group** menu, **Project tools ▸ Groups...**, **Colour a Group's Channels...**
and the Clip menu's **Move With Edit Group...**. The bullet is rewritten from the tree, the old
claim stays as dated history, the two reviewed inaccuracies are corrected (New VCA group is
`vca.create` then `vca.assign`, two undo steps; clip-local split/fade/delete/gain/crossfade
affect only the addressed clip), and the neighbouring "no interface / no Lua" notes carry dated
corrections. Docs and test comments only: no command, schema, format or audio-path change.

Acceptance, with the exact commands run from the worktree and the exits seen (round-3 logs in
`Q050-171.r3/logs/`):

| # | Criterion | Command | Exit |
|---|-----------|---------|------|
| 1 | The pre-M3.7 interface claim is clearly historical; current UI and remaining limits are accurate. `docs/KNOWN-LIMITATIONS.md:270-355` reviewed against the tree, and the stated UI held by DRIVING it: `VcaStripViewTest::theGroupsListOffersCreateEditSetAndPhaseLock` triggers Project tools ▸ Groups..., then through the opened dialog runs New Group (`vca.create` form), Rename, Add/Remove Track to Edit Set (forms), Phase Lock Off/On, reads state back and undoes with `control.undo`; `::theClipsMenuCarriesMoveWithEditGroup` submits the Clip menu's Move With Edit Group... form (`moved_count` 2, `delta` 500, both clips at 1500, one undo restores both); `::newVcaGroupIsCreateThenAssignTwoUndoSteps` holds the two-invocation create | `cmake --build build --target VcaStripViewTest -- -j4` (`build-3.log`), `ctest -R VcaStripViewTest --output-on-failure` (`ctest-2.log`) — all nine test functions + init/cleanup pass | 0 |
| 2 | The referenced UI and command paths match the merged tree. Every cited file:line re-read (`VcaStripView.cpp:106/:168`, `MixerView.cpp:403`, `MixerChannelView.cpp:347`, `CommandLists.cpp:59`, `ShellCommandMenus.cpp:327/:519/:526`, `InspectorPanel.cpp:383`, `Mixer.cpp:997/:762/:2504-2544/:2753-2825`, `ControlCommandsVca.cpp:468`, `ControlCommandsVcaShared.h:177/:311`, `ControlCommandsVcaMix.cpp:224`, `ScriptCommandBridge.cpp:69/:80/:272`, `MixerGroups.h:37`); command behavior behind the stated limits passes; figures unchanged (833 ids / 76 groups) | `cmake --build build --target zene VcaStripViewTest ControlVcaCommandsTest ControlVcaEditGroupsTest ControlVcaLockedEditsTest VcaGroupTest VcaStripLifetimeTest -- -j4` (`build-4.log`); `ctest -R "VcaGroupTest\|ControlVcaCommandsTest\|ControlVcaEditGroupsTest\|ControlVcaLockedEditsTest\|VcaStripLifetimeTest\|VcaStripViewTest\|ControlVcaCommands" --output-on-failure` (`ctest-3.log`) — 7/7; `python3 tests/doc-figures-gate.py` (`doc-figures-2.log`) | 0 |
| 3 | Test efficacy: the driven tests fail when their UI paths are broken (Phase Lock Off sends `locked: true`, Add Track points at `vca.track_remove`, the Clip line points at `clip.move`) | `cmake --build build --target VcaStripViewTest -- -j4` (`build-mut.log`), `ctest -R VcaStripViewTest --output-on-failure` (`ctest-mut.log`) — the Groups-list and Clip-menu tests fail | 8 |
| 3 | Same tests pass again with the paths restored | rebuilt (`build-3.log` precedes; restored tree rebuilt in `build-4.log`), `ctest -R VcaStripViewTest` (`ctest-3.log`) | 0 |

## Round-3 rework (review of round 2)

The reviewer found the Groups-list and Clip-menu tests only inspected configuration. They now
drive it: the Groups list is opened from its Project-menu entry and each action runs through the
dialog (New Group, Rename, Add / Remove Track to Edit Set, Phase Lock On / Off - forms filled
and submitted), and the Clip menu's Move With Edit Group... form is triggered from the real
menu, refused empty, then submitted with a group and position. State is read back (group name,
edit set, phase lock, clip positions, `moved_count`, `delta`) and each edit is undone with
`control.undo`, the registered command agents use. The feature report now has exactly one row
per criterion. Also fixed a latent use-after-free in `aStripWhoseGroupIsRemovedRetiresAtOnce`
(a `QPointer` rebuilt from a raw pointer after `deleteLater()` ran) that the new tests' longer
event-loop turns turned into a segfault (first run, `ctest-1.log`, exit 8).

## Round-1 rework

The reviewer found the blanket "each command/menu line is one undo step" overstated: `vca.list`
is read-only, **Groups...** opens a list, and **Move With Edit Group...** opens a form. The note
and both reports now say each **submitted mutating** UI action is one recorded step, name the
mutating actions, and state that the launchers themselves add no step (the list's `vca.list`
query writes nothing); **New VCA group** stays the two-command, two-step exception. Re-ran after
the edit: build exit 0 (`build-4.log`), the VCA selection 7/7 exit 0 (`ctest-5.log`),
`doc-figures` exit 0 (`doc-figures-3.log`), `git diff --check` exit 0 (`diff-check-2.log`), and
`feature_check.py` all rows PASS (`feature-check-2.log`).

Changed: `docs/KNOWN-LIMITATIONS.md`, `docs/050-features/Q050-171.md`,
`docs/VCA-EDIT-GROUPS.md`, `docs/VCA-GROUPS.md`, `docs/STATUS.md`, `data/manual/index.html`,
`include/CommandLists.h` (comment), `tests/CMakeLists.txt` (comment),
`tests/src/gui/VcaStripViewTest.cpp`, this report.

## Self-check

- **Each new test FAILS with your product change reverted, then PASSES restored.** This unit
  changes no product behavior; the non-test hunks are prose plus one header comment, so a literal
  reverse does not affect the tests. Efficacy was proven instead by removing the UI paths the
  tests assert: Clip menu `vca.edit_move`, the Groups list's `vca.track_add` action, and the
  create handler's `vca.assign` — `ctest` exit **8** (`ctest-mutation-1.log`), with
  `theChannelMenuEditsMembershipThroughTheCommands`,
  `newVcaGroupIsCreateThenAssignTwoUndoSteps`,
  `theGroupsListOffersCreateEditSetAndPhaseLock` and
  `theClipsMenuCarriesMoveWithEditGroup` failing; restored, exit **0** (`ctest-4.log`).
- **Every acceptance criterion is proven by a test I ran.** Criterion 1: `VcaStripViewTest`
  10/10 (`ctest-2.log`, `ctest-4.log`) holds the menus the text names and the two-undo create;
  criterion 2: the citation re-read plus `ControlVca*`/`VcaGroup*` 7/7 (`ctest-3.log`) and
  `doc-figures` exit 0. Both tables above name the commands.
- **One undo step per edit and honest reversibility.** No command was added or changed. The
  corrected text states the existing behavior: each **submitted mutating** UI action is one
  recorded step, and **New VCA group** is two registered commands (`vca.create`, `vca.assign`) =
  two undo steps; `newVcaGroupIsCreateThenAssignTwoUndoSteps` proves the order (first undo removes
  the assignment, second the group). The launchers do not edit by themselves: **Groups...** opens
  a list whose read-only `vca.list` makes no undo step, and **Move With Edit Group...** (like
  **Colour a Group's Channels...**) opens a form that becomes one step when submitted.
- **Nothing added runs on the audio thread.** The diff touches no audio-path code: it is
  Markdown/HTML, one header comment, test CMake comments and a QtTest. No allocation, lock, wait,
  I/O or signal was added anywhere, on the audio thread or off it.
- **Old projects still load and render as before; new data round-trips.** No serialization or
  engine code was touched. The note's save/load claim was re-read at `Mixer.cpp:2504-2544` /
  `:2753-2825` (a pre-edit-half `<vcagroup>` still loads as locked with an empty set), and
  `ControlVcaEditGroupsTest` / `VcaGroupTest` pass unchanged.
- **Qt 5.15 / MSVC / macOS / windows-arm64 portability.** No product code changed; the test uses
  QtTest and existing includes only, no Qt-6-only API, no `std::filesystem`, no windows macro
  names, no POSIX-only calls.
- **GUI and agent, the same command.** All actions the note lists go through
  `ControlRegistry` (`vca.assign`/`unassign`/`create`, `vca.set_links`, `vca.set_parent`,
  `vca.rename`/`remove`/`track_add`/`track_remove`/`set_phase_lock`, `vca.color_members`,
  `vca.edit_move`); the tests assert the entries and the registry ids. No new action was added, so
  no new GUI/agent pair was needed.

## Round-3 self-check

- **Gates.** `feature_check.py` cannot run in this jail (it writes its logs to a sibling directory
  outside the worktree: `PermissionError`), so its gates were run by hand, logs in
  `Q050-171.r3/logs/gate-*.log`: `fork-sources-gate.sh`, `all-sources-reproduce.sh`,
  `no-upstream-regression-gate.sh`, `scripted/check-namespace`, `no-tautology-gate.sh`,
  `doc-figures-gate.py` — all exit 0. `complexity-gate.sh --check` and `file-length-gate.sh
  --check` exit 1 (red on the train on purpose); no `REGRESSION` line names `VcaStripViewTest.cpp`
  or any other file this branch changed.
- **Test efficacy.** First round-3 run, before the `QPointer` fix: exit 8 (`ctest-1.log`, the
  segfault in the pre-existing strip test). With the three UI lines mutated: exit 8
  (`ctest-mut.log`); restored: exit 0 (`ctest-2.log`, `ctest-3.log`).
- **submit_check WARNs** ("no GUI file changed", "no command file changed") are expected: this is a
  docs unit; the GUI paths it describes already exist and are now driven by `VcaStripViewTest`.
- Undo steps, realtime, projects and portability: unchanged from the section above; this round
  touched only a QtTest file and Markdown, using `QtTest`, `QApplication`, `QMenu` and existing
  headers only (no Qt-6-only API, no `std::filesystem`).
