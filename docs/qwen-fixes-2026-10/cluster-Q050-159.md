IMPLEMENTED

# Q050-159 - Explain patterns, arrangement clips, launcher cells, and mixer routing

The manual now has a top-level **Patterns, clips and cells** section and "Your first beat" has a matching tour
step. Both use one concrete example - a kick-and-snare pattern at bar 1, a one-bar MIDI fill at bar 9, and
`kick.wav` in the sample track's launcher column - to separate what a pattern stores, what a timeline clip stores
and what a launcher cell references. The routing paragraph traces the sample track through the Inspector's
"To ..." field and Mix > Routing, and gives agents the same path with `track.overview`, `mixer.get_state` and
`track.set_mixer_channel`.

No playback, model, routing, command, serialization or audio-thread code changed. The only code change is the
help-task table entry that makes the new manual section reachable by F1 / `app.help {topic: "workflows"}`, plus a
guard so a command-less task does not claim the empty command group.

## Acceptance

| Criterion | Test | Command (as run) | Exit |
| --- | --- | --- | --- |
| Add a manual section and one tour step comparing patterns, arrangement clips and launcher cells with a concrete example. | `ControlWorkflowModels::check_manual` + `check_tour` reads the shipped `data/manual/index.html` section (PatternStore pattern 0, bar 1, bar 9, `kick.wav`, PatternTrack / PatternClip placeholder, MIDI clip, launcher cell) and parses `data/tours/first-beat.json` for exactly one "Patterns, clips and cells" step that targets the Inspector and uses the same bar 1 / bar 9 / `kick.wav` example. `HelpAccessTest::theReferenceIsGroupedByTheManualsTasks` also requires every manual `<h2 id>` to be a help task, in order. | `cd build/tests && ctest -R '^ControlWorkflowModels$\|^HelpAccessTest$\|^ShortcutsPageTest$' --output-on-failure` | 0 (3/3 passed) |
| Verify the example traces a track's signal to its mixer channel using the current UI and command names. | `ControlWorkflowModels::check_track_channel` + `check_reads`: the manual body must name the Inspector, "To ...", Mix > Routing, `track.overview`, `mixer.get_state`, `track.set_mixer_channel`, `arrangement.get_state`, `session.pattern_list`, `session.get_state`, `session.set_slot`. On a live `zene`: add a destination channel (ch-10) and rename it, add a sample track, `track.set_mixer_channel` routes it, `track.overview` reports ch-10, `mixer.get_state` has the same id and name, one `control.undo` restores the previous channel, and the three read commands answer. | same `ctest` invocation | 0 (3/3 passed) |

Red/green proof for the new documentation test (product docs reverted, no rebuild needed because the manual and
tour are read from `data/`):

| Step | Command | Exit |
| --- | --- | --- |
| Revert only `data/manual/index.html` and `data/tours/first-beat.json` to `HEAD`; run the new test | `cd build/tests && ctest -R '^ControlWorkflowModels$' --output-on-failure` | 8 - 7 documentation checks fail (`the manual has the Patterns, clips and cells section`, the example, the routing names, the channel-ownership sentence, the tour step, its text and its target); the live command trace still passes |
| Restore both files; run it again | `cd build/tests && ctest -R '^ControlWorkflowModels$' --output-on-failure` | 0 |

Red/green proof for the help-task alignment (new assertion in an existing test):

| Step | Command | Exit |
| --- | --- | --- |
| Revert only `src/gui/HelpTasks.cpp` to `HEAD`, rebuild `zene HelpAccessTest`, run | `cd build/tests && ctest -R '^HelpAccessTest$' --output-on-failure` | 8 - `theReferenceIsGroupedByTheManualsTasks` fails: `Compared lists have different sizes. Actual (manualTopics) size: 12, Expected (help::topics()) size: 11` |
| Restore `HelpTasks.cpp`, rebuild the same targets, run | `cd build/tests && ctest -R '^HelpAccessTest$' --output-on-failure` | 0 |

