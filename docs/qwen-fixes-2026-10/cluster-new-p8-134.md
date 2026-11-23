ALREADY-FIXED

new-p8-134: ALREADY-FIXED

The finding was REAL when recorded, and it is already fixed on this branch by commit `5d3a8e0809` ("fix: preserve output bus routes on preset load (new-p18-43)"), which landed before this job started. The finding's `clef_tier` was `new:needs_context` with historical line numbers; the code it describes has moved into `src/tracks/InstrumentTrackNotes.cpp`.

Code evidence, read this session:

- `src/tracks/InstrumentTrackNotes.cpp:424-432` — `saveZeneInstrumentSettings` omits `InstrumentOutputBuses::saveSettings` when `presetMode` is set, which is the save half the finding describes.
- `src/tracks/InstrumentTrackNotes.cpp:434-447` — `loadZeneInstrumentSettings(element, presetMode)` now reads routes only when the preset explicitly carries the node: `if (buses != nullptr && !m_previewMode && (!presetMode || !element.firstChildElement(QStringLiteral("outputbuses")).isNull()))` at `:441-443`. So a preset without `<outputbuses>` no longer clears the track's routes, while a project restore (which always passes nodes it saved) still resets them. `:444` is the only `buses->loadSettings` call site in `src/`.
- `src/core/Track.cpp:1057-1060` — `Track::loadPreset` calls `loadTrack(element, true)`; `:1077` is the project-state restore, `loadTrack(element, false)`. `:880` and `:954` dispatch those two modes into the two-argument `loadTrackSpecificSettings`.
- The only producers of preset documents are `src/gui/editors/TrackContainerView.cpp:604`, `src/core/ControlCommandsTrackPresets.cpp:100` and `src/core/ControlDeviceState.cpp:186`, and all three call `Track::loadPreset`. So every preset entrance reaches the guard, and every project load is distinguishable from it.
- `src/core/Track.cpp:1062-1066` — the default mode-aware virtual forwards to the legacy one-argument hook, so `SampleTrack`, `PatternTrack`, `NeutralTrack`, `AutomationTrack` and `TrackFolder` (which do not override the two-argument form) keep their previous load behaviour exactly.

Regression target, present and passing: `tests/src/core/InstrumentRoutingTestParts1.h:33-61`, `InstrumentRoutingTest::presetWithoutOutputBusNodeKeepsRoutes`. It routes bus 1 to a real mixer channel, loads a preset with no `<outputbuses>` and asserts the route survives (`:46`), then adds an explicit node and asserts it replaces the route (`:56`), then loads the same document as project state and asserts the route is cleared (`:60`). It is deterministic: builder-created DOM documents, no timers or threads.

| Evidence | Command | Exit |
| --- | --- | ---: |
| Baseline: focused target built at HEAD | `cmake --build build --target InstrumentRoutingTest -j4` | 0 |
| Baseline GREEN: 1/1 CTest target passed | `ctest -R InstrumentRoutingTest --output-on-failure` from `build/tests` | 0 |
| Step 4b RED setup: reverted only the preset guard in `src/tracks/InstrumentTrackNotes.cpp` to the pre-fix body and saved that diff | `git diff -- src/tracks/InstrumentTrackNotes.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/new-p8-134.r1/logs/fix.patch` | 0 |
| RED build | `cmake --build build --target InstrumentRoutingTest -j4` | 0 |
| RED: the reported symptom reproduces — `routeOf(1)` became `-1`, expected channel id `7`, at `InstrumentRoutingTestParts1.h:46`; 19 passed, 1 failed | `ctest -R InstrumentRoutingTest --output-on-failure` from `build/tests` | 8 |
| Restored the committed fix | `git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/new-p8-134.r1/logs/fix.patch` | 0 |
| GREEN build | `cmake --build build --target InstrumentRoutingTest -j4` | 0 |
| GREEN: 1/1 CTest target passed | `ctest -R InstrumentRoutingTest --output-on-failure` from `build/tests` | 0 |
| GREEN detail: `PASS : InstrumentRoutingTest::presetWithoutOutputBusNodeKeepsRoutes()`, `Totals: 20 passed, 0 failed` | `ctest -R InstrumentRoutingTest -V` from `build/tests` | 0 |
| Final feature gate table: every row PASS | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p8-134` | 0 |

Logs: `build-1.log`, `test-1-baseline.log`, `fix.patch`, `build-2-red.log`, `test-2-red.log`, `build-3-green.log`, `test-3-green.log`, `test-4-green-detail.log`, `gates-1.log` in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-134.r1/logs`.

