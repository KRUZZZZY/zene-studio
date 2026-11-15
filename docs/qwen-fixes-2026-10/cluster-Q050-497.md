IMPLEMENTED

# Q050-497 Earcons: monitoring tones for edits, refusals, recording and loop wraps

Branch `feat/050-Q050-497-oc`, based at `40b4322860`. The range adds the earcon engine and device-output mix
(`08a0db26fa`, `f206bd4847`), `view.earcons`, **View > Earcons**, and event hooks (`35fd14bb39`, `cd0dce5525`,
`3ffab7b9d3`), the output-buffer regression (`4d02eb4b0b`), and centralized successful-edit feedback plus editor-route
coverage (`7cddcf406e`). It also includes the menu undo regression (`7765dc84a2`), source-registry and argument-check
maintenance (`aba427a327`, `bd8d6c419b`), and removes the Q050-369 List-button changes from this unit (`516d53310e`).
Feature record: `docs/050-features/Q050-497.md`.

What changed: `lmms::earcons` (`include/Earcons.h`, `src/core/Earcons.cpp`) synthesizes six short tones into two
preallocated banks and plays them from a lock-free pending mask. `AudioEngine::renderNextBuffer` (`include/AudioEngine.h`)
mixes them into device output; `renderNextPeriod`, which feeds export and recorder taps, does not consume or mix them.
`ControlRegistry::dispatch` (`src/core/ControlRegistry.cpp:307`) queues `EditApplied` after every successful mutating
command, including `NoteEditor::run`, automation point commands, forms, palettes, and socket dispatch. The one
automation edit path that applies a point in place queues the same sound in `src/gui/editors/AutomationPointEdit.cpp:83`.
Other hooks cover refused transitions and record start/stop (`src/gui/AccessibleAnnouncements.cpp:199-201`), selection
at an automation endpoint (`src/gui/editors/AutomationEditorKeys.cpp`), and song loop wrap (`src/core/Song.cpp:707`).
The new `view.earcons` command (`category`, optional `enabled`, optional `level` 0..1; at least one setting required)
has a **View > Earcons** menu (`src/gui/MenuBarExtras.cpp`, `addEarconsMenu`) with one submenu per category. Settings
live in the UI config `ui/earcons`, not the project.

Independent verification on the submitted tree: `cmake --build build --target zene EarconsTest EarconsRenderTest
EarconsSettingsTest -- -j4` exit 0; from `build/tests`, `ctest -R '^EarconsTest$' --output-on-failure`,
`ctest -R '^EarconsRenderTest$' --output-on-failure`, and `ctest -R '^EarconsSettingsTest$' --output-on-failure`
each exit 0. `ControlCommandsSnapshot` and the out-of-scope `ItemListButtonsTest` also pass (exit 0). The exact gate
`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-feat-q050-497`
exits 0 with no new complexity or file-length item. Reversing the
`include/AudioEngine.h` product diff still builds (`zene` and `EarconsRenderTest`, exit 0), while
`ctest -R '^EarconsRenderTest$' --output-on-failure` exits 8 at `theDeviceOutputCarriesTheEarcon` because the device
buffer peak is zero. Restoring the product diff and rebuilding/rerunning the focused suites returns all three to exit 0.
Logs: `/tmp/Q050-497-restored-build.log`, `/tmp/Q050-497-final-EarconsTest.log`,
`/tmp/Q050-497-final-EarconsRenderTest.log`, `/tmp/Q050-497-final-EarconsSettingsTest.log`,
`/tmp/Q050-497-ControlCommandsSnapshot.log`, `/tmp/Q050-497-ItemListButtons-build.log`,
`/tmp/Q050-497-ItemListButtonsTest.log`, `/tmp/Q050-497-red-build.log`, `/tmp/Q050-497-red-ctest.log`, and
`/tmp/Q050-497-final-feature-check.log`.

