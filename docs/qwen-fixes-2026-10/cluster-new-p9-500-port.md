# FIXED - cluster new-p9-500-port: version numbers silently wrap or reset

Two defects, one cause and one consequence. Round 1 fixed the cause (the version number was
parsed into a `ushort`); round 2 fixed the consequence the first fix exposed (the agent
surface's `project.save_version` ignored the refusal and spun for ever).

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-500-port.r2/logs`
(the round-1 logs are in `.../new-p9-500-port.r1/logs`).

## 1. The reported bug: REAL, fixed in round 1 (commit 1148dbdce)

`src/gui/modals/VersionedSaveDialog.cpp` parsed the version into a `ushort` and asserted the
parse:

- `Mix-65535.wav` + increment: `65535 + 1` stored in a `ushort` wraps to `0` -> `Mix-00.wav`.
- `Mix-65536.wav` + increment: `toUShort` fails; `Q_ASSERT` compiles out in this build
  (`CMAKE_BUILD_TYPE=RelWithDebInfo`, `-DNDEBUG`), so the value is `0` -> `Mix-01.wav`.

Fix (round 1, already on this branch): `src/gui/modals/VersionedSaveDialog.cpp:46-51` parses
with `toULongLong` into a `qulonglong` and refuses an unparseable number or an increment past
`std::numeric_limits<qulonglong>::max()`; `:126-128` returns false before the name is touched.
The four regressions are in `tests/src/gui/VersionedSaveDialogTest.cpp:42-68`.

## 2. The consequence round 1 exposed: REAL, fixed in round 2 (this commit)

Luna's round-1 review (`qa/reviews/new-p9-500-port.r1.md`) returned REWORK:

> There is a new hang at ControlCommandsShell.cpp:398. `project.save_version` ignores
> `changeFileNameVersion`'s return value inside a loop. At the maximum numeric suffix, the
> function now refuses without changing the path; if that project file exists,
> `QFile::exists(next)` stays true and the command loops forever.

That is what the fix at `src/gui/ControlCommandsShell.cpp:398-400` did (before this commit):

```cpp
QString next = previous;
do { gui::VersionedSaveDialog::changeFileNameVersion(next, true); }
while (QFile::exists(next));
```

The refusal (false) was dropped, so `next` stayed the current name; the file it names exists,
so the loop never ended - a busy spin on the UI thread, with the command never answering.

### The fix (this commit)

`src/gui/ControlCommandsShell.cpp:400-415` - the loop now honours the refusal:

```cpp
QString next = previous;
for (;;)
{
    if (!gui::VersionedSaveDialog::changeFileNameVersion(next, true))
    {
        return ControlResult::failure(ControlErrorKind::Refused,
            QStringLiteral("the version number in '%1' cannot be advanced, so there is "
                "no next version to write: save the project under another name "
                "(project.save {path}) and try again").arg(previous));
    }
    if (!QFile::exists(next)) { break; }
}
```

The command refuses, in the typed vocabulary of the surface (`ControlErrorKind::Refused`),
before touching the file. `src/gui/ControlCommandsShell.cpp:380-386` - the command's own
description now says so ("Refused ... when the version number in the file's name cannot be
advanced"), because that description is what an agent reads.

### The regression (this commit)

`tests/src/gui/ControlCommandsSaveVersionTest.cpp` (new, registered in `tests/CMakeLists.txt`
and `tests/all-sources.txt`): a project saved as `mix-18446744073709551615.mmp` (the widest
version number), then `project.save_version` dispatched through the registry. The test asserts
that the call is refused, that it says why, that the file it cannot replace is byte-identical
afterwards, and that the session stayed on it.

The test cannot hang the suite: a helper thread takes the file away if the handler is still
running after its budget, so the pre-fix spin ends and the test reports the failure instead of
running for ever. The budget is a safety net, not an assertion - the outcome is the same
whether the file is taken away before or during the loop.

## 3. Callers and siblings checked

`changeFileNameVersion` has three callers; all three are consistent now:

| Caller | What it does with the refusal |
|---|---|
| `src/gui/modals/VersionedSaveDialog.cpp:151` (`incrementVersion`) | The name stays as it is and the dialog re-selects it. No loop, no hang; the + button simply does not move the version. |
| `src/gui/modals/VersionedSaveDialog.cpp:165` (`decrementVersion`) | Same, for the - button. |
| `src/gui/ControlCommandsShell.cpp:407` (`project.save_version`) | The only caller that loops; it now returns the refusal (this commit). |

Siblings of the command, checked and left alone:

- `project.save` (`src/core/ControlCommandsProjectFiles.cpp:178`) has no version loop: it writes
  to the path it is given, so it cannot spin.
- `MainWindow::saveProjectAsNewVersion` (`src/gui/MainWindow.cpp:1164-1180`) dispatches
  `project.save_version` and passes the answer to `handleSaveResult` (`src/gui/MainWindow.cpp:1999`),
  which shows the refusal; nothing else to change.
- The other commands in `src/gui/ControlCommandsShell.cpp` (`window.*`, `app.*`) do not touch the
  version number and are unchanged.
- The only other `while (QFile::exists(...))` in the tree is `src/core/RenderQueue.cpp:118`,
  which draws a UUID each turn and therefore always terminates; it is a different concern
  (the render output name) and is untouched.

## 4. RED / GREEN evidence

Every command below was run in this worktree; the exit codes are the ones observed.

| Step | Command | Exit | Result |
|---|---:|---:|---|
| Build the new test against the UNFIXED product code | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-500 cmake --build build --target ControlCommandsSaveVersionTest -j4 > logs/build-4-red.log 2>&1` | 0 | Built on the unfixed tree. |
| **RED** (unfixed product code) | `cd build/tests && ctest -R '^ControlCommandsSaveVersionTest$' --output-on-failure > logs/test-1-red.log 2>&1` | **8** | `FAIL! ... '!result.ok' returned FALSE. (project.save_version did not refuse a version it cannot advance)`; `Totals: 2 passed, 1 failed`. The run ended after 7.8 s - the pre-fix spin was broken by the helper thread, it did not hang. |
| Patch | `git diff -- src/gui/ControlCommandsShell.cpp tools/mcp-zene-control/zene_control/commands_snapshot.json > logs/fix.patch` | 0 | 68 lines: the handler fix, the description, the regenerated snapshot. |
| Revert | `git apply -R logs/fix.patch` | 0 | Product change reverted. |
| Rebuild on the reverted tree | `cmake --build build --target zene ControlCommandsSaveVersionTest -j4 > logs/build-11-revert-final.log 2>&1` | 0 | |
| **RED with the fix reverted** | `cd build/tests && ctest -R '^(ControlCommandsSaveVersionTest\|ControlCommandsSnapshot)$' --output-on-failure > logs/test-11-revert-final-red.log 2>&1` | **8** | `50% tests passed, 1 tests failed out of 2`; the same slot fails with the same message. The snapshot check still passes (the snapshot is part of the patch). |
| Re-apply | `git apply logs/fix.patch` | 0 | |
| Rebuild with the fix | `cmake --build build --target zene ControlCommandsSaveVersionTest -j4 > logs/build-12-reapply-final.log 2>&1` | 0 | |
| **GREEN** | `cd build/tests && ctest -R '^(ControlCommandsSaveVersionTest\|ControlCommandsSnapshot\|VersionedSaveDialogTest\|ProjectContainerSaveAsGuiTest)$' --output-on-failure > logs/test-12-reapply-final-green.log 2>&1` | **0** | `100% tests passed out of 4`. |

## 5. Affected family

| Command | Exit | Count |
|---|---:|---|
| `ctest -R '^(ControlCommandsSnapshot\|ControlCommandsSaveVersionTest\|VersionedSaveDialogTest\|ProjectContainerSaveAsGuiTest)$' --output-on-failure` (`logs/test-7-family.log`) | 0 | **4 of 4** ctests passed. `VersionedSaveDialogTest` 7 QTest slots, `ProjectContainerSaveAsGuiTest` 6, `ControlCommandsSaveVersionTest` 3, `ControlCommandsSnapshot` passed. |
| `ctest -R '^(ControlSuccessPathsTest\|ControlSuccessPaths\|ControlSaveState\|agent_surface\|ControlSurfaceHardening\|ControlSurfaceReferenceTest\|AgentSurfaceReportTest\|ControlCommandsSaveVersionTest\|VersionedSaveDialogTest\|ProjectContainerSaveAsGuiTest\|ControlCommandsSnapshot)$'` (`logs/test-8-control-family.log`) | 8 | 7 of 11 passed. The 4 failures are NOT this change: `ControlSurfaceReferenceTest` and `AgentSurfaceReportTest` were "Not Run" (their binaries were never built in this tree), `ControlSuccessPathsTest` aborted in a **stale** binary (built 2026-10-18 10:17, before the tree was reverted; it contained a slot that is not in this tree's source) and `ControlSuccessPaths` (python) failed on the MIDI import/export filters, whose plugin libraries were never built here (`build/plugins/MidiExport/` holds only the CMake scaffolding). |
| The three of those rebuilt and re-run (`logs/build-9-rebuild-family.log`, `logs/test-9-rebuilt-family.log`) | 0 | **3 of 3** passed: `ControlSurfaceReferenceTest`, `ControlSuccessPathsTest`, `AgentSurfaceReportTest`. |
| The python `ControlSuccessPaths` run again after the rebuild | - | Its own report line for the command under repair reads `project.save_version writes the next version beside the first  ok`; the only failures in that run are the two MIDI filter lines above. |
| The whole set once more on the **committed** tree: `ctest -R '^(ControlCommandsSaveVersionTest\|VersionedSaveDialogTest\|ProjectContainerSaveAsGuiTest\|ControlCommandsSnapshot\|ControlSuccessPathsTest\|ControlSurfaceReferenceTest\|AgentSurfaceReportTest)$'` (`logs/test-13-final-committed.log`) | 0 | **7 of 7** passed. |

Product build: `cmake --build build --target zene ... -j4` -> exit **0** in every step above.

## 6. Gates

| Command | Exit | Note |
|---|---:|---|
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`logs/feature-check-1.log`) | **0** | `RESULT: PASS` - fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures all exit 0; complexity and file-length report "no new item in your files". |
| the same command on the **committed** tree (`logs/feature-check-2-committed.log`) | **0** | `RESULT: PASS` again - the upstream-divergence gate only reads `<base>..HEAD`, so this is the run that covers the committed change. |
| `bash tests/complexity-gate.sh --check` (`logs/gate-complexity-1.log`) | 1 | The six pre-existing entries (XyPadPairTest, DawProjectReadTracks, ControlCommandsMixerTools, ControlCommandsMixerStrip, ProjectRevisions, ControlCommandsShortcuts) - the same list as the base listing in `~/.local/share/zene-bunny-fix/qwen/gate-baselines/ded355440871a86cc907cdc2d6465f2d3d597a6b.json`. None of them is this change's file; `registerSaveVersion` goes from CCN 4 to CCN 6 (target 10, measured with lizard). |
| `bash tests/file-length-gate.sh --check` (`logs/gate-file-length-1.log`) | 1 | The same fourteen pre-existing entries as the base listing. The new test file is 147 lines (limit 500). |
| `bash tests/all-sources-reproduce.sh` | 0 | `REPRODUCES` - the manifest carries the new test file. |

No gate limit and no baseline was moved.

The completion check (`python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree>
new-p9-500-port`, `logs/submit-check-2.log`) exits **0**: `RESULT: PASS - submit`, every
mechanical item green, and the questions it asks are answered in the Self-check below.

## 7. The command snapshot

`tools/mcp-zene-control/zene_control/commands_snapshot.json` was regenerated from a live
instance of this build (`tools/mcp-zene-control/snapshot_commands.py --from-file`, the capture
dumped from the running binary with `tests/control_socket_harness.py`; `logs/snapshot-dump-2.log`,
`logs/snapshot-regen-2.log`). The diff is three lines: `captured_at`, the provenance's
`source_file`, and the new `project.save_version` description. The id surface is unchanged -
828 ids across 76 groups, `ids_sha256` identical - so **no command was added** (the canary does
not move) and `ctest -R '^ControlCommandsSnapshot$'` passes (`logs/test-6-snapshot-check.log`,
`logs/test-12-reapply-final-green.log`).

## 8. Files in this commit

- `src/gui/ControlCommandsShell.cpp` - the refusal is honoured (the fix) and the command's
  description says so.
- `tools/mcp-zene-control/zene_control/commands_snapshot.json` - regenerated from this build.
- `tests/src/gui/ControlCommandsSaveVersionTest.cpp` - **new**: the regression.
- `tests/CMakeLists.txt` - registers the new test (source list and the offscreen property).
- `tests/all-sources.txt` - the new test file in the whole-tree manifest.
- `docs/qwen-fixes-2026-10/cluster-new-p9-500-port.md` - this report.

`src/gui/ControlCommandsShell.cpp` is fork-NEW (`tests/fork-sources.txt`), so it needs no
entry in the upstream-divergence ledger; the gate says so itself
("Fixes to fork-NEW code need no entry, but must ship a regression test in the same change") -
the regression above is that test.

## Self-check

**1. The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
`ctest -R '^ControlCommandsSaveVersionTest$'` exits **8** on the reverted tree
(`logs/test-11-revert-final-red.log`: `FAIL! ... '!result.ok' returned FALSE. (project.save_version
did not refuse a version it cannot advance)`, `Totals: 2 passed, 1 failed`) and exits **0** with
the change (`logs/test-12-reapply-final-green.log`: `100% tests passed out of 4` for the family,
`ControlCommandsSaveVersionTest` included). The same test also fails on the round-1 tree, which carries the widened version number but
not this fix (`logs/test-1-red.log`, exit 8): there the command answered SUCCESS and wrote the
project over the current file instead of refusing.

**2. The test is deterministic: no wall-clock timing, sleeps or races.**
The assertions read only the command's reply and the bytes on disk; nothing is timed, slept or
raced. The values are injected, not observed: the project is written to a `QTemporaryDir` path
whose suffix is the widest version number, and the call is dispatched through
`ControlRegistry::instance()->invoke("project.save_version")` - the same entry point the File >
Save as New Version menu action uses. The one time-based element is the helper thread's budget
(`kBudgetMs = 5000`), and it is a **safety net for the pre-fix spin**, not an assertion: the
helper only removes the file when the handler is still running after the budget, and the
outcome of the test is the same whether the file is removed before the handler's first
existence check or during the spin (in both cases the pre-fix code ends up writing the project
over the current file instead of refusing, and the test fails on the same assertion). With the
fix, the handler returns in microseconds and the helper never touches the file.

**3. The fix is at the cause and complete: every caller and sibling of what you changed was
checked and stays consistent.**
The three callers of `changeFileNameVersion` are listed in section 3: the two dialog buttons
cannot loop (no loop, the name simply stays), and the one caller that loops is the one fixed.
The sibling commands are listed there too: `project.save` has no version loop,
`MainWindow::saveProjectAsNewVersion` forwards the refusal to `handleSaveResult`, the
`window.*`/`app.*` commands in the same file do not touch the version number, and the only
other existence loop in the tree (`src/core/RenderQueue.cpp:118`) draws a UUID each turn. The
wider type the value flows into is unchanged from round 1 (`qulonglong` throughout
`changeFileNameVersion`); the value now stops at the refusal instead of wrapping.

**4. No collateral damage: old projects and saved data still load as before, and any limit or
check you added does not hit other callers that legitimately exceed it.**
No file format, no project data and no saved state is touched: the change is one command
handler's loop and its description. The version numbers that used to work still work - a
project at `mix-07.mmp` still becomes `mix-08.mmp` (the python suite's own check,
"project.save_version writes the next version beside the first", passes). The new refusal
fires only where the old code could not produce a name at all (the widest version number, or a
suffix that is not a number), and there the old code did not save a version either - it spun.
The round-1 widening (ushort -> qulonglong) is what let `mix-65535.mmp` reach `mix-65536.mmp`
at all; no limit was added here, so no caller that legitimately exceeds one is caught.

**5. If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
contract that shows it.**
Not applicable: the verdict is FIXED. The evidence is in sections 1 and 2
(`src/gui/modals/VersionedSaveDialog.cpp:46-51` and `:126-128` for the reported defect,
`src/gui/ControlCommandsShell.cpp:400-415` for the consequence).

**6. Sibling commands in the command files you changed - confirm each is consistent with your
fix (or say why it is not affected): app.about, app.ask, app.help, app.online_help,
app.task_cancel, app.tasks, app.tour, app.tours, app.version, app.whats_new.**
None of them is affected: they are registered in `src/gui/ControlCommandsShell.cpp` beside
`project.save_version`, and they answer about the About dialog, the task list, the help pages,
the tour and the version number of the product. The only thing this change alters in that file
is the `project.save_version` handler's version loop and that command's own description; the
`window.*` group in the same file (more than thirty commands, so the checker leaves it out) is
untouched as well. The regenerated snapshot carries the same ids for all of them
(`ids_sha256` unchanged).

**7. Realtime, undo and commands.**
Nothing on the audio thread changed: the fix is in a GUI command handler that runs on the UI
thread, and it adds no allocation, no lock, no wait and no logging on any path. No new control
command was added - the id surface is byte-identical (828 ids, the same `ids_sha256`), so the
`ControlCommandsSnapshot` canary does not move. Undo: `project.save_version` is registered as
`RC::Irreversible` (`src/core/ControlReversibilityTableShell.cpp:63`), so `bracketsEdit`
(`src/core/ControlRegistry.cpp:307`) gives it no journal bracket at all - a refusal records no
step and no snapshot, exactly like the refusal that was already there for a session with no
file.
