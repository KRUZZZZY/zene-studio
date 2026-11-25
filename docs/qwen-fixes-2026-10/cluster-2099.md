FIXED

# cluster 2099: a refused subproject placement left its rendered WAV orphaned

2099: FIXED

## Verdict: REAL

`project.subproject_add` renders another project into a new WAV and *then* makes the track that plays it
(`src/core/ControlCommandsSubprojects.cpp`, `addSubproject`). Between those two steps the file is named by
nothing: no clip, no `<subprojects>` link and no undo step. Before the fix the command returned
`refused` ("no track could be made for …") on that failure and left the WAV in
`<project>-subprojects/` — a file nothing in the project could ever play, and a path the caller was never
told.

The failure is real and not a dead branch: `landAudioTrack` (`src/core/ControlCommandsTrackFlatten.cpp:54`)
answers `nullptr` when `Track::create` refuses, and `Track::create` **throws** when it cannot allocate the
track (`src/core/Track.cpp:531-577`; the same seam is used by `tests/src/gui/ArrangerFileDropTest.cpp:80-83`).
The control surface translates that throw into a typed `refused` result
(`src/core/ControlRegistry.cpp:77-83`), so the command "returns an error without removing that file", exactly
as the finding says.

Evidence before the fix: the new regression test fails on the orphaned file only —

```
FAIL!  : ControlBounceRenderTest::aSubprojectRefusedForWantOfATrackLeavesNoRenderBehind() Compared lists have different sizes.
   Actual   (renders.entryList(QDir::Files)) size: 1
   Expected (QStringList{}) size: 0
   Loc: [../../tests/src/core/ControlBounceRenderTest.cpp(638)]
```

(log `test-1-red.log`, `ctest` EXIT=8; 14 of the 15 slots passed, so the render really ran and produced the
file, and the failure has nothing else in it).

## Fix (at the cause: who owns the render before a track plays it)

`src/core/ControlCommandsSubprojects.cpp:200-229`:

- `link` records the `<subprojects>` entry of a landed track (the step a redo also needs).
- `place` is the redo closure: land the same file on a new track and link it. Unchanged in behaviour.
- The first placement is guarded by itself, **not** by the link: `landAudioTrack` answering `nullptr`, or
  throwing because the song cannot allocate a track, deletes the render (`QFile::remove(audio)`, lines 221 and
  226) before the command returns/throws. A throw is re-thrown so the caller still gets the registry's typed
  error with its message and mutating note; only the file disappears.
- Deliberately *not* guarded: `section().entries.push_back` and everything after a landed track. A track that
  did land keeps the file it plays, so an allocation failure in the link can never delete audio a live clip
  plays. The redo path also keeps the file: a failed redo leaves the undo step able to retry, and that file is
  the add's own render, not a fresh one.

No save format, schema, registration or undo shape changed: the `<subprojects>` section is untouched, the
undo step is the same one, and the command list is unchanged (no new command).

## Test matrix

| path | scenario | coverage |
|---|---|---|
| normal | add renders, lands a track, links it, undo/redo | `aSubprojectIsRenderedLinkedListedAndRefreshed` (existing, still passes) |
| failure | render child fails | `subprojectRefusalsAreTypedAndLeaveNothingBehind` (existing, still passes) |
| refusal | missing file, non-project suffix, self-containment, no track / no link | `subprojectRefusalsAreTypedAndLeaveNothingBehind` (existing) |
| exhaustion | **the song cannot make another track** | `aSubprojectRefusedForWantOfATrackLeavesNoRenderBehind` (new) |
| reconfiguration | source changed after render, refresh, undo back | `aSubprojectIsRenderedLinkedListedAndRefreshed` (existing) |
| audio thread | none: this is a control handler on the UI thread; no process/render code touched | n/a |
| platforms | `QFile::remove`, `QDir::entryList`, `QStringList`, `std::bad_alloc` only; no Qt-6-only API, no POSIX path | the test builds where the suite builds |