## Acceptance
| Criterion | Test | Command | Exit |
| --- | --- | --- | --- |
| The earcon set plays on the monitoring-only path | `EarconsTest` covers defaults, category gating, levels, allocation, render exclusion and supported rates; `EarconsSettingsTest` covers refused, applied-edit, note-drawing, automation-key edit, record and selection-boundary hooks; `EarconsRenderTest` covers loop wrap, render/take exclusion, and samples in the actual device output buffer | `ctest -R '^EarconsTest$' --output-on-failure`; `ctest -R '^EarconsRenderTest$' --output-on-failure`; `ctest -R '^EarconsSettingsTest$' --output-on-failure` | 0 |
| Off by default, per category, own level, from settings and the socket | `EarconsSettingsTest` `everyCategoryIsOffByDefault` (all 5 off, `ui/earcons` empty), `theViewMenuSwitchesEditOnAndSetsItsLevel` (menu trigger -> enabled + stored + checked; 100% -> level 1.0 + stored + checked), `theSocketWritesOneUndoStep` (registry entry used by the socket returns `previous`; one `control.undo` restores both), `invalidArgumentsAreTyped` (unknown category, level 1.5, level -1, missing args, `enabled: 1`) | `ctest -R '^EarconsSettingsTest$' --output-on-failure` | 0 |
| No allocation, lock, wait, I/O or signal on the audio thread | design (`include/Earcons.h` contract; `play` = atomic load + release `fetch_or`; `beginBuffer` = single-consumer `exchange`; `nextSample` reads preallocated banks) + `EarconsTest` `theAudioPathDoesNotAllocate`, `theRenderPathNeverConsumesAnEarcon` | `ctest -R '^EarconsTest$' --output-on-failure` | 0 |
| Not in render output or recorded takes | `EarconsRenderTest` `renderOutputIsIdenticalWithEarconsOn` (every category on + every sound queued: same frame count, every sample equal, non-silent, earcons still queued), `aRecordedTakeHasNoEarcons` (master-tap take all zero, earcons still queued and audible afterwards) | `ctest -R '^EarconsRenderTest$' --output-on-failure` | 0 |

All three focused tests ran on the restored tree; each completed 1/1 with exit 0.

## Self-check
- **The device-output regression FAILS with the mix reverted and PASSES restored.** I reversed only the full
  `include/AudioEngine.h` product diff, rebuilt `zene` and `EarconsRenderTest` (exit 0), and ran
  `ctest -R '^EarconsRenderTest$' --output-on-failure`; it exited 8 with
  `theDeviceOutputCarriesTheEarcon: peak() > 0.01f` false at `tests/src/core/EarconsRenderTest.cpp:290`. I restored the
  diff, rebuilt, and reran all three focused suites; each passed (exit 0). This is a runtime RED at the assertion
  that observes actual device-output samples.
- **Every acceptance criterion is covered by the focused tests and source inspection.** Each table row names the tests
  and commands; the three suites passed after the output-mix reversal was restored.
- **One undo step, honest reversibility.** `view.earcons` is `TrueInverse` in
  `src/core/ControlReversibilityTableW3Custom.cpp:136` with the reversibility flag set; the recorded undo step writes the
  previous `ui/earcons` object back and re-applies it. `theSocketWritesOneUndoStep` sets `enabled: true, level: 1.0` in
  one call and a single `control.undo` restores both the in-memory state and the stored config; every menu entry is one
  `view.earcons` call, hence one checkpoint. The commands snapshot was regenerated live
  (`tools/mcp-zene-control/zene_control/commands_snapshot.json` is in the diff).
- **Nothing added runs on the audio thread that allocates, frees, locks, waits, does I/O or emits a signal.** The
  audio-path functions touched are `AudioEngine::renderNextBuffer` (`earcons::beginBuffer()` once per buffer,
  `earcons::nextSample()` once per frame) and `Song`'s loop wrap (`earcons::play`, preceded only by an atomic
  `fetch_add`). `play()` is one relaxed atomic load of the category plus a release `fetch_or` on a `uint32_t` pending
  mask; `beginBuffer()` consumes the mask with one `exchange` (single consumer, the device thread); `nextSample()` reads
  preallocated `float` arrays and a lock-free `std::atomic<float>` level. `prepare()` - which synthesizes and would
  allocate nothing but does fill a bank - runs only on the control thread while processing is stopped
  (`AudioEngine.cpp:807/1414/1443`). The three atomics are `static_assert`ed lock-free, and `EarconsTest
  theAudioPathDoesNotAllocate` probes the allocation operators. No mutex, no queue with several producers, no Qt signal,
  no `QString` on the audio path. TSan cannot run on this box, so the single-consumer claim is a design argument, not a
  TSan result.
