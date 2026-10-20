# FIXED - cluster new-p9-500-port: version numbers silently wrap or reset

Three rounds, one cause and its consequences. Round 1 fixed the cause (the version number was
parsed into a `ushort`); round 2 fixed the consequence the first fix exposed (the agent
surface's `project.save_version` ignored the refusal and spun for ever); round 3 made the
regression deterministic - a test seam instead of a timed helper thread that deleted the
fixture.

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-500-port.r2/logs`
(the round-1 logs are in `.../new-p9-500-port.r1/logs`).

## 1. The reported bug: REAL, fixed in round 1 (commit 1148dbdce)

`src/gui/modals/VersionedSaveDialog.cpp` parsed the version into a `ushort` and asserted the
parse:

- `Mix-65535.wav` + increment: `65535 + 1` stored in a `ushort` wraps to `0` -> `Mix-00.wav`.
- `Mix-65536.wav` + increment: `toUShort` fails; `Q_ASSERT` compiles out in this build
  (`CMAKE_BUILD_TYPE=RelWithDebInfo`, `-DNDEBUG`), so the value is `0` -> `Mix-01.wav`.

Fix (round 1, already on this branch): `src/gui/modals/VersionedSaveDialog.cpp` parses with
`toULongLong` into a `qulonglong` and refuses an unparseable number or an increment past
`std::numeric_limits<qulonglong>::max()`, before the name is touched. The four regressions are
in `tests/src/gui/VersionedSaveDialogTest.cpp:42-68`.

## 2. The consequence round 1 exposed: REAL, fixed in round 2 (commit fe509b7b9)

Luna's round-1 review (`qa/reviews/new-p9-500-port.r1.md`) returned REWORK:

> There is a new hang at ControlCommandsShell.cpp:398. `project.save_version` ignores
> `changeFileNameVersion`'s return value inside a loop. At the maximum numeric suffix, the
> function now refuses without changing the path; if that project file exists,
> `QFile::exists(next)` stays true and the command loops forever.

That was what the loop at `src/gui/ControlCommandsShell.cpp:398-400` did:

```cpp
QString next = previous;
do { gui::VersionedSaveDialog::changeFileNameVersion(next, true); }
while (QFile::exists(next));
```

The refusal (false) was dropped, so `next` stayed the current name; the file it names exists,
so the loop never ended - a busy spin on the UI thread, with the command never answering.

### The fix (round 2)

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
description now says so, because that description is what an agent reads.

## 3. The regression, made deterministic (round 3, this commit)

Luna's round-2 review (`qa/reviews/new-p9-500-port.r2.md`) returned REWORK on the TEST, not on
the fix:

> the regression uses a 5-second wait and a helper thread that deletes the test file ... That
> is a timing and file-operation race, contrary to the deterministic-test requirement.
> Replace it with a deterministic test seam or synchronization hook; the regression should not
> alter the file to escape the old loop.

The old helper (`StuckLoopBreaker`) waited 5 s and then removed the project file so that the
pre-fix loop would end. That was a race on the fixture and a wall-clock wait. It is gone.

### The seam

`include/VersionedSaveDialog.h:66-78` declares the seam, in the shape this codebase already
uses for test seams (`PortMapping::setTestEndpoints`, `WasmWorker::setAudioEndpointEntry`):

```cpp
using VersionNumberHook = std::function<bool(const QString& digits, qulonglong& version)>;
static void setVersionNumberHook( VersionNumberHook hook );
```

The hook answers **which version number a file name carries**. While no hook is installed the
product parses the digits itself, exactly as before - the shipped behaviour is unchanged
(`src/gui/modals/VersionedSaveDialog.cpp:46-57`):

```cpp
static bool versionNumberOf(const QString& digits, qulonglong& version)
{
	if (s_versionNumberHook) { return s_versionNumberHook(digits, version); }
	bool ok = false;
	version = digits.toULongLong(&ok);
	return ok;
}
```

`parseIncrementableVersion` (`:61-65`) asks that helper, and the rest of the round-1 rule is
untouched: an unparseable number, or an increment past the widest value, refuses before the
name is touched.

### What the test does with it

`tests/src/gui/ControlCommandsSaveVersionTest.cpp:77-111` - a project saved as
`mix-18446744073709551615.mmp` (the widest version number), then `project.save_version`
dispatched through the registry, with the hook answering two numbers in order:

1. the widest version number - so the FIRST answer refuses exactly as the digits in the name do
   (this is what the product sees with the fix: the command must refuse);
2. `42` - so that a caller which IGNORES the refusal gets a name that is free and its loop
   ends. The pre-fix code therefore answers "success" after two iterations instead of spinning
   for ever.

No timer, no sleep, no file operation outside the assertions: the loop is bounded by what the
hook says, so the RED run is a plain assertion failure (2.7 s, all of it the GUI start-up) and
the GREEN run is the same. The assertions are the three the review asked to keep - the command
refuses (and says why), the session's current project path is unchanged, and the bytes of the
file it cannot replace are unchanged.

## 4. Callers and siblings checked

`changeFileNameVersion` has three callers; all three are consistent:

| Caller | What it does with the refusal |
|---|---|
| `src/gui/modals/VersionedSaveDialog.cpp:165` (`incrementVersion`) | The name stays as it is and the dialog re-selects it. No loop, no hang; the + button simply does not move the version. |
| `src/gui/modals/VersionedSaveDialog.cpp:179` (`decrementVersion`) | Same, for the - button. |
| `src/gui/ControlCommandsShell.cpp:407` (`project.save_version`) | The only caller that loops; it now returns the refusal (round 2). |

Siblings of the command, checked and left alone:

- `project.save` (`src/core/ControlCommandsProjectFiles.cpp:178`) has no version loop: it writes
  to the path it is given, so it cannot spin.
- `MainWindow::saveProjectAsNewVersion` (`src/gui/MainWindow.cpp:1164-1180`) dispatches
  `project.save_version` and passes the answer to `handleSaveResult`
  (`src/gui/MainWindow.cpp:1999`), which shows the refusal; nothing else to change.
- The other commands in `src/gui/ControlCommandsShell.cpp` (`window.*`, `app.*`) do not touch the
  version number and are unchanged.
- The only other `while (QFile::exists(...))` in the tree is `src/core/RenderQueue.cpp:118`,
  which draws a UUID each turn and therefore always terminates; it is a different concern
  (the render output name) and is untouched.

## 5. RED / GREEN evidence (round 3)

Every command below was run in this worktree; the exit codes are the ones observed. The
reversed patch is the ROUND-2 FIX (`src/gui/ControlCommandsShell.cpp`) - the seam stays, because
reversing it would remove the hook the test installs (a build failure) instead of letting the
test fail on its assertion.

| Step | Command | Exit | Result |
|---|---:|---:|---|
| Build the seam and the test | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-500 cmake --build build --target zene ControlCommandsSaveVersionTest -j4 > logs/build-15-seam.log 2>&1` | 0 | |
| GREEN, before the reversal | `cd build/tests && ctest -R '^ControlCommandsSaveVersionTest$' --output-on-failure > logs/test-14-seam-green.log 2>&1` | 0 | `100% tests passed out of 1`. |
| The fix as a patch | `git diff 1148dbdce HEAD -- src/gui/ControlCommandsShell.cpp > logs/fix-r2.patch` | 0 | 39 lines: the round-2 fix in the handler. |
| Reverse it | `git apply -R logs/fix-r2.patch` | 0 | The fix is gone; the seam and the test stay. |
| Rebuild on the reverted tree | `cmake --build build --target zene ControlCommandsSaveVersionTest -j4 > logs/build-16-seam-revert.log 2>&1` | 0 | |
| **RED** | `cd build/tests && ctest -R '^ControlCommandsSaveVersionTest$' --output-on-failure > logs/test-15-seam-red.log 2>&1` | **8** | `FAIL! ... '!result.ok' returned FALSE. (project.save_version did not refuse a version it cannot advance)`; `Totals: 2 passed, 1 failed` in 2722 ms - the hook ended the loop at once, there is no wait and no file operation in the run. |
| Restore | `git checkout -- src/gui/ControlCommandsShell.cpp` | 0 | The committed fix is back. |
| Rebuild with the fix | `cmake --build build --target zene ControlCommandsSaveVersionTest -j4 > logs/build-17-seam-restore.log 2>&1` | 0 | |
| **GREEN** | `cd build/tests && ctest -R '^ControlCommandsSaveVersionTest$' --output-on-failure > logs/test-16-seam-green.log 2>&1` | **0** | `100% tests passed out of 1`. |
| The same test after the last edit (the hook is cleared in `cleanupTestCase` too) | `ctest -R '^ControlCommandsSaveVersionTest$' --output-on-failure > logs/test-18-final.log 2>&1` | 0 | `100% tests passed out of 1`. |
| The pair repeated on the **committed** tree | `git diff 1148dbdce HEAD -- src/gui/ControlCommandsShell.cpp > logs/fix-r2.patch` -> `git apply -R logs/fix-r2.patch` -> `cmake --build build --target zene ControlCommandsSaveVersionTest -j4 > logs/build-20-final-revert.log 2>&1` -> `ctest -R '^ControlCommandsSaveVersionTest$' --output-on-failure > logs/test-19-final-red.log 2>&1` | **0** (apply) / **0** (build) / **8** (test) | RED again: `FAIL! ... '!result.ok' returned FALSE. (project.save_version did not refuse a version it cannot advance)`, `Totals: 2 passed, 1 failed`, 2714 ms - no wait, no file operation. |
| Restore and rerun | `git checkout -- src/gui/ControlCommandsShell.cpp` -> `cmake --build build --target zene ControlCommandsSaveVersionTest VersionedSaveDialogTest ProjectContainerSaveAsGuiTest -j4 > logs/build-21-final-restore.log 2>&1` -> `ctest -R '^(ControlCommandsSaveVersionTest\|VersionedSaveDialogTest\|ProjectContainerSaveAsGuiTest\|ControlCommandsSnapshot\|ControlSuccessPathsTest\|ControlSurfaceReferenceTest\|AgentSurfaceReportTest)$' --output-on-failure > logs/test-20-final-green.log 2>&1` | 0 / 0 / **0** | GREEN: `100% tests passed out of 7`. `git status --porcelain` printed nothing. |

The round-2 evidence (the same test with the timed helper) is kept in the round-2 logs:
`logs/test-1-red.log` (exit 8), `logs/test-11-revert-final-red.log` (exit 8),
`logs/test-12-reapply-final-green.log` (exit 0), `logs/test-13-final-committed.log` (7 of 7).

## 6. Affected family

| Command | Exit | Count |
|---|---:|---|
| `ctest -R '^(ControlCommandsSaveVersionTest\|VersionedSaveDialogTest\|ProjectContainerSaveAsGuiTest\|ControlCommandsSnapshot\|ControlSuccessPathsTest\|ControlSurfaceReferenceTest\|AgentSurfaceReportTest)$' --output-on-failure` (`logs/test-17-family.log`) | 0 | **7 of 7** ctests passed. `VersionedSaveDialogTest` 7 QTest slots, `ProjectContainerSaveAsGuiTest` 6, `ControlCommandsSaveVersionTest` 3, plus the snapshot check and the three control-surface tests. |
| The same family after the last edit (`logs/test-18-final.log`, the regression alone) | 0 | `100% tests passed out of 1`. |
| The wider control family (`logs/test-8-control-family.log`, round 2) | 8 | 7 of 11 passed; the 4 failures were NOT this change: two binaries were never built in this tree, one was a STALE binary (built before the tree was reverted) and one python test failed on the MIDI import/export filters, whose plugin libraries were never built here (`build/plugins/MidiExport/` holds only the CMake scaffolding). The three that could be rebuilt pass (`logs/test-9-rebuilt-family.log`, exit 0). |

The python `ControlSuccessPaths` run reports `project.save_version writes the next version beside
the first  ok` - the ordinary version bump still works.

Product build: `cmake --build build --target zene ... -j4` -> exit **0** in every step above.

## 7. Gates

| Command | Exit | Note |
|---|---:|---|
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`logs/feature-check-3-seam.log`) | **0** | `RESULT: PASS` - fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures all exit 0; complexity and file-length report "no new item in your files". |
| `bash tests/complexity-gate.sh --check` (`logs/gate-complexity-2.log`) | 1 | The same six pre-existing entries as the base listing in `~/.local/share/zene-bunny-fix/qwen/gate-baselines/ded355440871a86cc907cdc2d6465f2d3d597a6b.json` (XyPadPairTest, DawProjectReadTracks, ControlCommandsMixerTools, ControlCommandsMixerStrip, ProjectRevisions, ControlCommandsShortcuts). None of them is this change's file. The touched functions stay under the target: `changeFileNameVersion` CCN 9, `parseIncrementableVersion` CCN 3, `versionNumberOf` CCN 2, `registerSaveVersion` CCN 6, the test's slot CCN 2 (measured with lizard). |
| `bash tests/file-length-gate.sh --check` (`logs/gate-file-length-2.log`) | 1 | The same fourteen pre-existing entries as the base listing. The new test file is 110 lines (limit 500). |
| `bash tests/all-sources-reproduce.sh` | 0 | `REPRODUCES` - the manifest carries the new test file. |

