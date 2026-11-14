IMPLEMENTED

# Q050-497 Earcons: monitoring tones for edits, refusals, recording and loop wraps

Branch `feat/050-Q050-497-oc`, cut from `050/m0-train-31` (`40b4322860`). Commits: `08a0db26fa` (the earcon engine and
the device-output mix), `f206bd4847` (prepared at every output rate up to 192 kHz), `35fd14bb39` (`view.earcons`, **View >
Earcons**, the four hooks), `cd0dce5525` (render/take tests and the edit and record hooks), `3ffab7b9d3` (loop-wrap
test), `aba427a327` (source registries derived from tracked files), `bd8d6c419b` (argument checks split into helpers,
under the complexity target), plus this report. Feature record: `docs/050-features/Q050-497.md`.

What changed: `lmms::earcons` (`include/Earcons.h`, `src/core/Earcons.cpp`) synthesizes six short tones into two
preallocated banks and plays them from a lock-free pending mask. `AudioEngine::renderNextBuffer` (`include/AudioEngine.h`
`:677`, `:689`) mixes them into the two output channels - the device output path only; `renderNextPeriod`, which feeds
export and the recorder taps, never calls them. Hooks: a mutating command that ran (`src/gui/CommandRun.cpp:121`), a
refused transition and record start/stop (`src/gui/AccessibleAnnouncements.cpp:199-201`), an automation selection at the
first/last point (`src/gui/editors/AutomationEditorKeys.cpp:150`), a song loop wrap (`src/core/Song.cpp:707`). The new
command `view.earcons` (`category` enum, `enabled`, `level` 0..1, at least one of the two) has a **View > Earcons** menu
(`src/gui/MenuBarExtras.cpp` `addEarconsMenu`) with one submenu per category. Settings live in the UI config
`ui/earcons`, not the project.

Logs: `/tmp/Q050-497.r2-logs/` (the runs/logs dir was not writable, so `DUET_LOG_DIR` pointed there). Build
`001-build-ccn.log` exit 0; `002-ctest-ccn.log` `EarconsSettingsTest` exit 0; gates `003-gates.log` feature_check RESULT
PASS; RED `004-build-red.log` exit 2; restored `005-build-green.log` exit 0; `006-ctest-green.log` exit 0; undo-depth
assertions `009-build-depth.log` exit 0, `010-ctest-depth.log` exit 0 (1/1).

## Acceptance
| Criterion | Test | Command | Exit |
| --- | --- | --- | --- |
| The earcon set plays on a monitoring-only path | `EarconsTest` (6 cases: silence by default, only an enabled category, levels, no allocation, render never consumes, every rate prepared); `EarconsSettingsTest` `aRefusedTransitionIsHeardOnlyWhenEnabled`, `anAppliedEditIsHeard`, `recordingStartAndStopAreHeard`, `theAutomationBoundaryIsHeard`; `EarconsRenderTest` `aLoopWrapQueuesItsEarcon` | `ctest -R '^EarconsTest$'`, `ctest -R '^EarconsSettingsTest$'`, `ctest -R '^EarconsRenderTest$'` (`006-ctest-green.log`: `ctest -R Earcons` 3/3) | 0 |
| Off by default, per category, own level, from settings and the socket | `EarconsSettingsTest` `everyCategoryIsOffByDefault` (all 5 off, `ui/earcons` empty), `theViewMenuSwitchesEditOnAndSetsItsLevel` (menu trigger -> enabled + stored + checked; 100% -> level 1.0 + stored + checked), `theSocketWritesOneUndoStep` (the socket call returns `previous`, one `control.undo` restores both), `invalidArgumentsAreTyped` (unknown category, level 1.5, level -1, missing args, `enabled: 1`) | same `ctest -R '^EarconsSettingsTest$'` | 0 |
| No allocation, lock, wait, I/O or signal on the audio thread | design (`include/Earcons.h` contract; `play` = atomic load + release `fetch_or`; `beginBuffer` = single-consumer `exchange`; `nextSample` reads preallocated banks) + `EarconsTest` `theAudioPathDoesNotAllocate`, `theRenderPathNeverConsumesAnEarcon` | `ctest -R '^EarconsTest$'` | 0 |
| Not in render output or recorded takes | `EarconsRenderTest` `renderOutputIsIdenticalWithEarconsOn` (every category on + every sound queued: same frame count, every sample equal, non-silent, earcons still queued), `aRecordedTakeHasNoEarcons` (master-tap take all zero, earcons still queued and audible afterwards) | `ctest -R '^EarconsRenderTest$'` | 0 |

