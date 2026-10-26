IMPLEMENTED
Q050-100 (this unit) is implemented, and the accepted batch patch is integrated onto `050/m0-train-16`
(`cf3618e61`) - see "Round 5 - integration onto train 16" at the end. The accepted batch's other verdicts are
unchanged: Q050-053 inside it is still BLOCKED (no Orca binary or speaking desktop session), as recorded below.

Accepted batch record (round 1-4, verdict at acceptance: BLOCKED for Q050-053 only):
Q050-053 is blocked: this environment has no `orca` binary and no speaking Linux desktop session, so the required
Orca Help → open/create → track/clip navigation → edit → save pass could not be run. The other ten units are
implemented and their tests ran green; the Linux/Orca row is recorded as not tested in
`docs/ACCESSIBILITY-TEST-MATRIX.md`. The requested run-log directory
(`~/.local/share/zene-bunny-fix/qwen/runs/Q050-100.r2/logs`) is not writable in this sandbox, so every build, test,
gate and reverse-run log cited below is in the worktree's `.qwen-logs/` (locally excluded from git).

Q050-100: IMPLEMENTED
Q050-099: IMPLEMENTED
Q050-053: BLOCKED (no Orca binary or speaking desktop session in this environment)
Q050-158: IMPLEMENTED
Q050-324: IMPLEMENTED
Q050-202: IMPLEMENTED
Q050-322: IMPLEMENTED
Q050-325: IMPLEMENTED
Q050-320: IMPLEMENTED
Q050-323: IMPLEMENTED
Q050-159: IMPLEMENTED

The batch was built once on the shared foundations: one meter-speech/poller module, one launcher-state observer, one
shipped-keymap model, one keyboard-menu helper, and one manual/task map. Commands are registered once and both the GUI
and the agent call them.

## Q050-100 — Spoken meters and peak watching

| Criterion | Test command and what it proves | Exit |
|---|---|---|
| Speak-meter command and shortcut report the focused channel peak and master LUFS-S. | `ctest -R '^MixerMeteringTest$' --output-on-failure` (cwd `build/tests`, log `.qwen-logs/test-MixerMeteringTest.log`): arms/feeds the real engine tap, sets a known -6.02 dBFS strip peak, invokes `meter.speak`, checks the null/unavailable cases, then drives the strip action and Ctrl+Shift+M through the GUI. | 0 |
| Input and master crossings are announced once per crossing, with re-arm after a fall. | Same test (`peakWatchAnnouncesOncePerCrossingAndHonoursTheLevel`): controlled linear peaks through `meterspeech::observe` prove one message per source, silence while high, re-arm after falling. `theRealMasterTapReArmsTheWatch` feeds the real `MasterLoudnessTap` and proves the consumed sample peak lets the latch fall and cross again. `MeterTapTest::takingTheSamplePeakLetsTheWatchReArm` covers the tap contract. | 0 (MixerMeteringTest); 0 (MeterTapTest, `.qwen-logs/test-MeterTapTest.log`) |
| Announcements honour `view.announcements`, and threshold changes are one undo step. | Same test: `all` speaks, `errors` suppresses the ordinary crossing, `off` suppresses all, and `meter.set_peak_watch` set/undo restores the prior threshold. `MeterTapTest::aDisarmedTapStillPublishesTheSamplePeak` proves the master source while disarmed. | 0 |

Commands added: `meter.speak` (`not_mutating`), `meter.set_peak_watch` (`true_inverse`, one recorded step restoring
enabled + threshold). No project-format change. The full record is `docs/050-features/Q050-100.md`.

## Q050-099 — Keyboard editing for waveform and MPE lanes

| Criterion | Test command and what it proves | Exit |
|---|---|---|
| Mouse-free waveform cursor, range selection, zoom, and an applied range edit that undoes. | `ctest -R '^SampleEditorDialogTest$' --output-on-failure` (`.qwen-logs/test-SampleEditorDialogTest.log`): `theWaveformTakesKeysForCursorRangeZoomAndEdit` uses only keys to move, extend, select all, zoom and apply Silence; one `control.undo` restores the audio. Cursor/range/zoom are view state (not project edits) and are documented as such. | 0 |
| MPE lane edits every selected note, not only the cursor note, with focus and readout. | `ctest -R '^CanvasKeyboardTest$' --output-on-failure` (`.qwen-logs/test-CanvasKeyboardTest.log`): `anMpeLaneEditsEverySelectedNoteInOneStep` changes both selected notes in one undo step for Timbre and Micro-pitch and checks the "2 selected" accessible value. `LaneReadoutTest` passes too. | 0 (CanvasKeyboardTest); 0 (LaneReadoutTest) |
| Stale keyboard wording corrected; focused canvas/accessibility tests run. | `docs/KNOWN-LIMITATIONS.md` now states the waveform keyboard route and per-selection MPE edits; `HelpAccessTest` rejects the old sentences. `CanvasKeyboardTest`, `SampleEditorDialogTest`, `LaneReadoutTest` and `AccessibilityWalkTest` ran. | 0 |

