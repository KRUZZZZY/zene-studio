# Cluster new-p8-131-port

**Verdict: FIXED.** The fix from `25bc601f0` (branch `bugs/qwen-new-p8-131`) is re-applied by hand on this branch, which is based on the newest line. The newest line still publishes a layer's zone list and its layer slot as two separate edits, so the bug is not already fixed here.

The finding's wording is slightly off. `RackZones::edit` already holds the model-change guard around every zone mutation (`src/core/RackZones.cpp:91-101`). The real defect is that the zone renumbering and the matching layer-slot update were two guarded edits. A period could run between them, so an audio worker could see zones routed to chains whose slots were not yet updated. `addLayer`, `insertLayer` and `removeLayer` now publish both in one edit.

## Evidence

Logs are under `LOGS=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-131-port.r1/logs`.

- Before (HEAD `4eb8d6e8a`): `src/core/InstrumentLayers.cpp:204` (`adopt`) and `:209` (`m_zones.addZone`) in `addLayer`; `:221` (`m_zones.assign`) and `:223` (`adopt`) in `insertLayer`; `:232` (`m_zones.assign`) followed by a separate `ChangeGuard` slot shift in `removeLayer`.
- After:
  - `addLayer` runs `adopt`, the mix reset and `publishMix` inside the `addZone` edit (`src/core/InstrumentLayers.cpp:207-212`).
  - `insertLayer` runs `makeRoomForMix`, `adopt` and `publishMix` inside the `assign` edit (`:225-230`).
  - `removeLayer` runs the slot shift, live-bit drop, `dropMix`, `delete track` and `publishMix` inside the `assign` edit under its own `ChangeGuard` (`:238-251`).
  - `publishMix()` reads `layer(index)` (`:369-372`), so the mix publish stays after the slot update, inside the same edit.
- `src/core/RackZones.cpp:114-124` (`addZone`) and `:158-166` (`assign`) take an optional `alongside` step that runs inside the same `edit()` callback, before the changed hook. Declarations are at `include/RackZones.h:127-135`.

## Regression test

- New file `tests/src/core/InstrumentLayerZonePublishTest.cpp`, test `layerZoneEditsPublishWithTheirSlots`. It adds a layer to a fresh track, then checks at the zone list's changed hook that `insertLayer(1)` and `removeLayer(1)` each leave one zone routed to the chain that holds the existing track.
- Why a new file: `InstrumentLayersCommandsTest.cpp` measures 498 code lines at HEAD against the 500 limit. The test added 39 and took it to 537, which the file-length gate reported as a new regression. The test is therefore in its own file.
- Registered in `tests/CMakeLists.txt` (source list, the `LMMS_TEST_PLUGIN_DIR` condition, and the `QT_QPA_PLATFORM` property) and in `tests/fork-sources.txt`.
- Three lines added to `tests/upstream-modifications.txt`, one for each inherited product file changed.

## Commands and exit codes

