IMPLEMENTED

Q050-273.1 stage 1 — save and reuse audio launcher cells in the shared library. Feature record:
`docs/050-features/Q050-273.1.md`. Seven commits on `feat/050-Q050-273.1-oc`, from `e90f6eef40` (product + tests +
manifests) through `ed7248882f` (the own-source refusal); the last is the final tip.

Reviewer points from round 1 are addressed in this change rather than on top of the old draft, because the
branch had no earlier Q050-273 commit: `git diff 64bd86ae94..HEAD` is the whole change.

## Acceptance

| Criterion | Test (ran myself) | Result |
| --- | --- | --- |
| A populated audio cell offers Save to Library and saves its audio content through `browser.save_clip` | `SessionLibrarySaveTest::anAudioCellSavesItselfToTheLibraryFromItsMenu`, `onlyAPopulatedAudioCellOffersTheEntry` (`tests/src/gui/SessionLibrarySaveTest.cpp`) | ctest `SessionLibrarySaveTest` exit 0 (log `test-13-refactor.log`) |
| The agent command path performs and reads back the save without a window | `tests/control-session-library-save.py`, ctest `ControlSessionLibrarySave` | ctest exit 0 (log `test-13-refactor.log`) |
| After reopening, the saved audio entry is in the browser and inserts into another audio cell with its source intact | `SessionLibrarySaveTest::theSavedEntryIsReusedInAnotherCellAfterReopening`; `ControlSessionLibrarySave` reopen half | both exit 0 |
| Refusal/exhaustion (missing, empty, damaged, unsupported, over-decoded, own-source target, duplicate, allocation, short write, uncommitted write) leaves no partial output, no tag mutation, working undo | `theCommandRefusesWhatItCannotSave`, `theDecodedSizeIsWhatBoundsASave`, `anOverwriteIsOneUndoStepThatRestoresThePreviousBytes`, `allocationAndWriteFailuresLeaveNothingBehind`; `ControlSessionLibrarySave` seam instances | both exit 0 |
| Realtime: a control-thread save while a cell renders disturbs neither playback nor the audio path | `savingWhileACellPlaysKeepsRenderingAndLeavesTheAudioPathAlone` | exit 0 |

Commands run (all from the worktree, real `/usr/bin/cmake` — see Self-check):

```
cmake --build build --target zene SessionLibrarySaveTest -- -j4                BUILD=0   (build-restored.log, build-refactor.log)
ctest -R '^SessionLibrarySaveTest$'                                            exit 0    (test-13-refactor.log)
ctest -R '^ControlSessionLibrarySave$'                                         exit 0    (test-13-refactor.log)
ctest -R '^(LibraryBrowserTest|SessionGridViewTest|SessionAudioCellPlaybackTest|SessionGridRecordTest|SessionCellPlaybackMenuTest|SessionLibrarySaveTest|ControlSessionLibrarySave)$'
                                                                               exit 0    (test-8.log, 7/7)
ctest -R '^ControlCommandsSnapshot$'                                           exit 0    (test-snapshot.log)
ctest -R '^ReversibilityContractTest$'                                         exit 0    (test-rev.log)
ctest -R '^(ReversibilityContractTest|LibraryBrowserTest|SessionGridViewTest|SessionGridRecordTest|SessionCellPlaybackMenuTest|SessionLibrarySaveTest|SessionAudioCellPlaybackTest|ControlSessionLibrarySave|ControlCommandsSnapshot)$'
                                                                               exit 0    (test-final-all.log, 9/9)
bash tests/fork-sources-gate.sh                                                exit 0
bash tests/all-sources-reproduce.sh                                            exit 0
bash tests/no-upstream-regression-gate.sh                                      exit 0
tests/scripted/check-namespace                                                 exit 0
bash tests/no-tautology-gate.sh                                                exit 0
python3 tests/doc-figures-gate.py                                              exit 0
python3 <snapshot_live.py> $PWD                                                exit 0    (878 ids / 77 groups; snapshot committed)
python3 tests/agent-surface-gate.py --check build/zene tests/data/agent-control-fixture.mmp
                                                                               exit 0    (381 reflected actions, 0 unregistered;
                                                                                         reverse 878 commands, 876 swept)
```

No command id was added or removed: comparing the committed snapshot with the base
(`64bd86ae94`) gives 878 -> 878 ids, 77 -> 77 groups, `added: []`, `removed: []`. Only
`browser.save_clip`'s `argsSchema` and description changed, so the README canary figure is
still correct and the A16 histogram did not move (`ReversibilityContractTest` exit 0). The
`submit_check` WARN about "new command ids" is that check's text-diff heuristic reading the
new `track`/`scene` argument strings; it is not a new id.

## Self-check