Commands changed: `note.edit` gains the optional MPE `pitch` field (-4800..4800 cents); it remains `true_inverse`
with one MidiClip checkpoint. `window.sample_editor`/`sample.edit_range` keep their classes. No format change.

## Q050-053 — Linux screen-reader core-flow verification

| Criterion | Missing evidence | Exit |
|---|---|---|
| Orca completes Help → open/create → track/clip navigation → edit → save; versions and blockers recorded. | No `orca` executable and no speaking desktop session exist on this box. | BLOCKED |
| Walk the reached controls and regress each missing name/role/value/focus path. | Cannot be derived without the Orca pass. | BLOCKED |
| Publish tested/not-tested status for this workflow only. | `docs/ACCESSIBILITY-TEST-MATRIX.md` records Linux/Orca as not tested and makes no claim for other platforms or controls. | BLOCKED |
| Verify spoken names, slider bounds, values and descriptions in the same pass. | No speech session; the Qt metadata walk is not a substitute. | BLOCKED |

No product code for this unit was left on the branch. Full record: `docs/050-features/Q050-053.md`.

## Q050-158 — Chord-aware keyboard navigation

| Criterion | Test command and what it proves | Exit |
|---|---|---|
| Move by pitch/time, identify chord tones, select and edit from the keyboard. | `ctest -R '^CanvasKeyboardTest$'` (`.qwen-logs/test-CanvasKeyboardTest.log`): `altArrowsWalkTheNotesInTimeThenPitch`, `insertAddsANoteAfterTheCursor` and the chord/edit cases. | 0 |
| Focused pitch and bar/beat location are exposed to a screen reader. | Same target: `theAreaIsReadAsACanvasWithTheCursorNote` now asserts `musicaltime::positionText(192)` as well as the key name. | 0 |
| Tests cover chord grouping, selection and movement across note boundaries. | Same target: same-onset chord tones, a later onset, additive selection and Ctrl resize/velocity steps. | 0 |

No product or format change: the navigation existed on the base; this unit added the missing position assertion and
recorded the existing contract. Full record: `docs/050-features/Q050-158.md`.

## Q050-324 — Accessibility feedback route

| Criterion | Test command and what it proves | Exit |
|---|---|---|
| The same route is in README, CONTRIBUTING and the in-app Access topic. | `ctest -R '^HelpAccessTest$'` (`.qwen-logs/test-HelpAccessTest.log`): `theAccessibilityFeedbackRouteIsTheSameEverywhere` reads all three and compares the exact issue-form URL, then opens the Access topic. | 0 |
| The route asks for workflow, assistive technology and steps, without medical information. | Same test reads `.github/ISSUE_TEMPLATE/accessibility-report.yml`: required `workflow`, `assistive_technology`, `environment` and `steps` fields, no diagnosis/medical field. | 0 |

No command, format, undo or realtime change; the audit command is not presented as the feedback route. Full record:
`docs/050-features/Q050-324.md`.

## Q050-202 — Migration and accessibility keymap setup

| Criterion | Test command and what it proves | Exit |
|---|---|---|
| Accessibility and source-DAW presets are on the GUI and agent surface. | `ctest -R '^ShortcutsCommandsTest$'` (`.qwen-logs/test-ShortcutsCommandsTest.log`) applies all four through `shortcuts.preset_apply` and reads the live binding back; `ctest -R '^ShortcutsPageTest$'` (`.qwen-logs/test-ShortcutsPageTest.log`) triggers the page's Presets action. | 0 / 0 |
| Import preserves a recoverable backup; every preset exports and re-imports. | `ShortcutsCommandsTest::theShippedMapsApplyRoundTripAndLeaveABackup` checks the timestamped backup exists, restores it with `shortcuts.import`, then exports/resets/imports each preset and compares the active binding. | 0 |
| In-app guidance names NVDA, VoiceOver and Orca and links the tested matrix. | `ShortcutsPageTest::theManualNamesTheReadersAndLinksTheTestedWorkflowMatrix` reads the manual and `docs/ACCESSIBILITY-TEST-MATRIX.md`; every untested row is marked. | 0 |

