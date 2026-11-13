IMPLEMENTED

# Round 2 - review round 1 (REWORK): a far pin saved while the bus was stereo

**The blocking finding.** A device pin on channel 41 did not survive a project save/open when its mixer bus was stereo
at save time, and widening the bus again left it missing. The load grew a device's matrix only to the host bus width
(none for stereo), so the bounds check dropped `c41_N`.

**Fix** (`src/core/AudioPortsModel.cpp`):
- `saveSettings` writes `track_channels` on `<pins>` when pins go past the stereo pair (the highest pair in use). A
  pair-only matrix, which covers every older project, writes nothing new.
- `loadSettings` grows the matrix to the larger of that count and the host's `HostBusScope` width before reading the
  pins.
- The count is validated like the port counts: 2 .. 256, otherwise ignored with a warning. A pin name outside the
  matrix is still dropped, so the BUG-PINBOUNDS contract and the existing `AudioPortsModelTest` slots hold unchanged.
- I also compacted my own `HostBusScope` definitions in that file to keep it under the 500-code-line gate.

**Regressions** (logs `~/.local/share/zene-bunny-fix/qwen/runs/Q050-155.2.r2/logs/`):

| Check | Test | Exit |
|---|---|---|
| discrete-64 pins -> bus set to stereo -> save -> open -> pins read back -> widened to discrete-64 -> 64-channel render with every device on its pair | `WidePinRoutingTest::farPinsSurviveASaveWhileTheBusIsNarrow` | 0 (test-1: 8/8; test-4) |
| saved count round-trips; `track_channels` 999 / -4 / x / 0 ignored (matrix stays 2, far pin dropped); pair-only matrix writes no count | `AudioPortsModelTest::loadSettingsGrowsToTheSavedTrackChannels` | 0 (test-1: 22/22; test-4) |
| the reviewer's reproduction | `QT_QPA_PLATFORM=offscreen python3 /tmp/q050-1552-pin-narrow-roundtrip.py ./build/zene` | 0 (reviewer-repro-1: after reopen on stereo and after re-widen, out pins `[(40, 0), (41, 1)]`) |

## Self-check (round 2)

- **New tests fail reverted, pass restored.** `src/core/AudioPortsModel.cpp` set to the round-1 commit (`git show
  HEAD:...`), rebuilt (build-2-reverted, exit 0): `ctest -R '^(WidePinRoutingTest|AudioPortsModelTest)$'` exit 8. Both
  new slots fail (`Compared values are not the same`), and every other slot passes (test-2-reverted). Restored the fixed
  file, rebuilt (build-3-restored, exit 0): the 24-test set exits 0, 24/24 (test-3-restored). That set is the three
  acceptance tests plus AudioPortsModelTest, AudioPortsTest, WideDiscreteBusTest, SurroundTest, SurroundExportTest,
  MixingRoutingTest, MixerToolsTest, PinConnectorTest, AudioBusTest, PluginAudioPortsTest, ReversibilityContractTest,
  DeviceToolsTest, DeviceToolsChainTest, ClipEffectsTest, MixerModesTest, MixerWindowsTest, AudioPluginTest,
  ProjectStateTest, ControlRoutingCommands, ControlPortsCommands and ControlCommandsSnapshot. After the file-length
  compaction (build-4, exit 0), the 6 focused tests exit 0 (test-4).
- **Every criterion proven by a test I ran:** `ControlWidePinRouting` 0, `WidePinRoutingTest` 0 (now 8 slots),
  `WidePinRoutingGuiTest` 0 (test-3-restored, test-4).
- **One undo step; reversibility.** No command changed in this round. `ReversibilityContractTest` and
  `ControlCommandsSnapshot` exit 0 (test-3-restored).
- **Realtime.** The change is in project save/load on the control thread only: an attribute write and a validated
  parse before `growTrackChannels`, which takes the routing change guard as before. Nothing on the audio path changed.
- **Old projects; round-trip.** A matrix with pins only on the pair writes no new attribute (asserted). A project
  without the attribute loads as before (`loadSettingsIgnoresMalformedAndOutOfRangePins` and the other existing slots
  pass). The narrowed-bus round trip is now covered, and `ProjectStateTest` exits 0.