No gate limit and no baseline was moved.

## 8. The command snapshot

`tools/mcp-zene-control/zene_control/commands_snapshot.json` was regenerated from a live
instance of this build in round 2 (`tools/mcp-zene-control/snapshot_commands.py --from-file`;
`logs/snapshot-dump-2.log`, `logs/snapshot-regen-2.log`). The diff is three lines:
`captured_at`, the provenance's `source_file`, and the `project.save_version` description. The
id surface is unchanged - 828 ids across 76 groups, `ids_sha256` identical - so **no command
was added** (the canary does not move) and `ctest -R '^ControlCommandsSnapshot$'` passes
(`logs/test-17-family.log`). Round 3 changed no command surface, so the snapshot is untouched
by this commit.

## 9. Files in this commit

- `include/VersionedSaveDialog.h` - declares the `VersionNumberHook` test seam and its setter.
- `src/gui/modals/VersionedSaveDialog.cpp` - the version number of a name comes from the
  installed hook when a test installed one, and from the parsed digits otherwise.
- `tests/src/gui/ControlCommandsSaveVersionTest.cpp` - the regression, rewritten around the
  seam: no timer, no thread, no file operation outside the assertions.
- `tests/upstream-modifications.txt` - the two changed upstream-inherited files get their
  reason lines (the header already had one from Q050-007; both get a line for this seam).
