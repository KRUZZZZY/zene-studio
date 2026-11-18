# Cluster 4154: groove-menu command failures are discarded

**Verdict: FIXED**

`src/gui/GrooveMenu.cpp` ran the piano roll's Groove-menu commands and dropped the
answer: `run()` called `ControlRegistry::instance()->invoke(...)` and threw the
returned `ControlResult` away, so a command that REFUSED (an empty clip carries no
note a groove can be measured from; a grid the command rejects) closed the menu
and failed in silence - the person who asked was told nothing.

## Evidence

The bug, before the fix (`src/gui/GrooveMenu.cpp:67-71`, the file's own line
numbers at the base commit `b61e5f965`):

```cpp
void run(const char* command, QJsonObject args, const MidiClip* clip)
{
	args.insert(QStringLiteral("clip"), control::clipIdOf(clip));
	ControlRegistry::instance()->invoke(QString::fromLatin1(command), args);   // the result is dropped
}
```

Every other path into the same commands already showed a refusal - the Groove menu
was the only one that swallowed it:

- `src/gui/CommandRun.cpp:87-97` - `announce()` is the interface's one way of
  showing what a command answered: a failure is floated over the window with its
  error message.
- `src/gui/NoteEditor.cpp:255-266` - the note editor's own `run()` invokes the
  same `groove.quantize` / `groove.apply` commands and calls `announce(...)` on
  a refusal.
- `src/gui/CommandRun.cpp:99-120` / `src/gui/CommandRun.cpp:122-131` -
  `runCommand()` / `addCommandAction()`, the path every other menu takes.

## The fix

`src/gui/GrooveMenu.cpp:68-77` - `run()` keeps the answer and shows a refusal:

```cpp
void run(const char* command, QJsonObject args, const MidiClip* clip, QWidget* parent)
{
	args.insert(QStringLiteral("clip"), control::clipIdOf(clip));
	const QString id = QString::fromLatin1(command);
	const ControlResult result = ControlRegistry::instance()->invoke(id, args);
	if (!result.ok) { announce(parent, CommandCall{id, {}, {}, QString(), false}, result); }
}
```

`announce()` is declared in `include/CommandForm.h:175` and defined in
`src/gui/CommandRun.cpp:87`; it floats the failure's message (and the command's
name) as a `TextFloat` over the window the menu belongs to. It shows nothing
when there is no display to show it on (`ControlRegistry::isHeadless()` or no
`getGUI()`), which is the interface's existing rule for every other refusal.

All three callers of `run()` pass the menu's own parent widget, so the message
appears where the command was asked for: `src/gui/GrooveMenu.cpp:88`
(`groove.extract`), `:102` (`groove.apply`), `:111` (`groove.quantize`).

Nothing else changed: no new command, no new file, no change to the commands
themselves, their schemas or their reversibility classes.

## The regression

`tests/src/gui/GrooveMenuTest.cpp:158-172` - `aRefusedCommandIsShownToTheUser()`:
an empty clip has no note a groove can be measured from, so `groove.extract`
refuses; the test reads the floating messages under the main window
(`messagesShown()`, `:90-98`) and requires the refusal to be there.

