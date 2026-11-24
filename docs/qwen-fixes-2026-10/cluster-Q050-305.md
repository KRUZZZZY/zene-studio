IMPLEMENTED

# cluster Q050-305 - Verify and synchronize live pin edits

**Unit.** Change audio pins and channel counts during playback without a data race, a stale routing cache or audio on the
wrong channel (`R-mixing-13`, area L09).

**Verdict.** `IMPLEMENTED` through the second acceptance criterion's own alternative: the synchronization is **already in
the product**, and this unit proves it with a new stress + ordering test and records the measured refutation. No product
code changed, because none is needed - `AudioPortsModel`'s write side has taken `AudioEngine`'s model-change guard since
the accepted cluster-23 fix (`e814e57e3e`, on this train), and the read side is either the render period that owns that
guard or the control thread that does the writes.

What was missing, and is what this branch adds: a test that drives the real mutators against a real render period, a
stress run that checks the caches and the routed audio period by period, and a red state that shows what the missing guard
does. The feature record `docs/050-features/Q050-305.md` holds the mutator/reader map with file and line.

## Round 2: what the review asked for, and what changed

The review accepted the test and the existing GUI and agent paths, reproduced the red (exit 8) and green (exit 0) runs,
and asked for four documentation corrections; its own fix for them was dropped for size (`57 product lines exceeds the
limit of 40`), so they are applied here, in this branch, without touching a product file:

| Review point | Change |
| --- | --- |
| "Corrected the GUI command path" | The Pin Connector's click does not write the model directly: `PinConnector::dispatchPin()` (`src/gui/PinConnector.cpp:262`) builds the same `port.set_pins` call an agent makes and runs it through the registry (`runCommand`, `:269`-`:270`), reached from the matrix view's `mousePressEvent` (`:579`). Only a view with no device address falls back to `Matrix::setPin` (`:581`). Corrected in the feature record ("Commands, formats, limits") and Self-check 6. |
| "and reversibility-class descriptions" | The classes were the wrong way round in the record: `port.set_pin` is a **snapshot** (`src/core/ControlReversibilityTableRouting.cpp:180`, an `AudioPortsModel` is a `SerializingObject`, never a `JournallingObject`, so the recorded inverse is `port.set_pin` again with the pin's previous value) and `port.set_pins` is a **true_inverse** (`:191`, an action checkpoint in the engine's `ProjectJournal`). Corrected in the feature record and Self-check 3. |
| "credited the GUI click and undo coverage to WidePinRoutingGuiTest" | The GUI evidence is `WidePinRoutingGuiTest::theChannelsMenuAndAPinClickRunThePinCommands` (`tests/src/gui/WidePinRoutingGuiTest.cpp:130`): it clicks the device card's pins button, checks the <b>Channels</b> entry runs `port.set_pins`, `QTest::mouseClick`s an output cell of the Pin Connector (`:189`) and takes it back with the main window's `undo()` (`:191`). It is now what the record and Self-check 7 name, and it passed this round (`test-5-family.log`, exit 0). |
| "clarified the manual" and "removed the stale TSan script reference" | `data/manual/index.html`'s `pins-during-playback` paragraph now names the commands the click and the menu run and the guard, instead of describing them vaguely. The test header no longer claims evidence from `tests/run-audio-ports-live-edit-tsan.sh` (no such script exists); it points at the feature record's measurement. The report's TSan note now cites `docs/MIXER-CONCURRENCY-FIXES.md` instead of a line number in a script. |

The review also noted that the round-1 `proof.json`, run directory and project `AGENTS.md` were absent where the brief
put them. The round-1 run directory no longer exists on this box (only `Q050-305.r2` does), and there is no `AGENTS.md`
under `projects/zene-studio-program/` or in this worktree (`ls` → `No such file or directory` for both), so this report
follows the rules as the brief prints them. The proof manifest is written to
`/home/kruzzzzy/.config/kads/zene/state/runs/Q050-305.r2/logs/proof.json`, and every log this report names is in that
directory.

## Files

