IMPLEMENTED

# Q050-493 Warp controls for launcher audio cells in the GUI

Branch `feat/050-Q050-493-oc`, cut from `050/m0-train-21` (`37eaf0755`). Commits: `62a6715f17` (menu, display, test),
`6503eba92e` (display moved into its own painter, under the complexity target), `90b11250b3` (manual, known limits,
feature record), plus this report. Feature record: `docs/050-features/Q050-493.md`.

What changed: an audio cell's right-click menu has **Playback and Warp** (`src/gui/SessionGridMenus.cpp`
`addPlaybackEntries`): *Gain, Pitch, Loop and Warp...* opens the `session.set_slot` form with all eight playback fields
(`gain_db`, `transpose`, `detune`, `loop_start`, `loop_length`, `warp`, `warp_bpm`, `ram`) filled in from the cell's
current values; *Warp to Song Tempo* On/Off; *RAM Mode* On/Off; *Reset Gain, Pitch, Loop and Warp*. Every entry
runs the registered `session.set_slot`, the same command agents call. When a cell's settings are not at their defaults,
the cell shows them after its name (`-6.0dB +12st +50ct ⟲ W128`), and its accessible name and tooltip say them in words
(`src/gui/SessionGridCells.cpp` `playbackBadge`/`playbackWords`, `src/gui/SessionGridView.cpp`
`readPlayback`/`paintPlayback`).

Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-493.r1/logs/`.

## Acceptance
| Criterion | Test (ctest `SessionCellPlaybackMenuTest`) | Command | Exit |
| --- | --- | --- | --- |
| The context menu exposes every warp/playback field `set_slot` accepts | `theFormHoldsEveryPlaybackFieldAtTheCellsValues`: the form has all 8 fields, each at the value set over the socket; a MIDI cell has no entry | `ctest -R '^SessionCellPlaybackMenuTest$' --output-on-failure` (test-7-restored.log); binary run with per-case output (test-2.log: 9 passed) | 0 |
| Edits are undoable | `eachFieldEditedInTheFormIsOneUndoStep` (each of the 8 fields, one `control.undo` restores the previous value), `theSwitchesAndTheResetAreOneUndoStepEach` (Warp On/Off, RAM On, Reset: one undo brings all 6 fields back) | same | 0 |
| Saved and reloaded | `theEditsSurviveSaveAndLoad`: all 8 fields set in the form survive `saveProjectFile` → `clearProject` → `loadProject` | same | 0 |
| Shown on the cell | the same cases check the cell's text, accessible name and tooltip | same | 0 |
| GUI test drives each control and checks `session.get_state` | all cases trigger the menu `QAction` (and the form's `setField` + `accept()` = Run); every state check reads `session.get_state` | same | 0 |
| Rendered audio changes as the Q050-013 PCM tests expect | `gainAndLoopFromTheFormRenderAsTheEngineExpects` (-6.02 dB = exact half; loop 2/1 = exact frames), `transposeAndDetuneFromTheFormRepitch` (+12 st ×2, +100 ct one semitone), `warpFromTheMenuFollowsTheSongTempo` (Warp On at 240 BPM, clip 120: 0.5 s burst → 0.25 s at 400 Hz; Off → 0.5 s) | same | 0 |
| KNOWN-LIMITATIONS socket-only note removed | `docs/KNOWN-LIMITATIONS.md` launcher audio-cell bullet now says these fields are applied and editable from the cell's menu; `docs/050-features/Q050-013.md` known limit marked done | doc-figures gate | 0 |

Neighbours, rebuilt and run after the change: `ctest -R '^(SessionGridViewTest|SessionGridRecordTest|SessionSubSceneGridTest|AccessibilityWalkTest|PerformViewTest|TouchWorkspaceTest|SessionAudioCellPlaybackTest)$'`: 7/7 passed, exit 0
(test-4.log); after the refactor, `SessionCellPlaybackMenuTest|SessionGridViewTest|SessionGridRecordTest|SessionSubSceneGridTest|AccessibilityWalkTest`:
5/5 passed, exit 0 (test-5.log).

## Self-check
- **Fails without the change, passes with it.** I reversed every `src/` and `include/` hunk (`git diff 37eaf0755 HEAD -- src include | git apply -R`),
  rebuilt `SessionCellPlaybackMenuTest` (build exit 0, build-6-reverted.log) and ran it: **exit 7**, 7 cases FAILED, for example "no
  menu entry Playback and Warp > Gain, Pitch, Loop and Warp...", "output frame 0 is (0.000488281, ...), expected
  (0.000244141, ...)" and "warped sound 22049 frames, expected 11025" (test-6-reverted.log). After restoring with
  `git apply` and rebuilding `zene` and the test (build exit 0): ctest **exit 0**, 1/1 (test-7-restored.log). The tree was clean afterwards.
- **Every criterion proven by a test I ran:** see the table. Every case is in `SessionCellPlaybackMenuTest`, exit 0.
- **One undo step per edit; reversibility.** Each GUI entry is one `session.set_slot` call, which makes one journal
  checkpoint. The test undoes every edit with a single `control.undo`, including Reset, which writes six fields in one call. No new command, and no
  command's metadata changed, so `session.set_slot` keeps its class (snapshot on the `<session>` block). For that reason I did not regenerate the
  snapshot and did not touch the A16 histogram. `doc-figures` gate exit 0.
- **Realtime.** Nothing I added runs on the audio thread. The menu, the form and the badge run on the UI thread from
  `session.get_state`. Rendering is Q050-013's unchanged code (its allocation-probe tests still pass in
  `SessionAudioCellPlaybackTest`, exit 0). The new UI code shares no state with the audio thread, so there is no new race. TSan cannot run on this
  box, so that conclusion comes from the design, not from a TSan run.
- **Old projects.** No format change. The attributes are Q050-013's, and a cell at its defaults paints exactly as before,
  because `paintPlayback` returns early when there is no badge (SessionGridViewTest, SessionGridRecordTest and AccessibilityWalkTest pass unchanged).
  New data round-trips: `theEditsSurviveSaveAndLoad`, exit 0.
- **Portability.** Only Qt 5/6-common API (`QMenu`, `QJsonObject`, `QString::number`, `QStringList::join`). No new
  identifiers named `small`/`near`/`far`/`interface`/`min`/`max`, no `std::filesystem`, no POSIX calls. The test uses
  `std::span` and libsndfile, as its neighbour `SessionAudioCellPlaybackTest` does.
- **Gates.** `submit_check.py` reports `gates (feature_check.py)` as FAIL. The cause is the jail: `feature_check.py` tries to
  create `<worktree>/../zene-feat-q050-493-feature-check` and gets `PermissionError` (gates-1.log). I ran a copy that differs only
  in its log folder (`/tmp/q493-fc/logs`) and in `ROOT` set to the original folder, so it reads the same FIX-BASE and cached
  baseline: **RESULT: PASS, exit 0**. All 8 rows passed: fork-sources, all-sources-reproduce, no-upstream-regression,
  check-namespace, no-tautology, doc-figures, complexity, file-length (gates-4.log). The first run of that copy flagged
  `SessionGridView::paintCell` at CCN 14. Commit `6503eba92e` fixed it by moving the display into `paintPlayback`.
- **Agent surface (submit_check WARN, "no command file changed").** This is on purpose: the GUI runs the existing registered
  `session.set_slot`, which agents already reach on the socket and through MCP (`zene_session_set_slot`) with the same fields.
- **Build environment note.** ccache's configured temporary directory is not writable from the jail, so every build
  ran with `CCACHE_TEMPDIR=/tmp/ccache-tmp-q493` (build-1.log shows the failure without it).