The test runs the REAL application (`guitest::startGui()`, `:113`) because the
refusal is shown the way the interface shows it - as a floating message over the
main window - and the registry is told there is a display to show it on
(`ControlRegistry::instance()->setHeadless(false)`, `:118`; the offscreen
platform plugin is a display). The existing test in the same file
(`extractApplyAndQuantize()`, `:128-156`) keeps every one of its assertions; it
now runs in the same real application.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before fix | `cmake --build build --target GrooveMenuTest -j20 > .../logs/build-2-red.log 2>&1` **EXIT=0**; `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > .../logs/test-1-red.log 2>&1` **EXIT=8** - `FAIL! : GrooveMenuTest::aRefusedCommandIsShownToTheUser() '!shown.isEmpty()' returned FALSE. (the refused groove.extract was shown to nobody)` at `tests/src/gui/GrooveMenuTest.cpp:169`; the other 3 test functions passed. |
| GREEN after fix | `cmake --build build --target zene GrooveMenuTest -j20 > .../logs/build-3-green.log 2>&1` **EXIT=0**; `ctest -R '^GrooveMenuTest$' --output-on-failure > .../logs/test-2-green.log 2>&1` **EXIT=0** - 1/1 CTest target passed. |
| Reversed-fix proof | `git diff -- src/gui/GrooveMenu.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` **EXIT=0**; `cmake --build build --target zene GrooveMenuTest -j20 > .../logs/build-4b-reverted.log 2>&1` **EXIT=0**; `ctest -R '^GrooveMenuTest$' --output-on-failure > .../logs/test-3-4b-reverted.log 2>&1` **EXIT=8** - the same assertion fails (`'!shown.isEmpty()' returned FALSE`), 3 passed / 1 failed. |
| Restored-fix proof | `git apply .../logs/fix.patch` **EXIT=0**; `cmake --build build --target zene GrooveMenuTest -j20 > .../logs/build-5-4b-restored.log 2>&1` **EXIT=0**; `ctest -R '^GrooveMenuTest$' --output-on-failure > .../logs/test-4-4b-restored.log 2>&1` **EXIT=0** - 1/1 CTest target passed. |
| Affected family | `ctest -R '^(GrooveMenuTest\|ControlGrooveCommandsTest\|GrooveTemplateTest\|NoteTransformTest\|NoteEditorTest\|NoteEditingCommandsTest)$' --output-on-failure > .../logs/test-5-family.log 2>&1` **EXIT=0** - 6 CTest test targets executed, 6/6 passed. |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py . > .../logs/gate-1-feature-check.log 2>&1` **EXIT=0** - `RESULT: PASS` (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS). The same gate on the untouched tree before the change was **EXIT=0** (`.../logs/gate-baseline.log`). |
| Baseline before the change | `ctest -R '^GrooveMenuTest$' --output-on-failure > .../logs/test-baseline.log 2>&1` **EXIT=0** (the pre-existing test passed before anything was touched). |

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4154.r1/logs/`
(`build-1.log`, `build-2-red.log`, `build-3-green.log`, `build-4b-reverted.log`,
`build-5-4b-restored.log`, `build-6-family.log`, `test-baseline.log`,
`test-1-red.log`, `test-2-green.log`, `test-3-4b-reverted.log`,
`test-4-4b-restored.log`, `test-5-family.log`, `gate-baseline.log`,
`gate-1-feature-check.log`, `fix.patch`).

## Build and test commands

All builds ran from the worktree root; all ctest runs ran from `build/tests`.

```text
cmake --build build --target GrooveMenuTest -j20 > .../logs/build-1.log 2>&1; echo EXIT=$?   # EXIT=0, the pre-existing test
cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > .../logs/test-baseline.log 2>&1; echo EXIT=$?   # EXIT=0
cmake --build build --target GrooveMenuTest -j20 > .../logs/build-2-red.log 2>&1; echo EXIT=$?   # EXIT=0, the new test, no product change yet
cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > .../logs/test-1-red.log 2>&1; echo EXIT=$?   # EXIT=8, RED
cmake --build build --target zene GrooveMenuTest -j20 > .../logs/build-3-green.log 2>&1; echo EXIT=$?   # EXIT=0, the fix
cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > .../logs/test-2-green.log 2>&1; echo EXIT=$?   # EXIT=0, GREEN
git diff -- src/gui/GrooveMenu.cpp > .../logs/fix.patch; git apply -R .../logs/fix.patch; echo EXIT=$?   # EXIT=0
cmake --build build --target zene GrooveMenuTest -j20 > .../logs/build-4b-reverted.log 2>&1; echo EXIT=$?   # EXIT=0
cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > .../logs/test-3-4b-reverted.log 2>&1; echo EXIT=$?   # EXIT=8, the fix reverted
git apply .../logs/fix.patch; echo EXIT=$?   # EXIT=0
cmake --build build --target zene GrooveMenuTest -j20 > .../logs/build-5-4b-restored.log 2>&1; echo EXIT=$?   # EXIT=0
cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > .../logs/test-4-4b-restored.log 2>&1; echo EXIT=$?   # EXIT=0, GREEN again
cmake --build build --target ControlGrooveCommandsTest GrooveTemplateTest NoteTransformTest NoteEditorTest NoteEditingCommandsTest -j20 > .../logs/build-6-family.log 2>&1; echo EXIT=$?   # EXIT=0
cd build/tests && ctest -R '^(GrooveMenuTest|ControlGrooveCommandsTest|GrooveTemplateTest|NoteTransformTest|NoteEditorTest|NoteEditingCommandsTest)$' --output-on-failure > .../logs/test-5-family.log 2>&1; echo EXIT=$?   # EXIT=0, 6/6
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py . > .../logs/gate-1-feature-check.log 2>&1; echo EXIT=$?   # EXIT=0, RESULT: PASS
```

## Self-check

