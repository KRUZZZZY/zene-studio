FALSE-POSITIVE

# cluster new-p19-5 — BUG 15: "Rack selection updates may race with audio-thread routing"

**Verdict: FALSE-POSITIVE.** The finding read the call site
`src/core/ControlCommandsRack.cpp:368` and concluded "no synchronization is visible in
this call". The synchronization is not at the call site; it is inside the function the
call site invokes, `Rack::setSelectedChain()` (`src/core/Rack.cpp:215-224`), which takes
the audio engine's model-change guard — the lock `AudioEngine::renderNextPeriod()` holds
for a whole render period. The rack's selector has been guarded this way since the feature
landed (`git log -S requestChangeInModel -- src/core/Rack.cpp` names the feature commit
`af27e193f3 feat(racks): the mixer channel's rack - parallel chains and a chain selector`
as the only commit that introduced the call), so this was never a defect on this branch.

## The claim and what the code does

| step | file:line | what happens |
| --- | --- | --- |
| the handler the finding names | `src/core/ControlCommandsRack.cpp:368` | `rack.setSelectedChain(index);` |
| … which is the guarded function | `src/core/Rack.cpp:215` | `void Rack::setSelectedChain(int index)` |
| … taking the guard | `src/core/Rack.cpp:217-218` | `auto* engine = Engine::audioEngine(); if (engine != nullptr) { engine->requestChangeInModel(); }` |
| … then writing the state | `src/core/Rack.cpp:220-221` | `m_selectedChain = index; rebuildRoutingGraph();` |
| … then dropping it | `src/core/Rack.cpp:223` | `if (engine != nullptr) { engine->doneChangeInModel(); }` |
| what `requestChangeInModel()` is | `src/core/AudioEngine.cpp:1991` | `m_changeMutex.lock();` (`doneChangeInModel()` is `m_changeMutex.unlock()`, line 2004) |
| what the mutex is | `include/AudioEngine.h:994` | `std::recursive_mutex m_changeMutex;` |
| the audio thread that owns it | `src/core/AudioEngine.cpp:1224` | `const auto lock = std::lock_guard{m_changeMutex};` — first statement of `renderNextPeriod()`, held to its end |
| the rack read under that lock | `src/core/Mixer.cpp:782-787` | `if (m_rack.canProcessThroughRack(m_bus)) { … m_rack.processAudioBuffer(…) }` |
| the intended contract | `docs/RACKS.md:103-105` | "The node set, the planar buffers and every chain's `AudioBus` are built in `rebuildRoutingGraph()`, called from `addChain()`, `removeChain()`, `setSelectedChain()`, `clear()` and `loadSettings()` — all control-thread, all inside the audio engine's model-change guard except `loadSettings()` …" |

So a selection change issued while a render period is running parks in
`m_changeMutex.lock()` until `renderNextPeriod()` releases the period, and the write to
`m_selectedChain` / `m_routedChains` / the graph lands at a block boundary. That is exactly
the "audio-safe routing snapshot or lock-free handoff … that takes effect at a block
boundary" the finding asked for, implemented as this tree's standard model-change handoff.
`requestChangeInModel()` short-circuits only on the rendering thread itself
(`static thread_local bool s_renderingThread`, `src/core/AudioEngine.cpp:471`), so it
cannot deadlock the audio thread.

## Every sibling of the changed state was checked

`m_selectedChain` is written in exactly these places, and each one is inside the guard:

| caller | file:line | guard |
| --- | --- | --- |
| the command's handler | `src/core/ControlCommandsRack.cpp:368` | `Rack::setSelectedChain` (Rack.cpp:218) |
| the undo step's inverse and the redo | `src/core/ControlCommandsRack.cpp:110`, `:114` | same function |
| the rack-wrap command | `src/core/ControlCommandsRackWrap.cpp:95`, `:102`, `:106` | same function |
| device moves | `src/core/ControlDeviceMoves.cpp:178` | same function |
| `removeChain()` sliding the selector down | `src/core/Rack.cpp:203-204` | `requestChangeInModel()` at Rack.cpp:194 |
| `clear()` | `src/core/Rack.cpp:246` | `requestChangeInModel()` at Rack.cpp:236 |
| `loadSettings()` | `src/core/Rack.cpp` (`RACK_ELEMENT` read) | **not guarded** — the one documented exception, `docs/RACKS.md:104-105`; project load, not a live edit. Reported under Other findings, not changed. |