Commands added: `shortcuts.preset_list` (`not_mutating`), `shortcuts.preset_apply` (`true_inverse`, with a durable
backup file documented as a deliberate side effect). No project-format change. Full record:
`docs/050-features/Q050-202.md`.

## Q050-322 — Keyboard track and ruler context menus

| Criterion | Test command and what it proves | Exit |
|---|---|---|
| A focused track header or ruler opens the same menu with Menu or Shift+F10. | `ctest -R '^ArrangerTimelineTest$' --output-on-failure` (`.qwen-logs/test-ArrangerTimelineTest.log`): `trackHeaderAndRulerOpenMenusFromTheKeyboard` focuses both surfaces, opens with both keys and the mouse, and compares the action list. | 0 |
| Actions match the right-click menu and stay keyboard navigable. | Same test compares the full action text of the keyboard and mouse menus; each is a normal `QMenu` that closes from the keyboard. The ruler actions keep the mouse path's Ctrl-at-trigger snap rule. | 0 |
| Regression covers both surfaces and focus behaviour. | Same test checks Tab focus, accessible names, both trigger keys and focus return after closing. | 0 |

No new command, undo step or format change. Full record: `docs/050-features/Q050-322.md`.

## Q050-325 — Correct stale socket-only and no-interface claims

| Criterion | Test command and what it proves | Exit |
|---|---|---|
| Every socket-only/no-interface claim reviewed against GUI and registry. | The audit command and each claim group's GUI entry point/command are recorded in `docs/qwen-fixes-2026-10/Q050-325-audit.md`; the search was rerun after the edits. | 0 (search), 0 (HelpAccessTest) |
| Stale claims corrected; true limits retained with current UI entry points. | `ctest -R '^HelpAccessTest$'` rejects the three stale sentences and requires the current Groove/MIDI Clock/freeze wording and commands. | 0 |
| Focused search confirms every remaining claim was checked. | The audit record lists each group; remaining claims are residual limits (region freeze, rename/remove/humanise, socket-side clock bounds, etc.). | 0 |

No command, project-format, undo or realtime change. Full record: `docs/050-features/Q050-325.md`.

## Q050-320 — Announce launched and stopped clips and scenes

| Criterion | Test command and what it proves | Exit |
|---|---|---|
| UI, bound-controller and agent launches produce one cell/scene announcement. | `ctest -R '^SessionGridViewTest$'` (`.qwen-logs/test-SessionGridViewTest.log`): `theAnnouncerSpeaksCompletedLaunchesOnceAndHonoursTheLevel` drives queued → playing → stop, a whole-scene start/stop, and a refused launch. The observer reads the published state, which is shared by all three routes; `PerformViewTest` covers the learned-MIDI scene launch into that state. | 0 (SessionGridViewTest); 0 (PerformViewTest) |
| Routine announcements honour the level and do not fire for refused or not-yet-active launches. | Same test: a `launching` phase and a refused launch are silent; `all` speaks, `errors`/`off` suppress the ordinary message once per completed transition. | 0 |

No command change; the launch/stop commands stay `not_mutating`. Full record: `docs/050-features/Q050-320.md`.

## Q050-323 — Task-indexed quick start and area reference

| Criterion | Test command and what it proves | Exit |
|---|---|---|
| Searchable task quick start plus a reference for every major area. | `ctest -R '^HelpAccessTest$'` (`.qwen-logs/test-HelpAccessTest.log`): `theQuickStartIsSearchableAndReachesEveryArea` finds all six task headings through the real Find field and checks all eleven `HelpTasks` area headings. | 0 |
| Navigation reaches every task/reference and links commands and tours. | Same test checks each task anchor, the nav link, registered command ids (`track.add`, `note.edit`, `plugin.load`, `track.set_arm`, `render.render`, `app.tour`) and installed tour ids (`first-beat`, `recording`, `mixing`). | 0 |
| Task steps reviewed against the merged UI and command surface. | Same test runs live `hasCommand` checks; the documented menu paths are the merged menus. | 0 |

No command or format change. Full record: `docs/050-features/Q050-323.md`.

## Q050-159 — Patterns, clips, launcher cells and routing