## Self-check

**? Each new test FAILS with your product change reverted (reverse the non-test hunks, rebuild, run: exit code?), then PASSES restored.**
Yes. `ControlWorkflowModels` fails 8 with only the manual/tour product hunks reverted and passes 0 restored (table
above; logs `test-red-final.log`, `test-green-restored.log`). The new `HelpAccessTest` assertion fails 8 with only
`HelpTasks.cpp` reverted and passes 0 restored (tables above; `test-red-helptasks.log`, `test-green-helptasks.log`).

**? Every acceptance criterion is proven by a test you ran yourself (name the test and its exit code per criterion).**
Criterion 1: `ControlWorkflowModels` (manual/tour checks) and `HelpAccessTest::theReferenceIsGroupedByTheManualsTasks`
- 3/3 ctest targets passed, exit 0. Criterion 2: `ControlWorkflowModels` (routing checks) on the real binary - same
run, exit 0. The red runs above show the documentation checks are not tautological.

**? Each edit is ONE undo step, and every new or changed command has an honest reversibility class.**
No new or changed command metadata. The section only names existing commands; the reads (`track.overview`,
`mixer.get_state`, `arrangement.get_state`, `session.pattern_list`, `session.get_state`) are `not_mutating`, and
`track.set_mixer_channel` / `session.set_slot` are `true_inverse`. The transcript exercises the one-undo path of
`track.set_mixer_channel` (undo restores `previous`), and no snapshot/A16 histogram change is needed.

**? Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a signal.**
No engine or audio-path file changed. `src/gui/HelpTasks.cpp` is a static help table read by the GUI help centre;
the manual and tour are data files; the tests run on the test process. Nothing added is reachable from the audio
thread.

**? Old projects still load and render exactly as before; new data round-trips through save and load.**
No project serialization, model, playback or render code changed. Only `data/manual/index.html`,
`data/tours/first-beat.json`, the help-task table, tests and docs changed, so old projects load and render as before.

**? Qt 5.15 / MSVC / macOS < 10.15 portability: no Qt-6-only API without a guard, no windows.h macro names, no std::filesystem outside _WIN32.**
`HelpTasks.cpp` only extends an `std::array` of `const char*` and skips empty group strings; `HelpAccessTest` uses
`QString`, `QStringList`, `QString::indexOf` and `QString::mid`. No Qt 6-only API, no `windows.h` macro name, no
`std::filesystem`. The Python transcript is not product code.

## Gates and environment note

- `python3 tests/doc-figures-gate.py` -> 0 (`RESULT: PASS`; 828 ids / 76 groups, unchanged).
- The official `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` cannot create its log
  directory outside the worktree in this session (`PermissionError: .../zene-feat-q050-159-feature-check`).
  The same script with only its log path redirected to `/tmp` (`/tmp/qwen_checks/feature_check.py`, no logic
  change) -> 0: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology,
  doc-figures, complexity (no new item in my files) and file-length all PASS.
- The official
  `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> Q050-159` -> 1, with the single FAIL
  `gates (feature_check.py)` caused by that same log-directory permission; every other row PASSes (the
  agent-command row is the expected WARN for a docs unit that adds no command).
- The same `submit_check.py`, with only the log path of the `feature_check.py` it calls redirected to `/tmp` ->
  **0, `RESULT: PASS - submit`**; `gates (feature_check.py)` PASS. Logs:
  `submit-check-official.log`, `submit-check-patched.log`.
- Final test run: `ctest -R '^ControlWorkflowModels$|^HelpAccessTest$|^ShortcutsPageTest$'` -> 0 (3/3 passed).

## Files

`data/manual/index.html`, `data/tours/first-beat.json`, `src/gui/HelpTasks.cpp`, `tests/src/gui/HelpAccessTest.cpp`,
`tests/control-workflow-models.py`, `tests/CMakeLists.txt`, `tests/fork-sources.txt`,
`docs/050-features/Q050-159.md`.