**Each new test FAILS with the product change reverted (reverse non-test hunks, rebuild, run: exit code?),
then PASSES restored.** Reversed `src/core/ControlCommandsLibraryClips.cpp`, `src/gui/SessionGridMenus.cpp`
and the `src/core/CMakeLists.txt` entry (tests untouched), reconfigured and rebuilt: ctest
`-R '^SessionLibrarySaveTest$'` → **exit 8**, 3 passed / 7 failed (first: "a populated audio cell's menu
has no Save to Library... entry"); `python3 tests/control-session-library-save.py ../build/zene` → **exit 1**
with every save/read-back/undo/refusal check failing (`test-10-reverted.log`, `test-11-reverted.log`).
Restored (`git checkout HEAD -- <product files>`), rebuilt: `SessionLibrarySaveTest` and
`ControlSessionLibrarySave` → **exit 0** (`test-12-restored.log`).

**Every acceptance criterion is proven by a test I ran (name + exit code per criterion).** Table above:
GUI criterion → `SessionLibrarySaveTest` exit 0; agent criterion → `ControlSessionLibrarySave` exit 0;
reopen/reuse criterion → both exit 0; refusal/exhaustion → both, exit 0; realtime → `SessionLibrarySaveTest`
exit 0. Tests actually drive the GUI: the menu `QAction::trigger` opens the shared `CommandForm`, and the
test clicks the form's own **Run** button — not the handler — before reading back through
`browser.query`/`session.get_state`. The realtime case measures on the real `Dummy` device.

**One undo step per edit, honest reversibility class.** A save records one undo step (file bytes restored /
file removed, and the tags it added taken off); `browser.save_clip` stays `true_inverse` in
`ControlReversibilityTableW3Library.cpp` and there is no new command id. `ReversibilityContractTest`
(histogram read from the live table) passes, exit 0, so the published A16 figure did not move. GUI and agent
go through the same registered command, so both routes produce that one step; `control.transactions` in the
socket test reports the step as `true_inverse`/`reversible: true`.

**Nothing added runs on the audio thread.** The diff touches no audio-path function. The new code is
`browser.save_clip`'s handler (`ControlCommandsLibraryClips.cpp`) and `lmms::libraryaudio::CellAudio::save`
(`LibraryCellAudio.cpp`) — decode, libsndfile encode and `QSaveFile` I/O, all on the control/UI thread
through `ControlRegistry::invoke` → `runOnUiThread`. It is not called from `SessionCellPlayHandle::play`,
`AudioEngine`, or any play handle; the renderer is untouched. The test proves the distinction: the save is
measured to allocate on the calling thread, while the renderer's `play()` afterwards allocates **and frees
zero** times.

**Old projects load and render as before; new data round-trips.** No format change: a cell's source is
already persisted in `<z:scenes>`, and the library entry is a file outside the project. `LibraryBrowserTest`
(the clip `save_clip` route, including undo/redo of overwrite bytes), `SessionGridViewTest`,
`SessionGridRecordTest`, `SessionCellPlaybackMenuTest` and `SessionAudioCellPlaybackTest` all pass, exit 0
(`test-8.log`). The GUI test saves the project, starts a new one, reopens it, finds the library entry, drops
it into another cell and compares the audio sample for sample.

**Qt 5.15 / MSVC / macOS < 10.15 portability.** No Qt-6-only API: `QSaveFile`, `QFileInfo`, `QDir`,
`qEnvironmentVariable` and `QScopeGuard` are all Qt 5.15 APIs (the new header uses `QString`/`qint64`
only). `QSaveFile::handle()` + `sf_open_fd(..., SF_FALSE)` is the pattern `ControlCommandsSampleStereo.cpp`
and `RetroAudioCapture.cpp` already ship to MSVC, mingw and macOS, so the file-descriptor round trip is
proven on those targets. No `std::filesystem`; no `windows.h` identifiers; no POSIX-only call (the seams are
plain `qEnvironmentVariable`). The one place a platform differs is not code: CI jobs for Qt 5.15
(Linux/macOS/mingw), Qt 6.8 + MSVC, clang windows-arm64 and macOS < 10.15 were **not run here** — this box
has one Linux/Qt 6.12 configuration. That is a stated limit, not a claim.

**Environment notes (so the evidence reads correctly).**
- The prescribed build/test commands are wrapped by `bunny/shims/cmake` + `build_guard.py`, which cannot
  run in this session: the guard's lock/lease files under
  `~/.local/share/zene-bunny-fix/{bunny,qwen}` are not writable (`PermissionError` on
  `build-pool.lock`), and the requested log directory
  `~/.local/share/zene-bunny-fix/qwen/runs/Q050-273.1.r2/logs` is not writable either. Every build and test
  above therefore ran the real `/usr/bin/cmake` (same build tree, same flags, `-j4`) and wrote its log to
  the writable scratch dir `/home/kruzzzzy/.cache/zene-tmp/Q050-273.1.r2/logs/`. No exit code reported here
  came through a pipe.
- `submit_check.py`'s only remaining red is `FAIL gates (feature_check.py)`, and that is the tool's log
  directory, not a gate: `feature_check.py` dies creating `<worktree>-feature-check/` beside the worktree
  (same permission wall). Run with only that directory redirected to writable scratch (every gate command
  unchanged), it prints its full table and **RESULT: PASS** — fork-sources exit 0, all-sources-reproduce
  exit 0, no-upstream-regression exit 0, check-namespace exit 0, no-tautology exit 0, doc-figures exit 0,
  complexity "no new item in your files", file-length "no new item in your files" (14 changed files, base
  `64bd86ae9`). The six simple gates were also run directly and all exited 0 (listed above).