| File | Why |
| --- | --- |
| `tests/src/core/AudioPortsLiveEditStressTest.cpp` | New test (ctest #19): `liveEditsWaitForTheRenderPeriod()` and `pinEditsDuringRenderKeepCachesAndOutputConsistent()`. Round 2 changed one comment block (the stale TSan script reference). |
| `tests/CMakeLists.txt` | Registers `AudioPortsLiveEditStressTest` in `LMMS_TESTS`, with the reason. |
| `tests/fork-sources.txt`, `tests/all-sources.txt` | Register the new fork test source (+ the feature record and this report) and re-derive the entry list. |
| `docs/050-features/Q050-305.md` | Acceptance table, mutator/reader map, TSan measurement, GUI coverage, limits. Round 2: the GUI command path, the two reversibility classes, the GUI credit and the TSan reference corrected. |
| `data/manual/index.html` | `pins-during-playback` paragraph. Round 2: clarified, naming the commands the click and the menu run. |
| `docs/qwen-fixes-2026-10/cluster-Q050-305.md` | This report. |

No product file (`src/`, `include/`, `plugins/`) is touched by any commit of this branch.

## Acceptance

| # | Criterion | Test and command | What it proves | Exit |
| --- | --- | --- | --- | --- |
| 1 | A stress test edits pins and channel counts while a deterministic audio render runs, checking cache and output consistency | `AudioPortsLiveEditStressTest::pinEditsDuringRenderKeepCachesAndOutputConsistent` - `ctest -R '^AudioPortsLiveEditStressTest$' --output-on-failure --timeout 300` from `build/tests` (`test-1-green.log`) | 600 real render periods through `AudioPorts::Router` (`PluginAudioPorts`, a real `AudioBus` pair, a deterministic input and processor), each inside the engine's period guard, while a second thread runs a 15-step pin / batch-pin / track-channel-count script between periods. Every period compares the used-channel caches, the cached track-channel upper bound and the cached direct-routing decision with a recomputation from the pins the router is about to read, and the processor input and the routed track channels with the audio those pins imply. Final state saved and reloaded. This round's direct run: `LIVEEDITS periods=600 edits=686 edits_inside_a_period=0` (`test-2-green-direct.log`), and the same binary passed 25 consecutive runs (`test-3-repeat.log`: `runs=25 failures=0`) | 0 |
| 2 | Run the targeted test under ThreadSanitizer; either prove the candidate race and make it pass after synchronization, or record a measured refutation without an unnecessary code change | `AudioPortsLiveEditStressTest::liveEditsWaitForTheRenderPeriod` (deterministic ordering half) + the TSan measurement below | A render period is opened on the test thread with `requestChangeInModel()` (the lock `renderNextPeriod()` holds) and each mutator is driven on a second thread: it must park on the guard (`modelChangeWaiters() > 0`) and must not complete before the period ends. With the guard removed the edit returns inside the period - exactly the write the render thread would read - and the run fails on that assertion; the stress half of the same run reports the damage (`459 of 600 render periods read a routing state the caches or the audio disagree with`). So the candidate race is real, its remedy is the guard the product already has, and no new product change is justified | 0 (`test-1-green.log`); with the guard removed **8** (`test-4-red.log`) |

Supporting runs, all from `build/tests` and this round:

| Run | Command | Exit |
| --- | --- | --- |
| Port/routing and GUI family | `ctest -R '^(AudioPortsLiveEditStressTest\|AudioPortsModelTest\|AudioPortsTest\|PluginAudioPortsTest\|PinConnectorTest\|WidePinRoutingGuiTest\|AudioBusTest)$' --output-on-failure --timeout 300` (`test-5-family.log`) | 0 - 7/7 passed |
| Agent path and reversibility | `ctest -R '^(ControlPortsCommands\|ControlWidePinRouting\|ControlRoutingCommands\|ReversibilityContractTest)$' --output-on-failure --timeout 600` (`test-6-control.log`) | 0 - 4/4 passed |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py $PWD` (`gates-1.log`) | 0 - 8/8 PASS |

## The red measurement

The synchronization this unit is about is the guard in `takeRoutingEditGuard()` (`src/core/AudioPortsModel.cpp:64`). It
was removed with one line - `return engine->requestChangesGuard();` (`:69`) replaced by an empty guard
(`logs/red-guard-removed.patch`) - and the test rebuilt:

```
$ cmake --build build --target AudioPortsLiveEditStressTest -j4                 # build-2-red.log, EXIT=0
$ ctest -R '^AudioPortsLiveEditStressTest$' --output-on-failure --timeout 300   # test-4-red.log, EXIT=8
FAIL!  : ...::liveEditsWaitForTheRenderPeriod() '!finishedInsideThePeriod' returned FALSE. (Matrix::setPin()
         changed the routing state while the render period was open ...)
FAIL!  : ...::pinEditsDuringRenderKeepCachesAndOutputConsistent() 'inconsistentPeriods == 0' returned FALSE.
         (459 of 600 render periods read a routing state the caches or the audio disagree with; first:
         pin matrices have 8/4 rows for 4 track channels)
0% tests passed, 1 tests failed out of 1
```

Restored (`git checkout -- src/core/AudioPortsModel.cpp`), rebuilt with the family targets (`build-3-green-family.log`,
EXIT 0) and run (`test-1-green.log`, EXIT 0, `100% tests passed out of 1`). In round 1 the same red state also aborted on
heap corruption (`malloc(): unaligned tcache chunk detected`, signal 6) and, in another run, reported a wrong channel
(`period 0: processor input 1 is 0, the pins say 2`). The deterministic failure is the ordering slot; the stress slot is
what names the damage.

## ThreadSanitizer

- **TSan works on this box as a toolchain.** A two-thread probe built with `g++ -fsanitize=thread -g -O1` reported its
  race (`SUMMARY: ThreadSanitizer: data race /tmp/tsan_probe.cpp:2`, exit 66).
- **A TSan build of the product is refused to this lane.** The lane may not configure a build tree:
  `cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DWANT_DEBUG_TSAN=ON -DWANT_QT6=ON -DUSE_WERROR=OFF` →
  `bunny-guard: configuring is the parent's job; the lane build tree is already configured. Use 'cmake --build build
  --target <t>'.`, **exit 97**. The configured tree has `WANT_DEBUG_TSAN:BOOL=OFF`, and the switch is a compile flag, so
  the targeted test cannot be instrumented without a configure. The review reproduced this refusal independently.
- **Neither fallback is available:** `valgrind` is not installed (`valgrind not found`), and the kernel's ASLR knob is not
  readable by this user (`cat /proc/sys/vm/mmap_rnd_bits` → `Permission denied`) - the reason the repo's own TSan runner
  wants `setarch -R` (`docs/MIXER-CONCURRENCY-FIXES.md`, "How to reproduce the sanitizer evidence").

So the race argument rests on the design, on the ordering measurement and on the red state, as the lane brief allows:
every mutator of the routing state takes the period guard (`src/core/AudioPortsModel.cpp:153`, `196`, `227`, `313`, `350`,
`518`, `535`, `541`, `562`, `686`), the render readers run inside the period that owns it, and the control-side readers
(Pin Connector paint, the ports commands) run on the UI thread that also does the writes
(`ControlRegistry::dispatch` → `runOnUiThread`, `src/core/ControlRegistry.cpp:274`, `:219`).

## Self-check

1. **"Each new test FAILS with your product change reverted (reverse the non-test hunks, rebuild, run: exit code?), then
   PASSES restored."** - There is no product hunk: this unit's acceptance is met by the measured refutation, and the
   product already contains the synchronization. The equivalent measurement was made against the synchronization itself:
   removing the guard (`logs/red-guard-removed.patch`, one line at `src/core/AudioPortsModel.cpp:69`), rebuilding
   (`build-2-red.log`, EXIT 0) and running the same test gave **EXIT 8**, failing on `'!finishedInsideThePeriod'` in the
   ordering slot and on `inconsistentPeriods == 0` (459 of 600 periods) in the stress slot. Restored (`git checkout`),
   rebuilt (`build-3-green-family.log`, EXIT 0), run → **EXIT 0** (`test-1-green.log`), 25 consecutive green runs
   (`test-3-repeat.log`). The deterministic red is what makes the test non-vacuous: without the guard the edit completes
   inside the period.
2. **"Every acceptance criterion is proven by a test you ran yourself (name the test and its exit code per
   criterion)."** - Criterion 1: `AudioPortsLiveEditStressTest::pinEditsDuringRenderKeepCachesAndOutputConsistent`,
   `ctest -R '^AudioPortsLiveEditStressTest$'` → exit 0, `LIVEEDITS periods=600 edits=686 edits_inside_a_period=0`
   (`test-2-green-direct.log`). Criterion 2: `AudioPortsLiveEditStressTest::liveEditsWaitForTheRenderPeriod`, same ctest
   run → exit 0, and the red run → exit 8 (`test-4-red.log`); the TSan attempt and its refusal are in the section above.