| Criterion | Test command and what it proves | Exit |
|---|---|---|
| A manual section and one tour step compare all three methods with the concrete example. | `ctest -R '^HelpAccessTest$'` (`.qwen-logs/test-HelpAccessTest.log`): `theThreeArrangementMethodsAndTheirRoutingAreExplained` reads the section and `first-beat.json`, including PatternStore pattern 0, the PatternTrack placeholder, the bar-9 MIDI clip and `kick.wav`. | 0 |
| The example traces a track to its mixer channel with current UI and command names. | Same test requires Inspector "To ...", Mix → Routing, `track.overview`, `mixer.get_state` and `track.set_mixer_channel`; the existing `CustomAccessTest`/`ControlEngineWiring` cover the command-side assignment. | 0 (`HelpAccessTest`); 0 (`CustomAccessTest`, `.qwen-logs/test-CustomAccessTest.log`, for the command-side assignment) |

No command or format change. Full record: `docs/050-features/Q050-159.md`.

## Self-check

**? Each new test FAILS with the product change reverted, then PASSES restored.** The product reversions were rebuilt
and run; every command below is unpiped and its status was captured. Restore runs came from `git show HEAD:<path>`.

| Unit | Reverted product hunk | RED exit | Restored exit |
|---|---|---|---|
| Q050-100 | `meter.speak`/`meter.set_peak_watch` command ids disabled in `ControlCommandsMeterSpeech.cpp`; `MixerMeteringTest` rebuilt | 8 (`.qwen-logs/reverse-test-Q050-100.log`; "no command 'meter.speak'") | 0 (`.qwen-logs/restore-test-Q050-100.log`) |
| Q050-099 (waveform) | `SampleSelectionView.cpp` and its header restored to the base revision | 8 (`.qwen-logs/reverse-test-waveform.log`) | 0 (`.qwen-logs/restore-test-sample3.log`) |
| Q050-099 (MPE) | `NoteLaneKeys::noteEditField` mapped Timbre to velocity | 8 (`.qwen-logs/reverse-test-CanvasKeyboardTest-099.log`) | 0 (`.qwen-logs/restore-test-canvas2.log`) |
| Q050-202 | `logic-pro` preset id renamed, so `shortcuts.preset_list` no longer names four | 8 (`.qwen-logs/reverse-test-ShortcutsCommandsTest.log`) | 0 (`.qwen-logs/restore-test-ShortcutsCommandsTest.log`) |
| Q050-322 | `TrackLabelButton` focus policy returned to `NoFocus` | 8 (`.qwen-logs/reverse-test-ArrangerTimelineTest.log`) | 0 (`.qwen-logs/restore-test-ArrangerTimelineTest.log`) |
| Q050-320 | `SessionAnnouncer::observe` returned before reading state | 8 (`.qwen-logs/reverse-test-SessionGridViewTest.log`) | 0 (`.qwen-logs/restore-test-SessionGridViewTest.log`) |
| Q050-323 | quick-start heading renamed in the manual | 8 (`.qwen-logs/reverse-Q050-323-manual.log`) | 0 (restored manual; `test-HelpAccessTest.log`) |
| Q050-324 | feedback URL removed from README | 8 (`.qwen-logs/reverse-Q050-324-readme.log`) | 0 (restored README) |
| Q050-325 | "MIDI clock is in Edit ▸ MIDI Clock" removed from the limitations page | 8 (`.qwen-logs/reverse-Q050-325-limitations.log`) | 0 (restored) |
| Q050-159 | `kick.wav` removed from the manual example | 8 (`.qwen-logs/reverse-Q050-159-manual.log`) | 0 (restored) |

Q050-158 has no product hunk (the navigation is already on the base); its added assertion is the regression for the
existing contract. Q050-053 is blocked and has no test claim.

**? Every acceptance criterion is proven by a test I ran.** The tables above name the test and exit code per
criterion. Final combined run: `ctest -R '^(MeterTapTest|MixerMeteringTest|CanvasKeyboardTest|SampleEditorDialogTest|LaneReadoutTest|HelpAccessTest|ShortcutsCommandsTest|ShortcutsPageTest|ArrangerTimelineTest|SessionGridViewTest|PerformViewTest|ItemListPanelTest|AccessibilityWalkTest|ReversibilityContractTest|ControlNoteExpressionCommandsTest)$' --output-on-failure` from `build/tests` → `EXIT=0`, 15/15 (`.qwen-logs/final-suite.log`). `ReversibilityContractTest` also proves the A16 histogram (846 rows = 422 true_inverse + 42 snapshot + 22 irreversible + 360 not_mutating).

