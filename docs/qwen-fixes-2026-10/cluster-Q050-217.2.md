IMPLEMENTED

# Q050-217.2 - MIDI bindings for saved workflows (stage 2/4)

## Integration onto 050/m0-train-24 (round 2, 2026-10-30)

Accepted patch `feat/050-Q050-217.2-oc @ 039b15c8ac` (source base `4ee5321ffc`) applied by the driver onto
`050/m0-train-24 @ 2a7fd9f9c7` in `zene-integrate-Q050-217.2-2a7fd9f9c7`. Logs:
`~/.local/share/zene-bunny-fix/qwen/runs/Q050-217.2.r2/logs/`.

**Conflicts and how they were resolved**

- `tests/upstream-modifications.txt` - both sides appended rows at the end of the file; kept both (the train's
  rows, then this unit's three rows). `fork-sources` and `all-sources-reproduce` pass (below).
- `tools/mcp-zene-control/zene_control/commands_snapshot.json` - only metadata conflicted (`captured_at`,
  `source_socket`, `version`); the schema/description hunks merged cleanly. Regenerated from a live instance of
  this build with `snapshot_live.py` (`snapshot-1.log`, EXIT=0): **878 ids / 77 groups**, unchanged, and the
  regenerated file differs from the merged one only in those metadata lines.

**The regression the bisect named (`agent_surface`) - cause and fix**

`ctest -R '^agent_surface$'` on the merged tree: **exit 8** (`test-2.log`): `FAIL: NEW action without a registered
command: 'menu:Launcher/Controller/Bind a Workflow/No Workflows Yet (Workflows > Record a Workflow...)'`. The
gate's sweep runs `window.command_palette`, whose menu walk (`src/gui/CommandPalette.cpp:81`) fires `aboutToShow`
on every menu. That fills the patch's lazily built **Bind a Workflow** submenu, and with no workflows in the
fixture its only entry is the disabled placeholder, which declared no command. The fix
(`src/gui/RecordLaunchMenus.cpp`, `fillWorkflowBinding`): the placeholder sets `controlCommand` to `workflow.list`,
the command whose empty answer it reports. This is the same pattern `MacroMenu::addStatusLine`
(`src/gui/MacroMenu.cpp:50-56`) uses for the Workflows menu's status lines. No baseline, allowlist or exempt entry
was added, so the gate is not weakened. After the fix: `agent_surface` **exit 0** three runs in a row
(`test-3-1..3.log`), and a verbose fourth run (`test-7.log`, exit 0) reports "388 reflected, 6 exempt;
reflection: 382 registered, 0 unregistered (0 new); reverse: 878 commands, 876 swept, 2 allowlisted" (was 381
registered, 1 unregistered).

**Semantic difference from the accepted patch:** exactly those 4 added lines (one property call and its comment).
Checked by comparing the `+/-` lines of `git diff 4ee5321ffc 039b15c8ac` with `git diff HEAD` over every
non-generated file of the patch: the only extra lines are those 4. None of the four product files
(`include/LaunchBindings.h`, `src/core/LaunchBindings.cpp`, `src/core/ControlCommandsLaunchBindings.cpp`,
`src/gui/RecordLaunchMenus.cpp`) changed on the train between the source base and the target
(`git diff 4ee5321ffc 2a7fd9f9c7 --quiet` exit 0 for each), so the accepted design runs unchanged.

**Affected failure paths and platform hazards (together)**

- The lazily built submenu shows either the placeholder or per-workflow entries. Both now declare a live command
  (`workflow.list`; `session.bind_command`/`session.command_bindings` through `addCommandAction`), whichever walk
  opens it first (the palette, the gate, or a user).
- The placeholder is disabled, so the palette skips it (`!action->isEnabled()`), and the property cannot make it
  run anything.
- `QObject::setProperty(const char*, QVariant)` from a `QStringLiteral` works on Qt 5.15 and Qt 6.8, with no new
  include, no OS API and no identifier hazard. MSVC, clang-arm64 and macOS < 10.15 are unaffected. Nothing touches
  the audio or MIDI thread: menu building runs on the UI thread in `aboutToShow`.

**Evidence on the integrated tree (commit `22032a4760` plus these docs)**

