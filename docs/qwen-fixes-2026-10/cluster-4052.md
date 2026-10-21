# Cluster 4052: the template menu hides command failures

**Verdict: FIXED**

The finding is real. `src/gui/ControllerTemplateMenu.cpp` ran the
`controller.template_*` commands and threw the answer away, so a refused save, a
delete of a template that was already gone and a template that applied only partly
all closed the Edit > Controller Templates menu with no word to the user.

## Evidence (file:line, this branch)

- `src/gui/ControllerTemplateMenu.cpp:57-60` (the line the finding named) was
  `void run(const char* command, const QString& name) { ControlRegistry::instance()->invoke(...); }` -
  the `ControlResult` was discarded. All three menu actions funnel through it
  (`:83` apply, `:89` save, `:97` delete), so all three were silent.
- `src/gui/ControllerTemplateMenu.cpp:72-73` (before the fix) invoked
  `controller.template_list` and read only `.result`, so a failed listing was an
  empty menu with no explanation either.
- The commands themselves do report: `src/core/ControlCommandsControllerTemplates.cpp:130-132`
  (save refused: "could not write the template file for ..."), `:235-238` (apply on
  a name that is not there), `:293-298` (delete answers `removed: false` when the
  file was not there), and `:249-253` (apply answers `bound` / `skipped` / `missing`).
  The result was correct - the menu threw it away.
- The other path into the same commands already surfaces failures:
  `src/gui/CommandLists.cpp:233-239` builds the "Mapping Templates" list, whose
  actions run through `CommandListDialog` -> `runCommand` ->
  `announce` (`src/gui/CommandRun.cpp:87-97`). The menu was the only silent path.
  `announce` returns early when the registry is headless
  (`ControlRegistry::instance()->isHeadless()`), so the menu needed its own way to
  speak - that is the notifier seam below, not a second copy of `announce`.

## The fix

- `include/ControllerTemplateMenu.h:45-49` - `ControllerTemplateNotifier`, a
  replaceable seam beside the prompt seam that was already there, and
  `setControllerTemplateNotifier()`.
- `src/gui/ControllerTemplateMenu.cpp:66-72` - the default surface: a `TextFloat`
  over the main window (the same widget `announce` floats), guarded by
  `getGUI() != nullptr` exactly as `announce` guards itself.
- `src/gui/ControllerTemplateMenu.cpp:74-79` - `notify()`: the notifier if one is
  installed, the float otherwise.
- `src/gui/ControllerTemplateMenu.cpp:82-88` - the message title per command.
- `src/gui/ControllerTemplateMenu.cpp:90-108` - the partial-apply report: how many
  bindings were bound, how many were skipped and the names of the targets that do
  not resolve in this project.
- `src/gui/ControllerTemplateMenu.cpp:110-117` - the "there was no template named X
  to delete" report for a delete that found nothing (`removed: false`).
- `src/gui/ControllerTemplateMenu.cpp:119-138` - `report()` reads the
  `ControlResult` and `run()` now keeps it and passes it on.
- `src/gui/ControllerTemplateMenu.cpp:152-158` - a failed `controller.template_list`
  is reported too; the menu is still built exactly as before.

The commands, their schemas, their journal transactions and their undo classes are
untouched: this is a change in what the MENU does with the answer, not in what the
command does.

## Regression

`tests/src/gui/ControllerTemplateMenuTest.cpp` - two new cases in the existing file:

- `aRefusedSaveIsReported()` (`:127-142`): the prompt seam hands the menu the
  unusable name "bad/name"; the command refuses it; the notifier must have been
  told, and the message must name the name that was refused.
