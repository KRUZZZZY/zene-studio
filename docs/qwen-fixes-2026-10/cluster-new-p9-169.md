FIXED

Cluster `new-p9-169`: "Re-enabling an insert can replay stale dry audio" (`src/core/HardwareInsert.cpp`).
Working tree: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-169`,
branch `bugs/qwen-new-p9-169`, cut from FIX-BASE `050/m0-train-35`.

## Verdict: REAL

The dry delay line of a hardware insert kept its history across a disable, so switching the insert back on
emitted audio buffered before the disable. Evidence read in this tree (the finding's line numbers are the
historical ones from `ec338df4`; `src/core/HardwareInsert.cpp` is 429 lines here):

- `src/core/HardwareInsert.cpp:227` - `process()` returns immediately while the insert is disabled
  (`if (!m_settings.enabled || m_send.size() < ...) { return; }`), so nothing advances `m_dry` during a bypass.
- `src/core/HardwareIo.cpp:242` - the mixer's per-period entry point (`hardwareio::processInsert`) returns early
  as well when `!insert.enabled()`, so every caller agrees: a disabled insert is not processed at all.
- `src/core/HardwareInsert.cpp:121` - `prepare()` returns as soon as the buffers are large enough
  (`if (m_send.size() >= size) { return; }`), so the `m_dry.init(...)` at `:127` does not run again when the
  insert is re-enabled at the same block size.
- `src/core/HardwareInsert.cpp:378-379` - `writeOutput()` restores the delay from `latencyFrames()` on every
  processed period (`m_dry.setDelayFrames(latencyFrames()); m_dry.process(buffer, frames);`). On the first
  period after a re-enable the line therefore reads one block behind a write cursor that never moved while the
  insert was off: the block written just before the disable.
- Contract: `HardwareInsert::process()` is documented as writing "the delayed dry signal" of *this* period
  (`include/HardwareInsert.h:122-124`). Emitting a block from before the bypass is not that.

Reproduced before any change (log `test-1-red.log`, see RED below): with `mix` 0 and a one-period latency,
the first period after a re-enable emitted `0.25`, the level played *before* the disable, instead of silence.

## Fix

The line loses its history on the two transitions that stop and restart it, at the one funnel every caller
already goes through (`HardwareInsert::setSettings`, interface thread, under the engine change lock, no
render in flight):

- `include/LatencyCompensation.h:86-90` - new `void reset();` declaration (declaration and definition in the
  same commit).
- `src/core/LatencyCompensation.cpp:207-215` - `reset()`: `std::fill(m_ring.begin(), m_ring.end(), SampleFrame{})`,
  `m_write = 0`, `m_delay.store(0)`. Storage keeps the size `init()` gave it: no allocation, no growth, no lock.
- `src/core/HardwareInsert.cpp:156-160` - `if (settings.enabled != m_settings.enabled) { m_dry.reset(); }`,
  i.e. on both the enabled->disabled and the disabled->enabled transition. `prepare()`'s growth re-init is
  untouched.

Realtime: nothing new runs on the audio thread. `reset()` is only reached from `setSettings`, which the
interface thread calls under the change lock (the doc block at `include/HardwareInsert.h:68-72`); it allocates,
frees, locks, waits and logs nothing. Undo is unchanged and no control command was added, so the
ControlCommandsSnapshot canary does not move. `save()`/`load()` are untouched: saved projects and their
`<hardwareinsert>` element load exactly as before.

## Test matrix

| Path | Covered by | Expectation |
| --- | --- | --- |
| Normal | two periods of 0.25 through the insert, `mix` 0, latency = 1 period | second period emits 0.25 (dry, one period late) |
| Bypass / failure | three periods with the insert disabled | each emits 0.25 (the bypassed dry signal passes through) |
| Reconfiguration | re-enable at the same block size (no `prepare()` growth) | first period emits silence, not the pre-disable block |
| Exhaustion | not applicable: the change touches no queue, no capacity and no clamp (`MaxFrames` untouched) | - |
| Audio thread | `reset()` overwrites storage `init()` allocated; called between periods under the change lock | no allocation, free, lock, wait or log |
| Platform | plain C++20 + Qt 6, no platform-specific code | built and tested on linux |

Seams used, no wall clock: the test drives `hardwareio::processInsert(channel, fpp)` - the mixer's own
per-period call - with a buffer it fills itself, and `mixer.hardware_insert` for the settings, so every value
is injected and every period is played by hand.

## Commands and exit codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-169.r1/logs`.