| Run | Log | Exit |
| --- | --- | --- |
| `cmake --build build --target zene MidiWorkflowBindingsTest MidiWorkflowBindingsGuiTest ... -- -j4` | `build-2.log`, `build-3.log`, `build-7.log` | 0 |
| Focused set, 16 tests: `MidiWorkflowBindingsTest`, `MidiWorkflowBindingsGuiTest`, `ControlWorkflowBindings`, `MidiCommandBindingsTest`, `MidiCommandBindingsGuiTest`, `ReversibilityContractTest`, `CollabPolicyTest`, `WorkflowFeedbackTest`, `ControlCommandsMacrosTest`, `RecordingPassTest`, `PerformViewTest`, `ControlSessionLaunch`, `ControlCustomisation`, `ControlCommandsSnapshot`, `ControlWorkflowModels`, `agent_surface` | `test-6.log` (100% of 16) | **0** |
| Red proof: the four product files checked out from `HEAD~1` (the train), rebuilt (`build-red-1.log` EXIT=0), `ctest -R '^(MidiWorkflowBindingsTest\|MidiWorkflowBindingsGuiTest\|ControlWorkflowBindings)$'` | `test-red-1.log` (0 of 3, 13 `FAIL!` lines) | **8** |
| Restored with `git checkout HEAD -- <files>`, rebuilt (`build-7.log` EXIT=0), the same three in the focused set | `test-6.log` | **0** |
| `python3 tests/doc-figures-gate.py` | `docfig-1.log` | 0 |
| `feature_check.py` (all eight gate rows PASS; complexity/file-length: no new item in this unit's files) | `feature-check-2.log` | 0 |

**Pre-existing failures that are not this patch's.** `ReversibilityUndoDeviceTest` and
`ControlReversibilityTranscript` fail on this box, and they fail **identically with the product change reverted**
(`test-red-2.log`, exit 8: the first segfaults inside its LADSPA restore slot after
`aRemovalWithAnUnusableDocumentIsNotRecorded`; the second reports "section E could not run: plugin.list offered
no 'amplifier' device"). Before `ladspaeffect` was built (`build-6.log`) both failed with "the LADSPA host plugin
'ladspaeffect' is not in this build" (`test-4.log`). Neither test, nor any code path they exercise, is touched by
this unit. In the same reverted run, `agent_surface` passed. That confirms the train itself is green on that gate
and the placeholder was the whole regression.

The sections below are round 1's report for the accepted patch, kept as written.

Cluster Q050-217, stage 2 of 4: "Bind MIDI notes and CCs to saved workflows and run each through the existing
workflow command path." Built in
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-feat-q050-217.2` on
`feat/050-Q050-217.2-oc`, commit `da9c5e4d80`.

## What was built

The stage 1 surface, widened rather than duplicated: one MIDI port, one 15 ms UI-thread drain, one config store
(`midi/commandbindings`), one channel-specificity rule. A stored binding now carries exactly ONE target -
`command` (stage 1) or `workflow` (this stage) - and `LaunchBindings::targetKey()` is the single reader of
which. A workflow target stores the workflow's NAME and runs it through `workflow.run`, the command the
Workflows menu and the socket already call, so an activation is acknowledged at once and its steps run on the
event loop with the workflow's own behaviour (each step records and undoes as itself, a stopped run reports where).
`workflow.run` resolves the name against the library in use, so the binding follows the shared/project scope and
edits instead of freezing a copy; a name the library no longer holds is a no-op, and the readback reports it as
`resolved: false` with the reason rather than deleting the user's binding. `session.bind_command` refuses a
name the library does not hold at bind time (typed `not_found`, naming what there is), and refuses both or
neither target (`invalid_args`).

- Agent surface: `session.bind_command` takes `workflow`; `session.unbind_command` removes either kind;
  `session.command_bindings` adds `targets` (kind, name, resolved, reason), `workflows` and `workflow_scope`
  beside the unchanged `bindings`.
- GUI: **Launcher > Controller > Bind a Workflow**, one submenu per workflow in the library in use, with
  *Bind a Note...*, *Bind a Control Change...* and *Workflow and Command Bindings*; each entry is an action
  declaring `session.bind_command` and fixing the target and trigger kind, so the menu and an agent take the
  identical `ControlRegistry` path, and the schema-built form asks only for the number and channel. The submenu
  is rebuilt from the library as it opens.

## Acceptance

| Criterion | Command | Exit |
| --- | --- | --- |
| 1. Bind a note and a CC to saved workflows; replay both and verify the selected workflow completes | `ctest -R '^MidiWorkflowBindingsTest$'` from `build/tests` (`ctest-2.log`: `aBoundNoteRunsTheSavedWorkflow`, `aBoundControlChangeRunsTheSavedWorkflowOncePerSweep`) | **0** |
| 1 (a real GUI entry runs it) | `ctest -R '^MidiWorkflowBindingsGuiTest$'` (`ctest-3.log`: `theMenuBindsANoteThatRunsTheWorkflow`, `theMenuBindsAControlChangeThatRunsTheWorkflow`) | **0** |
| 2. Bindings survive readback and keep resolving the selected workflow from the active shared or project library | `ctest -R '^MidiWorkflowBindingsTest$'` (`aBindingReadsBackWithItsTargetAndWhetherItResolves`, `theBindingSurvivesAConfigReloadAndFollowsTheWorkflow`, `theBindingFollowsTheActiveWorkflowLibrary`, `aProjectScopedWorkflowSurvivesTheProject`) | **0** |
| 2 (across a real restart of the binary) | `ctest -R '^ControlWorkflowBindings$'` (the socket transcript, 13 checks) | **0** |
| 2 (through the interface) | `ctest -R '^MidiWorkflowBindingsGuiTest$'` (`theMenuReadsBackAndTheBindingFollowsTheLibrary`) | **0** |
| 2 (one undo step, A16 classes) | `ctest -R '^(MidiWorkflowBindingsTest\|ReversibilityContractTest)$'` | **0** / **0** |
| All three new tests, reverted product | see the red proof below | **8** |

Focused green set on the committed tree (11 tests, one `ctest -R`):
`MidiWorkflowBindingsTest`, `MidiWorkflowBindingsGuiTest`, `ControlWorkflowBindings`, `MidiCommandBindingsTest`,
`MidiCommandBindingsGuiTest`, `CollabPolicyTest`, `ReversibilityContractTest`, `ControlCommandsMacrosTest`,
`ControlCommandsSnapshot`, `RecordingPassTest`, `PerformViewTest` - `ctest-final-green.log`, **exit 0**
(100% of 11).

### Red proof (the product change reverted, the tests kept)

The four product files' diff was saved, reversed with `git apply -R`, and rebuilt
(`build-red-2.log`, `build-red-3.log`, both `EXIT=0`):

- `ctest -R '^(MidiWorkflowBindingsTest|MidiWorkflowBindingsGuiTest)$'` - **exit 8**: all 9 core slots and all
  4 GUI slots fail (`ctest-red-2.log`: "a note could not be bound to a saved workflow", "Launcher > Controller
  has no Bind a Workflow submenu").
- `ctest -R '^ControlWorkflowBindings$'` - **exit 8**: 6 of its checks fail (`ctest-red-3.log`).
- Restored with `git apply`, rebuilt (`build-12.log` `EXIT=0`), the same three tests - **exit 0**
  (`ctest-9.log`, 100% of 3).

### Other runs

- `bash tests/fork-sources-gate.sh` - **0** (2228 fork-NEW, 1213 whole-tree, 49 tooling, 0 stale;
  `all-sources.txt` REPRODUCES).
- `bash tests/no-upstream-regression-gate.sh` - **0**.
- `python3 tests/doc-figures-gate.py` - **0** (snapshot measures 878 ids / 77 groups; README matches).
- `bash tests/no-tautology-gate.sh` - **0**; `tests/scripted/check-namespace` - **0**.
- `bash tests/complexity-gate.sh --check` and `tests/file-length-gate.sh --check` - exit 1 on the train's
  pre-existing items, but **no `REGRESSION` line names a file this unit changed** (the two new test sources are
  395 and 277 code lines, under the 500 limit; no new function over target).
- `bash tests/all-sources-reproduce.sh` - **0** (`REPRODUCES`).

## Format changes

None to the project. One config addition, additive inside the existing `midi/commandbindings` array: a binding
may carry `workflow` (a string name) instead of `command`. An old config's bindings have no such key and keep
their meaning. Old projects load and render exactly as before: a project-scoped workflow the binding names lives
in the project's existing `<workspace>` element (design theme 19), which this unit does not touch, and
`aProjectScopedWorkflowSurvivesTheProject` proves both the round trip and that a project with no project-scoped
data re-saves without a `<workspace>` element.

## New commands

None. The snapshot was regenerated from a live instance because the three existing ids' descriptions and
`session.bind_command`/`session.unbind_command`'s schema gained the `workflow` property; the surface is
unchanged at 878 ids / 77 groups, so `README.md`'s canary needs no new figure and the A16 histogram is
unchanged (`rows=887 true_inverse=434 snapshot=42 irreversible=25 not_mutating=386`).

## Self-check

**Round 2 (integration) answers first; round 1's follow each question.**

- *Red/green:* train versions of the four product files → rebuild EXIT=0 → the three new tests **exit 8**
  (`test-red-1.log`); restored → rebuild EXIT=0 → **exit 0** (`test-6.log`). `agent_surface` with the patch but
  without the placeholder fix: **exit 8** (`test-2.log`); with it: **exit 0** (`test-3-*.log`, `test-6.log`).
- *Criteria:* `MidiWorkflowBindingsTest` 0, `MidiWorkflowBindingsGuiTest` 0, `ControlWorkflowBindings` 0 on the
  integrated tree (`test-6.log`).
- *Undo/reversibility:* no command added or reclassified; `ReversibilityContractTest` 0 (`test-6.log`).
- *Realtime:* the integration fix is one `setProperty` on a menu action, built on the UI thread in
  `aboutToShow`. The audio and MIDI paths are untouched.
- *Projects:* no format change. `MidiWorkflowBindingsTest::aProjectScopedWorkflowSurvivesTheProject` 0.
- *Portability:* `QObject::setProperty` + `QStringLiteral` are Qt 5.15/6.8 API, with no new identifiers or
  includes.
- *Checker runs:* `submit_check.py` as installed reports one FAIL, the gates row, because `feature_check.py`
  makes its log directory beside the worktree, and this sandbox refuses that (`PermissionError`,
  `submit-check-1.log`, `feature-check-1.log`). Copies in `/tmp/q2172-fc/` that change only that log path (and
  point `ROOT` at the real qwen directory, for the cached `gate-baselines/2a7fd9f9c7….json`) give
  `feature_check` RESULT: PASS (`feature-check-2.log`) and `submit_check` RESULT: PASS - submit
  (`submit-check-3.log`). The one remaining WARN, "canary figures - new command ids: control.quit, control.undo,
  control.version, kept.mmp, plain.mmp", is a false positive. These are existing ids and file names quoted in the
  tests. The live surface is 878 ids / 77 groups, unchanged, and `doc-figures-gate` passes.
- *Build environment:* the ccache config's `temporary_dir` (under `dsh/home`) is not writable here, so builds
  ran with `CCACHE_TEMPDIR=/tmp/ccache-tmp-q2172` (`build-1.log` shows the refused temp file; nothing else
  changed).

**? Each new test FAILS with your product change reverted (reverse the non-test hunks, rebuild, run: exit
code?), then PASSES restored.**
Reversed the four product files (`include/LaunchBindings.h`, `src/core/LaunchBindings.cpp`,
`src/core/ControlCommandsLaunchBindings.cpp`, `src/gui/RecordLaunchMenus.cpp`) with `git apply -R`, rebuilt
`zene` and the two Qt tests (`build-red-2.log` EXIT=0) and the binary for the transcript (`build-red-3.log`
EXIT=0). `ctest -R '^(MidiWorkflowBindingsTest|MidiWorkflowBindingsGuiTest)$'` **exit 8** (13 `FAIL!` lines,
every slot) and `ctest -R '^ControlWorkflowBindings$'` **exit 8** (6 checks). Restored with `git apply`, rebuilt
(`build-12.log` EXIT=0), and the same three tests **exit 0** (`ctest-9.log`, 100% of 3).

**? Every acceptance criterion is proven by a test you ran yourself (name the test and its exit code per
criterion).**
Criterion 1 (note): `MidiWorkflowBindingsTest::aBoundNoteRunsTheSavedWorkflow` - a press runs the selected
workflow (`workflow.status` done, its own name and total, its tracks added), the release / velocity-0 note-on
add none, the next press runs it again - exit **0**. Criterion 1 (CC): `::aBoundControlChangeRunsTheSavedWorkflowOncePerSweep`
- a 1/2/40/127 sweep runs it once, 0 adds nothing, the next sweep runs it again, another controller/channel
adds nothing - exit **0**. Criterion 1 through the GUI: `MidiWorkflowBindingsGuiTest::theMenuBindsANoteThatRunsTheWorkflow`
and `::theMenuBindsAControlChangeThatRunsTheWorkflow` - the built menu action, the real `CommandForm`, live
events - exit **0**. Criterion 2: `::aBindingReadsBackWithItsTargetAndWhetherItResolves`,
`::theBindingSurvivesAConfigReloadAndFollowsTheWorkflow`, `::theBindingFollowsTheActiveWorkflowLibrary`,
`::aProjectScopedWorkflowSurvivesTheProject` - exit **0**; `ControlWorkflowBindings` (a real restart on the
same config) - 13/13, exit **0**. Criterion 2 through the GUI:
`MidiWorkflowBindingsGuiTest::theMenuReadsBackAndTheBindingFollowsTheLibrary` - exit **0**.

**? Each edit is ONE undo step, and every new or changed command has an honest reversibility class.**
`MidiWorkflowBindingsTest::aWorkflowBindingIsOneUndoStep`: a second bind replaces in one `control.undo` and an
unbind is one more; no new command id was added, and `ReversibilityContractTest` (exit **0**) verifies the live
table still holds `session.bind_command`/`session.unbind_command` `true_inverse` and `session.command_bindings`
`not_mutating`. The trigger path adds no undo step of its own (it dispatches `workflow.run`, whose row is
`not_mutating` because each step records as itself).

**? Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a signal.**
`MidiSurfaceInput::decode()` is untouched (still fills a plain `LaunchTrigger` into a fixed
`MpscQueue<LaunchTrigger, 256>`). The new work is all in `LaunchBindings::applyTrigger` /
`dispatchNoteCommand` / `dispatchTarget` / `runWorkflow`, which run on the UI thread from the 15 ms drain
timer (or `drainPending()` in a harness): a QString read from the config array and one
`ControlRegistry::invoke("workflow.run", {name})`. Workflow steps run later on the event loop via
`WorkflowRun`'s own `QTimer::singleShot(0, ...)`. Nothing in the diff is in `processImpl`/`play`/
`renderPeriod`/`AudioEngine::`/`tap(`/`pushPeriod` paths, and no `RealtimeScope` is involved.

**? Old projects still load and render exactly as before; new data round-trips through save and load.**
No project schema change: a binding is config, and the workflow it names already lives in the project's
existing `<workspace>` element. `aProjectScopedWorkflowSurvivesTheProject` saves a project with a
project-scoped workflow, clears it (the binding stops resolving), reopens the file (it resolves and runs), and
separately proves a project with no project-scoped data writes no `<workspace>` element. `perform`-less
render paths are untouched; `RecordingPassTest` and `PerformViewTest` (the launcher/MIDI neighbours) pass,
exit **0**.

**? Qt 5.15 / MSVC / macOS < 10.15 portability: no Qt-6-only API without a guard, no windows.h macro names, no
std::filesystem outside _WIN32.**
The diff adds only `QStringLiteral`, `QLatin1String`, `QString`, `QStringList`, `QJsonObject`/`QJsonArray`/
`QJsonValue`, `QJsonArray::fromStringList` (Qt 5.4+), `QMenu::aboutToShow`, `QAction`, and a lambda with a
structured-free capture; no `QString::midRef`, no Qt 6 container API, no `std::filesystem`, no `windows.h`
macros, and no identifier named `small`/`near`/`far`/`interface`/`min`/`max`. `git diff HEAD~1 -- include src`
grepped for those names and `std::filesystem`/`QT_VERSION` - no hits.

### Notes and limits

- Local TSan does not run on this box, so the threading argument is by construction (POD queue item, UI-thread
  latch sets, UI-thread config reads) and stated in `docs/050-features/Q050-217.2.md`, not measured.
- **Environment note (the one submit_check row that cannot pass here).** This session may not write outside the
  worktree: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-217.2.r1/logs`, `$HOME` and the worktree's PARENT
  directory are read-only, and the bunny build shim's `build-pool.lock` is unwritable (so the shim's `cmake`
  fails before compiling). Consequently (a) the build/test logs quoted above live in `/tmp/q050-217.2-logs/`
  and were produced with `/usr/bin/cmake --build ... -- -j4`, the brief's -j4 cap; (b) `feature_check.py` and
  `submit_check.py` create their log directory as a SIBLING of the worktree
  (`<worktree>-feature-check`), which this sandbox refuses with `PermissionError` - so their "gates" row reports
  FAIL for that mkdir, not for a gate. Every gate they invoke was run directly and recorded above: the six
  simple gates each exit **0**, and both ratchets list **no new item in a file this unit changed** (the only
  `REGRESSION` lines are the train's pre-existing ones), which is exactly the row's pass condition
  (`complexity-3.log`, `file-length.log`), so `feature_check.py`'s own RESULT is PASS here. The check was also run
  with only that log directory redirected to `/tmp`, and every other row was PASS.
- `docs/qwen-fixes-2026-10/cluster-Q050-217.2.md` first line is exactly `IMPLEMENTED`.