- `docs/qwen-fixes-2026-10/cluster-new-p9-500-port.md` - this report.

`src/gui/ControlCommandsShell.cpp` (the round-2 fix) and
`tools/mcp-zene-control/zene_control/commands_snapshot.json` are already committed.

## Self-check

**1. The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
`ctest -R '^ControlCommandsSaveVersionTest$'` exits **8** with the round-2 fix reversed
(`logs/test-15-seam-red.log`: `FAIL! ... '!result.ok' returned FALSE. (project.save_version did
not refuse a version it cannot advance)`, `Totals: 2 passed, 1 failed`) and exits **0** with it
(`logs/test-16-seam-green.log`: `100% tests passed out of 1`). The reversed patch is
`logs/fix-r2.patch` (`git diff 1148dbdce HEAD -- src/gui/ControlCommandsShell.cpp`), reversed
with `git apply -R` and restored with `git checkout -- src/gui/ControlCommandsShell.cpp`.

**2. The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected
values it uses).**
The seam is `VersionedSaveDialog::setVersionNumberHook` (`include/VersionedSaveDialog.h:76-77`,
`src/gui/modals/VersionedSaveDialog.cpp:46-71`): the hook answers which version number a name
carries, and the product asks it instead of parsing the digits while a test installed it. The
test injects two numbers in order - the widest version number first (so the first answer
refuses exactly as the digits do) and `42` after it (so a caller that ignores the refusal gets
a free name and its loop ends). The injected values are fixed; nothing is timed, slept or
raced, and the file is only READ - never removed, rewritten or renamed - by the test. The
helper thread, the 5-second budget and the `QFile::remove` are gone. The assertions read the
command's reply and the bytes on disk.

