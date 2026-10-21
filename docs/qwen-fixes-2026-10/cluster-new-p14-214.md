new-p14-214: FIXED
5214: FIXED
new-p14-215: ALREADY-FIXED

# Cluster new-p14-214 batch (new-p14-214, 5214, new-p14-215)

All three verdicts are for `src/gui/editors/PianoRoll.cpp` on `bugs/qwen-new-p14-214`.
The requested log folder `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-214.r1/logs`
rejected every write in this session (`touch` and the `write` tool both `EACCES`, even though the
directory is owned by this user), so all build/test logs were kept in the worktree at
`build/qwen-logs-214/` (git-ignored). The commands below are exact and their exit codes were observed.

## new-p14-214: FIXED

`src/gui/editors/PianoRoll.cpp:4171` now takes one `MidiClip` journal checkpoint inside
`wheelEvent()` immediately after the note vector is known non-empty and before the volume/panning
loop. Undo therefore restores the wheel change instead of skipping it.

Regression: `tests/src/gui/PianoRollRecordTest.cpp:228`
(`aWheelEditIsOneJournalStepAndUndoRestoresTheVolume`). It sends a real `QWheelEvent` to the
`PianoRoll` inside the real `PianoRollWindow`, requires the volume step and exactly one journal
step, then undoes and requires the original volume.

Without the fix the same test failed at `Engine::projectJournal()->undoDepth()`: actual `0`,
expected `1` (`build/qwen-logs-214/test-3-red.log`, EXIT=8). With the fix it passes
(`build/qwen-logs-214/test-6-restored.log`, EXIT=0).

## 5214: FIXED

`include/PianoRoll.h:413` adds `m_recordingNoteIds`, parallel to `m_recordingNotes`.
`src/gui/editors/PianoRoll.cpp:4535` records the live handle's stable `Note::id()` with each pending
start; `finishRecordNote()` at `:4567` matches the release to that identity first (key fallback only
for callers that hand in two separate `Note` objects, preserving the old contract). `stop()` at
`:4493` clears both lists.

Regression: `tests/src/gui/PianoRollRecordTest.cpp:186`
(`overlappingStartsOfOnePitchKeepTheirOwnRelease`). Two starts of the same pitch overlap; their
releases arrive in the opposite order. The test requires the earlier start to keep length 240 and
the later start length 96. Key-only pairing swaps them (96 / 240), which is the observed RED.

Without the fix: `test-3-red.log` EXIT=8, actual 96 expected 240 at the first assertion.
With the fix: `test-6-restored.log` EXIT=0.

## new-p14-215: ALREADY-FIXED

`enterValue()` is only called from `PianoRoll::mouseDoubleClickEvent()` (`src/gui/editors/PianoRoll.cpp:2144`).
The mouse press that precedes every double click enters the note-edit branch and takes the checkpoint
before any edit there: `src/gui/editors/PianoRoll.cpp:1829`
(`m_midiClip->addJournalCheckPoint();`, immediately before `mouseMoveEvent(me)`). Thus the accepted
dialog value is already covered by exactly one journal step on the real user path.

Adding another checkpoint inside `enterValue()` would take two checkpoints for one gesture (press +
dialog) and break the one-honest-step rule, so no product change was made.

The guard at `tests/src/gui/PianoRollRecordTest.cpp:254`
(`anAcceptedEnterValueIsOneJournalStepThroughTheMousePath`) drives the real path: first click,
double click, accept the dialog, assert exactly one new journal step, then undo and require the
original volume. It passes on the unmodified product (`test-3-red.log`: this case is one of the
5 passing cases while the two real bugs fail), which is the evidence for ALREADY-FIXED.

## Evidence table