**The regression test FAILS with the product change reverted and PASSES with it.**
Reverting only `src/gui/GrooveMenu.cpp` (the product file; the test file stays as
it is) and rebuilding gives `ctest -R '^GrooveMenuTest$'` **EXIT=8** with
`FAIL! : GrooveMenuTest::aRefusedCommandIsShownToTheUser() '!shown.isEmpty()'
returned FALSE. (the refused groove.extract was shown to nobody)`
(`.../logs/test-3-4b-reverted.log`); restoring it gives **EXIT=0**
(`.../logs/test-4-4b-restored.log`). The same command on the untouched tree
before the change was **EXIT=0** (`.../logs/test-baseline.log`), so the new test
is the only thing that changed the outcome.

**The test is deterministic.** No wall-clock timing, no sleep, no race: the
refusal is produced by the command itself (an empty clip - `groove.extract`
refuses with "no groove could be read from ...: it carries no note a slot can be
measured from", `src/core/ControlCommandsGroove.cpp:175-181`), the prompts are
INJECTED through the menu's own seam (`setGroovePrompts`, `include/GrooveMenu.h:52`
- the name and the strength are given, not asked for), and the message is read
synchronously from the widgets that exist right after the action was triggered.
The only environment the test sets is the registry's "there is a display" flag
(`ControlRegistry::instance()->setHeadless(false)`), which is what a desktop run
has.

**The fix is at the cause and complete.** The cause is the dropped
`ControlResult` in `run()`; the fix reads it and shows the refusal. Every caller
of `run()` was checked and passes the message's parent: `groove.extract`
(`src/gui/GrooveMenu.cpp:88`), `groove.apply` (`:102`), `groove.quantize`
(`:111`). The sibling paths into the same commands already showed refusals and
were left consistent with this one: `NoteEditor::run()`
(`src/gui/NoteEditor.cpp:255-266`), `addCommandAction()` /
`runCommand()` (`src/gui/CommandRun.cpp:99-131`), the shell's
`addCommandAction`-based groove entries (`src/gui/shell/ShellCommandMenus.cpp:175`,
`:245-246`) and the note editor's menus
(`src/gui/noteeditor/NoteEditorMenus.cpp:245`, `:259-260`). One neighbouring
call was checked and deliberately NOT changed: the `groove.list` READ in
`populate()` (`src/gui/GrooveMenu.cpp:91-93`) - its failure leaves the "Apply
groove" submenu empty and disabled, which the person can see, and a message on
every menu opening would be noise.

**No collateral damage.** No project file, save format, command schema,
command id or reversibility class was touched: the diff is one include, one
function body and the three call sites in `src/gui/GrooveMenu.cpp` plus the test
file. Old projects load exactly as before - nothing reads the new code outside
the Groove menu, and the commands themselves are unchanged. No limit or check
was added that another caller could trip over: `announce()` is the interface's
own helper and it decides for itself whether a display exists
(`src/gui/CommandRun.cpp:90`).

**Realtime, undo and commands.** Nothing was added to the audio thread: the
change is in a menu handler on the UI thread, and it allocates nothing on any
realtime path (it calls the same `announce()` every other menu already calls).
Undo is untouched - the fix adds no command, changes no command's behaviour and
records no transaction; a refused command still records none (the refusal
happens before anything is written, `src/core/ControlCommandsGroove.cpp:175-181`,
ahead of the pool write at `:183`).
No new control command was added, so the `ControlCommandsSnapshot` canary does
not move.

**Portability.** The fix uses only Qt 6 API that the file already used
(`QMenu`, `QJsonObject`) plus `announce()`/`CommandCall` from the project's own
`include/CommandForm.h`; no Qt-6-only API without a guard, no `windows.h`
macro names, no `std::filesystem`.

**Verdict is not ALREADY-FIXED or FALSE-POSITIVE**, so nothing is owed there:
the bug was real at the base commit - `src/gui/GrooveMenu.cpp:67-71` dropped the
`ControlResult` (the pre-change hunk is in `.../logs/fix.patch`, and the
reversed-fix run above fails on the untouched product code).

## Files committed

- `src/gui/GrooveMenu.cpp` (the fix)
- `tests/src/gui/GrooveMenuTest.cpp` (the regression)
- `docs/qwen-fixes-2026-10/cluster-4154.md` (this report)

No new file was added, so no scope manifest (`tests/fork-sources.txt`,
`tests/upstream-modifications.txt`) needed a new entry; both files this change
touches were already registered (`include/GrooveMenu.h`, `src/gui/GrooveMenu.cpp`
in `tests/fork-sources.txt`; `tests/src/gui/GrooveMenuTest.cpp` in
`tests/all-sources.txt`).