**3. The fix is at the cause and complete: every caller and sibling of what you changed was
checked and stays consistent.**
The three callers of `changeFileNameVersion` are listed in section 4: the two dialog buttons
cannot loop (no loop, the name simply stays), and the one caller that loops is the one fixed.
The sibling commands are listed there too: `project.save` has no version loop,
`MainWindow::saveProjectAsNewVersion` forwards the refusal to `handleSaveResult`, the
`window.*`/`app.*` commands in the same file do not touch the version number, and the only
other existence loop in the tree (`src/core/RenderQueue.cpp:118`) draws a UUID each turn. The
seam is consulted by exactly one function (`versionNumberOf`, reached from
`parseIncrementableVersion` and from nowhere else), and with no hook installed it is the
product's own parse.

**4. No collateral damage: old projects and saved data still load as before, and any limit or
check you added does not hit other callers that legitimately exceed it.**
No file format, no project data and no saved state is touched. The version numbers that used
to work still work - a project at `mix-07.mmp` still becomes `mix-08.mmp` (the python suite's
own check, "project.save_version writes the next version beside the first", passes). The seam
is inert unless a test installs it: with no hook installed the product parses the digits
exactly as in round 1, so the shipped behaviour of the Save As dialog and of every other
caller is byte-identical. The refusal fires only where the old code could not produce a name
at all (the widest version number, or a suffix that is not a number), and there the old code
did not save a version either - it spun.

**5. If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
contract that shows it.**
Not applicable: the verdict is FIXED. The evidence is in sections 1-3.

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
thread, and the seam is a static hook in the Save As dialog's own translation unit - it adds
no allocation, no lock, no wait and no logging on any path. No new control command was added -
the id surface is byte-identical (828 ids, the same `ids_sha256`), so the
`ControlCommandsSnapshot` canary does not move. Undo: `project.save_version` is registered as
`RC::Irreversible` (`src/core/ControlReversibilityTableShell.cpp:63`), so `bracketsEdit`
(`src/core/ControlRegistry.cpp:307`) gives it no journal bracket at all - a refusal records no
step and no snapshot, exactly like the refusal that was already there for a session with no
file.

FIXED