| Check | Exact command and observed result |
| --- | --- |
| Baseline family build | `CCACHE_TEMPDIR=$PWD/build/ccache-tmp cmake --build build --target PianoRollRecordTest -j4 > build/qwen-logs-214/build-1-baseline.log 2>&1; echo EXIT=$?` -> **EXIT=0** |
| Baseline test | From `build/tests`: `ctest -R '^PianoRollRecordTest$' --output-on-failure > ../qwen-logs-214/test-1-baseline.log 2>&1; echo EXIT=$?` -> **EXIT=0**, 1/1 target, 3.22 s |
| Initial RED (tests added, product unchanged) | `cmake --build build --target PianoRollRecordTest -j4 > .../build-3-red.log 2>&1; echo EXIT=$?` -> **EXIT=0**; `ctest -R '^PianoRollRecordTest$' --output-on-failure > .../test-3-red.log 2>&1; echo EXIT=$?` -> **EXIT=8**, 5 passed / 2 failed (wheel and 5214); the Enter Value guard passed |
| GREEN after both fixes | Same build -> **EXIT=0**; same ctest -> **EXIT=0** (`test-4-green.log`), 7 Qt cases, 1/1 CTest target |
| Save production patch | `git diff -- src/gui/editors/PianoRoll.cpp include/PianoRoll.h > build/qwen-logs-214/fix.patch; echo PATCH_EXIT=$?` -> **EXIT=0** (146 lines) |
| Reverse production patch | `git apply -R build/qwen-logs-214/fix.patch; echo REVERSE_EXIT=$?` -> **EXIT=0** |
| Rebuild with fix reversed | `CCACHE_TEMPDIR=$PWD/build/ccache-tmp cmake --build build --target PianoRollRecordTest -j4 > .../build-6-reverted.log 2>&1; echo BUILD_EXIT=$?` -> **EXIT=0** |
| RED with fix reversed | From `build/tests`: `ctest -R '^PianoRollRecordTest$' --output-on-failure > ../qwen-logs-214/test-5-reverted.log 2>&1; echo TEST_EXIT=$?` -> **EXIT=8**, 5 passed / 2 failed (same wheel and 5214 assertions) |
| Restore production patch | `git apply build/qwen-logs-214/fix.patch; echo APPLY_EXIT=$?` -> **EXIT=0** |
| Rebuild restored | `cmake --build build --target PianoRollRecordTest -j4 > .../build-7-restored.log 2>&1; echo BUILD_EXIT=$?` -> **EXIT=0** |
| GREEN restored | From `build/tests`: `ctest -R '^PianoRollRecordTest$' --output-on-failure > ../qwen-logs-214/test-6-restored.log 2>&1; echo TEST_EXIT=$?` -> **EXIT=0**, 1/1 target |
| Affected family | `cmake --build build --target PianoRollBenchmarkTest AccessibilityWalkTest -j4` -> **EXIT=0** (`build-8-family.log`); `ctest -R '^(PianoRollRecordTest\|PianoRollBenchmarkTest\|AccessibilityWalkTest)$' --output-on-failure` -> **EXIT=0**, 3/3 targets passed (`test-7-family.log`) |
| Final product build | `CCACHE_TEMPDIR=$PWD/build/ccache-tmp cmake --build build --target zene -j4 > .../build-9-zene-final.log 2>&1; echo EXIT=$?` -> **EXIT=0** |
| Gates (requested path) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-214 > build/qwen-logs-214/gate-1-feature.log 2>&1; echo EXIT=$?` -> **EXIT=1**, but only because it cannot create its sibling log directory: `PermissionError: [Errno 13] .../zene-qwen-new-p14-214-feature-check` (sandbox, same EACCES as the requested log folder) |
| Gates (same script, same tree, only its log destination redirected) | `python3 build/qwen-logs-214/local-gates/feature_check.py "$PWD" > build/qwen-logs-214/gate-2-feature.log 2>&1; echo EXIT=$?` -> **EXIT=0**; the copy changes only `ROOT` to the real harness directory and the sibling log path to `/tmp/zene-fc-214`; the eight gate rows are: fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS (no new item in the changed files), file-length PASS (no new item), `RESULT: PASS` |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py "$PWD" new-p14-214` -> **EXIT=1**, solely on the same `feature_check.py` sibling-directory EACCES; the same checker with its `feature_check` log path redirected reached the PASS table recorded below the Self-check |