The RED run is the proof that the test catches this bug rather than passing vacuously: with the guard removed, the exact assertion that the finding predicts — an unrouted bus after a preset load — fails, and no other test in the family fails. With the committed code restored, it passes.

This job changed no product code, so there is no fix commit beyond this report. Files in this commit: `docs/qwen-fixes-2026-10/cluster-new-p8-134.md` only.

## Self-check

- **Code that shows the verdict (ALREADY-FIXED):** `src/tracks/InstrumentTrackNotes.cpp:441-443`, the `!presetMode || !element.firstChildElement("outputbuses").isNull()` guard, with its only route-load call at `:444`; the preset/project mode split at `src/core/Track.cpp:1059` versus `:1077` and `:880` versus `:954`. Introducing the fix commit: `5d3a8e0809`, present at `HEAD` before this session.
- **Regression test fails without the product change and passes with it:** with the guard reverted, `ctest -R InstrumentRoutingTest --output-on-failure` from `build/tests` exited 8 with `Actual -1 / Expected 7` at `InstrumentRoutingTestParts1.h:46`; with the committed guard, the same command exited 0 and `-V` reported `PASS : InstrumentRoutingTest::presetWithoutOutputBusNodeKeepsRoutes()`, `Totals: 20 passed, 0 failed`. Both runs and their build steps are in the table above.
- **Command files:** this job changed no command file and no file under `src/core/Control*`, so the sibling-command list (`app.*`, `arrangement.*`) is untouched. The one preset-loading command that reaches this code, `ControlCommandsTrackPresets.cpp:100`, calls `Track::loadPreset` and therefore takes the same guarded path as the GUI; its behaviour for a preset without routes is the fixed one, and no reply schema or argument check changed. `ControlDeviceState.cpp:186` reaches it the same way.
- **Why not FIXED here:** the product change already existed at `HEAD` before this session; making a second, cosmetic edit to claim it would have been a change outside the bug. The verdict rests on the code reference above plus the RED/GREEN pair that shows the branch's guard is what makes the assertion pass.
- **Does the existing test really cover the reported path:** yes. It calls the production `Track::loadPreset` (not a test seam) at `InstrumentRoutingTestParts1.h:45` and `:55`, and the project-restore comparison at `:59` uses `loadSettings`, so the preset/project distinction that the finding asks for is the thing under test.
- **Caller and sibling audit:** the single route-load call site is `src/tracks/InstrumentTrackNotes.cpp:444`; the single mode dispatch is `Track::loadTrack`, whose two callers pass literal `true` (preset) and `false` (project) at `src/core/Track.cpp:880` and `:954`. All three preset entrances go through `Track::loadPreset`. Track types that do not override the two-argument loader are unaffected because the base implementation delegates to the legacy one-argument hook.
- **Collateral and compatibility:** project files still load exactly as before — a project restore passes `presetMode=false`, which skips the new condition entirely. Presets that do carry an explicit `<outputbuses>` node still replace routes, as the `:56` assertion shows. No limit or rejection was added, so no other caller can be wrongly refused.
- **Realtime, undo and commands:** the change this verdict covers touches only the load path on the control thread; `InstrumentOutputBuses::loadSettings` already ran there. No allocation, lock or logging was added to process or render code, no undo step changed, and no control command was added.
- **Registration:** no file was added or modified in this session's commit apart from the report, which is not a registered product or test source, so `fork-sources` and `upstream-modifications` are unchanged and both gates PASS.
- **Unverified:** I did not run the GUI harness or the full test suite; the verdict needs neither, and the focused family is the one the finding names. Local TSan does not run on this box and was not attempted; no concurrency question arises here because the behaviour is a synchronous DOM load.

## Other findings

None encountered in the paths read for this job.
