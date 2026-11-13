IMPLEMENTED

# Cluster Q050-296.1: a 5.1 or 7.1 channel's speakers run through its devices

Branch `feat/050-Q050-296.1-oc`, feature record `docs/050-features/Q050-296.1.md`. Stage 1 of 4 of Q050-296
(post-fader inserts, sends, the mono/polarity/width/EQ stages and the meters stay on L/R).

## Acceptance

| Criterion | Test | Command | Exit (RED -> GREEN) |
|---|---|---|---|
| A distinct test signal on every 5.1/7.1 speaker is processed and appears on its matching output channel, without loss or reordering | `SurroundDevicePathTest::everySpeakerIsProcessedOnItsOwnOutput(5.1, 7.1)` and `WidePinRoutingGuiTest::aSurround51BusShowsItsSpeakers` | `cd build/tests && ctest -R '^(SurroundDevicePathTest\|WidePinRoutingGuiTest)$' --output-on-failure` | 8 -> 0 |
| Pins survive a save and load. | `SurroundDevicePathTest::pinsSurviveASaveAndLoad` | same | 8 -> 0 |
| One undo puts the device back on the pair. | `SurroundDevicePathTest::undoRestoresThePair` | same | 8 -> 0 |
| A stereo device without pins is compatible. | `SurroundDevicePathTest::aStereoDeviceWithoutPinsLeavesSpeakersUntouched` | same | 0 (slot passed in RED too: a control; the ctest entry exits 8 -> 0) |
| The wide mix allocates nothing. | `SurroundDevicePathTest::theWideInsertMixAllocatesNothing` | same | 8 -> 0 |
| An invalid format is refused. | `SurroundDevicePathTest::anInvalidFormatIsRefused` | same | 0 (slot passed in RED too: a control; the ctest entry exits 8 -> 0) |

Exit codes are per ctest entry; in the RED run the slots everySpeakerIsProcessedOnItsOwnOutput(5.1, 7.1), pinsSurviveASaveAndLoad, undoRestoresThePair, theWideInsertMixAllocatesNothing and aSurround51BusShowsItsSpeakers failed, while the two control slots passed (see 009-red.log).

RED: the product commit `5c7400499f`'s `src/` and `include/` hunks reverse-applied with
`git apply -R /tmp/q050-296.1-logs/fix.patch`; build exit 0 (`/tmp/q050-296.1-logs/008-build-red.log`), ctest
exit 8, 2 of 2 tests failed (`/tmp/q050-296.1-logs/009-red.log`). GREEN: hunks restored with
`git apply /tmp/q050-296.1-logs/fix.patch`; build exit 0 (`/tmp/q050-296.1-logs/010-build-green.log`), ctest
exit 0, 100% of 2 (`/tmp/q050-296.1-logs/011-green.log`). The failing RED assertions are the six-speaker bus
(`pair 0: bus channels 2, expected 6/8`) and the missing 5.1 Pin Connector (`connector != nullptr` false).

## Gates

`feature_check.py` on this tree: exit 0, all 8 gates PASS (`/tmp/q050-296.1-logs/013-gates.log`). The first run
(`012-gates.log`) failed only the complexity gate: `pinSpeakers` (test helper) was CCN 11 over the target, fixed by
splitting the per-pair check into `pinPair` and looping over the channels - no assertion or behaviour change.

`ReversibilityContractTest`: build exit 0 (`/tmp/q050-296.1-logs/014-build-rev.log`), ctest exit 0, 1 of 1
(`/tmp/q050-296.1-logs/015-rev.log`). No command id or schema changed (no A16 change); the snapshot was regenerated live anyway, see Notes.

## Commits

`git log --oneline 3ab01006f..HEAD` (the seven commits of this branch; a plain `master..HEAD` also carries
the whole `050/m0-train-28` history, 8499 commits):

```
this commit docs(mixer): Q050-296.1 report states the RED slots exactly (Q050-296.1)
0cdbad48ed docs(mixer): Q050-296.1 cluster report and red/green evidence (Q050-296.1)
09562356e7 docs(mixer): Q050-296.1 cluster report and red/green evidence (Q050-296.1)
d79070ae6b test(mixer): keep pinSpeakers within the complexity gate (Q050-296.1)
923ffd008e docs(mixer): correct Q050-296.1 metadata and which layouts take the wide bus (Q050-296.1)
f3a22bf9b2 docs(mixer): 5.1/7.1 speakers through channel devices - manual and feature notes (Q050-296.1)
7b9d7fa87d test(gui): Pin Connector on a 5.1 bus reaches every speaker through port.set_pins (Q050-296.1)
5c7400499f feat(mixer): 5.1/7.1 speakers run through the channel's devices on the wide insert bus (Q050-296.1)
e5599dca9d test(mixer): 5.1/7.1 speakers through pinned channel devices to matching outputs, RED before the fix (Q050-296.1)
```

## Notes

- The run log folder `~/.local/share/zene-bunny-fix/qwen/runs/Q050-296.1.r1/logs/` is not writable, so the
  logs live in `/tmp/q050-296.1-logs`, `/tmp/Q050-296.1.r1-logs` and `/tmp/q050-296-p3-logs`.
