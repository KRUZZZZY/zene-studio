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

## Files

| File | Why |
| --- | --- |
| `tests/src/core/AudioPortsLiveEditStressTest.cpp` | New test (ctest #19): `liveEditsWaitForTheRenderPeriod()` and `pinEditsDuringRenderKeepCachesAndOutputConsistent()`. |
| `tests/CMakeLists.txt` | Registers `AudioPortsLiveEditStressTest` in `LMMS_TESTS`, with the reason. |
| `tests/fork-sources.txt`, `tests/all-sources.txt` | Register the new fork test source (+ the feature record and this report) and re-derive the entry list. |
| `docs/050-features/Q050-305.md` | Acceptance table, mutator/reader map, TSan measurement, limits. |
| `data/manual/index.html` | New `pins-during-playback` paragraph: a pin edit lands between two audio periods. |
| `docs/qwen-fixes-2026-10/cluster-Q050-305.md` | This report. |

No product file (`src/`, `include/`, `plugins/`) is touched.

## Acceptance

| # | Criterion | Test and command | What it proves | Exit |
| --- | --- | --- | --- | --- |
| 1 | A stress test edits pins and channel counts while a deterministic audio render runs, checking cache and output consistency | `AudioPortsLiveEditStressTest::pinEditsDuringRenderKeepCachesAndOutputConsistent` - `ctest -R '^AudioPortsLiveEditStressTest$' --output-on-failure --timeout 300` from `build/tests` (`test-10-green.log`) | 600 real render periods through `AudioPorts::Router` (`PluginAudioPorts`, a real `AudioBus` pair, a deterministic input and processor), each inside the engine's period guard, while a second thread runs a 15-step pin / batch-pin / track-channel-count script between periods. Every period compares the used-channel caches, the cached track-channel upper bound and the cached direct-routing decision with a recomputation from the pins the router is about to read, and the processor input and the routed track channels with the audio those pins imply. Final state saved and reloaded. Green run: `LIVEEDITS periods=600 edits=838 edits_inside_a_period=0`; the same binary passed 25 consecutive runs | 0 for the slot file; **25/25 runs 0** (`runs=25 failures=0`) |
| 2 | Run the targeted test under ThreadSanitizer; either prove the candidate race and make it pass after synchronization, or record a measured refutation without an unnecessary code change | `AudioPortsLiveEditStressTest::liveEditsWaitForTheRenderPeriod` (deterministic ordering half) + the TSan measurement below | A render period is opened on the test thread with `requestChangeInModel()` (the lock `renderNextPeriod()` holds) and each mutator is driven on a second thread: it must park on the guard (`modelChangeWaiters() > 0`) and must not complete before the period ends. With the guard removed the edit returns inside the period - exactly the write the render thread would read - and the run fails on that assertion; the stress half of the same run aborts on heap corruption. So the candidate race is real, its remedy is the guard the product already has, and no new product change is justified | 0 (`test-10-green.log`); with the guard removed **8** (`test-9-red.log`) |

Supporting runs, all from `build/tests`:

| Run | Command | Exit |
| --- | --- | --- |
| Port/routing family | `ctest -R '^(AudioPortsLiveEditStressTest\|AudioPortsModelTest\|AudioPortsTest\|PluginAudioPortsTest\|PinConnectorTest\|AudioBusTest)$' --output-on-failure --timeout 300` (`test-12-family.log`) | 0 - 6/6 passed |
| Agent path (control socket transcripts) | `ctest -R '^(ControlPortsCommands\|ControlWidePinRouting\|ControlRoutingCommands)$' --output-on-failure --timeout 600` (`test-14-control.log`) after building the plug-in modules they load (`build-14-plugins.log`, exit 0) | 0 - 3/3 passed (the earlier `test-13-control.log` failed only because that tree had no loadable built-in effect) |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py $PWD` (`gates-1.log`) | 0 - 8/8 PASS |

## The red measurement

The synchronization this unit is about is the guard in `takeRoutingEditGuard()` (`src/core/AudioPortsModel.cpp:64`). It
was removed with one line - `return engine->requestChangesGuard();` (`:69`) replaced by an empty guard
(`logs/red-guard-removed.patch`) - and the test rebuilt:

```
$ cmake --build build --target AudioPortsLiveEditStressTest -j4      # build-11-red.log, EXIT=0
$ ctest -R '^AudioPortsLiveEditStressTest$' --output-on-failure --timeout 300   # test-9-red.log, EXIT=8
FAIL!  : AudioPortsLiveEditStressTest::liveEditsWaitForTheRenderPeriod() '!finishedInsideThePeriod'
         returned FALSE. (Matrix::setPin() changed the routing state while the render period was open ...)
malloc(): unaligned tcache chunk detected
Received signal 6 (SIGABRT)
0% tests passed, 1 tests failed out of 1
```

Restored (`git checkout -- src/core/AudioPortsModel.cpp`), rebuilt (`build-12-green.log`, EXIT=0) and run
(`test-10-green.log`, EXIT=0, `100% tests passed out of 1`). An earlier red run of the same binary also reported the
stress half directly: `259 of 600 render periods read a routing state the caches or the audio disagree with; first: period
0: processor input 1 is 0, the pins say 2` (`test-7-red.log`) - a wrong channel, not merely a stale value.

## ThreadSanitizer

- **TSan works on this box as a toolchain.** A two-thread probe built with `g++ -fsanitize=thread -g -O1` reported its
  race (`SUMMARY: ThreadSanitizer: data race /tmp/tsan_probe.cpp:2`, exit 66).
- **A TSan build of the product is refused to this lane.** The lane may not configure a build tree:
  `cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DWANT_DEBUG_TSAN=ON -DWANT_QT6=ON -DUSE_WERROR=OFF` →
  `bunny-guard: configuring is the parent's job; the lane build tree is already configured. Use 'cmake --build build
  --target <t>'.`, **exit 97** (`configure-tsan-2.log`). The configured tree has `WANT_DEBUG_TSAN:BOOL=OFF`, and the
  switch is a compile flag, so the targeted test cannot be instrumented without a configure.
- **Neither fallback is available:** `valgrind` is not installed (`valgrind not found`), and the kernel's ASLR knob the
  repo's own TSan runner documents (`tests/run-mixer-concurrency-tsan.sh:14`) is unreadable here
  (`cat /proc/sys/vm/mmap_rnd_bits` → `Permission denied`).

So the race argument rests on the design, on the ordering measurement and on the red state, as the lane brief allows:
every mutator of the routing state takes the period guard (`src/core/AudioPortsModel.cpp:153`, `196`, `227`, `313`, `350`,
`518`, `535`, `541`, `562`, `686`), the render readers run inside the period that owns it, and the control-side readers
(Pin Connector paint, the ports commands) run on the UI thread that also does the writes
(`ControlRegistry::dispatch` → `runOnUiThread`, `src/core/ControlRegistry.cpp:274`, `:219`).

## Self-check

1. **"Each new test FAILS with your product change reverted (reverse the non-test hunks, rebuild, run: exit code?), then
   PASSES restored."** - There is no product hunk: this unit's acceptance is met by the measured refutation, and the
   product already contains the synchronization. The equivalent measurement was made against the synchronization
   itself: removing the guard (`logs/red-guard-removed.patch`, one line at `src/core/AudioPortsModel.cpp:69`), rebuilding
   (`build-11-red.log`, EXIT 0) and running the same test gave **EXIT 8**, failing deterministically on
   `'!finishedInsideThePeriod'` (and aborting the stress half on heap corruption). Restored (`git checkout`), rebuilt
   (`build-12-green.log`, EXIT 0), run → **EXIT 0** (`100% tests passed out of 1`), 25 consecutive green runs. The
   deterministic red is what makes the test non-vacuous: without the guard the edit completes inside the period.
2. **"Every acceptance criterion is proven by a test you ran yourself (name the test and its exit code per
   criterion)."** - Criterion 1:
   `AudioPortsLiveEditStressTest::pinEditsDuringRenderKeepCachesAndOutputConsistent`, `ctest -R
   '^AudioPortsLiveEditStressTest$'` → exit 0, `LIVEEDITS periods=600 edits=838 edits_inside_a_period=0`. Criterion 2:
   `AudioPortsLiveEditStressTest::liveEditsWaitForTheRenderPeriod`, same ctest run → exit 0, and the red run → exit 8;
   the TSan attempt and its refusal are in the section above.