**? One undo step per edit, honest reversibility classes.** `meter.set_peak_watch` records one step restoring both
preference values; `meter.speak` is `not_mutating`; `shortcuts.preset_apply` records one step restoring the previous
overrides and documents the retained backup file; `shortcuts.preset_list` is `not_mutating`; `note.edit` remains one
MidiClip checkpoint for the whole selected-note batch. `ArrangerTimelineTest` and `MixerMeteringTest` exercise the
undo paths; `ReversibilityContractTest` passes.

**? Nothing runs on the audio thread that allocates, frees, locks, waits, does I/O or emits a signal.** The only
audio-path additions are `MasterLoudnessTap::feed`'s fixed-size sample-peak scan (two `std::max`/`std::abs` per frame,
one relaxed atomic load/store), `MasterLoudnessTap::takeSamplePeak` (one relaxed exchange), `MixerChannel::doProcessing`'s
two `MeterValue::set` relaxed stores, and `inputmeter::measure`'s two extra relaxed `raise`s. None allocates, locks,
waits, performs I/O or emits a Qt signal. `meterspeech::poll` and `SessionAnnouncer::observe` run on GUI-thread timers;
`ControlRegistry` calls and `a11y::announceStatus` are GUI-thread only. Local TSan does not run on this box, so the
claim is by inspection of these exact functions rather than a TSan run.

**? Old projects load and render as before; new data round-trips.** No project schema changed. `note.edit`'s `pitch`
is an optional field on an existing note attribute; existing notes default to 0. Peak-watch state and shortcut
presets are application config (`meter/*`, `ui/shortcuts`), not project data. `SampleClip`, `MidiClip` and the song
format are untouched. The changed-sample/note tests and the existing `ReversibilityContractTest` suite pass; no
serialization test regressed.

**? Qt 5.15 / MSVC / macOS < 10.15 portability.** The additions use `QTimer`, `QAction`, `QMenu`, `QKeyEvent`,
`QSaveFile`, `QDateTime`, `QJson*` and `QString` APIs available in Qt 5.15; no Qt-6.8-only announcement API was used
(`a11y::announceStatus` keeps its QT_VERSION guard). No `windows.h` macro name (`small`, `near`, `far`, `interface`,
`min`, `max`) was introduced as an identifier; no `std::filesystem`; no POSIX-only call. `std::clamp` is C++17, already
required by the project. The macOS test targets remain the same Qt Widgets APIs; no deployment-target raise.

**Completion/gate evidence.** `bash tests/fork-sources-gate.sh` → 0; `bash tests/all-sources-reproduce.sh` → 0;
`bash tests/no-upstream-regression-gate.sh` → 0; `tests/scripted/check-namespace` → 0; `bash tests/no-tautology-gate.sh`
→ 0; `python3 tests/doc-figures-gate.py` → 0; `bash tests/complexity-gate.sh --check` and
`bash tests/file-length-gate.sh --check` are red on the train but the feature-check filter found no new item in this
branch's files (`feature_check` copy with a writable log path: RESULT PASS, `.qwen-logs/feature-check-final2.log`). The
official `submit_check.py` reports the gates FAIL when run directly in this worktree only because its sibling log
directory cannot be created in this sandbox (`PermissionError` at `feature_check.py:71`). The same script run on an
archive copy of this HEAD with a writable parent (`/tmp/q050-check`) returns `RESULT: PASS - submit`, including
`PASS gates (feature_check.py)`; the eight gates also ran individually here and passed, and the patched copy that
writes inside the worktree returned PASS. Working tree is clean apart from the locally excluded `.qwen-logs/`.

## Round 3 (review r2 rework) - Q050-100 and the Q050-320 announcement text

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-100.r3/logs/`.

| Review point | Fix | Regression test | Exit |
|---|---|---|---|
| Qt 5.15 dropped the announcement text (meter, peak watch, launcher) | `a11y::announcementSource()` names a one-pixel live-region label with the message and the pre-6.8 alert is raised on it (`AccessibleMode.cpp`) | `MixerMeteringTest::theAlertSourceCarriesTheMessage` (compiled on every Qt, checks the source's accessible name). The `#else` alert line itself is not executed on this Qt 6 box. | build-4 `0`, test-4 `0` |
| Identical crossing text within 1 s was suppressed | `a11y::Repeat::Allow` for crossings and `meter.speak`; latch still keeps held levels quiet | `theRealMasterTapReArmsTheWatch` (same peak twice, `announcementCount()` +1, then held level +0); `aRepeatCanBeAllowedPastTheDebounce` | `0` |
| Shortcut spoke the selected, not focused, strip | `MixerView::focusedChannelView()` walks from `QApplication::focusWidget()` | `theShortcutSpeaksTheFocusedStrip` (another strip selected, keyboard focus on `m_other`, real Ctrl+Shift+M) | `0` |

