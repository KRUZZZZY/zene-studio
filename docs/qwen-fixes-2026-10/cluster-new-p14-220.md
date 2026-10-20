FIXED

# Cluster new-p14-220: Page Up/Down can prompt about unsaved work when no song step is possible

## Verdict

REAL, and fixed. Keyboard Page Up/Down (and the grid's Next/Previous Song entries, which have the same prompt-first order) asked `MainWindow::mayChangeProject(true)` before checking whether the setlist had a song to step to. `mayChangeProject` also stops the transport before it asks, so the keyboard path at the end of the set stopped playback and asked a question for nothing. Now the end-of-set check runs first, and an out-of-range step does nothing.

## Evidence (pre-fix code, file:line)

- `src/gui/PerformView.cpp:488-496` (original numbering): `stepSong` called `window->mayChangeProject(true)` and only then `session.setlist_go`. Nothing checked the index or the item count first.
- `src/gui/PerformView.cpp:520-521`: `Key_PageDown` / `Key_PageUp` call `stepSong("next")` / `stepSong("previous")` with no other check.
- `src/gui/PerformView.cpp:409-410`: the buttons are disabled at the ends (`index + 1 < items.size()`, `index > 0`). The keyboard path had no equivalent guard, which is the gap the finding describes.
- `src/gui/MainWindow.cpp:754-760`: `mayChangeProject(bool stopPlayback)` calls `Engine::getSong()->stop()` before any check on whether the song is modified. A step that is refused at the boundary therefore still stops the transport.
- `src/core/ControlCommandsSessionPerform.cpp:262-264` (`setlistGo`): at the ends it does not fail. It clamps: `next` on the last song resolves to the current index, and `previous` on item 0 resolves to item 0. So an unguarded step reopens the song already open. I read this from the handler; I did not reproduce the reopen in the app. The RED run shows the transport stop, which is the part I observed.
- `src/gui/SessionGridMenus.cpp:80-95` (`addSongStep`): the grid's Next Song and Previous Song entries use the same prompt-first order. They are never disabled at the ends, so the same bug is reachable from the grid. Fixed too.

## The fix

- `src/gui/PerformView.cpp:489-494`: `stepSong` reads `session.setlist`, computes the target index the same way `setlistGo` does (`index ± 1`), and returns when the target is outside `[0, items)`. That happens before the question, so nothing is asked and the transport is not stopped.
- `src/gui/SessionGridMenus.cpp:86-90`: the grid's `addSongStep` lambda applies the same check before `mayChangeProject`.

Behaviour changes, all at the ends of the setlist on the two GUI paths:
1. Page Down at the last song, Next Song there, Page Up / Previous Song at the first song: no prompt, no transport stop, no reopen of the open song. (Previously, with no unsaved edits, the reopen happened silently.)
2. Previous Song when the set has not started (index -1) no longer opens item 0. That matches the button, which is disabled at `index <= 0`.

Not changed: `session.setlist_go` itself. Agents calling it directly still get the clamping. That is a control-command behaviour the finding did not ask to change, so I left it alone. Flagged for the owner in the final message.

## The regression test

`tests/src/gui/PerformViewTest.cpp`: new slot `songStepsAtTheEndsAreNothing()` (line 308), with two helpers in the anonymous namespace: `openUnsavedAndPlaying()` (line 96) and `nothingHappened()` (line 106).

It saves two real songs, sets the setlist to them, and then:
- Page Down at the last song (`performKey(Qt::Key_PageDown)`), then the grid's Next Song at the last song;
- Page Up at the first song (`performKey(Qt::Key_PageUp)`), then the grid's Previous Song at the first song.

Before each step it opens the end song unsaved and playing. After each step it checks that the transport is still playing, the same project file is open, the song is still modified, and the setlist index is unchanged.

Design choices:
- `ControlRegistry::setQuitPromptAnswer(Discard)` is the existing seam that answers the "unsaved work" question without a dialog. Under offscreen, a dialog would block the test; with the seam, a step that asks shows up as a transport stop.
- `QVERIFY2` is called from the test body, not from inside a lambda, so the first failure ends the test.
- A local scope guard restores the transport, the empty set and the prompt answer however the test ends. Without it, a RED run left state behind and broke `learnMidiBindsTheNextNoteToAScene` (see the superseded runs below).
- After each load the test calls `QCoreApplication::sendPostedEvents()`. Without that, the second `project.open` in the binary segfaulted inside `Song::clearProject`, which ran a queued `TrackView` lambda against a track it had just removed (backtrace in `test-1-red.log`). I did not investigate that crash further and I make no claim about it in the app; it is flagged below.
- The only waits are a bounded `QTest::qWaitFor(transportPlaying, 3 s)` for playback to start, the same pattern as the file's existing `QTRY` checks. The assertions after each key press or trigger are synchronous.

## Commands and exit codes

Run from the worktree unless noted. Every build used `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14`: the default ccache temp dir under `/run/user/1000` is not writable here (`build-1-red.log`, EXIT=2, "ccache: error: failed to create temporary file"). The logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-220.r1/logs/`.

RED on the original product code (final test file; fix reverted with `git apply -R logs/fix.patch`):

| Step | Command | Exit | Log |
| --- | --- | --- | --- |
| Save patch | `git diff -- src/gui/PerformView.cpp src/gui/SessionGridMenus.cpp > logs/fix.patch` | 0 | - |
| Revert fix | `git apply -R logs/fix.patch` | 0 | - |
| Build | `cmake --build build --target zene PerformViewTest -j4` | 0 | build-10-final-revert.log |
| RED | `ctest -R '^PerformViewTest$' --output-on-failure` (in build/tests) | **8** | test-9-final-RED-reverted.log |

RED output: `FAIL! : PerformViewTest::songStepsAtTheEndsAreNothing() 'nothingHappened(second, 1)' returned FALSE. (Page Down at the last song stopped, asked about, or reopened the song)`, `Loc: [PerformViewTest.cpp(336)]`.

GREEN with the fix:

| Step | Command | Exit | Log |
| --- | --- | --- | --- |
| Re-apply fix | `git apply logs/fix.patch` | 0 | - |
| Build | `cmake --build build --target zene PerformViewTest -j4` | 0 | build-11-final-green.log |
| GREEN | `ctest -R '^PerformViewTest$' --output-on-failure` (in build/tests) | **0** | test-10-final-GREEN.log |
| Direct run | `./PerformViewTest` (in build/tests, offscreen) | 0 | test-11-final-direct.log |

Affected family: `ctest -R '^PerformViewTest$'` ran **1 test** (#427), which contains **10 test functions**. Direct run: `Totals: 10 passed, 0 failed, 0 skipped`. Nothing else in the family was changed.

Superseded runs, same product code, earlier versions of the test (recorded so none of them is mistaken for the proof above):
- `test-1-red.log`: EXIT 8, SEGFAULT. Assertions were inside lambdas, so a failed check did not end the test; the second project load crashed (see above).
- `test-2-red.log`: EXIT 8. The intended failure at line 325 plus a collateral failure in `learnMidiBindsTheNextNoteToAScene` caused by state the RED run left behind (no scope guard yet).
- `test-3-red.log`: EXIT 8, `FAIL! ... nothingHappened(second, 1)`, `Loc: PerformViewTest.cpp(335)`. Clean RED on the original code, before the test used `sendPostedEvents()`.
- `test-4-green.log`, `test-7-reapply-green.log`, `test-8-final-green.log`: EXIT 0 on earlier test versions.

Gates, final state (each run directly from the worktree, unpiped exit codes; logs `gate-10` to `gate-17`):

| Gate | Command | Exit |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

The ratchets exit 1 on the existing train items (6 complexity and 15 file-length REGRESSION lines, all in other files). None names `src/gui/PerformView`, `src/gui/SessionGridMenus` or `tests/src/gui/PerformViewTest`. I did not check the base commit to show those items already exist there: the base-listing cache lives outside the write area.

`feature_check.py` could not run as written. It creates `<worktree>-feature-check/` next to the worktree, which is outside the write area, and fails with `PermissionError` at `feature_check.py:71`. No logs were written by it. The six simple gates and the two ratchets above are the same commands it lists, run one at a time.

## Files committed

- `src/gui/PerformView.cpp`
- `src/gui/SessionGridMenus.cpp`
- `tests/src/gui/PerformViewTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p14-220.md`

No new files were created, so no registration in `tests/fork-sources.txt`, `tests/upstream-modifications.txt` or `tests/CMakeLists.txt` was needed. `PerformView.cpp` and `SessionGridMenus.cpp` are already in `tests/fork-sources.txt`. `PerformViewTest.cpp` is already registered at `tests/CMakeLists.txt:1162`.

## Self-check

Bug-cluster items. The checker's feature-only items (acceptance table, Qt 5.15 portability, audio thread, manual) do not apply to a bug fix.

1. **Each new test FAILS with the product change reverted and PASSES restored.** Yes. Reverted: `ctest` EXIT 8, `PerformViewTest.cpp(336)` (test-9). Restored: EXIT 0 (test-10); direct run 10/10 (test-11).
2. **The test is deterministic: no wall-clock timing, sleeps or races.** No timing assertion. The only waits are a bounded poll for playback to start (`qWaitFor`, 3 s, the file's existing `QTRY` pattern), and `sendPostedEvents()`, which flushes queued events without a timer. The assertions after each key press or trigger run synchronously. The quit-prompt answer is injected with `setQuitPromptAnswer`, so no dialog can block.
3. **The fix is at the cause and complete.** The cause is the order of the question and the end-of-set check. Callers checked: the keyboard (`performKey` → `stepSong`), the buttons (disabled at the ends and also routed through `stepSong`), and the grid's Next/Previous Song (`addSongStep`, which is fixed). `session.setlist_go` itself is unchanged on purpose.
4. **No collateral damage.** No limit was added, and no file format or save path changed. Old projects and saved setlists load exactly as before. The only behaviour change is at the ends of the setlist on the two GUI paths, listed above.
5. **Realtime, undo and commands.** The change is GUI-thread only and touches no audio code. It reads `session.setlist`, which does not mutate. No command was added or changed, so the snapshot is not affected, and no undo step was added or removed.
6. **Feature-branch and verdict items.** The verdict is FIXED, so no ALREADY-FIXED or FALSE-POSITIVE evidence is needed. `submit_check.py`'s gate row runs `feature_check.py`, which cannot run in this sandbox (see above).

Open for the owner:
- `session.setlist_go` with `to=next` on the last song is still accepted and reopens the open song. Agents that call it directly will see that. I did not change it, because it is a control command and the finding asked for a GUI-side fix.
- The `Song::clearProject` → `sendPostedEvents` → `TrackView` crash seen during the first version of the test is unexplained. It did not show up in the final test, which settles between loads. I did not check whether it can happen in the app.