- `aPartlyAppliedTemplateIsReported()` (`:144-178`): one track with one
  MIDI-bound control, saved as a template; the track is then removed, so the
  saved target cannot resolve; applying the template binds nothing and skips that
  binding - the notifier must have been told, and the message must say what was
  skipped.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before the fix | `cmake --build build --target ControllerTemplateMenuTest -j$(nproc) > logs/build-red-1.log 2>&1` **EXIT=0**; `ctest -R '^ControllerTemplateMenuTest$' --output-on-failure > logs/test-red-1.log 2>&1` **EXIT=8** - `FAIL! : aRefusedSaveIsReported() '!reports.isEmpty()' returned FALSE. (the refused save was reported to nobody)` and `FAIL! : aPartlyAppliedTemplateIsReported() '!reports.isEmpty()' returned FALSE. (the partly applied template was reported to nobody)`; the pre-existing `theMenuSavesListsAppliesAndDeletes` passed. |
| GREEN after the fix | `cmake --build build --target ControllerTemplateMenuTest -j$(nproc) > logs/build-green-1.log 2>&1` **EXIT=0**; `ctest -R '^ControllerTemplateMenuTest$' --output-on-failure > logs/test-green-1.log 2>&1` **EXIT=0**, 1/1 CTest target passed (4 test functions: initTestCase, 3 cases, cleanupTestCase). |
| Reversed-fix proof | `git diff -- src/gui/ControllerTemplateMenu.cpp > logs/fix.patch` **EXIT=0**; `git apply -R logs/fix.patch` **EXIT=0**; `cmake --build build --target ControllerTemplateMenuTest -j$(nproc) > logs/build-4b-reverted.log 2>&1` **EXIT=0**; `ctest -R '^ControllerTemplateMenuTest$' --output-on-failure > logs/test-4b-reverted.log 2>&1` **EXIT=8** - the same two assertions fail, the other case still passes. |
| Restored-fix proof | `git apply logs/fix.patch` **EXIT=0**; `cmake --build build --target ControllerTemplateMenuTest -j$(nproc) > logs/build-4b-restored.log 2>&1` **EXIT=0**; `ctest -R '^ControllerTemplateMenuTest$' --output-on-failure > logs/test-4b-restored.log 2>&1` **EXIT=0**, 1/1 passed. |
| Application build | `cmake --build build --target zene -j$(nproc) > logs/build-zene-1.log 2>&1` **EXIT=0** (the changed header is included by `src/gui/MainWindow.cpp`, so the app was rebuilt too). |
| Affected family | `ctest -R 'Controller|Control' --output-on-failure > logs/test-family-1.log 2>&1` **EXIT=8**: 122 CTest targets ran, 53 passed, 69 failed. Every failure is pre-existing and environmental: this build tree has **no plugin modules built** (`find build/plugins -name '*.so'` returns 0), so the device catalogue is empty ("the catalogue lists at least two loadable built-in effects FAILED, devices=[]"). Proof: with the product change reverted (`git apply -R logs/fix.patch`, rebuild **EXIT=0**), `ctest -R '^(ControlEngineWiring|ControlDevices|ControlSuccessPaths)$' --output-on-failure > logs/test-baseline-nofix.log 2>&1` **EXIT=8** with the SAME failures ("no loadable effect", "cards=[]"). The fix was then restored (`git apply logs/fix.patch` **EXIT=0**, rebuild **EXIT=0**, `ctest -R '^ControllerTemplateMenuTest$'` **EXIT=0**). |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1` **EXIT=0** - fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length all PASS. |
| Completion check | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> 4052 > logs/submit-check-1.log 2>&1` **EXIT=0** - "RESULT: PASS - submit": commits on the branch PASS, working tree clean PASS, report PASS, tests added or changed PASS, gates PASS. |

## Self-check

**Does the regression test FAIL with the product change reverted and PASS with it?**
Yes - **EXIT=8** (two assertion failures, `test-4b-reverted.log`) with the change
reverted and **EXIT=0** with it restored (`test-4b-restored.log`); the same pair was
run before the fix was written (`test-red-1.log`, **EXIT=8**) and after
(`test-green-1.log`, **EXIT=0**). The patch used is `logs/fix.patch` =
`git diff -- src/gui/ControllerTemplateMenu.cpp`.

