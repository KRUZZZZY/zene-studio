FIXED

## Decision

REAL. `SessionGridView::rebuild()` created a `QToolButton` for every track/scene pair
(`src/gui/SessionGridView.cpp:202`, inside the loops at `:191` and `:200`), while
`session.set_grid` accepts 256 tracks and 512 scenes (`src/core/ControlCommandsSession.cpp:290-291`).
A maximum grid therefore attempted 131,072 cell buttons on the UI thread.

The fix is the UI-specific limit the finding permits: the authored grid remains unchanged, but the
view draws at most 64 tracks by 64 scenes. `MaxVisibleTracks`/`MaxVisibleScenes` are at
`src/gui/SessionGridView.cpp:55-56`; `rebuild()` keeps the authored dimensions separately and builds
only the capped prefix at `:184-187`. The status line says the grid is truncated at `:281-285`, and
the Grid Size menu still defaults from the authored dimensions at `:510`. The drop-preview status
also uses the authored dimensions (`src/gui/SessionGridDrop.cpp:132`). The member split is in
`include/SessionGridView.h:178-181`.

## Regression test

`tests/src/gui/SessionGridViewTest.cpp:497-524` (`aLargeGridIsDrawnWithBoundedCellWidgets`) first
proves `session.get_state` still accepts 256 x 512, then sets 65 x 65 (one cell beyond the UI cap),
constructs the real view, and counts only buttons whose `controlCommand` is `session.launch_slot`.
It requires exactly 4,096 such buttons, rejects the first cell outside the cap in either direction,
checks the truncation message, and finally resizes back to 2 x 2 to exercise reconfiguration.

The RED run does not construct a 131,072-widget view (the freeze under test); it exercises the
same unbounded construction path at 65 x 65, where the old code deterministically creates 4,225
cell buttons instead of 4,096.

## Round 3: the cap must not cap the state the view reports

The round-2 review found that capping the drawn grid also capped what the view derives from it. At a
grid wider or taller than 64: (a) a scene button read "empty" when its only clip sat on a track beyond
the drawn 64 (and playback there was dropped), while `PerformView.cpp:354` computes the same state over
the whole model; (b) a visible cell lost its "replaces" tooltip when its track played a clip in a scene
row beyond the drawn 64. Both were correct before the cap, so both are regressions of this fix. The 64 x 64
widget cap stays; the authored state is now model-wide again, with the drawn-stride vectors (`m_names`,
`m_badges`, `m_playback`, `m_selected`, `m_dropCell`) untouched, so `SessionGridDrop.cpp` needs no change:

- `src/gui/SessionGridView.cpp:190` `rebuild` sizes `m_playingScene` by the authored track count, and
  `:304`/`:309` `readColumns` sizes `m_phases` by `m_gridTracks` and accepts every reported column, so a
  track above the drawn prefix still counts as playing in `sceneState`.
- `:354-369` `readAuthoredClips` (called at `:255`) records per drawn scene whether any authored track holds
  a clip (`m_sceneHasClip`) and, per drawn track, the name of the clip it plays in any scene row
  (`m_playingName`).
- `:455` `paintScenes` turns Empty into Stopped when `sceneHoldsClip` (`:439`) says a hidden track holds a clip.
  `sceneState`'s signature is unchanged.
- `:430-435` `launchNote` treats a playing scene as valid when it is below `m_gridScenes` and takes the playing
  clip's name from `m_playingName`, so `launchReplaces` still returns the note.
- `include/SessionGridView.h`: `m_sceneHasClip`, `m_playingName`, `readAuthoredClips`, `sceneHoldsClip`.

Report drift from round 1 corrected: the truncation status text is `SessionGridView.cpp:281-285`
(the branch is the `if` at `:281`), and the pre-fix per-cell creation was `:202` at the base commit,
not in the current file.

### Regression tests added (`tests/src/gui/SessionGridViewTest.cpp`)

- `aCellNamesTheClipItsTrackPlaysInAnUndrawnScene`: a 2 x 65 grid, clips kick at (1,1) and deep at (1,64),
  launch (1,64); the view must not draw (1,64) and cell (1,1)'s tooltip must contain "replaces deep".
- `aSceneButtonCountsTheTracksThatAreNotDrawn`: a 65 x 2 grid (65 song tracks); scene 2 reads "empty" with no
  clip, "stopped" once track 65 holds a clip, and "playing" while track 65 plays it.
- Both use a `GridRestorer` guard that stops playback, clears their clips and restores the 2 x 2 grid even when
  a QVERIFY returns early, so a failure cannot cascade into another test. (My first red run failed
  `aSceneButtonCountsTheTracksThatAreNotDrawn` on its "empty" precondition only because of such a leftover clip
  from the first test; the guard fixed that, and the red below fails at the intended assertions.) They are
  deterministic: fixed dimensions, a synchronous `refresh()`, and the same `QTRY_*` wait for the engine that the
  neighbouring launch tests use; no sleeps or wall-clock assertions.

