FIXED

# cluster new-p9-58 — BUG 2: the undo of a revision restore restored the wrong file

## Decision: REAL (was REAL on this branch, now fixed)

The finding said the undo callback of `revisions.restore` "looks up mutable revision slot `0`
instead of preserving the bytes it needs to restore". Both recorded undo halves did exactly that, on
this branch, at the lines below (`git log -1` at the start: `21c2f3ee2e`, branch `bugs/qwen-new-p9-58`).

* `src/core/ControlCommandsRevisions.cpp:189` (before this fix) — `undoRestoreStep()`:
  `if (control::restoreProjectRevision(target, 0, &reason)) { return; }`. The undo of
  `revisions.restore` restored **revision 0 by number**.
* `src/core/ControlCommandsProjectFiles.cpp:394` (before this fix) — the undo closure of
  `project.restore_revision`, the same command family, the same wrong-slot assumption:
  `if (control::restoreProjectRevision(target, 0, &reason)) { return; }`.
* Why slot 0 is not the file that restore replaced: `control::restoreProjectRevision`
  (`src/core/ProjectRevisions.cpp:441-455`) rotates the live file in *before* it installs the staged
  bytes, and `rotateProjectRevision` (`src/core/ProjectRevisions.cpp:322-355`) shifts
  `rev0 -> rev1 -> rev2` and trims everything past `ProjectRevisionPolicy::Keep == 3`
  (`include/ProjectRevisions.h:43-52`). So **every later save moves the replaced file off slot 0**,
  and the undo then wrote a file the restore never touched over the live project and reported
  `undone: true`.

Measured on the unfixed tree (`test-red-1.log`, `test-red-4.log`): 4 of the 5 new assertions fail,
including "control.undo of revisions.restore did not put back the file the restore replaced - it
restored revision 0 by NUMBER, and that slot now holds the restored document".

## The fix (at the cause, both siblings)

The replaced file is identified by **content**, exactly the discipline the *redo* half of these two
commands already uses for the bytes it brought in (Luna clusters 4878 / 5738): the sha256 of the live
file is taken **before** the restore replaces it, and the undo restores the retained revision that
still holds those bytes. When no retained revision holds them any more (three rotations since), the
undo REFUSES typed instead of overwriting the project with another revision's bytes.

* `src/core/ControlCommandsRevisions.cpp:196-215` — `undoRestoreStep()` takes `replacedSha` and
  resolves it with `revisionHoldingBytes()` (`:120`); the refusal is recorded through
  `control::recordActionRestoreFailure()` so `control.undo` answers `Refused` naming
  `revisions.restore`.
* `src/core/ControlCommandsRevisions.cpp:460` — the handler takes that sha256 before the restore;
  `:269-275` (`recordRestoreUndoStep`) carries it into the closure.
* `src/core/ControlCommandsProjectFiles.cpp:381`, `:399-417` — the same change in
  `project.restore_revision`'s undo closure.
* `src/core/ControlCommandsProjectFiles.cpp:456-475` — its advertised `__transaction.inverse` is now
  `control.undo` (the recorded step) in both cases. It used to advertise
  `project.restore_revision(revision: 0)`, i.e. a slot number, which is the same wrong claim; the
  created-file branch and `revisions.restore` (Luna cluster 1223) already advertise `control.undo`.
* `src/core/ControlReversibilityTableRevisions.cpp:91-96` and `docs/REVISION-TIMELINE.md:38-44` — the
  A16 mechanism text and the user document said the undo "restores revision 0"; they now say what it
  does. The A16 row count and histogram are unchanged (`ReversibilityContractTest` passes).

Nothing else changed: no command was added or removed, no schema changed, no gate, ratchet or
baseline was touched, and the undo is still one step per command (every case below is one
`control.undo` per restore).

## Regression test

`tests/src/core/RevisionRestoreUndoAfterSaveTest.cpp` (new, registered in `tests/CMakeLists.txt`,
`tests/fork-sources.txt`, `tests/all-sources.txt`, and `tests/CMakeLists.txt` in
`tests/upstream-modifications.txt`). It drives the real registry against real files in a temp
directory, so the socket path is what is proved. Five cases:

| case | path covered |
| --- | --- |
| `undoWithoutARotationRestoresTheReplacedFile` | normal path: nothing rotates, the replaced file is still slot 0 |
| `undoAfterASaveRotatedTheSetRestoresTheReplacedFile` | **the bug**: the rotation a save performs (`control::rotateProjectRevision`, the same call `project.save` makes) moves the replaced file to revision 1 |
| `undoOfProjectRestoreRevisionAfterASaveRotatedTheSetRestoresTheReplacedFile` | the sibling command `project.restore_revision`, same defect |
| `undoAfterASecondRestoreRotatedTheSetRestoresTheReplacedFile` | registry-only route: a second `revisions.restore` rotates the set, then two `control.undo`s |
| `undoRefusesTypedWhenTheReplacedBytesLeftTheKeepThreeSet` | exhaustion: three rotations evict the bytes; the undo must refuse typed and leave the live file alone |

The rotation is injected deterministically (`rotateProjectRevision` + a plain write), never by a
sleep, a clock or a race. Driving the `project.save` command itself would put the save's own recorded
inverse *above* the restore's step and the step under test would not run (it would take three undos);
the test comment says so.

`tests/src/core/RevisionTimelineTestParts2.h` — the two existing failure-path guards
(`undoOfARestoreThatCannotPutTheFileBackFailsTyped`,
`undoOfAProjectRestoreRevisionThatCannotPutTheFileBackFailsTyped`) removed **revision 0** and expected
the undo to fail. The undo's artefact is now the *bytes*, so the guards remove **every retained copy**
of the replaced file, assert that at least one existed, and keep every original assertion (`!ok`,
`Refused`, names the command, file unchanged). That is the same shape the redo's own exhaustion case
in that file already uses; nothing was weakened, and both guards pass on the unfixed tree as well
(`test-red-3-family.log`).

## Commands and exit codes (all logs in `~/.config/kads/zene/state/runs/new-p9-58.r1/logs`)

RED, before any product change (`tests/.../RevisionRestoreUndoAfterSaveTest.cpp` only):

```
$ cmake --build build --target RevisionRestoreUndoAfterSaveTest -j 4 > logs/build-red-2.log 2>&1
EXIT=0
$ cd build/tests && ctest -R RevisionRestoreUndoAfterSaveTest --output-on-failure > logs/test-red-1.log 2>&1
EXIT=8      # 3 passed, 4 failed
```

GREEN, with the fix:

```
$ cmake --build build --target RevisionRestoreUndoAfterSaveTest zene -j 4 > logs/build-green-1.log 2>&1
EXIT=0
$ cd build/tests && ctest -R RevisionRestoreUndoAfterSaveTest --output-on-failure > logs/test-green-1.log 2>&1
EXIT=0
```

Step 4b, proved with the FINAL product code and `logs/fix.patch` (the patch committed in
`7db49a6dfe`), run once more after the commit so the chain below is exactly what was executed
(`logs/proof-red-1.log`, `logs/build-red-5.log`, `logs/test-red-5.log`, `logs/build-green-5.log`,
`logs/test-green-5.log`):

```
$ git diff -- src/core/ControlCommandsRevisions.cpp src/core/ControlCommandsProjectFiles.cpp \
      src/core/ControlReversibilityTableRevisions.cpp docs/REVISION-TIMELINE.md > logs/fix.patch
EXIT=0
# RED, the whole chain (recorded: APPLY_R_EXIT=0, BUILD_EXIT=0, CTEST_EXIT=8)
$ git apply -R logs/fix.patch && cmake --build build --target RevisionRestoreUndoAfterSaveTest -j 4 \
      && cd build/tests && ctest -R RevisionRestoreUndoAfterSaveTest --output-on-failure > logs/test-red-5.log 2>&1
EXIT=8      # Totals: 3 passed, 4 failed - the test catches the bug
# GREEN, the whole chain (recorded: APPLY_EXIT=0, BUILD_EXIT=0, CTEST_EXIT=0)
$ cd <worktree> && git apply logs/fix.patch && cmake --build build --target RevisionRestoreUndoAfterSaveTest zene -j 4 \
      && cd build/tests && ctest -R RevisionRestoreUndoAfterSaveTest --output-on-failure > logs/test-green-5.log 2>&1
EXIT=0
$ git status --short      # empty: the tree is the committed fix again
```

The first measurement of the same proof (before the final comment compaction) is in
`logs/build-red-4.log`, `logs/test-red-4.log`, `logs/build-green-4.log`, `logs/test-green-4.log`:
builder `EXIT=0`, ctest `EXIT=8` (3 passed, 4 failed), then ctest `EXIT=0`.

The rest of the family (12 tests: `RevisionTimelineTest`, `ProjectRevisionRestoreStagingTest`,
`ProjectRestoreMissingTargetTest`, `RevisionRestoreCreatedFileUndoTest`,
`RevisionRestoreUndoAfterSaveTest`, `ReversibilityUndoTest`, `ReversibilityContractTest`,
`UndoBoundsTest`, `ProjectContainerSaveTest`, `UndoHonestyTest`, `JournallingObjectRestoreTest`,
`RevisionsDocsTest`):