- **Old projects still load and render exactly as before; new data round-trips.** No project-format change at all: the
  settings are UI config (`ui/earcons`), and a project never stores earcon state, so old projects load and render
  unchanged. `renderOutputIsIdenticalWithEarconsOn` proves the render is sample-identical with every earcon on
  (non-silent fixture, same frame count). New data round-trips: `theViewMenuSwitchesEditOnAndSetsItsLevel` and
  `theSocketWritesOneUndoStep` read the stored `ui/earcons` back, and `uistore::applyStoredEarcons`
  (`src/gui/UiConfigStore.cpp:99-106`) re-applies it at startup, ignoring an out-of-range stored level.
- **Completeness through the real user and agent dispatch paths, with edge cases.** User path: **View > Earcons > <Category> >
  Enabled / 25-100 %** builds a `CommandCall` and runs the registered `view.earcons`; `theViewMenuSwitchesEditOnAndSetsItsLevel` triggers the real `QAction`s and checks the in-memory state, the stored document and the menu check state. The agent test invokes `ControlRegistry::invoke`, which `ControlServer::dispatchLine` uses for socket commands (`src/core/ControlServer.cpp:559`); `theSocketWritesOneUndoStep` and `invalidArgumentsAreTyped` cover values, results and undo. The command snapshot includes `view.earcons`. Edge cases tried: missing
  category, unknown category (`bogus`), level 1.5, level -1, no `enabled`/`level` at all, `enabled` as the number 1,
  every category off at startup, and a song that wraps its loop with `LoopWrap` queued. Stored out-of-range levels are
  ignored by `applyStoredEarcons` in source (`src/gui/UiConfigStore.cpp:99-106`).
- **The GUI test drives the visible control and checks the same registered command ran.** `EarconsSettingsTest` opens the
  real View menu (`aboutToShow`), finds the `earconsMenu` and the per-category submenu, and calls `trigger()` on the
  *Enabled* and *100%* actions - not the handler and not the registry directly - then asserts the state the registered
  command wrote (`earcons::enabled`, `earcons::level`, `uistore::read("earcons")`), that `control.undo_depth` grows by
  one after each menu trigger (depth0, depth0+1, depth0+2), and that two `control.undo` calls restore first the level
  (Edit still enabled) and then the switch (Edit disabled and not stored), back to depth0. This ran green: `010-ctest-depth.log`,
  exit 0, 1/1 (`009-build-depth.log` exit 0). `theAutomationBoundaryIsHeard` and `anAppliedEditIsHeard` drive
  `QTest::keyClick` and a real `runCommand`, and measure the monitoring output, not an internal flag.
- **Qt 5.15 / MSVC / macOS portability.** The changed code uses Qt APIs shared by Qt 5.15 and Qt 6; it adds no
  `std::filesystem` or Qt-6-only API. `std::min`/`std::max` are used, and the project defines `NOMINMAX` in the root
  `CMakeLists.txt:33`. The audio code uses standard C++ atomics, arrays and math. This host has no Qt 5.15, Windows,
  macOS 10.15 or clang windows-arm64 build, so those compile targets were not run.

## Note on scope
`aba427a327` (source registries derived from tracked files) and `bd8d6c419b` (argument checks split into helpers) are
repo-hygiene and complexity-target changes on this branch, not earcon behaviour; they keep the fork-source gate and the
complexity gate green.

## Portability limit
No non-Linux CI target was available locally. The changed code and project build settings were inspected for Qt 5.15,
MSVC, macOS 10.15 and clang windows-arm64 compatibility; hosted CI must provide those builds.