RED (test added, product code untouched):

```
cmake --build build --target MixerInsertLatencyTest -j4 > $L/build-1-red.log 2>&1            # EXIT=0
cd build/tests && ctest -R MixerInsertLatencyTest --output-on-failure > $L/test-1-red.log 2>&1 # EXIT=8
```

`test-1-red.log`: `FAIL! : MixerInsertLatencyTest::reEnablingDoesNotReplayTheDryAudioBufferedBeforeTheDisable()
'reEnabled == 0.f' returned FALSE. (the first period after re-enabling emitted 0.25 instead of silence: the
audio buffered before the disable is being replayed)`.

GREEN (fix applied):

```
cmake --build build --target zene MixerInsertLatencyTest -j4 > $L/build-2-green.log 2>&1       # EXIT=0
cd build/tests && ctest -R MixerInsertLatencyTest --output-on-failure > $L/test-2-green.log 2>&1 # EXIT=0
```

Family (the ctest -R family of the area, plus every test source that includes the changed header):

```
cmake --build build -j4 --target HardwareIoTest MixerLatencyTextTest MixerConcurrencyTest PdcMixerTest \
  InstrumentLatencyPdcTest HardwareOutputBusTest > $L/build-3-family.log 2>&1                  # EXIT=2
```

That EXIT=2 is not a build error: the same command also named `ControlHardwareIo ControlPdcCommands
ControlLowLatencyCommands`, which are ctest-driven python tests and not make targets (`make: *** No rule to
make target 'ControlHardwareIo'. Stop.`). The C++ targets in it built (`[100%] Built target
HardwareOutputBusTest`) and are rebuilt cleanly by the two later commands below.

```
cd build/tests && ctest -R '^(MixerInsertLatencyTest|MixerLatencyTextTest|HardwareIoTest|MixerConcurrencyTest|PdcMixerTest|InstrumentLatencyPdcTest|HardwareOutputBusTest|ControlHardwareIo|ControlPdcCommands|ControlLowLatencyCommands)$' --output-on-failure > $L/test-3-family.log 2>&1  # EXIT=0, 10/10 passed
cmake --build build -j4 --target AudioBusPlayHandleQueueTest InstrumentLowLatencyMonitorTest > $L/build-7-header-users.log 2>&1  # EXIT=0
cd build/tests && ctest -R '^(MixerInsertLatencyTest|MixerLatencyTextTest|HardwareIoTest|MixerConcurrencyTest|PdcMixerTest|InstrumentLatencyPdcTest|InstrumentLowLatencyMonitorTest|AudioBusPlayHandleQueueTest|HardwareOutputBusTest|ControlHardwareIo|ControlPdcCommands|ControlLowLatencyCommands)$' --output-on-failure > $L/test-6-family-wide.log 2>&1  # EXIT=0, 12/12 passed
```

`AudioBusPlayHandleQueueTest`, `InstrumentLatencyPdcTest`, `InstrumentLowLatencyMonitorTest` and `PdcMixerTest`
are the test sources that include `include/LatencyCompensation.h` (with `WasmSandboxTest.cpp`, a wasm-only
target not built here); `MixerStrip.cpp`, `EffectChain.cpp`, `InstrumentOutputBuses.cpp`,
`ControlCommandsPdc.cpp`, `ControlCommandsTrackOffset.cpp` and `AudioBusHandle.cpp` are in `lmmsobjs`, which
the `build-2-green.log` run rebuilt (228 translation units) and `zene` links.

Step 4b, proof that the test catches the bug (product files only, tests excluded):