## The pin added (no product change)

`tests/src/core/RackTest.cpp` — new slot `aSelectionChangeWaitsForTheRenderPeriod()`.
No product file changed: `git diff --stat -- src/core/` prints nothing, and
`git status --short` lists only `tests/src/core/RackTest.cpp` and this report.

The test stands a render period in with a thread that takes the same guard
(`requestChangeInModel()`), then asks for a selection change from a second thread, and
decides on an **ordering of events**: the change either parks at the guard — observed
through the engine's own seam `AudioEngine::modelChangeWaiters()`
(`include/AudioEngine.h:671`), which counts control threads parked inside
`requestChangeInModel()` — or it completes, in which case the test fails. While it is
parked the test asserts the audio thread's view is untouched (`rack.selectedChain()` is
still `Rack::Parallel`, `rack.routedChains()` is still `{0, 1}`); after the period ends it
asserts both took the new value. No sleeps, no wall-clock assertion: the 10 s deadline only
turns a hang into a failure message, and both threads are joined through `qScopeGuard` on
every exit path, including a failed `QVERIFY`.

On the unmodified product code this passes — which is the FALSE-POSITIVE proof, not a
regression, so there is no RED on unmodified code.

## Teeth proof

To show the assertion has teeth — that it fails exactly when the handoff the finding
assumed is missing — the guard was temporarily removed *from* `Rack::setSelectedChain()`
(the two `requestChangeInModel()` / `doneChangeInModel()` lines and the `engine` local),
the target rebuilt, and the test rerun. It failed:

```
FAIL!  : RackTest::aSelectionChangeWaitsForTheRenderPeriod()
         'selectorStage.load(std::memory_order_acquire) != 2' returned FALSE.
         (the selection change went through while a render period owned the model:
          Rack::setSelectedChain() no longer takes the model-change guard)
   Loc: [../../tests/src/core/RackTest.cpp(796)]
Totals: 13 passed, 1 failed, 0 skipped, 0 blacklisted, 504ms
```

`sleep`-free and wall-clock-free — the run exited 1. The sabotage was reverted by copying
the saved file back over `src/core/Rack.cpp`, and `git diff --stat -- src/core/` is empty
afterwards. The committed tree therefore contains no product change.

## Test matrix

| path | covered by | where |
| --- | --- | --- |
| normal: a selection change with the model free | `theSelectorRoutesOneChainAndIdlesTheOther` | `tests/src/core/RackTest.cpp:392-396` |
| the finding's failure path: change issued while a period owns the model | `aSelectionChangeWaitsForTheRenderPeriod` (**new**) | `tests/src/core/RackTest.cpp:736-813` |
| edge: `Rack::Parallel`, then chain 0, then `Parallel` again | `theSelectorRoutesOneChainAndIdlesTheOther` | `tests/src/core/RackTest.cpp:437`, `:454` |
| rejection: a selector that names no chain leaves the rack un-wired | `aSelectorThatNamesNoChainLoadsAsBefore`, `aMalformedSelectorDoesNotBecomeChainZero`, `anOverflowingSelectorDoesNotBecomeChainZero` | `tests/src/core/RackTest.cpp:687`, `:697`, `:708` |
| reconfiguration: save / reload of the selector | `rackConfigurationSurvivesSaveAndReload`, `anOldProjectWithNoRackStillLoads`, and the save/reload half of `checkCorruptSelectorLoadsUnwired` | `tests/src/core/RackTest.cpp:482`, `:593`, `:946` |
| audio-thread constraints | no product code touched; `canProcessThroughRack()` / `processAudioBuffer()` unchanged, so `processingThroughTheRackAllocatesNothing` is unaffected | `tests/src/core/RackTest.cpp:460` |
| platform: the test uses `<thread>`, `<future>`, `<atomic>`, `<chrono>` and `qScopeGuard` — the same set `tests/src/core/ScriptDawModelRegistrationTest.cpp:29-35` uses, which polls with `std::this_thread::yield()` rather than `std::atomic::wait` (macOS 11; the AudioEngine floor is 10.13) | `aSelectionChangeWaitsForTheRenderPeriod` | `tests/src/core/RackTest.cpp:736` |