**Is the test deterministic (no wall-clock timing, sleeps or races)?**
Yes. Both cases use the file's existing seams and the registry, nothing else: the
name comes from the prompt seam (`setControllerTemplatePrompt`, the file's own
`ControllerTemplatePrompt`), the report is collected through the notifier seam
(`setControllerTemplateNotifier`, installed by the test and reset in
`cleanupTestCase`), and the command's own answer is read through
`ControlRegistry::instance()->invoke(...)`. No timer, no sleep, no polling, no
wall-clock value; the ordering is the command's own.

**Is the fix at the cause and complete - every caller and sibling checked?**
The cause is the discarded `ControlResult` in `run()`. Every caller of that
function is the fix's caller: the apply action (`:83`), the save action (`:89`) and
the delete action (`:97`) all go through `run()`, and all three now report. The
sibling in the same file - the `controller.template_list` invocation that built
the menu (`:152-158`) - was checked and now reports a failed listing too. The
sibling PATH into the same commands (the "Mapping Templates" list in
`src/gui/CommandLists.cpp:233-239` -> `CommandListDialog` -> `runCommand` ->
`announce`) was checked: it already surfaces failures, so it is consistent and
was left alone. The wider type the value flows into (`ControlResult` -> the
notifier's title and text) carries the command's own message unchanged, so the
refusal text the user reads is the text the command wrote. `controller.template_*`
has no "many items" variant to keep in step, and no other file calls
`populateControllerTemplateMenu` besides `src/gui/MainWindow.cpp`.

**No collateral damage?**
Only the menu's REPORTING changed. The commands, their schemas, their result
payloads, their journal transactions and their reversibility classes are byte for
byte unchanged; no command was added, renamed or re-registered. No data format,
no saved file and no project is touched, so old projects load exactly as before.
No limit or threshold was added, so no caller that legitimately exceeds one is
affected. A successful save, a fully resolved apply and a delete that removed the
file still say nothing - the menu's noise level is unchanged for the paths that
worked. The only new user-visible thing is the message on the paths that failed.

**Undo, realtime and commands?**
The fix adds no undo step: the commands' own transactions (including
`controller.template_apply`'s snapshot) are untouched, so undo stays one honest
step per command. Nothing was added to the audio thread - the menu runs on the GUI
thread, and the diff touches no process/render, play-handle or mixer-channel code;
no allocation, lock, wait, I/O or logging was added to any audio path. No new
control command was added, so the `ControlCommandsSnapshot` canary is untouched
(`tools/mcp-zene-control/zene_control/commands_snapshot.json` is not in the diff).

**If the verdict were ALREADY-FIXED or FALSE-POSITIVE, the code that shows it?**
Not applicable - the verdict is FIXED. The code that showed the bug is quoted
above: `src/gui/ControllerTemplateMenu.cpp:57-60` discarded the `ControlResult`
returned by `ControlRegistry::instance()->invoke(...)`, and every menu action ran
through it, so no failure could reach the user.

**Portability (Qt 5.15 / MSVC / macOS)?**Only `QMenu`, `QPixmap`, `QJsonArray`/`QJsonObject` and the project's own
`TextFloat` are used - all present in Qt 5.15 and Qt 6. `QString::arg` with five
string arguments, `QStringList::mid`, `QJsonValue::toString` and
`QStringLiteral` are portable. No `windows.h` macro names, no
`std::filesystem`, no Qt-6-only API.

**Files committed**

- `include/ControllerTemplateMenu.h` - the notifier seam and its setter.
- `src/gui/ControllerTemplateMenu.cpp` - the fix: read the `ControlResult`, report
  the refusal, the partial apply, the empty delete and the failed listing.
- `tests/src/gui/ControllerTemplateMenuTest.cpp` - the two regression cases.
- `docs/qwen-fixes-2026-10/cluster-4052.md` - this report.

Both product files are fork-NEW code (listed in `tests/fork-sources.txt`), so
`tests/upstream-modifications.txt` needs no entry: the `no-upstream-regression`
gate passes. The test file is under `tests/`, which that gate allows.