New regression test: `tests/src/core/ControlBounceRenderTest.cpp:608-639`
(`aSubprojectRefusedForWantOfATrackLeavesNoRenderBehind`), in the existing test file for the subproject and
bounce verbs. It injects the failure with the existing product seam
`Track::setCreateFaultProbeForTest` (a probe that throws `std::bad_alloc` once, reset immediately after the
call), asserts the refusal, the unchanged track count, no link recorded, and an **empty** render folder that
the call itself created (`QVERIFY(renders.exists())` proves the render ran). No sleeps, no wall clock, no
timing: the decisive assertion is a directory listing.

## Commands and their EXIT codes (logs in `~/.config/kads/zene/state/runs/2099.r1/logs`)

RED (test only, product unfixed):

```
cmake --build build --target ControlBounceRenderTest -j4 > logs/build-1-red.log 2>&1; echo EXIT=$?
EXIT=0                                                          (build-1-red.log)
cd build/tests && ctest -R ControlBounceRenderTest --output-on-failure > logs/test-1-red.log 2>&1; echo EXIT=$?
EXIT=8                                                          (test-1-red.log: 14 passed, 1 failed - the new test)
```

GREEN (fix in):

```
cmake --build build --target ControlBounceRenderTest zene -j4 > logs/build-2-green.log 2>&1; echo EXIT=$?
EXIT=0                                                          (build-2-green.log)
cd build/tests && ctest -R ControlBounceRenderTest --output-on-failure > logs/test-2-green.log 2>&1; echo EXIT=$?
EXIT=0                                                          (test-2-green.log: 100% passed)
```

Step 4b, the test against the reverted product change:

```
git diff -- src/core/ControlCommandsSubprojects.cpp > logs/fix.patch; echo DIFF_EXIT=$?
DIFF_EXIT=0    (54-line patch, product hunk only)
git apply -R logs/fix.patch; echo REVERSE_APPLY_EXIT=$?
REVERSE_APPLY_EXIT=0
cmake --build build --target ControlBounceRenderTest -j4 > logs/build-3-reverted.log 2>&1; echo EXIT=$?
EXIT=0                                                          (build-3-reverted.log)
cd build/tests && ctest -R ControlBounceRenderTest --output-on-failure > logs/test-3-reverted-red.log 2>&1; echo EXIT=$?
EXIT=8                                                          (test-3-reverted-red.log: 14 passed, 1 failed)
git apply logs/fix.patch; echo REAPPLY_EXIT=$?
REAPPLY_EXIT=0
cmake --build build --target ControlBounceRenderTest zene -j4 > logs/build-4-restored.log 2>&1; echo EXIT=$?
EXIT=0                                                          (build-4-restored.log)
cd build/tests && ctest -R ControlBounceRenderTest --output-on-failure > logs/test-4-restored-green.log 2>&1; echo EXIT=$?
EXIT=0                                                          (test-4-restored-green.log: 100% passed)
```

Product path, through the real binary and the control socket (the same verbs on the agent surface):

```
QT_QPA_PLATFORM=offscreen python3 control-success-paths.py ../build/zene > logs/test-5-socket.log 2>&1; echo EXIT=$?
EXIT=1    - the two failures are `project.import` / `project.export_midi`, "no import filter in this build
            takes …" / "the midiexport filter is not loadable in this process": this build tree has no MIDI
            filter plug-ins (build/plugins has no *midi* target), unrelated to this fix. Both subproject
            checks pass:
              project.subproject_add renders the file onto a new track   ok
              project.subproject_refresh renders it again into a new file ok
```