- **Portability.** Plain Qt DOM and `std::max`; the `thread_local` stays in the .cpp (never in an exported class).
  Only GCC/Qt6 was compiled locally.
- **Gates.** `feature_check.py` still cannot create its log directory outside my write jail. My copy, with only the
  log directory pointed at `runs/Q050-155.2.r2/logs/feature-check` and `ROOT` pinned, gives exit 0 with every row
  PASS (feature-check-3). The first two runs failed for these reasons:
  - feature-check-1: my copy still wrote to the r1 log folder (permission denied).
  - feature-check-2: `file-length` flagged `src/core/AudioPortsModel.cpp` at 505 code lines; I compacted my own
    additions.

  `submit_check.py`'s gates row therefore still reports FAIL in this sandbox, for that reason only.

# Round 1 record

# Q050-155.2 - Carry 64 channels through device pins and mixer routes

Stage 2 of 4 of Q050-155 (L09, P1). Both acceptance criteria are met and proven by tests I ran in this worktree. Full
record: `docs/050-features/Q050-155.2.md`. Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-155.2.r1/logs/`.

In short: a device in a mixer channel set to discrete-N reaches every one of its N channels, by index, through its pins.
Before this change the devices saw the stereo pair only. The channel's devices now run on a wide `AudioBus`, whose pairs
are preallocated off the audio thread. A device's pin matrix grows to the bus width. The new `port.set_pins` sets a
whole routing as one undo step, `port.get_state` reports the bus, and `mixer.get_state` / `routing.matrix` say how many
channels a non-stereo route carries. In the GUI, the Pin Connector opened from a device card gets a Channels menu and
command-driven pin clicks. The pin router is clamped to the bus it is handed.

## Acceptance

All from `build/tests`, `QT_QPA_PLATFORM=offscreen`, no `LMMS_PLUGIN_DIR` exported (tests set their own).

| Criterion | Test | Exit |
|---|---|---|
| A control-socket test configures 64 channel pins and mixer routes, reads their state back, and confirms one-step undo. | `ctest -R '^ControlWidePinRouting$'` (`tests/control-wide-pin-routing.py`, real `zene` over the socket, 13 checks): two discrete-64 channels; `mixer.route_to` one into the other; `mixer.get_state` and `routing.matrix` report the route `channels: 64`, `channel_routing: index`; one `control.undo` removes it. 32 amplifiers, each moved by `port.set_pins` onto its pair (64 channel pins, in and out), read back by `port.get_state` (every device on its own pair, `bus.channels` 64, out pins cover 0..63 exactly once); one `control.undo` moves only the last device back; a channel past the bus and a channel past a stereo bus are refused, typed, writing nothing. | 0 (test-6.log: `PASS: 13 checks`; test-16-restored.log) |
| A render confirms the configured pin and mixer routes preserve each channel index. | `ctest -R '^WidePinRoutingTest$'` (7 slots): 64 distinct constants on a discrete-64 channel carrying 32 amplifiers, amplifier k on channels 2k/2k+1 at its own gain, routed into a discrete-64 bus whose direct out is outputs 1..64 of a 128-output device. After real `masterMix` periods, output c = level(c) x gain(c/2) and 65..128 silent; one undo and a redo change exactly the expected outputs; after a mixer save/clear/load the pins and the render match again; the master mix allocates nothing; on a stereo channel a far-pinned device leaves the pair untouched. | 0 (test-4.log: 7 passed; test-16-restored.log) |
| (GUI path, definition of done 2) | `ctest -R '^WidePinRoutingGuiTest$'`: device card pins button opens the Pin Connector (64 rows), `Channels > Channels 41-42` (`controlCommand = port.set_pins`) moves the device, `control.undo` undoes it; a mouse click on a pin runs `port.set_pins` and the main window's Edit > Undo takes it back. | 0 (test-4.log: 3 passed; test-16-restored.log) |

## Self-check

- **Each new test fails with the product reverted, passes restored.** The 19 product files (`include/AudioPorts.h
  AudioPortsModel.h ControlDeviceSupport.h ControlMixerSupport.h ControlRegistryGroups.h MixerSurround.h PinConnector.h`,
  `src/core/AudioPortsModel.cpp ControlCommandsMixer.cpp ControlCommandsMixerTools.cpp ControlCommandsPorts.cpp
  ControlMixerSupport.cpp ControlReversibilityTableRouting.cpp Effect.cpp Mixer.cpp MixerSurround.cpp MixerSurroundMix.cpp`,
  `src/gui/PinConnector.cpp src/gui/shell/DeviceCard.cpp`) were checked out from the base `af010bc3c`, with the tests
  left in place:
  - `WidePinRoutingGuiTest` does not compile (build-13-reverted.log, exit 2: `PinConnector` has no member
    `channelsMenu`). Its ctest then ran the older binary left from the previous build and reported exit 0
    (test-14-reverted.log). That binary was stale and that run is not evidence.
  - With `zene`, `WidePinRoutingTest` and `amplifier` rebuilt (build-14-reverted.log, exit 0): `WidePinRoutingTest`
    exit 8 (5 of 7 slots fail, test-12-reverted.log); `ControlWidePinRouting` exit 8 (route readback and every
    `port.set_pins` check FAILED, test-13-reverted.log).
  - Engine-only revert (`MixerSurround.h/.cpp`, `MixerSurroundMix.cpp`, `Mixer.cpp`; commands kept; build-15, exit 0):
    `WidePinRoutingTest` exit 8, 4 of 7 slots fail (`32 pinned devices: output 3: 0.0234375, expected 0.0229688`),
    test-15-engine-reverted.log. So the render needs the wide bus itself, not only the new command.
  - Restored with `git checkout HEAD -- <files>` (`git status` clean), rebuilt (build-16-restored.log, exit 0): 23
    tests - the three new ones plus 20 neighbours - ctest exit 0, 23/23 (test-16-restored.log).
- **Every acceptance criterion proven by a test I ran:** the table above - `ControlWidePinRouting` 0,
  `WidePinRoutingTest` 0, `WidePinRoutingGuiTest` 0.
- **One undo step per edit; honest reversibility.**
  - `port.set_pins` (new) is `true_inverse`: one `addUndoStep` on the engine's journal per call, which `control.undo`,
    Edit > Undo and `control.redo` all use. Each step re-resolves the device by its stable ids and applies the before or
    after pins as one routing edit. Each is asserted in tests: the socket test (undo moves one device), the render test
    (undo, then redo), and the GUI test (`control.undo` after the menu entry, `MainWindow::undo` after a pin click).
    The socket test sets `control.set_undo_coalescing` to 0, so "one step" is measured per command.
  - `port.set_pin` stays `snapshot`. The reads stay `not_mutating`.
  - `ReversibilityContractTest` exit 0 after I added the row and the A16 histogram line
    (`rows=934 true_inverse=460 snapshot=42 irreversible=25 not_mutating=407`; the test had named the expected figure).
  - Snapshot regenerated live with `snapshot_live.py` (925 ids / 78 groups, `wasm.*` out); README and
    `tests/doc-figures-gate.py` canaries set to 925; `tests/doc-figures-gate.py` exit 0; `ControlCommandsSnapshot`
    exit 0.
- **Realtime (audio-thread functions in the diff):**
  - `surround::insertsTakeWideBus` reads the format, frame count, pair capacity and the chain's enabled flag and size,
    all written only on the control thread under the change lock.
  - `surround::processWideInserts`, `toPair` and `fromPair` use a `std::array` of 32 pointers on the stack and an
    `AudioBus` (pointer + bitset) on the stack, and copy between already-allocated side planes and insert pairs. The
    pairs are grown only in `ChannelSurround::setFormat`/`init` (control thread, change lock, never shrink). They then
    call the existing `EffectChain::processAudioBuffer(AudioBus&)`.
  - `AudioPortsRouter::send/receive` and `process` gained only `std::min` and an index comparison.
  - `Effect::processAudioBuffer(AudioBus&)`: a `std::fill_n` over the existing pair and bitset sets.
  - None of these allocate, free, lock, wait, do I/O or emit a signal. `WidePinRoutingTest::theWideInsertMixAllocatesNothing`
    counts 0 allocations in a master mix with 32 wide inserts.
  - Pin edits (`setPins`, `growTrackChannels`) take the engine's model-change guard, as every routing edit does.
    `HostBusScope` is a thread-local value on the loading thread.
  - Local TSan does not run on this box; this paragraph is the race analysis.
- **Old projects; round-trip.**
  - Stereo, mono and speaker-layout channels take the old insert path, unchanged. A discrete channel's devices with
    default pins process the pair exactly as before, and the side channels pass them untouched.
  - The pins file format is unchanged (`cN_M`). Without a discrete host, a load grows nothing:
    `AudioPortsModelTest::loadSettingsIgnoresMalformedAndOutOfRangePins` exit 0. My first version grew the matrix from
    the file and that test caught it, so growth now comes only from the host channel's `HostBusScope`.
  - Round-trip: `pinsPastThePairSurviveASaveAndLoad` exit 0.
  - Old-project and neighbouring tests: `WideDiscreteBusTest`, `SurroundTest`, `SurroundExportTest`, `MixingRoutingTest`
    and `MixerToolsTest` exit 0 (test-16-restored.log); `ProjectStateTest`, `SurroundPannerTest` and `HardwareIoTest`
    exit 0 (test-17.log).
  - One honest nuance: a pinless legacy device (a LADSPA host, say) on a discrete channel now has its engine sanitizer
    (`sanitizeAll`, NaN/inf cleared, samples clamped to +-1000) run over the side channels too, because the side
    channels are now on the bus it processes. Normal audio is unaffected.
- **Portability (Qt 5.15 / MSVC / macOS < 10.15).** No Qt-6-only API (QMenu/QToolButton/QAction/QJsonArray, all Qt 5).
  No `small`/`near`/`far`/`interface`/`min`/`max` identifiers and no `std::filesystem`. `std::optional` is read with
  `*`, not `.value()` (no `bad_optional_access`). The new tests QSKIP when the amplifier module cannot be found, as
  their neighbours do. Only GCC/Qt6 was compiled locally; the other targets were not built.
- **Gates.**
  - `submit_check.py` reports `FAIL gates` because `feature_check.py` cannot create its log directory
    (`<worktree>-feature-check` is outside my write jail; `PermissionError`, feature-check-1.log).
  - I ran a copy, `/tmp/q1552-fc/feature_check.py`. Its only changes: `ROOT` pinned to the original `qwen/`
    directory, and the log directory set to this run's `logs/feature-check`.
  - Result: exit 0, every row PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
    no-tautology, doc-figures, complexity and file-length with no new item; feature-check-3.log). The first run
    (feature-check-2.log) had flagged four functions over CCN 10; I split them.
- **Registration.** New files are in `tests/fork-sources.txt` and `tests/all-sources.txt` (the C++ ones; the recipe
  reproduces). The changes to the inherited `src/core/Mixer.cpp`, `src/core/Effect.cpp` and `tests/CMakeLists.txt` are
  declared in `tests/upstream-modifications.txt`. The tests are in `tests/CMakeLists.txt` with
  `add_dependencies(... amplifier)` and the plugin directory.
- **Build note.** The worktree's ccache temp dir is outside my write jail. I built with
  `CCACHE_TEMPDIR=/tmp/ccache-tmp-q1552` (build-1 failed on exactly that; build-2 onward exit 0). New test targets
  appeared after the first build's own `cmake_check_build_system`; I did not run a configure step (the shim refuses
  it).

## Known limits

- A track (an instrument's own outputs) still feeds a mixer channel its first pair. The Q050-155.1 record said stage 2
  would carry "track routes", but this unit's title and acceptance are device pins and mixer routes, and the
  track-to-mixer buffer (`AudioBusHandle`) is untouched.
- Racks, post-fader inserts and speaker-layout channels still hand their devices the pair only.
- Per-pair sends and their Routing Matrix editing are stages 3-4.
- `port.set_pin` is not on the redo stack (unchanged; `port.set_pins` is).
- See `docs/050-features/Q050-155.2.md`.