```
git diff -- src/core/HardwareInsert.cpp src/core/LatencyCompensation.cpp include/LatencyCompensation.h > $L/fix.patch
                                                                                               # DIFF_EXIT=0, 53 lines
git apply -R $L/fix.patch                                                                      # REVERSE_APPLY_EXIT=0
cmake --build build --target MixerInsertLatencyTest -j4 > $L/build-4-reverted.log 2>&1          # BUILD_EXIT=0
cd build/tests && ctest -R '^MixerInsertLatencyTest$' --output-on-failure > $L/test-4-reverted.log 2>&1  # TEST_EXIT=8 (FAIL)
git apply $L/fix.patch                                                                         # APPLY_EXIT=0
cmake --build build --target zene MixerInsertLatencyTest -j4 > $L/build-5-reapplied.log 2>&1   # BUILD_EXIT=0
cd build/tests && ctest -R '^MixerInsertLatencyTest$' --output-on-failure > $L/test-5-reapplied.log 2>&1 # TEST_EXIT=0 (PASS)
```

The reverted run failed with the same message as the first RED run; the re-applied run passed.

Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > $L/gates-1.log 2>&1  # GATES_EXIT=0
```

```
feature-check kads-new-p9-169 (base fa4a5f2e7, 433 changed files)
  PASS  fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures
  PASS  complexity      no new item in your files
  PASS  file-length     no new item in your files
