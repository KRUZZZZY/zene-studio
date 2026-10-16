IMPLEMENTED

Q050-090 Plug-in manager vendor and background scan: `plugin.list` carries each device's vendor, the Plug-in
Manager/Modules lists show vendor and per-file scan status, and a background rescan runs with progress and
cancellation while the cached catalogue stays served. Every acceptance criterion is met and proven by a test
run below. Detail: `docs/050-features/Q050-090.md`.

## Acceptance table

| # | Criterion | Exact command (from `build/tests` unless noted) and result |
|---|---|---|
| 1a | Vendor in the manager | `ctest -R ControlDeviceCatalogueTest --output-on-failure > .../logs/test-2-catalogue.log 2>&1; echo EXIT=$?` **EXIT=0** (`listedVendorIsThePickerVendor` passed: every entry carries `vendor`, amplifier is "Zene Studio"). |
| 1b | Vendor through commands | `ctest -R ControlPluginScanBackground --output-on-failure > .../logs/test-3-background.log 2>&1; echo EXIT=$?` **EXIT=0** (`check_vendor` held on the live socket). |
| 1c | Per-file scan status in the manager | Same `ControlPluginScanBackground` run, **EXIT=0** (`check_done_run`: 10/10 files reported with run-vocabulary statuses; `check_cancel`: sorted cache read-back). |
| 2a | Background rescan with progress | Same run, **EXIT=0** (paced start answered task 1 / 10 files; mid-run progress 0..1; unpaced run reached `done` with progress 1.0; `task.list` holds both runs). |
| 2b | Cancel keeps cached results | Same run, **EXIT=0** (paced run cancelled at 0/10, outcome `cancelled`; `plugin.list` named the identical 50-device catalogue before, during and after). |
| 2c | Second start / sync rescan refused mid-run | Same run, **EXIT=0** (both typed `busy`; `plugin.rescan` runs after; cancel after end typed `refused`). |

## Builds (from the worktree root, `-j4`)

- `cmake --build build --target zene -- -j4 > .../logs/build-3.log 2>&1; echo EXIT=$?` **EXIT=0**.
- `cmake --build build --target ControlDeviceCatalogueTest tripleoscillator audiofileprocessor amplifier -- -j4 > .../logs/build-4.log 2>&1; echo EXIT=$?` **EXIT=0** (then `patman`: `build-5-patman.log` **EXIT=0**).
- `cmake --build build --target PluginScanCacheTest ReversibilityContractTest PluginQuarantineMenuTest -- -j4 > .../logs/build-6-tests.log 2>&1; echo EXIT=$?` **EXIT=0**.
- `cmake --build build --target DevicePanelTest EffectDeviceMenuTest EffectRackAddTest -- -j4 > .../logs/build-7-guitests.log 2>&1; echo EXIT=$?` **EXIT=0**.

## Neighbour tests (all from `build/tests`, EXIT=0)

`ControlDeviceCatalogueTest`, `ControlPluginScanCommands`, `PluginScanCacheTest`, `ReversibilityContractTest`
(histogram re-taken to rows=840/irreversible=23/not_mutating=358), `ControlCommandsSnapshot` (snapshot
regenerated live: 831 ids / 76 groups), `PluginQuarantineMenuTest`, `DevicePanelTest`, `EffectDeviceMenuTest`,
`EffectRackAddTest`. Full logs in `.../logs/test-*.log`.

## Gates

- `bash tests/complexity-gate.sh --check` **EXIT=1**: red at the train-5 tip for 6 pre-existing functions; none of this lane's is newly listed.
- `bash tests/file-length-gate.sh --check` **EXIT=1**: red at the train-5 tip for 14 pre-existing files; none of this lane's is newly listed (`ControlDeviceCatalogueTest.cpp` was already over and grows 567 -> 598 CODE lines for the vendor slot).
- `bash tests/fork-sources-gate.sh` **EXIT=0**; `bash tests/all-sources-reproduce.sh` **EXIT=0**; `bash tests/no-upstream-regression-gate.sh` **EXIT=0**; `tests/scripted/check-namespace` **EXIT=0**; `bash tests/no-tautology-gate.sh` **EXIT=0**; `python3 tests/doc-figures-gate.py` **EXIT=0** (831 ids / 76 groups).

## New commands and reversibility

`plugin.scan_start` (irreversible), `plugin.scan_status` (not_mutating), `plugin.scan_cancel` (not_mutating).
`plugin.list` entries gain `vendor`. No project format change (scan cache stays format version 1).

## Known limits

Quarantine is snapshotted at run start (mid-run edits apply next run); a cancelled run's measured records stay
cached while only the catalogue commit is atomic; TSan does not run on this box, so the no-races argument is by
construction (single-thread confinement), stated in `src/core/PluginFactoryBackgroundScan.cpp`.
