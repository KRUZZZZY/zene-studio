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

Acceptance, with the exact commands run from the worktree and the exits seen (`Q050-171.r1/logs/`):

| # | Criterion | Command | Exit |
|---|-----------|---------|------|
| 1 | The pre-M3.7 interface claim is clearly historical; current UI and remaining limits accurate | `docs/KNOWN-LIMITATIONS.md:270-355` reviewed against the tree (old text under "The pre-M3.7 record"; current text names the strip, channel menu, Groups list, Clip entry, and separates UI omissions from limits on every surface) | 0 |
| 1 | The current UI inventory is held by a test, not just prose | `cmake --build build --target VcaStripViewTest -- -j4` (`build-1.log`), `ctest -R VcaStripViewTest -V` (`ctest-2.log`) — 10/10, including `theGroupsListOffersCreateEditSetAndPhaseLock`, `theClipsMenuCarriesMoveWithEditGroup`, `newVcaGroupIsCreateThenAssignTwoUndoSteps` | 0 |
| 2 | Every referenced UI/command path matches the merged tree | `grep -n` / `sed -n` over `VcaStripView.cpp:106/:168`, `MixerView.cpp:403`, `MixerChannelView.cpp:347`, `CommandLists.cpp:59`, `ShellCommandMenus.cpp:327/:519/:526`, `InspectorPanel.cpp:383`, `Mixer.cpp:997/:762/:2504-2544/:2753-2825`, `ControlCommandsVca.cpp:468`, `ControlCommandsVcaShared.h:177/:311`, `ControlCommandsVcaMix.cpp:224`, `ScriptCommandBridge.cpp:69/:80/:272`, `MixerGroups.h:37`; live snapshot has 21 `vca.*` ids | 0 |
| 2 | Command behavior behind the stated limits still passes | `cmake --build build --target ControlVcaCommandsTest ControlVcaEditGroupsTest ControlVcaLockedEditsTest VcaGroupTest VcaStripLifetimeTest -- -j4` (`build-2.log`); `ctest -R "VcaGroupTest\|ControlVcaCommandsTest\|ControlVcaEditGroupsTest\|ControlVcaLockedEditsTest\|VcaStripLifetimeTest\|VcaStripViewTest\|ControlVcaCommands" --output-on-failure` (`ctest-3.log`) — 7/7 | 0 |
| 3 | Test efficacy: the new checks fail when their UI paths are removed | Temporarily removed the Clip menu's `vca.edit_move` line, the Groups list's edit-set action, and the create handler's `vca.assign`; `cmake --build build --target VcaStripViewTest -- -j4` (`build-mutation-1.log`), `ctest -R VcaStripViewTest -V` (`ctest-mutation-1.log`) — the four affected checks fail | 8 |
| 3 | Tests pass again with the paths restored | Rebuilt (`build-3.log`), `ctest -R VcaStripViewTest -V` (`ctest-4.log`) — 10/10 | 0 |
| 4 | Figures and gates unchanged | `python3 tests/doc-figures-gate.py` (`doc-figures-1.log`): 833 ids / 76 groups; `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py $PWD` (`feature-check-1.log`) — all eight rows PASS | 0 |

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
