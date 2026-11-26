FIXED

# Cluster new-p11-25 — Collecting the open project left disk and memory out of sync

**Verdict: FIXED.** The finding is **REAL**, not already fixed and not a false positive.

`project.collect_media` copied the media and rewrote the project FILE, and then stopped there.
When the file it was given was the one the session has open, the live song kept the external
paths, so the next save from the application wrote them back over the collected references: the
collection was silently undone. The handler now hands the collected document to the open session
and records ONE undo step that restores both halves.

## The defect, in code (pre-fix lines, measured this session)

| Where | What it says |
| --- | --- |
| `src/core/ControlCommandsCollectMedia.cpp:158` (pre-fix `collectMedia()`) | reads the file, rewrites it through `controlRelinkProjectAsset()`, and records `addUndoStep(write previous bytes, write written bytes)` — the FILE only. It never included `Engine.h` or `Song.h` and never asked whether the project it was rewriting was the session's own file. |
| `src/core/ControlProjectAssetsRelink.cpp:181` → `:150-155` (`commitRelink`) | the rewrite is a raw file write; it has no session half by design ("The engine half here only rewrites; it never records", `:44-48`). |
| `src/core/SampleClip.cpp:1031` (`saveSettings`) | a save writes `src = sampleFile()`, i.e. whatever the live `SampleBuffer` still holds. That is the mechanism of the loss: the live buffer held the external path. |
| `src/core/Song.cpp:2480` (`saveProjectFile`) | writes the live session's document; nothing compares it with the file collect_media just rewrote. |

Measured in the RED run (`logs/test-3-red.log`, `logs/test-10-reverted.log`): after
`project.collect_media` on the open project, a save from the session wrote

```
Actual   : .../ControlProjectArchiveTest-RGFHsa/library/clip.wav      <- the external original
Expected : .../ControlProjectArchiveTest-RGFHsa/song/media/clip.wav   <- the copy the command made
```

so the file the command had just made portable was overwritten with the old reference. The same
run shows the second half: an unsaved session was collected over without a word
(`collectingTheOpenProjectRefusesWhileTheSessionHasUnsavedChanges`, `'!refused.ok' returned FALSE`).

## The fix (at the cause)

`src/core/ControlCommandsCollectMedia.cpp`:

- `:169` `struct OpenSession` — the song whose file this is (nullptr when the project is not open),
  the pre-collect session snapshot (the undo half) and the sections a partial session omitted.
- `:181` `prepareOpenSession()` — refuses, BEFORE a single file is copied, when the session has
  UNSAVED changes (the file-based rewrite would drop them) or when the session cannot be captured
  for undo. A project that is not open takes the same path as before: `song == nullptr`.