Gates (all gates, one table):

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/feature-check.log 2>&1; echo EXIT=$?
feature-check kads-2099 (base 3436a808b, 4 changed files; logs in .../build/feature-check)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
EXIT=0                                                          (feature-check.log)
```

The new report and test are registered in `tests/fork-sources.txt` (the file ends with a newline); no inherited
file was changed, so `tests/upstream-modifications.txt` is untouched.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Reverted: build `EXIT=0`, `ctest` `EXIT=8` (14 passed, 1 failed, the new slot; `test-3-reverted-red.log`).
  Restored: build `EXIT=0`, `ctest` `EXIT=0` (`test-4-restored-green.log`). The initial RED before the fix was
  the same failure (`test-1-red.log`, `EXIT=8`).
- **The test is deterministic.** No sleep, no wall clock, no race. The failure is injected with the existing
  product seam `Track::setCreateFaultProbeForTest` (`include/Track.h`, used the same way by
  `tests/src/gui/ArrangerFileDropTest.cpp:509-517`): a probe that throws `std::bad_alloc` on its first call and
  is reset right after the command. The render is the suite's own stand-in child (`fakeRender` in this test's
  `main`), so no device, no timing. The assertion is a directory listing of the project's `-subprojects`
  folder; `QVERIFY(renders.exists())` shows the render ran.
- **The fix is at the cause and complete; every caller and sibling was checked.** Callers of `landAudioTrack`:
  `bounce.land`/`range.bounce` (`src/core/ControlCommandsRenderBounce.cpp:220,253,296`),
  `track.flatten` (`src/core/ControlCommandsTrackFlatten.cpp:137`) and `project.subproject_refresh` are
  unchanged and stay consistent — `refreshSubproject` renders, then always points the existing track's clips
  at the new file, keeping the old one for its undo step, so no failure sits between its render and its
  reference; `track.flatten` never returns a failure after its render: the render's path is what the structural
  undo step it pushes (`ControlCommandsTrackFlatten.cpp:122-137`) replays. `bounce.land`/`range.bounce` are the
  same *shape* but not the same ownership: their render path is the caller's `out` argument or the documented
  `<project>-bounces/` default, so the file is an artifact a user can find and use, and deleting it would be a
  second behaviour change (see Other findings). The subproject add was the only path where a fresh,
  internally named render could be abandoned. The redo closure still lands the same file through `place`, so a
  failed redo keeps the file the undo step needs to retry; the delete is scoped to the first placement only.
- **No collateral damage.** No saved format, section, schema, command id, reversibility row or undo shape
  changed; `<subprojects>` is written and read exactly as before. The guard covers only the placement, so a
  track that did land always keeps the file its clip plays (an allocation failure in `entries.push_back` cannot
  delete it). Existing slots, including save/reload of the link and the undo/redo pair, still pass
  (`test-4-restored-green.log`), and the socket run adds/refreshes a subproject through the real binary with
  `ok` replies (`test-5-socket.log`).
- **Undo is one honest step.** The add still pushes exactly one step (`addUndoStep` at
  `src/core/ControlCommandsSubprojects.cpp:230-234`), which removes the track and the link. The failure path
  pushes nothing, before and after the fix (the exception is raised before `addUndoStep`), so a refused add
  leaves no undo entry.
- **Realtime.** No audio-thread code is touched: the change is in a control-command handler that runs on the
  UI thread (`ControlRegistry::runOnUiThread`), and `QFile::remove` there is ordinary command I/O on a path
  that is already doing a child-process render.
- **Verdict is REAL, not ALREADY-FIXED/FALSE-POSITIVE**, so there is no "already fixed" claim to evidence: the
  pre-fix `if (track == nullptr) { return ControlResult::failure(...); }` at
  `src/core/ControlCommandsSubprojects.cpp:206-207` (HEAD before this commit) returned without touching
  `audio`, and the RED run above is that behaviour.
- **Sibling commands in the changed command file** — `project.subproject_add` (fixed), `project.subproject_refresh`
  (renders into a new file, then always re-points the existing track's clips: no failure between render and
  reference, and the superseded render is kept for the undo step), `project.subprojects` (read-only). All three
  are consistent with the fix; no other command in the file renders.

## Other findings

- **Same shape, different ownership, left alone on purpose.** `bounce.land`, `range.bounce` and `track.flatten`
  also return an error when `landAudioTrack` answers `nullptr`, and their render stays on disk
  (`ControlCommandsRenderBounce.cpp:219-224,251-256,294-299`). There the path is the caller's (`out`) or the
  documented `<project>-bounces/` default, so the file is the artifact the caller asked for, not an unlinked
  internal one; deleting it would destroy a render a user can see and use. Reported, not changed, per the
  scope rule.
- A render child that fails may leave a partial file at its target: `render()`
  (`ControlCommandsSubprojects.cpp:154-161`) returns before the target is referenced, and the child's own
  partial output is not removed. Not this finding (the render there *fails*, it does not succeed and then go
  unlinked); worth its own unit.
- `tests/control-success-paths.py` exits 1 in this build tree because `project.import` / `project.export_midi`
  need MIDI filter plug-ins that this build does not produce (`build/plugins` has no MIDI filter). Unrelated to
  this fix.
