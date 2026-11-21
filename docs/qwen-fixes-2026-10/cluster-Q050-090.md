IMPLEMENTED

Q050-090 Plug-in manager vendor and background scan (round 2 rework): `plugin.list` carries each device's
vendor, the Plug-in Manager/Modules lists show vendor and per-file scan status, and a background rescan runs
with progress and cancellation while the cached catalogue stays served. Round 1's review asked for three
changes, all made on top of the round-1 commits: `plugin.scan_start` now marks the run active, shows it in
the task area and returns BEFORE touching a directory or a library (planning plus one-library-per-turn
pre-loading run as scheduled cancellable chunks after the task and active status exist); the transcript now
requires nonzero intermediate progress, cancels only after at least one completed file, and verifies the
cancelled run's newly scanned records remain cached; the vendor slot moved out of
`ControlDeviceCatalogueTest.cpp` (byte-identical to train 5 again) into the separately registered
`ControlDeviceVendorTest`. Every acceptance criterion is met and proven by a test run below. Detail:
`docs/050-features/Q050-090.md`.

## Acceptance table

| # | Criterion | Exact command (from `build/tests` unless noted) and result |
|---|---|---|
| 1a | Vendor in the manager | `ctest -R ControlDeviceVendorTest --output-on-failure > .../logs/test-01-vendor.log 2>&1; echo EXIT=$?` **EXIT=0** (`listedVendorIsThePickerVendor` passed: every entry carries `vendor`, amplifier is "Zene Studio"). |
| 1b | Vendor through commands | `ctest -R ControlPluginScanBackground --output-on-failure > .../logs/test-02-background.log 2>&1; echo EXIT=$?` **EXIT=0** (`check_vendor` held on the live socket). |
| 1c | Per-file scan status in the manager | Same `ControlPluginScanBackground` run, **EXIT=0** (`check_done_run`: every file reported with run-vocabulary statuses; sorted cache read-back). |
| 2a | Background rescan with progress | Same run, **EXIT=0** (`scan_start` active-first with `total_files` 0; first turn plans `total_files` > 0; nonzero progress after the first completed file while still active; unpaced run reached `done` with progress 1.0; `task.list` holds both runs). |
| 2b | Cancel keeps cached results | Same run, **EXIT=0** (cancel after `done_files` >= 1, outcome `cancelled`; identical pre-run catalogue before, during and after; every completed file's path still in `scan_cache_list`; pre-run records kept). |
| 2c | Second start / sync rescan refused mid-run | Same run, **EXIT=0** (both typed `busy`; `plugin.rescan` runs after; cancel after end typed `refused`). |

## Builds (from the worktree root, `-j4`)

- `cmake --build build --target zene ControlDeviceVendorTest ControlDeviceCatalogueTest PluginScanCacheTest ReversibilityContractTest -- -j4 > .../logs/build-01.log 2>&1; echo EXIT=$?` **EXIT=2** (re-configure for the new test target landed mid-invocation, so the new target name resolved against the old makefiles; `zene` itself built).
- `cmake --build build --target ControlDeviceVendorTest ControlDeviceCatalogueTest PluginScanCacheTest ReversibilityContractTest -- -j4 > .../logs/build-02.log 2>&1; echo EXIT=$?` **EXIT=0**.
- `cmake --build build --target zene -- -j4 > .../logs/build-03.log 2>&1; echo EXIT=$?` **EXIT=0** (description-text fix rebuild).
- `cmake --build build --target ControlDeviceVendorTest ControlDeviceCatalogueTest PluginScanCacheTest ReversibilityContractTest -- -j4 > .../logs/build-04.log 2>&1; echo EXIT=$?` **EXIT=0** (all binaries current with the final sources).
- Snapshot capture from a live instance (`.../logs/snapshot-01.log`, `.../logs/snapshot-02.log`): 831 ids / 76 groups, no `wasm.*`; only the `plugin.scan_status` description (plus capture provenance) changed.

## Neighbour tests (all from `build/tests`, EXIT=0)

`ControlDeviceCatalogueTest` (`.../logs/test-03-catalogue-cache.log`), `ControlPluginScanCommands` and
`ControlCommandsSnapshot` (`.../logs/test-05-snapshot-rescan.log`), `PluginScanCacheTest`
(`.../logs/test-03-catalogue-cache.log`), `ReversibilityContractTest`
(`.../logs/test-04-reversibility-scan-snapshot.log`, histogram unchanged at
rows=840/irreversible=23/not_mutating=358). Final re-run of all seven with current binaries
(`.../logs/test-06-final.log`) **EXIT=0**. The GUI files are byte-identical to the round-1 runs that
passed (`PluginQuarantineMenuTest`, `DevicePanelTest`, `EffectDeviceMenuTest`, `EffectRackAddTest`);
round 2 changes no GUI code.

## Gates

- `bash tests/complexity-gate.sh --check` **EXIT=1**: red at the train-5 tip for the same 6 pre-existing functions; REGRESSION set byte-identical to `../zene-cachyos`, none of this lane's.
- `bash tests/file-length-gate.sh --check` **EXIT=1**: red at the train-5 tip for the same 14 pre-existing files; REGRESSION set byte-identical to `../zene-cachyos` (`ControlDeviceCatalogueTest.cpp` back at 567 CODE lines; the vendor proof lives in the new 76-CODE-line `ControlDeviceVendorTest.cpp`).
- `bash tests/fork-sources-gate.sh` **EXIT=0**; `bash tests/all-sources-reproduce.sh` **EXIT=0**; `bash tests/no-upstream-regression-gate.sh` **EXIT=0**; `tests/scripted/check-namespace` **EXIT=0**; `bash tests/no-tautology-gate.sh` **EXIT=0**; `python3 tests/doc-figures-gate.py` **EXIT=0** (831 ids / 76 groups).

## New commands and reversibility

`plugin.scan_start` (irreversible), `plugin.scan_status` (not_mutating), `plugin.scan_cancel` (not_mutating).
`plugin.list` entries gain `vendor`. No project format change (scan cache stays format version 1).

## Known limits

Quarantine is snapshotted when the run plans (its first turn, after the start returns); a cancelled run's
measured records stay cached while only the catalogue commit is atomic; TSan does not run on this box, so
the no-races argument is by construction (single-thread confinement), stated in
`src/core/PluginFactoryBackgroundScan.cpp`.