```
$ cd build/tests && ctest -R "<the twelve>" --output-on-failure > logs/test-family-5.log 2>&1
EXIT=0      # 100% tests passed, 12 of 12
```

`ReversibilityUndoDeviceTest` is NOT in that list because it fails for an unrelated, pre-existing
reason ("the LADSPA host plugin 'ladspaeffect' is not in this build"). It fails identically with the
fix reverted (`logs/test-red-3-family.log`, EXIT=8, same two cases), so it is not a regression here;
`tests/CMakeLists.txt` and the plugin scan are untouched by this change.

Gates (unpiped, exit recorded):

```
$ python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>
feature-check kads-new-p9-58 (base fa4a5f2e7, 435 changed files)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
EXIT=0      # logs/feature-check-2.log
```

## Test matrix (step 3)

* **Normal path** — no rotation between the restore and the undo: `undoWithoutARotationRestoresTheReplacedFile`
  and the existing `RevisionTimelineTest::aRedoRestoresTheBytesTheRestoreSelected`.
* **Failure** — the artefact is gone: the two updated `RevisionTimelineTest` guards (undo refuses
  `Refused`, names the command, leaves the file untouched) and the redo counterparts, unchanged.
* **Exhaustion** — three rotations evict the replaced bytes: `undoRefusesTypedWhenTheReplacedBytesLeftTheKeepThreeSet`.
* **Reconfiguration** — none: no setting, cap or policy value changed; the keep-3 policy, its 8 MiB cap
  and the schema of both commands are untouched.
* **Audio thread** — no render/play path is touched. The changed code is two control-command closures
  (UI/control thread, they do file I/O by design) plus two string constants; no allocation, mutex,
  wait or log was added to anything that runs per period.
* **Platforms** — only `QFile`/`QCryptographicHash` and the existing policy helpers
  (`projectRevisionState`, `restoreProjectRevision`) are used; no new platform-specific code, no new
  file over 500 code lines (file-length PASS), and everything that includes
  `ProjectRevisions.h`/`RevisionTimeline.h` was rebuilt (`zene` + the twelve test targets).

## Self-check

* **Registration** — `tests/src/core/RevisionRestoreUndoAfterSaveTest.cpp` is in
  `tests/fork-sources.txt` and `tests/all-sources.txt` (same entry, sorted position), and
  `tests/CMakeLists.txt` is in `tests/upstream-modifications.txt` with its reason. The new file is
  `git add`-ed before the gates run: the manifests' recipe derives from tracked paths, and the first
  `feature_check` run (before the add) correctly reported it missing (`logs/feature-check-1.log`,
  FAIL on `fork-sources`); the final run PASSes (`logs/feature-check-2.log`). Both lists end with a
  newline.
* **Agent surface / commands** — no command, argument, result schema or description was added or
  removed; the two changed result strings (`recovered_by`, `__transaction.mechanism`) are free-form
  text, not schema, and `control-commands-snapshot.py`'s canary list of commands is unchanged (no new
  command id anywhere in the diff).
* **Undo stays one honest step** — every case above restores the project with a single
  `control.undo`; the step count is unchanged (the same one `control::addUndoStep` call per restore,
  only its closure carries one more string).
* **Old projects and saved data** — untouched: the project file formats, the `.rev0..rev2` slots, the
  `.bak`, the autosave sidecar and `project.get_state`'s `revisions` object are unchanged. A file over
  the 8 MiB cap is still refused before anything is written (`RevisionTimelineTest::aRefusedRestoreRecordsNoUndoStep`
  passes), and no check was added that could reject a legitimate caller.
* **Test proves the change** — step 4b above, with the final patch: RED `EXIT=8` (4 failing
  assertions), GREEN `EXIT=0`.
* **Docs** — `docs/REVISION-TIMELINE.md` says what the undo does now; the A16 row text in
  `src/core/ControlReversibilityTableRevisions.cpp` matches it; the row count and histogram are
  unchanged (`ReversibilityContractTest` passes, `doc-figures` passes).
* **Realtime** — stated above; no per-period code touched.
* **Not verified / limits** — local TSan does not run on this box, so no race claim is made: the
  change adds no shared state (the closures capture two `QString`s by value and run on the control
  thread). The residual behaviour, stated plainly: the replaced bytes are only recoverable while the
  keep-3 set retains them; after three further rotations the undo refuses typed instead of restoring
  a wrong file. That is the honest bound of the policy, the same one the redo half already applies,
  and it is covered by its own case.