Neighbours: `002-ctest-ccn.log` ran `EarconsSettingsTest` only (1/1, exit 0). `ctest -R Earcons` on the restored
tree: 3/3 passed, exit 0 (`006-ctest-green.log`).

## Self-check
- **Each new test FAILS with the product change reverted, PASSES restored.** With all product hunks of `src/` and
  `include/` reversed by `git apply -R` and the tests unchanged, the build **exits 2**, first error
  `tests/src/core/EarconsTest.cpp:37: fatal error: Earcons.h: No such file or directory`
  (`004-build-red.log`). This is a compile-time RED: the feature adds the header the tests include, so there is no
  per-case runtime RED. Restored and rebuilt: exit 0 (`005-build-green.log`); `ctest -R Earcons` 3/3 pass, exit 0
  (`006-ctest-green.log`).
- **Every acceptance criterion is proven by a test I ran.** The table above names the test and its exit per criterion; all
  four rows are exit 0 from `006-ctest-green.log`, plus `002-ctest-ccn.log` exit 0 for `EarconsSettingsTest`. No row rests
  on a test I did not run.
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
- **Completeness through the real user and agent paths, with edge cases.** User path: **View > Earcons > <Category> >
  Enabled / 25-100 %** builds a `CommandCall` and runs the registered `view.earcons`; `theViewMenuSwitchesEditOnAndSetsItsLevel` triggers the real `QAction`s and checks the in-memory state, the stored document and the menu check state. Agent
  path: the socket/MCP call, `theSocketWritesOneUndoStep` plus `invalidArgumentsAreTyped`. Edge cases tried: missing
  category, unknown category (`bogus`), level 1.5, level -1, no `enabled`/`level` at all, `enabled` as the number 1, a
  stored level out of range (ignored), every category off at startup, and a song that wraps its loop with `LoopWrap`
  queued.
- **The GUI test drives the visible control and checks the same registered command ran.** `EarconsSettingsTest` opens the
  real View menu (`aboutToShow`), finds the `earconsMenu` and the per-category submenu, and calls `trigger()` on the
  *Enabled* and *100%* actions - not the handler and not the registry directly - then asserts the state the registered
  command wrote (`earcons::enabled`, `earcons::level`, `uistore::read("earcons")`), that `control.undo_depth` grows by
  one after each menu trigger (depth0, depth0+1, depth0+2), and that two `control.undo` calls restore first the level
  (Edit still enabled) and then the switch (Edit disabled and not stored), back to depth0. This ran green: `010-ctest-depth.log`,
  exit 0, 1/1 (`009-build-depth.log` exit 0). `theAutomationBoundaryIsHeard` and `anAppliedEditIsHeard` drive
  `QTest::keyClick` and a real `runCommand`, and measure the monitoring output, not an internal flag.
- **Qt 5.15 / MSVC / macOS portability.** Only Qt 5/6-common API (`QMenu`, `QAction`, `QJsonObject`, `QStringList`,
  `QStringLiteral`); no Qt-6-only API and no version guard needed. No `std::filesystem`, no `windows.h` macro-name
  identifiers (`small`, `near`, `far`, `interface`, `min`, `max`) in the added code; a grep of `git diff
  40b4322860..HEAD -- src include` for `std::filesystem`, `windows.h` and Qt-6-only includes found nothing. The audio
  code uses only `<atomic>`, `<array>`, `<cmath>`, `<cstdint>` and `<algorithm>`.

## Note on scope
`aba427a327` (source registries derived from tracked files) and `bd8d6c419b` (argument checks split into helpers) are
repo-hygiene and complexity-target changes on this branch, not earcon behaviour; they keep the fork-source gate and the
complexity gate green.

## Not done
The brief asked for `git merge 050/m0-train-31`. The lane's git guard (bunny-guard, AGENTS.md rules 1/2/11/12) refused it
with **exit 97**, and I did not run a merge. The branch is still **3 commits behind** `050/m0-train-31`, and the
`tools/mcp-zene-control/zene_control/commands_snapshot.json` conflict is unresolved. This needs a merge by an actor
allowed to merge; until then the snapshot and any train-side change to the same files are not reconciled here.