- No new command ids and no changed command metadata: `ControlCommandsPorts.cpp` changed only the value of
  `port.get_state`'s `bus.format` (`hostBusJson`), not a schema or description. The snapshot was still
  regenerated live from `build/zene` (`snapshot_commands.py --socket`, exit 0: 931 ids / 78 groups, surface
  `ids_sha256 bc0c9a3463e...` unchanged), so its diff is provenance only (`captured_at`, lane, socket,
  version). No README canary change: the figure is the same 931/78.
- Post-fader inserts are out of scope (stage 1/4 of Q050-296).
- TSan was not run locally. The race reasoning: the wide insert pairs are grown only in
  `ChannelSurround::setFormat` on the control thread under the engine's change lock, and the audio path only
  reads them; the allocation probe test shows the wide master mix allocates nothing.

## Self-check

**Every acceptance criterion has a RED and a GREEN exit code** - yes, in the table above and in the feature
record. RED (`/tmp/q050-296.1-logs/009-red.log`, exit 8) with `5c7400499f`'s `src/`+`include/` hunks reversed;
GREEN (`/tmp/q050-296.1-logs/011-green.log`, exit 0, 2/2) restored. A later re-run of the same ctest on the
current tree: exit 0, 2/2 (`/tmp/q050-296.1-logs/017-green-rerun.log`).

**Every acceptance criterion is proven by a test I ran myself** - `SurroundDevicePathTest` covers
`everySpeakerIsProcessedOnItsOwnOutput(5.1)` and `(7.1)` (exit 0), `pinsSurviveASaveAndLoad`, `undoRestoresThePair`,
`aStereoDeviceWithoutPinsLeavesSpeakersUntouched`, `theWideInsertMixAllocatesNothing`, `anInvalidFormatIsRefused`;
`WidePinRoutingGuiTest` covers `aSurround51BusShowsItsSpeakers` and `theChannelsMenuAndAPinClickRunThePinCommands`.
Both CTest entries pass (exit 0) in 011 and 017; the RED run fails both (exit 8) with exactly the six-speaker
assertions and the missing connector.

**One undo step, honest reversibility class** - `port.set_pins` is `RC::TrueInverse` with a recorded action
checkpoint (`src/core/ControlReversibilityTableRouting.cpp:191`); the 5.1 GUI slot triggers Channels > Channels
5-6, sees `4:0,5:1` on both directions, runs one `control.undo` and sees `0:0,1:1` back. The pin click runs the
same command and `getGUI()->mainWindow()->undo()` takes it back in one step. `port.get_state` is read-only
(`mutating: false`) and unchanged as a command. `mixer.channel_format` keeps its existing class.

**Nothing on the audio thread allocates, frees, locks, waits, does I/O or emits a signal** - the audio-path
edits are `MixerChannel::processInserts` (one branch: named surround layouts now take the same
`surround::processWideInserts` call discrete ones already took), `surround::insertsTakeWideBus` and
`processWideInserts` in `MixerSurroundMix.cpp` (a format predicate and pair mapping over the pre-allocated
buffer; no allocation, no lock, no signal), and `ChannelSurround::setFormat` (the only place `m_pairs` grows -
control thread, under the engine's change lock). `hostBusJson` and `controlHostBusChannels`
(`ControlCommandsPorts.cpp`) are agent-command code on the main thread. The allocation probe
(`theWideInsertMixAllocatesNothing`) measures zero allocations for the wide master mix.

**Old projects load and render as before** - `savedDiscreteChannels` now returns the count for any layout
wider than the pair, and the pair path is untouched (`insertsTakeWideBus` still requires an enabled non-empty
chain and room on the wide bus); `aStereoDeviceWithoutPinsLeavesSpeakersUntouched` proves a default-pinned device
still passes the other speakers through, and `pinsSurviveASaveAndLoad` proves the round-trip
(`SurroundDevicePathTest`, exit 0).

**Completeness, real user path and agent path** - agent path: `mixer.channel_format` -> `plugin.load` ->
`plugin.param_set` -> `port.set_pins`, then `port.get_state` (`bus.channels` 6/8, `bus.format` "5.1"/"7.1");
GUI path: the real `DeviceCard` pins button, the `PinConnector`'s six rows (three Channels entries), a menu
trigger and a matrix click. Edge cases tried: 5.1 and 7.1 (both data rows), the 7.1 save/reload (pins and render
after `clear()`+`loadSettings`), an undo after the pinned pair, an invalid `mixer.channel_format` (state
unchanged), and a device with default pins. Not tried here: a project reopen through `project.open` (covered by
the same save/load path) and post-fader inserts (out of scope).

**The GUI test drives the visible control and the same registered command ran** - `WidePinRoutingGuiTest` clicks
the `DeviceCard` pins button (`QTest::mouseClick`), checks the connector's row count, triggers the
`Channels 5-6` menu action whose `controlCommand` property is `port.set_pins`, then clicks a matrix cell; it
asserts the pins through `port.get_state` and takes each back with one undo - no direct handler call.

**Qt 5.15 / MSVC / macOS < 10.15 portability** - no Qt-6-only API, no `windows.h` macro names and no
`std::filesystem` in the diff (checked over `src/`, `include/` and `tests/src/`): only `QString`, `QJsonObject`,
`QDomElement` and `std::min`. The tree is clean after the final commit.