### The pre-submit items, one by one

1. **"The regression test FAILS with your product change reverted and PASSES with it (exit codes)"** —
   measured twice; the last measurement uses the exact patch that is committed:
   `git apply -R logs/fix.patch` `EXIT=0`, rebuild `EXIT=0`, `ctest -R RevisionRestoreUndoAfterSaveTest`
   `EXIT=8` (`Totals: 3 passed, 4 failed`); `git apply logs/fix.patch` `EXIT=0`, rebuild `EXIT=0`,
   `ctest -R RevisionRestoreUndoAfterSaveTest` `EXIT=0`. Logs: `build-red-5.log`, `test-red-5.log`,
   `build-green-5.log`, `test-green-5.log` (and the earlier `-4` pair with the same exits).
2. **"The test is deterministic: no wall-clock timing, sleeps or races"** — no `QTest::qWait`, no
   sleep, no clock and no thread in the test. Its state is injected by writes to a `QTemporaryDir` and
   by one call to the product's own `control::rotateProjectRevision()` (the call `project.save` makes
   before it writes), and it reads back the bytes with plain `QFile` reads. `Engine::init(true)` and
   `ControlRegistry::setReady(true)` in `initTestCase` are the only environment it needs, and
   `ZENE_RECOVERY_FILE` is pointed at the temp directory so no real session file is read.
3. **"The fix is at the cause and complete: every caller and sibling"** — the cause is the two
   recorded undo halves, and both are fixed (`ControlCommandsRevisions.cpp:196`,
   `ControlCommandsProjectFiles.cpp:399`); there is no third caller of
   `restoreProjectRevision(target, 0, ...)` left in the tree — `git grep -n "restoreProjectRevision("`
   shows only the two undo halves (now content-resolved), the policy's own definition, the timeline
   delegation (`RevisionTimeline.cpp:433`) and `project.save`'s advertised inverse. The redo halves
   already resolved by content and are unchanged; the wider value is a `QString` sha256 that flows
   into `revisionHoldingBytes()` in the same file that computed it.
4. **"No collateral damage"** — the file formats, the `.rev0..rev2` slots, the `.bak`, the autosave
   sidecar and every result schema are unchanged; a project file over the 8 MiB cap is still refused
   before anything is written (`RevisionTimelineTest::aRefusedRestoreRecordsNoUndoStep` passes) and no
   new limit was added that a legitimate caller could exceed. The whole-tested family above (12 tests,
   including `ProjectContainerSaveTest`, `ReversibilityUndoTest`, `ProjectRestoreMissingTargetTest`)
   passes, which is the old-data and other-caller evidence.
5. **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE"** — not applicable: the verdict is FIXED and
   the pre-fix lines are quoted at the top of this report (`ControlCommandsRevisions.cpp:189`,
   `ControlCommandsProjectFiles.cpp:394` on the branch before the patch).
6. **"Sibling commands in the command files you changed"** — the only other change in a command file
   is a *text* change: the mechanism string of the `revisions.restore` row and the
   `project.restore_revision` transaction, plus the `revisions.restore` `recovered_by` result string.
   `app.about`, `app.ask`, `app.help`, `app.online_help`, `app.task_cancel`, `app.tasks`, `app.tour`,
   `app.tours`, `app.update_check_get`, `app.update_check_run`, `app.update_check_set`, `app.version`,
   `app.whats_new` and `arrangement.fit_tracks` / `arrangement.get_state` live in other command files
   or in other A16 rows of `ControlReversibilityTableShell.cpp` /
   `ControlReversibilityTableArrangement.cpp`; my diff does not touch them, their rows, their schemas
   or their handlers, so they are unaffected (evidence: `git diff --stat` lists four files, none of
   them theirs; `ReversibilityContractTest`'s row count and histogram PASS unchanged). The two
   siblings that DO share the mechanism — `revisions.restore` and `project.restore_revision` — are both
   fixed and both advertise `control.undo` as their inverse.

## Other findings

* `ReversibilityUndoDeviceTest` fails on this machine for an unrelated reason: "the LADSPA host
  plugin 'ladspaeffect' is not in this build" (two cases). Reproduced with the fix reverted, so it is
  pre-existing and environmental; not touched here.
* `cmake -S . -B build` is refused by the lane guard ("configuring is the parent's job"), so the new
  test target was picked up by the automatic regeneration `cmake --build` performs; recorded in
  `logs/cmake-reconfigure-1.log` and `logs/build-regen-1.log`.