RESULT: PASS
```

GUI smoke and the screenshot artifact (no `src/gui` product file changed; the test file lives under
`tests/src/gui`, so this also shows the app still starts and renders after the change):

```
QT_QPA_PLATFORM=offscreen python3 tests/control-shell.py build/zene $L/shots > $L/gui-shell.log 2>&1   # GUI_EXIT=0
sha256sum $L/shots/shell-mix.png   # 87a3c77db3e8470f40338f2209bdc611ebb77ff07ac160796680b7bf26df759c
```

`gui-shell.log` ends with `PASS: shell control-surface driver (every check held)`, including "the three views
render three different images".

Registration: `include/LatencyCompensation.h`, `src/core/LatencyCompensation.cpp`,
`src/core/HardwareInsert.cpp` and `tests/src/gui/MixerInsertLatencyTest.cpp` are already listed in
`tests/fork-sources.txt` (`:1193`, `:2094`, `:2079`, `:3235`), so no list needed a new entry and no inherited
upstream file was touched (`tests/upstream-modifications.txt` unchanged).

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Reverted run:
  `test-4-reverted.log`, `TEST_EXIT=8`, `FAIL! : ...reEnablingDoesNotReplayTheDryAudioBufferedBeforeTheDisable`.
  Re-applied run: `test-5-reapplied.log`, `TEST_EXIT=0`, `100% tests passed out of 1`. Both came from the same
  binary target rebuilt between runs (`build-4-reverted.log`, `build-5-reapplied.log`, both `BUILD_EXIT=0`).
- **The test is deterministic: no wall-clock timing, sleeps or races.** It uses two injections and no clock:
  the buffer it fills itself (`playIntoInsert` writes `level` into `channel.m_buffer` and calls
  `hardwareio::processInsert`, the mixer's own per-period seam, exactly like `Mixer.cpp:803`), and the settings
  it passes through `mixer.hardware_insert`. Period count and levels are fixed, the delay is exactly one
  period, and nothing is read from the device: `Engine::audioEngine()->audioDev()->stopProcessing()`
  (`MixerInsertLatencyTest.cpp:103`) means no render thread competes with the test.
- **The fix is at the cause and complete: every caller and sibling was checked.** `setSettings` is the single
  funnel: `ControlCommandsHardwareInsert.cpp:59` (`applyInsert`), `:74` (`undoPing`), `HardwareIo.cpp:205`
  (`ChannelHardwareIo::clear`) and `HardwareInsert::load` all reach the reset. The one other path that skips
  `processInsert` is a frozen bus (`Mixer.cpp:997`), which is a different feature and is listed under Other
  findings rather than changed here. The siblings of `LatencyCompensation` keep working because the change is
  additive: `AudioBusHandle.cpp:208` already advances its line with silence while it has nothing to deliver, so
  it never replays stale history the way the un-fed insert did.
- **No collateral damage.** `save()`/`load()` and the `<hardwareinsert>` schema are untouched, so old projects
  load exactly as before (a project that saves the insert on goes false->true once at load, when the line is
  empty, so the reset is a no-op there). No limit or clamp was added or moved (`MaxFrames`,
  `MaxManualOffsetFrames` untouched), so no caller that legitimately exceeds a limit is affected. Behaviour
  outside the two transitions is byte-identical: the delay is still set per period from `latencyFrames()`.
- **ALREADY-FIXED or FALSE-POSITIVE evidence.** Not applicable: the verdict is FIXED, and the code read above
  (`HardwareInsert.cpp:121`, `:227`, `:378-379`) plus the failing RED run show the defect was live.
- **Sibling commands in the command files you changed.** No command file was changed by this fix: the fix is
  in `HardwareInsert::setSettings` and `LatencyCompensation::reset()`. The commands that reach that function
  are `mixer.hardware_insert` (settings) and `mixer.hardware_insert_ping`/its undo path; each stays consistent
  because `setSettings` keeps applying the whole settings struct and the ping reset rule
  (`routeChanged || !settings.enabled || waitingAtAnotherRate`) is untouched. The `ControlHardwareIo`,
  `ControlPdcCommands` and `ControlLowLatencyCommands` suites pass (`test-6-family-wide.log`, EXIT=0). The
  command list in the check (`app.about`, `app.version`, `arrangement.*`, ...) belongs to the shared snapshot
  and no file behind it was modified here.

## Send-back point 5 (loop repair): BLOCKED on the harness mount

The loop driver is `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/qwenfix.py`, and that path is on a
read-only filesystem, so the three repairs asked for (record a zero-commit text-only stop as no-progress, do
not re-queue the same batch unchanged, make the "your earlier commits are already on this branch" line
conditional on `head_before != head_after`) cannot be made from here. The exact output:

```
$ (echo test >> qwenfix.py)
(eval):1: read-only file system: qwenfix.py
APPEND_EXIT=1
$ findmnt -T .../qwen/qwenfix.py
TARGET  SOURCE  FSTYPE OPTIONS
/home/kruzzzzy/.local/share/zene-bunny-fix  /dev/nvme0n1p2[/@home/...]  btrfs  ro,nosuid,nodev,noatime,...
```

An `edit` through the tool surface failed the same way (`[Errno 30] Read-only file system:
.../qwen/.qwenfix.py.oah1utlb.tmp`). The driver's current text at those three places, read this session, is
`qwenfix.py:2004-2013` (the zero-commit branch: `retry = {"item": item, ...}` re-queues `item` unchanged with
its `batch` still in it, `write_json(D["rework"] / f"{s}.json", retry)`, and the run record
(`run_dir/record.json`) keeps no outcome, so the stop reads as finished), `qwenfix.py:1398-1404` (the
unconditional "Your earlier commits are already on this branch." at `:1401`) and the run record write at
`qwenfix.py:1912-1913` - that is the whole change needed, and it needs a writable checkout of the harness.
A copy of the driver as it stands is in the log folder (`qwenfix.py.pre-loop-repair`). The product fix and
this report do not depend on it.

## Other findings

- `src/core/Mixer.cpp:997` (`if (!frozen) { processInserts(fpp); }`): a frozen bus also skips the insert, so
  freezing a channel while its dry line holds audio and unfreezing it can replay that audio the same way. It
  is a different transition with its own state contract (bus freeze intentionally preserves rendered state),
  so it is left alone here rather than folded into this fix.
- `ctl` note for the queue: round 0 of this unit stopped with no commit and no report at all; that is why the
  brief arrived claiming earlier commits were on the branch when there were none (`git log --grep=p9-169`
  returned nothing before this run).