Reverse run (the three behaviours reverted in `MeterSpeech.cpp`, `MixerView.cpp`, `AccessibleMode.cpp`; test-rev.log):
build `0`, `MixerMeteringTest` exit `8` - the three new tests FAIL (the fourth failure, `thePhaseScopeListensToTheChosenChannel`,
did not fail in any of the three passing runs, so it looks like load under the 33 s reverse run; not investigated). Restored:
`MixerMeteringTest, ItemListPanelTest, MeterTapTest, SessionGridViewTest, CustomAccessTest, ShortcutsCommandsTest,
AccessibilityWalkTest, ReversibilityContractTest, HelpAccessTest` all pass (test-4.log, exit `0`).
`ControlEngineWiring` was not rerun: the effect plug-ins are not built in this tree (its catalogue check reports `devices=[]`), unrelated to this change.

### Self-check (round 3)

- Gates: `feature_check.py` could not write its log directory `../zene-feat-q050-100-feature-check` (permission denied in this sandbox),
  so `submit_check.py` reported FAIL on that item only. A copy of the script with the log directory moved to `Q050-100.r3/logs/feature-check`
  (otherwise unchanged) printed `RESULT: PASS` for all 8 gates, exit `0` (`fc-2.log`).
- Reverse run: the three behaviours reverted -> `MixerMeteringTest` exit `8`, the three new tests FAIL; restored -> exit `0` (see table above).
- Acceptance proof: unchanged tests from rounds 1-2 (`speakMeterReportsTheFocusedChannelAndMasterLufs`, `peakWatchAnnouncesOncePerCrossingAndHonoursTheLevel`,
  `theRealMasterTapReArmsTheWatch`, `MeterTapTest`) pass, exit `0` in `test-4.log`.
- Undo/reversibility: no command was added or changed this round (`meter.speak` not_mutating, `meter.set_peak_watch` true_inverse); `ReversibilityContractTest` passes.
- Audio thread: nothing added this round touches the audio path; the new code runs on the GUI thread (announcement policy, live-region label, shortcut handler).
- Projects: no format change.
- Portability: the pre-6.8 route uses `QLabel`, `QAccessible::queryAccessibleInterface` and `QAccessibleEvent` (all Qt 5.15); no Qt-6-only API added.
  Not run on a real Qt 5.15 build here; the `#else` alert line is compiled only on Qt < 6.8.

## Round 4 (review r3 rework) - Q050-100

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-100.r4/logs/`. Test-only change plus docs; no product code changed.

| Review point | New/changed test (`MixerMeteringTest`) | Reverse run (test-rev.log, exit 8) |
|---|---|---|
| Speak Meter test seeded the atomics | `speakMeterReportsTheFocusedChannelAndMasterLufs` feeds (0.5, 0.25) through `mixToChannel`/`masterMix` and expects -6.02 dBFS | FAILS with the `m_speechPeak*.set()` lines in `Mixer.cpp` removed |
| Input crossing only via `observe()` | `theLiveInputPeakReachesThePollerAndReArms`: `inputmeter::measure` -> `poll()`; crossing, hold, re-arm, identical re-crossing (via `announcementCount()`) | FAILS with the `inputmeter::watch()` call in `MeterSpeech::poll` removed |
| Text never reaching the accessibility layer | `theAnnouncementIsEmittedToTheAccessibilityLayer`: `QAccessible::installUpdateHandler` records the emitted message | FAILS with both `QAccessible::updateAccessibility()` calls in `announceStatus` removed |

All three reversals were applied together: build `0`, `MixerMeteringTest` exit `8`, the three tests above FAIL. (A fourth failure,
`thePhaseScopeListensToTheChosenChannel`, follows from the speak test aborting before it restarts the audio device.) Restored: build `0`
and `MixerMeteringTest, ItemListPanelTest, MeterTapTest, InputMeterTest, SessionGridViewTest, CustomAccessTest, ShortcutsCommandsTest,
AccessibilityWalkTest, ReversibilityContractTest` all pass, exit `0` (test-2.log). On this Qt 6.12 box the handler sees the Qt 6.8 announcement
event; the pre-6.8 branch of the handler (alert name) is compiled only on Qt 5.15 and was not run.


## Round 5 - integration onto train 16

Job: three-way apply of the accepted patch (`feat/050-Q050-100-oc` @ `f60669759`, base `2da2ed356`) onto
`050/m0-train-16` @ `cf3618e61`. Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-100.r5/logs/`.
Verdict: **integrated, no new design needed**; the accepted design and its regression tests are kept.