Steps 4b commands, copied exactly (worktree root, then `build/tests` for ctest):

```text
git diff -- src/gui/editors/PianoRoll.cpp include/PianoRoll.h > build/qwen-logs-214/fix.patch   # EXIT=0
git apply -R build/qwen-logs-214/fix.patch                                                      # EXIT=0
CCACHE_TEMPDIR=$PWD/build/ccache-tmp cmake --build build --target PianoRollRecordTest -j4 > build/qwen-logs-214/build-6-reverted.log 2>&1   # EXIT=0
cd build/tests && ctest -R '^PianoRollRecordTest$' --output-on-failure > ../qwen-logs-214/test-5-reverted.log 2>&1   # EXIT=8
git apply build/qwen-logs-214/fix.patch                                                         # EXIT=0
CCACHE_TEMPDIR=$PWD/build/ccache-tmp cmake --build build --target PianoRollRecordTest -j4 > build/qwen-logs-214/build-7-restored.log 2>&1   # EXIT=0
cd build/tests && ctest -R '^PianoRollRecordTest$' --output-on-failure > ../qwen-logs-214/test-6-restored.log 2>&1   # EXIT=0
```

## Files committed

- `include/PianoRoll.h`
- `src/gui/editors/PianoRoll.cpp`
- `tests/src/gui/PianoRollRecordTest.cpp`
- `tests/upstream-modifications.txt` (reasons for the two inherited files)
- `docs/qwen-fixes-2026-10/cluster-new-p14-214.md`

## Self-check

1. **Does each regression fail with the product change reverted and pass restored?**
   Yes. The wheel and 5214 cases both failed with `fix.patch` reversed (test-5-reverted.log,
   EXIT=8) and both passed restored (test-6-restored.log, EXIT=0). The Enter Value case is a guard
   for an already-present caller checkpoint: it passes on the unmodified product, as the
   ALREADY-FIXED evidence above records.
2. **Determinism.** No sleeps or wall-clock timing. The wheel test sends one synthetic
   `QWheelEvent` at injected coordinates; the record test injects the transport positions and the
   note lengths before each `midiNoteOn` / `midiNoteOff`; the Enter Value dialog is driven by a
   zero-delay event-loop callback after the dialog is up.
3. **Cause and completeness.** Wheel: every mutation in `wheelEvent()` is behind the one new
   checkpoint, for both volume and panning modes. Recording: every pending start now carries its
   handle id, every release removal removes the matching id, `stop()` clears both lists, and a
   key-only fallback keeps the previous contract for separate `Note` objects. `enterValue()`'s
   only caller was checked: `mouseDoubleClickEvent()` -> real press at `:1829` checkpoints first.
4. **No collateral damage.** No serialization format, project-load path or command surface changed.
   The recording fix only replaces which pending start a release consumes; a note-on/off pair from
   one handle behaves as before, and projects load/save exactly as before.
5. **Undo and realtime.** The wheel edit is one checkpoint per wheel event; the Enter Value path
   keeps exactly one checkpoint (no second one added). Nothing in the recording lists or journal is
   reached from the audio thread; `finishRecordNote()` already ran on the GUI thread and still
   allocates only what it did before.
6. **Portability.** No new Qt-6-only API, no `windows.h` names, no `std::filesystem`.
7. **ALREADY-FIXED evidence.** `src/gui/editors/PianoRoll.cpp:2144` is the only `enterValue()`
   call, and `:1829` is the checkpoint on its mouse-press path. The guard test at
   `tests/src/gui/PianoRollRecordTest.cpp:254` passes unmodified.