- `:238` `recordCollectionStep()` — for the open project: the collected document is REOPENED in the
  session (`:255` `open.song->loadProject(project)`, the load `project.open` runs, so project-relative
  `local:` references resolve against the project's own directory), the undo/redo history the load
  clears is held across it and put back (`Engine::projectJournal()->detachHistory()` /
  `reattachHistory()`, the pattern `src/core/ControlCommandsDocuments.cpp:244` uses), and ONE
  `addStructuralUndoStep()` (`:295`) restores **the file's previous bytes AND the session they were
  collected from** — the same call is the redo. The session and its file are one document again, so
  the modified flag is put back to the state an open leaves (`:276` `noteProjectSaved`).
  A collected document that cannot be reopened re-writes the previous bytes and refuses.
  For a project that is not open, the step is byte-for-byte the one recorded before this change.
- `:310` `collectMedia()` gained exactly one branch (`prepareOpenSession`) and delegates the step
  recording: CCN stays at 9, under the gate's 10 (`logs/gates-1.log`).

The reopen is `project.open`'s own machinery, so it also stops the transport and rewinds the play
positions, as opening any project does; the copies stay on disk, as the command's description and
the A16 row say, and one `control.undo` still puts the file's previous bytes back.

## Test matrix

| Path | Test (all in `tests/src/core/ControlProjectArchiveTest.cpp`) | What it proves |
| --- | --- | --- |
| normal path: open project, clean | `:926` `collectingTheOpenProjectsMediaLeavesTheSessionSayingWhatTheFileSays` | the copies are made, the command writes `local:media/...`, the live reference names the copy, `project.get_state` reports the session's own file and `modified=false`, and **a save from the session writes the copy** |
| undo (one honest step) | same test, `REV_UNDO_OR_FAIL()` | ONE `control.undo` restores the file bytes AND the session (its next save names the external original again) |
| failure path: unsaved session | `:978` `collectingTheOpenProjectRefusesWhileTheSessionHasUnsavedChanges` | `Refused` naming the reason, the project file byte-identical, **no `media/` folder created** (refused before anything is written) |
| reconfiguration / other callers | existing `collectMediaCopiesBesideTheProjectAndUndoRestoresTheFile`, `collectMediaRefusesAMediaFolderThatSymlinksOutOfTheProject`, `relink*`, `LibraryProjectsTest` (hand off → `collectProjectMediaInto`) | a project that is not open keeps the old file-only step; the symlink containment and the relink verbs are untouched; `project.handoff`, the other caller of the media-copy helpers, works on a copy and passes |
| exhaustion | existing `relinkAndCollectMediaAcceptProjectFilesAbove64MiB` | the 256 MiB document cap is unchanged (the reopen reads the same document through the same loader) |
| audio thread | none of the changed code runs there: the whole change is in a control-command handler (control/GUI thread) | no allocation, lock, wait, log or signal was added to any render path |
| supported platforms | `QTemporaryDir`, `QFileInfo`, `QDir`, `PathUtil::toAbsolute`; no POSIX-only API, no Qt-6-only API, no wall-clock timing | builds and runs on the hosted Qt 5 / MSVC / macOS jobs like the rest of this file |

## Commands, with their EXIT codes (copied from this session's logs)

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p11-25.r1/logs`.

RED (test written, product code untouched):

- `cmake --build build --target ControlProjectArchiveTest -j 4 > logs/build-3.log 2>&1` → `EXIT=0`
- `cd build/tests && ctest -R '^ControlProjectArchiveTest$' --output-on-failure > logs/test-3-red.log 2>&1` → `EXIT=8`
  (2 failures: the save wrote `library/clip.wav` instead of `song/media/clip.wav`; the unsaved session was collected over)

GREEN (fix in place):

- `cmake --build build --target zene ControlProjectArchiveTest -j 4 > logs/build-8.log 2>&1` → `EXIT=0`
- `cd build/tests && ctest -R '^ControlProjectArchiveTest$' --output-on-failure > logs/test-8.log 2>&1` → `EXIT=0` (`100% tests passed out of 1`)

Family (built first: `cmake --build build --target ControlProjectMediaTest LibraryProjectsTest -j 4 > logs/build-9-family.log 2>&1` → `EXIT=2`, because the third name I asked for, `ControlReversibilityTranscript`, is a ctest entry with no build target — the two test executables built):

- `cd build/tests && ctest -R '^(ControlProjectArchiveTest|ControlProjectMediaTest|LibraryProjectsTest|ControlReversibilityTranscript|ControlCommandsSnapshot)$' --output-on-failure > logs/test-9-family.log 2>&1` → `EXIT=8`
  - `ControlProjectArchiveTest`, `ControlProjectMediaTest`, `LibraryProjectsTest`, `ControlCommandsSnapshot`: **Passed** (4 of 5)
  - `ControlReversibilityTranscript`: **Failed**, `AssertionError: expected ok=true, got {'error': {'kind': 'not_found', 'message': "the LADSPA host plugin 'ladspaeffect' is not in this build"}}` — a build/environment gap in this tree, NOT this change: the same test fails identically with the product change reverted (`logs/test-10-reverted.log`, section "2/2 Test #969: ControlReversibilityTranscript ...***Failed").

4b — the test catches the bug (product file only; the test file stays in the tree):

- `git diff -- src/core/ControlCommandsCollectMedia.cpp > logs/fix.patch` → `DIFF_EXIT=0` (206 lines)
- `git apply -R logs/fix.patch` → `APPLY_R_EXIT=0`
- `cmake --build build --target zene ControlProjectArchiveTest -j 4 > logs/build-10-reverted.log 2>&1` → `BUILD_REVERTED_EXIT=0`
- `cd build/tests && ctest -R '^(ControlProjectArchiveTest|ControlReversibilityTranscript)$' --output-on-failure > logs/test-10-reverted.log 2>&1` → `TEST_REVERTED_EXIT=8` — **FAILS** without the fix
- `git apply logs/fix.patch` → `APPLY_EXIT=0`
- `cmake --build build --target zene ControlProjectArchiveTest -j 4 > logs/build-11-restored.log 2>&1` → `BUILD_RESTORED_EXIT=0`
- `cd build/tests && ctest -R '^ControlProjectArchiveTest$' --output-on-failure > logs/test-11-restored.log 2>&1` → `TEST_RESTORED_EXIT=0` — **PASSES** with it

Gates:

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1` → `EXIT=0`
  (`fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`,
  `doc-figures`, `complexity`, `file-length`: all PASS; `RESULT: PASS`)

Registration: no new product or test file was added (`src/core/ControlCommandsCollectMedia.cpp` and
`tests/src/core/ControlProjectArchiveTest.cpp` already exist and are registered; the `fork-sources` and
`no-upstream-regression` gates pass), so neither list file needed an entry.

## Files changed

- `src/core/ControlCommandsCollectMedia.cpp` — the fix (`:169`, `:181`, `:238`, `:310`).
- `tests/src/core/ControlProjectArchiveTest.cpp` — the two regression tests and their fixture (`:183` `pcmWave`, `:234` `openProjectWithAnExternalSample`, `:926`, `:978`).
- `docs/KNOWN-LIMITATIONS.md` — the "Project assets" section states the open-project behaviour, the dirty-session refusal and that `project.relink` / `project.restore_revision` stay file verbs.
- `data/manual/index.html` — the Collect Media paragraph says the open project is repointed at the copies.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reverted:
  `TEST_REVERTED_EXIT=8` (`logs/test-10-reverted.log`: "Actual …/library/clip.wav" vs "Expected …/song/media/clip.wav",
  and "an unsaved session was collected over"). Restored: `TEST_RESTORED_EXIT=0` (`logs/test-11-restored.log`).
- **The test is deterministic.** No sleep, no wall clock, no race: it writes a real 16-bit PCM WAV
  (`:183`), opens a real project through `Song::loadProject`, drives the registered verb through
  `ControlRegistry::invoke`, and reads the result back through `Song::saveProjectFile` +
  `project.missing_assets`. Every path it compares comes from a `QTemporaryDir` it created.
- **The fix is at the cause and complete; every caller and sibling was checked.** Callers/siblings of
  `controlRelinkProjectAsset()`: `project.collect_media` (fixed here) and `project.relink`
  (`src/core/ControlCommandsProjectArchive.cpp:391`), which is a DIFFERENT verb and is deliberately
  left a file verb — its documented contract (Known limitations, "Project assets can be inspected,
  hashed and relinked"; `docs/A16-REVERSIBILITY.md` §8 item 5 for the same family's
  `project.restore_revision`) is that it rewrites the file and does not touch the session, and the
  finding names the collection. That asymmetry is now stated in Known limitations and listed under
  Other findings below. The other caller of the media-copy helpers,
  `collectProjectMediaInto()` (`src/core/ControlCommandsCollectMedia.cpp:357`, used by
  `project.handoff`), works on a copy of the project and never on the session's file — unchanged, and
  `LibraryProjectsTest` passes. Actions mirrored to the GUI path: the Project menu's "Collect Media..."
  and the Project Files list's extra both send `project.collect_media`, so the fix reaches the user
  path without a new command.
  Sibling commands in the `project` group were checked for the same shape and are unaffected:
  `project.save` / `project.open` already own the session; `project.missing_assets` and
  `project.hash_assets` write nothing; `project.restore_revision` and `project.relink` are file verbs by
  their documented contract (above); `project.handoff`, `project.save_as_template`,
  `project.import_tracks`, `project.alternative_*` write other files and leave the session's own file
  alone.
- **No collateral damage.** A project that is not open takes the pre-fix path byte for byte (same
  `addUndoStep`, same `__transaction` payload text) — the existing `collectMedia*` and `relink*` tests
  pass. The 256 MiB document cap, the `dir` containment check and the symlink refusal are untouched
  (their tests pass). Old projects load as before: the reopen is the ordinary `Song::loadProject`, and
  no format, attribute or save shape changed. The one visible consequence is that collecting INTO the
  open project now performs a project load (transport stops, play positions rewind, as any open):
  stated in Known limitations.
- **Undo, realtime and commands.** One honest step: the open-project case records exactly ONE
  structural step (file bytes + session), proved by the single `REV_UNDO_OR_FAIL()` restoring both;
  the non-open case keeps its one step. No control command was added, the command ids and their
  descriptions are unchanged (`ControlCommandsSnapshot` passes), and nothing in the change runs on
  the audio thread: it is all control-thread command code with no allocation, lock, wait, log or
  emit added to any render path.
- **Qt 5.15 / MSVC / macOS portability.** The new code uses `QString`, `QByteArray`, `QDir`,
  `QFileInfo`, `std::shared_ptr` and `std::function` only; no Qt-6-only API, no POSIX-only call, no
  `std::filesystem`, no absolute-path spelling test (`PathUtil::toAbsolute` is used instead).

## Other findings

- **`project.relink` has the same disk/memory hazard and is NOT fixed here.** Relinking the file the
  session has open and then saving from the application puts the old reference back, exactly as the
  collection did. It is out of this finding's scope (the finding names
  `project.collect_media`; the scope guard is enabled for this run), and its documented contract is
  explicitly file-only. It needs the same treatment (`prepareOpenSession()` +
  `recordCollectionStep()` would take it directly) as its own unit.
- **`local:` references do not survive a snapshot restore.** `Song::restoreUndoSnapshot()` loads a
  snapshot from a `QTemporaryDir` (`src/core/SongUndoSnapshot.cpp:104-106`) and `Song::loadProject()`
  sets the project file name from the path it is loading (`src/core/Song.cpp:2040`), while
  `PathUtil::baseLocation(Base::LocalDir)` resolves `local:` against
  `Engine::getSong()->projectFileName()` (`src/core/PathUtil.cpp:49-62`). An undo or redo of a session
  whose samples are stored as `local:` paths therefore loads them from the temporary directory. Seen
  while designing this fix; not touched by it, and not proved by a test here.
- **`Song::saveProjectFile()` can leave the session marked modified** (measured this session: the flag
  reads false after the load, and is set again after a bare `saveProjectFile()`, which is why every
  caller pairs it with `noteProjectSaved()`). Not a defect on its own — recorded because it is easy to
  misread as one.