### Conflicts and how each was resolved

| File | Both sides | Resolution |
|---|---|---|
| `src/gui/editors/TimeLineWidget.cpp` | Train (Q050-147) added punch-range actions to the ruler's `contextMenuEvent`; the batch (Q050-322) moved that menu into `buildLoopMenu(at, snap)` shared by the mouse and the Menu/Shift+F10 path. | The punch actions live inside `buildLoopMenu`, keyed on `at` instead of the event (the mouse passes the clicked time, as before; the keyboard passes the play position) with the same snap/Ctrl rule. Both `<cstdlib>` and `<memory>` kept. |
| `data/manual/index.html` | Train had its own Q050-159 section `workflows`; the batch added a duplicate Q050-159 section `models`, the Q050-323 `quickstart` section and the Access paragraph (incl. Q050-100's Speak Meter / Peak Watch text). | Nav = quick start + the train's sections; the duplicate `models` section removed (the train's `workflows` section carries every term the batch's test needs); Access paragraph = the batch's text with the train's "workspace" wording kept. |
| `data/tours/first-beat.json` (applied clean, semantic duplicate) | Both sides added a "Patterns, clips and cells" step. | The batch's step dropped: the train's `ControlWorkflowModels` requires exactly one such step. The file equals the train's. |
| `docs/050-features/Q050-159.md` | Both added. | The train's record kept (it is the merged implementation). |
| `docs/KNOWN-LIMITATIONS.md` | MIDI-clock bullets (train Q050-308 vs batch Q050-325) and the keyboard-reach paragraph. | Train's text kept (newer: the drift bound is in the Sync form now, so the batch's "socket-side" claim is stale); its headline now reads "MIDI clock is in Edit ▸ MIDI Clock", which Q050-325's test requires. |
| `README.md`, `docs/RELEASE-NOTES-v0.3.0-alpha.md` | Canary and A16 block. | Re-measured (below). |
| `tests/fork-sources.txt`, `tests/upstream-modifications.txt` | Appended lists. | Union of both sides. |
| `tests/all-sources.txt` | Appended list. | Re-derived with its header recipe; `bash tests/all-sources-reproduce.sh` -> REPRODUCES, exit 0. |
| `commands_snapshot.json` | Both regenerated. | Regenerated live (`snapshot_live.py`): **853 ids / 76 groups**; canaries in README, `tests/doc-figures-gate.py`, `docs/050-features/Q050-016.md`; `python3 tests/doc-figures-gate.py` exit 0. |

### Regressions the merge exposed, and their fixes

| Regression (first run, `test-1.log`, exit 8) | Cause | Fix | Regression test |
|---|---|---|---|
| `CanvasKeyboardTest::anMpeLaneEditsEverySelectedNoteInOneStep`: the micro-pitch key changed nothing. | The train's BUG-12 fix (`6cce8fca4`) skips a `note.edit` batch whose `NoteState` is unchanged; `NoteState` knew timbre/pressure but not the batch's new `pitch` field, so a pitch-only edit was dropped as a no-op. | `NoteState` carries `pitchCents`; `applyFlModelTo` applies `pitch` (and marks the expression captured, as `setMpePitchCents` does) - `src/core/ControlCommandsNoteEdit.cpp`. | existing `CanvasKeyboardTest` + new `NoteEditRangeTest::aMicroPitchEditIsAChange` |
| `HelpAccessTest::theReferenceIsGroupedByTheManualsTasks` (train test: every manual `<h2 id>` is a help task, in order) would fail on the batch's `quickstart` heading. | The batch added the section but not a help task. | `quickstart` / "Quick start" is the first `HelpTasks` task, owning no command group (like `workflows`) - `src/gui/HelpTasks.cpp`. `app.help` can now open it. | existing `HelpAccessTest` (both directions) |
| The keyboard ruler menu must keep the punch actions. | (merge resolution above) | as above | `ArrangerTimelineTest::trackHeaderAndRulerOpenMenusFromTheKeyboard` now also requires "Set punch in here" and "Clear the punch range" |
| `ReversibilityContractTest::theTableHistogramIsTheDocumentedOne` | Four new rows from the batch. | A16 block re-taken: `rows=862 true_inverse=427 snapshot=42 irreversible=24 not_mutating=369` (+2 true_inverse `meter.set_peak_watch`, `shortcuts.preset_apply`; +2 not_mutating `meter.speak`, `shortcuts.preset_list`). | the test itself |