## Commands and results (round 3)

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/5360.r3/logs/`. `fix.patch` is the product-only diff: `git diff -- include/SessionGridView.h src/gui/SessionGridView.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/5360.r3/logs/fix.patch` (EXIT 0, 121 lines), taken before the commit.

| Phase | Exact command | Exit | Evidence |
|---|---|---:|---|
| Reverse the fix | `git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/5360.r3/logs/fix.patch` | 0 | `reverse-5.log` |
| RED build | `cmake --build build --target zene SessionGridViewTest -j4` | 0 | `build-5-red.log` |
| RED test | `ctest -R '^SessionGridViewTest$' --output-on-failure` (from `build/tests`) | 8 | `test-4-red.log`: 14 passed, 2 failed: `aCellNamesTheClipItsTrackPlaysInAnUndrawnScene` at SessionGridViewTest.cpp(566) tooltip is "Press to launch kick"; `aSceneButtonCountsTheTracksThatAreNotDrawn` at (590) name is "Launch Scene 2, empty" |
| Restore the fix | `git apply /home/kruzzzzy/.config/kads/zene/state/runs/5360.r3/logs/fix.patch` | 0 | `reverse-6.log` |
| GREEN build | `cmake --build build --target zene SessionGridViewTest SessionGridRecordTest SessionSubSceneGridTest SessionTransportBarTest SessionCellPlaybackMenuTest AccessibilityWalkTest -j4` | 0 | `build-6-green.log` |
| GREEN test | `ctest -R '^SessionGridViewTest$' --output-on-failure` (from `build/tests`) | 0 | `test-5-green.log`: 1/1 passed (includes the round-1 cap test `aLargeGridIsDrawnWithBoundedCellWidgets`) |
| Family test | `ctest -R '^(SessionGridViewTest|SessionGridRecordTest|SessionSubSceneGridTest|SessionTransportBarTest|SessionCellPlaybackMenuTest|AccessibilityWalkTest)$' --output-on-failure` (from `build/tests`) | 0 | `test-6-family.log`: 6/6 passed |

The round-1 cap test passes in the same GREEN run, so the 64 x 64 bound is kept (round 1's own RED/GREEN
reversal of that cap, `cellButtons` 4225 vs 4096, is in `5360.r2/logs/test-1-red.log`).

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exited 0
(`gate-1.log`): all eight rows PASS (fork-sources, all-sources-reproduce, no-upstream-regression,
check-namespace, no-tautology, doc-figures, complexity, file-length). It reports 6 changed files because of
an untracked, pre-existing `Testing/` directory (ctest droppings) that is not part of this change.

## GUI proof

`QT_QPA_PLATFORM=offscreen python3 /home/kruzzzzy/.config/kads/zene/state/runs/5360.r3/logs/capture_5360.py` exited 0 through the real control socket
(`screenshot-1.log`): a 256 x 512 grid, launcher shown, `cluster-5360-launcher.png` at 1600 x 1000, sha256
`1f588c143934fb0976f5c6756bbdd48edc0224a89e6ee7a73e54ec5716daef28` (identical to round 2's capture: the
visible layout is unchanged by the state fix).

## Test matrix

- Normal path: the existing 2 x 2 launcher tests and the five family targets above still pass.
- Failure/exhaustion path: 65 x 65 exceeds the UI cap; the view is bounded and the model still accepts 256 x 512.
- Reconfiguration path: the regression resizes 65 x 65 -> 2 x 2 and verifies a visible cell exists.
- Audio-thread constraints: no render/process/handle code changed, so nothing new allocates, locks, waits or logs on the audio thread.
- Supported-platform build requirements: no new API was introduced; the Linux/Qt 6 build is verified here.

## Self-check

- The regression tests fail with the product change reverted and pass with it: red EXIT 8
  (`test-4-red.log`, both new tests fail at their intended assertions), green EXIT 0 (`test-5-green.log`).
- Deterministic: fixed grid sizes, synchronous `refresh()`, engine waits via the existing `QTRY_*` pattern, no
  sleeps or wall-clock timing; `GridRestorer` removes order dependence between tests.
- The fix is at the cause and complete: every authored-state reader now sees the whole model. Callers checked:
  `rebuild`, `readColumns`, `refresh`, `paintCell` (reads `m_playingScene[track]`, drawn tracks only),
  `launchNote`, `paintScenes`, `stopColumn` (drawn tracks' Stop buttons only, `m_tracks`). The drawn-stride
  vectors and `SessionGridDrop.cpp`/`SessionGridSelect.cpp` are unchanged. Sibling `PerformView.cpp:354`
  already computes the scene state over the whole model, so the two views now agree. `SessionAnnouncements.cpp`
  keeps its own model-wide state and is untouched.
- No collateral damage: `session.set_grid` still accepts 256 x 512 and no command, schema or save format
  changed; grids up to 64 x 64 behave exactly as before (the added vectors equal the drawn ones there; the
  eight pre-existing launcher tests and the family pass).
- Realtime/undo/commands: GUI-thread code only; no audio-path file, control command or undo action touched.
- Registration: no new file; the two changed product files were already registered, and the gate passes.
- Portability: C++20 and existing Qt API only (`std::vector<bool>`, `QJsonArray`); no Qt-6-only call.
- ALREADY-FIXED/FALSE-POSITIVE evidence: not applicable, the verdict is FIXED.
- Concurrency: all state is touched on the GUI thread (timer-driven `refresh()`); not run under TSan, which
  does not run on this box.

Delegation: Haiku ran the reverse/rebuild/RED/restore/GREEN/family build-and-test cycles (three starts: the first
exposed the cascading-failure test flaw, the second a moc keyword clash with a member named `slots`, the third
gave the results above); I verified the RED failure lines, GREEN and family totals and `git status` from the
logs, and ran the gate and the screenshot myself.

## Other findings

None.