3. **"Each edit is ONE undo step, and every new or changed command has an honest reversibility class."** - No command was
   added or changed and no undo path was touched: `port.set_pin` still journals its single inverse from the value it read
   before the write (`src/core/ControlCommandsPorts.cpp:302`-`:307`), `port.set_pins` is one `snapshot` step, and the view's
   own `Matrix::setPin` journals nothing. `ControlReversibilityTableRouting.cpp` is unchanged, so no A16 histogram row
   moves and the commands snapshot does not need regenerating (no command metadata changed). The Pin Connector test
   `PinConnectorTest` (ctest #580, exit 0 in `test-12-family.log`) still drives the click and its undo.
4. **"Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a signal."** - This
   branch's three commits touch seven files and add 952 lines, all of them a test, three manifests, a feature record, a
   report and a manual paragraph (`git diff --stat 21c2f3ee2e..HEAD`: `data/manual/index.html`, `docs/...`,
   `tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/fork-sources.txt`,
   `tests/src/core/AudioPortsLiveEditStressTest.cpp`); no product code. The test's render half only calls what the mixer and
   the router call in production, under the same guard, on a test thread - it is not the audio thread. No realtime path
   changed at all. (The pre-submit check's FLAGGED lines name `plugins/LadspaEffect/LadspaEffect.cpp` and the GUI/command
   items name other files: those come from the train history between `FIX-BASE` and this branch's cut point, not from this
   unit's commits - the diffstat above is the whole of what these commits change.)
5. **"Old projects still load and render exactly as before; new data round-trips through save and load."** - No product
   file changed, so the load and render paths are byte-identical to the train. The test additionally saves the edited
   device and loads it into a fresh one and requires the same pins up to the same bound
   (`reloadedState.upperBound == finalState.upperBound` and row-by-row pin equality), which is the existing project format
   (the `track_channels` attribute pins past the pair already use).
6. **"Completeness: walk each acceptance criterion through the REAL user path and the agent path and say which edge case
   you tried."** - User path: Pin Connector click → `PinConnector::mousePressEvent` → `Matrix::setPin`
   (`src/gui/PinConnector.cpp:581`, or `port.set_pin` through `dispatchPin`), exercised by `PinConnectorTest` (exit 0).
   Agent path: `port.set_pin` / `port.set_pins` → `Matrix::setPin` / `AudioPortsModel::setPins`
   (`src/core/ControlCommandsPorts.cpp:306`, `:383`, `:416`), exercised over the control socket by
   `ControlPortsCommands` and `ControlWidePinRouting` (both exit 0). Edge cases in the stress script: a pin off, a batch
   pin, growing track channels 2 -> 8 and back, pins on track channels past the bus's single pair, a pin edit that turns
   the cached direct routing off and a batch that turns it back on, a no-op `setChannelCounts(2, 2)` (still takes the
   guard), and the final save/load. Not covered: a bus with more than one pair (see the limits in the feature record).
7. **"The GUI test drives the visible control (click/trigger/type) and checks that the SAME registered command ran (undo
   depth +1)."** - No GUI file changed in this unit, and no user-facing action was added: the click and the commands
   already existed and both reach the code this unit verifies. The GUI half is the existing
   `PinConnectorTest::pinEditsRunUnderTheEngineModelChangeGuard` (ctest #580, exit 0) together with the pin-click tests in
   the same binary, which click the real widget; nothing here replaces or weakens them.
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
- The stress half's in-period consistency check is a strong invariant but its detection of an *unsynchronized* edit is
  probabilistic (259 of 600 periods in one red run, zero in another because the run aborted earlier); the deterministic
  failure is the ordering slot. That split is deliberate and is stated in the test's header.
- The stress half renders one stereo bus pair. Pins past the pair are edited and are checked through the caches and the
  save/load, not through audio.