| Step | Command (abbreviated; full paths in the log folder) | EXIT | Result |
|---|---|---|---|
| RED, first attempt | `cmake --build build --target InstrumentLayersCommandsTest` (log 002) | 0 | Built. Test ran as SKIP (no instrument plugin built, see notes). |
| RED, real run | `./InstrumentLayersCommandsTest layerZoneEditsPublishWithTheirSlots -v2` (log 006) | 1 | `'insertPublishedTogether' returned FALSE` on unfixed code. |
| RED, ctest | `ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure` (log 007) | 8 | 0% passed; the same assertion fails. |
| Save patch | `git diff -- include/RackZones.h src/core/RackZones.cpp src/core/InstrumentLayers.cpp > $LOGS/fix.patch` | 0 | Saved. |
| Revert fix | `git apply -R $LOGS/fix.patch` | 0 | Product files back to HEAD. |
| RED build | `cmake --build build --target InstrumentLayerZonePublishTest -j2` (log 018) | 0 | Built. |
| RED, final file | `ctest -R '^InstrumentLayerZonePublishTest$' --output-on-failure` (log 019) | 8 | `'insertPublishedTogether' returned FALSE`; 0% passed, 1 failed. |
| Reapply fix | `git apply $LOGS/fix.patch` | 0 | Fix restored. |
| GREEN build | `cmake --build build --target InstrumentLayerZonePublishTest -j2` (logs 020, 029) | 0, 0 | Built. |
| GREEN, direct run | `./InstrumentLayerZonePublishTest -v2` (log 017) | 0 | 3 passed, 0 failed, 0 skipped. The layer test runs; it does not skip. |
| GREEN, ctest | `ctest -R '^InstrumentLayerZonePublishTest$' --output-on-failure` (log 027) | 0 | 100% passed, 1 of 1. |
| Dependent targets | build of the 90 test targets whose sources include the changed headers (log 012) | 0 | All built against the fixed headers. |
| Affected family, first try | `ctest -R 'Rack\|Instrument\|Layer\|Zone' -j2` (log 024) | 8 | 8 "Not Run": their executables had not been built. |
| Affected family, final | same command after building those 8 targets (log 026) | 0 | 20 of 20 passed. |
| Dependent tests | `ctest -R` over the 89 runnable dependent tests, `-j2` (log 028) | non-zero (3 failed) | 86 passed, 3 failed. See below. |
| Complexity gate | `bash tests/complexity-gate.sh --check` (logs 022, 030) | 1, 1 | Fails on 6 entries that also fail on HEAD. |
| File-length gate | `bash tests/file-length-gate.sh --check` (logs 023, 031) | 1, 1 | Fails on 14 entries that also fail on HEAD. |
| HEAD baseline for gates | `git archive HEAD \| tar -x -C /tmp/p8-head-tree`, then both gates there (logs 014, 015) | 1, 1 | Same failure sets as the final tree (diffed, identical). |

The regression test, with the fix in place, is the one run GREEN above. Its RED result is recorded with the product fix reverted, both before and after it moved into its own file.

### Dependent-test failures (log 028)

All three failures are environmental and in code this change does not touch. They were not re-run on HEAD.

- `SlicerTPlayheadTest`: reports `SlicerT did not accept the fixture file` (and a missing poll timer). I did not establish the cause.
- `UndoHonestyTest::armingSeveralTracksIsOneStep`: writing `~/Documents/Zene Studio/zene-take-trk21.wav` is refused. That path is outside the write-jail.
- `PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid`: the SWH Decimator LADSPA fixture is not loaded on this machine.

### Gates

Both ratchet gates exit 1, on HEAD and on this change alike. The failure sets are identical, so this change adds no complexity or file-length regression. The failing entries are in files this change does not touch (`XyPadPairTest.cpp`, `ControlCommandsMixerTools.cpp`, `SmfInterchangeReader.cpp`, `TakeLane.cpp`, `RackMacrosTest.cpp`, and others). Fixing them is outside this bug, and I did not change any gate limit or baseline.

## Notes

- Luna's round-0 note says the earlier commits are "already on this branch". They are not: `25bc601f0`, `3d4c4b203` and `21be57d9d` are not ancestors of HEAD (`git merge-base --is-ancestor`). I did not cherry-pick or merge them. I wrote the change by hand from `git show 25bc601f0`.
- The newest line also carries the layer mix (`m_mix`, `publishMix()`), which `25bc601f0` predates. `publishMix()` reads the layer slots, so the mix reset and publish stay after the slot update, inside the same edit.
- Environment: the default ccache temp dir (`/run/user/1000/ccache-tmp`) is not writable, so every build used `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p8-131`.
- Environment: no instrument plugin was built in `build/`, so the layer tests skipped. I built the `tripleoscillator` target (`build/plugins/libtripleoscillator.so`). Without a real instrument the regression cannot run at all, so the earlier "RED" result that was a skip counts for nothing.
- Build parallelism: `zene` at `-j4` (log 008), everything else at `-j2`.
- I touched `tests/CMakeLists.txt` (mtime only) once, to force regeneration after the list edit. The build system regenerated automatically; I did not run a manual configure.