Prerequisites confirmed to exist before editing: `Rack` with `addChain` / `clear` /
`setSelectedChain` / `selectedChain` / `routedChains`, `AudioEngine::modelChangeWaiters()`,
`AudioEngine::requestChangeInModel()` / `doneChangeInModel()`, and the `RackTest` target
(`tests/CMakeLists.txt:715` → executable `RackTest`, no CMake change needed).

## Commands and EXIT codes (all observed, `L` = the log folder)

| # | Command | EXIT |
| --- | --- | --- |
| 1 | `cmake --build build --target RackTest -j4 > $L/build-1.log 2>&1` | 0 |
| 2 | `cd build/tests && ctest -R RackTest --output-on-failure > $L/test-1.log 2>&1` | 8 — `RackTest Passed`, `ZeneRackTest` "Not Run" (that binary is not built in this tree) |
| 3 | `cd build/tests && ctest -R '^RackTest$' --output-on-failure > $L/test-2.log 2>&1` | 0 (1/1) |
| 4 | `QT_QPA_PLATFORM=offscreen ./build/tests/RackTest > $L/test-3-direct.log 2>&1` | 0 — `PASS : RackTest::aSelectionChangeWaitsForTheRenderPeriod()`, `Totals: 14 passed, 0 failed, 0 skipped` |
| 5 | guard removed from `Rack::setSelectedChain()`; `cmake --build build --target RackTest -j4 > $L/build-2-sabotage.log 2>&1` | 0 |
| 6 | `QT_QPA_PLATFORM=offscreen ./build/tests/RackTest > $L/test-4-sabotage.log 2>&1` | **1** — the `FAIL!` quoted above, `Loc: ../../tests/src/core/RackTest.cpp(796)` |
| 7 | `cp $L/Rack.cpp.orig src/core/Rack.cpp`; `git diff --stat -- src/core/` | 0 — prints nothing (no product change) |
| 8 | `cmake --build build --target RackTest -j4 > $L/build-3-restored.log 2>&1` | 0 |
| 9 | `cd build/tests && ctest -R '^RackTest$' --output-on-failure > $L/test-5-restored-ctest.log 2>&1` | 0 (1/1) |
| 10 | `cmake --build build --target RackMacrosTest RackZonesTest RackZoneRoutingTest RackZoneFeedRunTest -j4 > $L/build-4-rack-family.log 2>&1` | 0 |
| 11 | `cd build/tests && ctest -R '^(RackTest\|RackMacrosTest\|RackZonesTest\|RackZoneRoutingTest\|RackZoneFeedRunTest)$' --output-on-failure > $L/test-7-rack-family-built.log 2>&1` | 0 (5/5 passed) |
| 12 | `cd build/tests && ctest -R Rack -E ZeneRackTest --output-on-failure > $L/test-6-rack-family.log 2>&1` | 8 — the un-built `*Rack*` binaries report "Not Run", and `ControlDrumRackPads` / `ControlDrumRackChains` fail with `build lists no loadable audiofileprocessor module`; see Other findings |
| 13 | `python3 …/feature_check.py <worktree> > $L/gate-1-feature-check.log 2>&1` | 0 — `RESULT: PASS`, 8/8 PASS (`fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity` "no new item in your files", `file-length` "no new item in your files") |
| 14 | `python3 …/submit_check.py <worktree> new-p19-5 > $L/check-1-submit.log 2>&1` (before the commit) | 1 — the expected "nothing committed / report missing" rows |
| 15 | `python3 …/submit_check.py <worktree> new-p19-5 > $L/check-2-submit.log 2>&1` (after the commit) | see below |

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p19-5.r1/logs`.

## Self-check

**? The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
There is no product change in this commit — the verdict is FALSE-POSITIVE. The guard the
finding assumed missing is the product behaviour, so the proof reverts *it*: with
`requestChangeInModel()`/`doneChangeInModel()` removed from `Rack::setSelectedChain()`
(row 5-6 above) the test exits **1**; with the file restored (row 7-9) it exits **0**
(`Totals: 14 passed, 0 failed`). Same test, same binary target, only the guard differs.

**? The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).**
The seam is `AudioEngine::modelChangeWaiters()` (`include/AudioEngine.h:671`), the engine's
own counter of control threads parked inside `requestChangeInModel()`; it is incremented
around `m_changeMutex.lock()` (`src/core/AudioEngine.cpp:1990-1992`), so "the change reached
the guard" is an event, not a duration. The other half of the decision is
`selectorStage`, an injected stage counter that the selection thread publishes with
release/acquire. There is no `sleep`, no `QTest::qWait` and no assertion on elapsed time;
the 10 s deadline only converts a hang into a failure message, and the two threads are
joined through `qScopeGuard` on every exit path. Both possible interleavings are decided
the same way: parked at the guard → pass, completed → fail.

**? The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.**
Nothing was changed in the product. The state the finding worried about was traced through
every writer of `m_selectedChain` — the table "Every sibling of the changed state was
checked" above: the handler (`ControlCommandsRack.cpp:368`), its undo/redo step (`:110`,
`:114`), the rack-wrap command (`ControlCommandsRackWrap.cpp:95`, `:102`, `:106`), device
moves (`ControlDeviceMoves.cpp:178`), `removeChain()` (`Rack.cpp:203-204` under the guard at
`:194`) and `clear()` (`Rack.cpp:246` under the guard at `:236`). All but `loadSettings()`
take the guard; `loadSettings()`'s exception is pre-existing and documented
(`docs/RACKS.md:103-105`).

**? No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.**
No product code, no persistence code and no schema changed — only a new test slot in
`tests/src/core/RackTest.cpp`, which registers in no saved format. The full `RackTest` suite
including the save/reload selector slots passes (rows 3, 4, 9), and the four sibling rack
suites pass (row 11). No limit or check was added to any handler.

**? If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.**
`src/core/Rack.cpp:215-224` — `Rack::setSelectedChain()` brackets the write
(`m_selectedChain = index; rebuildRoutingGraph();`) with
`Engine::audioEngine()->requestChangeInModel()` / `doneChangeInModel()`, which are
`m_changeMutex.lock()` / `.unlock()` (`src/core/AudioEngine.cpp:1991`, `:2004`) on the
`std::recursive_mutex` at `include/AudioEngine.h:994` that `renderNextPeriod()`
(`src/core/AudioEngine.cpp:1224`) holds for a whole period — the same period in which
`MixerChannel::processInserts()` reads the rack (`src/core/Mixer.cpp:782-787`). The
contract is stated at `docs/RACKS.md:103-105`. The pin above measures exactly that
ordering.

## Other findings

1. `Rack::loadSettings()` is the one rack mutator that does **not** take the model-change
   guard (`docs/RACKS.md:103-105` records it as inheriting `EffectChain::loadSettings()`'s
   hazard). Out of scope of this finding, which names the `rack.set_selected` handler; left
   unchanged.
2. `ControlDrumRackPads` and `ControlDrumRackChains` fail in this build tree, before and
   independently of this change, with `FAIL: build lists no loadable audiofileprocessor
   module` — the app they launch answers `plugin.list` with `{"count":0,…,"total":10}`.
   Environment/build coverage, not a rack defect; log `test-6-rack-family.log`.
3. `ctest -R Rack` also matches `ZeneRackTest`, whose binary is absent because
   `plugins/ZeneRack` is not configured in this build tree; it reports "Not Run" and makes
   the unanchored `-R Rack` run exit 8. Use `-R '^RackTest$'` for this suite.