3. **"Each edit is ONE undo step, and every new or changed command has an honest reversibility class."** - No command was
   added or changed and no undo path was touched, so the classes are the table's and they are honest for this path:
   a pin click runs **`port.set_pins`** (one pin in its list) through the registry, which is a **true_inverse** - an action
   checkpoint in the engine's `ProjectJournal`, restored by Edit > Undo and by `control.undo` as one routing edit
   (`src/core/ControlReversibilityTableRouting.cpp:191`); the single-pin agent verb **`port.set_pin`** is a **snapshot** -
   an `AudioPortsModel` is a `SerializingObject`, never a `JournallingObject`, so the step records the pin's previous value
   and `control.undo` replays `port.set_pin` with it (`applies: command`, `:180`). Neither row changed, so no A16 histogram
   line and no commands snapshot changes. `ReversibilityContractTest` passes (ctest #240, exit 0 in `test-6-control.log`),
   and `WidePinRoutingGuiTest` takes a real click back with one `undo()` (exit 0 in `test-5-family.log`).
4. **"Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a signal."** - Every
   commit of this branch adds a test, three manifests, a feature record, a report and a manual paragraph; no product code
   (`git diff --stat 21c2f3ee2e..HEAD`: `data/manual/index.html`, `docs/050-features/Q050-305.md`,
   `docs/qwen-fixes-2026-10/cluster-Q050-305.md`, `tests/CMakeLists.txt`, `tests/all-sources.txt`,
   `tests/fork-sources.txt`, `tests/src/core/AudioPortsLiveEditStressTest.cpp`). The test's render half only calls what the
   mixer and the router call in production, under the same guard, on a test thread - it is not the audio thread. No
   realtime path changed at all. (The pre-submit check's FLAGGED lines name `plugins/LadspaEffect/LadspaEffect.cpp` and its
   GUI/command items name other files: those come from the train history between `FIX-BASE` and this branch's cut point,
   not from this unit's commits - the diffstat above is the whole of what these commits change.)
5. **"Old projects still load and render exactly as before; new data round-trips through save and load."** - No product
   file changed, so the load and render paths are byte-identical to the train. The test additionally saves the edited
   device and loads it into a fresh one and requires the same pins up to the same bound
   (`reloadedState.upperBound == finalState.upperBound` and row-by-row pin equality), which is the existing project format
   (the `track_channels` attribute pins past the pair already use).
6. **"Completeness: walk each acceptance criterion through the REAL user path and the agent path and say which edge case
   you tried."** - User path: a click on a Pin Connector cell → `PinConnector::MatrixView::mousePressEvent`
   (`src/gui/PinConnector.cpp:579`) → `PinConnector::dispatchPin()` (`:262`) → the registered **`port.set_pins`** command
   through `runCommand` (`:269`-`:270`), the same command an agent calls; the device card's pins button and its
   <b>Channels</b> menu entry go through the same command. Exercised by `WidePinRoutingGuiTest`
   (`theChannelsMenuAndAPinClickRunThePinCommands`, ctest #737, exit 0) and `PinConnectorTest` (ctest #580, exit 0). Agent
   path: `port.set_pin` / `port.set_pins` → `Matrix::setPin` / `AudioPortsModel::setPins`
   (`src/core/ControlCommandsPorts.cpp:306`, `:383`, `:416`), exercised over the control socket by `ControlPortsCommands`
   and `ControlWidePinRouting` (both exit 0 in `test-6-control.log`). Edge cases in the stress script: a pin off, a batch
   pin, growing track channels 2 -> 8 and back, pins on track channels past the bus's single pair, a pin edit that turns
   the cached direct routing off and a batch that turns it back on, a no-op `setChannelCounts(2, 2)` (still takes the
   guard), and the final save/load. Not covered: a bus with more than one pair (see the limits in the feature record).
7. **"The GUI test drives the visible control (click/trigger/type) and checks that the SAME registered command ran (undo
   depth +1)."** - That is `WidePinRoutingGuiTest::theChannelsMenuAndAPinClickRunThePinCommands`
   (`tests/src/gui/WidePinRoutingGuiTest.cpp:130`, ctest #737, exit 0 in `test-5-family.log`): it opens the real device
   card's pins button with `QTest::mouseClick`, asserts the <b>Channels 41-42</b> entry carries
   `controlCommand == "port.set_pins"`, then `QTest::mouseClick`s an output pin cell of the Pin Connector (`:189`) and calls
   the main window's own `undo()` (`:191`), which is the one undo step the click recorded - the same registered command the
   agent path uses. No GUI file changed in this unit and no user-facing action was added; nothing here replaces or weakens
   those tests.
8. **"Qt 5.15 / MSVC / macOS < 10.15 portability: no Qt-6-only API without a guard, no windows.h macro names, no
   std::filesystem outside _WIN32."** - The new test uses `QtTest`, `QDomDocument`, `QScopeGuard` (Qt >= 5.12),
   `std::thread`, `std::atomic`, `std::bitset`, `std::optional` and `std::chrono::steady_clock`; no Qt 6 only API, no
   `std::filesystem`, no POSIX call, no absolute-path test. No identifier named `small`, `near`, `far`, `interface`,
   `min` or `max`. It links only core (`lmmsobjs`), so no plug-in module had to be added to `add_dependencies`. It was
   compiled and run here with Qt 6.12, gcc 16.

## Limits and what a reviewer should know

- The measured refutation is a design argument plus a deterministic ordering observation, not a sanitizer report: TSan
  cannot be built in a lane tree (exit 97 above). A TSan run of the same slot, on a tree configured with
  `WANT_DEBUG_TSAN=ON`, is the one experiment this branch could not perform.
- The stress half's in-period consistency check is a strong invariant, but how loudly it detects an *unsynchronized* edit
  varies with where the scheduler puts the edit (459 of 600 periods in this round's red run, 259 of 600 in round 1's, and
  zero in a run that aborted on heap corruption first); the deterministic failure is the ordering slot. That split is
  deliberate and is stated in the test's header.
- The stress half renders one stereo bus pair. Pins past the pair are edited and are checked through the caches and the
  save/load, not through audio.