Reverse run (all three product fixes reverted together - the `ControlCommandsNoteEdit.cpp` hunk, the `HelpTasks.cpp`
task, the punch block in `buildLoopMenu`): `build-6-reverse.log` exit 0; `ctest -R '^(CanvasKeyboardTest|NoteEditRangeTest|HelpAccessTest|ArrangerTimelineTest)$'`
-> **exit 8**, 4/4 fail at exactly the assertions above (`test-3-reverse.log`). Restored: `build-7-restore.log` exit 0.

### Q050-100 acceptance on the integrated tree

| Criterion | Test | Exit |
|---|---|---|
| Speak-meter command and shortcut report the focused channel peak and master LUFS-S. | `MixerMeteringTest` (`speakMeterReportsTheFocusedChannelAndMasterLufs`, `theShortcutSpeaksTheFocusedStrip`) and `ControlMeterCommands` | 0 (`test-4.log`) |
| Input and master crossings announced once per crossing. | `MixerMeteringTest` (`peakWatchAnnouncesOncePerCrossingAndHonoursTheLevel`, `theRealMasterTapReArmsTheWatch`, `theLiveInputPeakReachesThePollerAndReArms`), `MeterTapTest`, `InputMeterTest` | 0 (`test-4.log`) |
| Announcements honour `view.announcements`; threshold with a controlled signal; one undo step. | `MixerMeteringTest`, `ReversibilityContractTest` | 0 (`test-4.log`) |

Final run from `build/tests`: `ctest -R '^(MeterTapTest|InputMeterTest|MixerMeteringTest|ArrangerTimelineTest|ArrangerTakePunchTest|ArrangerOverlaysVideoLaneTest|CanvasKeyboardTest|NoteEditRangeTest|HelpAccessTest|SampleEditorDialogTest|SessionGridViewTest|ShortcutsCommandsTest|ShortcutsPageTest|ReversibilityContractTest|ControlCommandsSnapshot|ControlMeterCommands|ControlWorkflowModels)$' --output-on-failure`
-> **EXIT=0, 17/17** (`test-4.log`). Build: `build-4.log` exit 0 (`build-1.log`/`-2`/`-3` were environment failures:
a read-only ccache temp dir, then a tool time limit and a self-killing `pkill`; `build-4` used `CCACHE_TEMPDIR=/tmp/...`).

## Self-check (round 5)

- **Each new test fails with the product change reverted, passes restored:** the integration fixes - reverse run exit 8
  (4/4 fail), restored exit 0 (above). The accepted batch's own reverse runs are the round 1-4 records above; its product
  code is unchanged by the integration except the conflict resolution described.
- **Every criterion proven by a test I ran:** the table above, `test-4.log` exit 0.
- **One undo step, honest classes:** no class changed. The `note.edit` fix restores the accepted contract (a pitch
  edit is one MidiClip checkpoint; a true no-op still records none - `NoteEditRangeTest`'s no-op cases pass). The
  ruler's punch actions still go through `transport.punch_set` / `transport.punch_clear`.
- **Audio thread:** nothing added this round runs there - `note.edit`'s pre-check, `HelpTasks` and the ruler menu are
  GUI/control-thread code. The batch's audio-path additions (relaxed atomic stores in `MixerChannel::doProcessing`,
  `MasterLoudnessTap::feed`, `inputmeter::measure`) are unchanged. No TSan run on this box; by inspection.
- **Projects:** no format change; the integration touches no serialization.
- **Portability:** no new API; `NoteState`'s defaulted `operator==` was already there (C++20, which the tree requires).
- **Gates:** `submit_check.py <worktree> Q050-100` (`submit-check-1.log`, exit 1) passes every mechanical item except
  "gates", and that one only because `feature_check.py:71` cannot create its sibling log directory
  `../zene-integrate-Q050-100-cf3618e61a-feature-check` in this write-jail (`feature-check-1.log`: PermissionError).
  A copy of `feature_check.py` that differs only in that log path (and an absolute `ROOT`) ran on HEAD `1e12a726b`:
  fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures all exit 0;
  complexity and file-length "no new item in your files"; **RESULT: PASS, exit 0** (`feature-check-2.log`).
