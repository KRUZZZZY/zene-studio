IMPLEMENTED

# cluster Q050-047 - Correct rack UI limitations documentation

Unit Q050-047 (P0, S, docs, area L10, member R-workflow-123). Branch `feat/050-Q050-047-oc` from train 5
(`de5a85a96`). Every acceptance criterion is met and proven by a test I ran; the one GUI gap the documentation
rewrite found (an empty parallel chain had no way to take its first device) was closed in the same change.

## Acceptance

| Criterion | Test / evidence | How it proves it | Exact command | Exit |
| --- | --- | --- | --- | --- |
| Rewrite the stale socket-only rack bullets | `docs/KNOWN-LIMITATIONS.md:259-295` (claims re-checked at `src/gui/shell/DevicePanel.cpp:390-409`, `src/gui/EffectDeviceMenu.cpp:247-252`, `src/gui/shell/ShellCommandMenus.cpp:438-440`, `src/gui/CommandLists.cpp:91-103`) | the Rack bullet no longer says chain effects are socket-only; "No racks in the interface" is marked superseded; only the truly absent items (narrow macro binding, visual key map, Lua) stay | `grep -n "still socket-only" docs/KNOWN-LIMITATIONS.md` shows the reduced clause | n/a (docs) |
| An empty parallel chain receives its first device from the GUI | `RackMenuTest::anEmptyParallelChainHasItsOwnAddDeviceButton` | two empty parallel chains each show a `deviceAddToChain` button; clicking chain 2's loads a device into chain 2 only (chains 0/1 empty), and `control.undo` takes it back - one step | `cd build/tests && ctest -R '^RackMenuTest$' --output-on-failure` | 0 |
| The panel's Add device fills the channel's selected chain | `RackMenuTest::addDeviceLoadsIntoTheSelectedChain` | parallel selected -> chain 0; `rack.set_selected {chain:1}` -> chain 1 alone; each one undo | same | 0 |
| The Inspector `plugin.load` form route still fills an empty chain | `RackMenuTest::anEmptyParallelChainTakesItsFirstDeviceFromTheInsertForm` | `CommandForm` exposes `chain`, sets it to 1, chain 1 gains the device, chain 0 stays empty | same | 0 |
| Displayed Zones / Add Zone actions match command availability | `RackMenuTest::theZonesEntriesRunRegisteredCommands` | Rack submenu shows `Zones...` / `Add Zone...` (`controlCommand=rack.zone_add`); `rack.zone_add`, `rack.zone_remove`, `rack.get_state` all registered; `CommandLists::rackZones` list/add/remove ids match | same | 0 |
| No regression in the Device panel | `DevicePanelTest` | unchanged, green | `ctest -R '^DevicePanelTest$' --output-on-failure` | 0 |
| No regression in the device picker's neighbours | `EffectRackAddTest`, `EffectDeviceMenuTest`, `EffectPickerOrganiserTest` | green with the new test-only `DevicePicker::setChooser` hook (defaults empty) | `ctest -R 'EffectRackAddTest|EffectDeviceMenuTest|EffectPickerOrganiserTest' --output-on-failure` | 0 |

RackMenuTest direct run: **11 passed, 0 failed, 0 skipped, 0 blacklisted** (QSKIP is gone because
`add_dependencies(RackMenuTest amplifier)` now builds the effect module for a targeted build).

## Gates (exact exits)

- `tests/complexity-gate.sh --check` -> 1 (red on train 5 on purpose; no file of this unit is newly listed)
- `tests/file-length-gate.sh --check` -> 1 (red on train 5 on purpose; no file of this unit is newly listed)
- `tests/fork-sources-gate.sh` -> 0
- `tests/all-sources-reproduce.sh` -> 0
- `tests/no-upstream-regression-gate.sh` -> 0
- `tests/scripted/check-namespace` -> 0
- `tests/no-tautology-gate.sh` -> 0

## Logs

`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-047.r3/logs/`: `build-1.log` (EXIT=0),
`build-2.log` (EXIT=0), `test-RackMenuTest-1.log` (EXIT=0), `test-RackMenuTest-direct-1.log` (EXIT=0),
`test-DevicePanelTest-1.log` (EXIT=0), `test-picker-neighbours-2.log` (EXIT=0), plus the gate logs
`gate-complexity-1.log`, `gate-file-length-1.log`, `gate-fork-sources-1.log`,
`gate-all-sources-reproduce-1.log`, `gate-no-upstream-regression-1.log`, `gate-check-namespace-1.log`,
`gate-no-tautology-1.log`.
