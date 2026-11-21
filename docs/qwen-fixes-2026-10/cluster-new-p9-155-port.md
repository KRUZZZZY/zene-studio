# Cluster new-p9-155-port: 16-bit GIG sample amplitude

**Verdict: FIXED** (port of `11ba5477f` from `bugs/qwen-new-p9-155` onto `050/m0-train-5`; the bug was still present on this line, so it is not ALREADY-FIXED)

`GigInstrument::loadSample()` converted signed 16-bit PCM with `1 / 0x10000`. Full-scale input therefore reached about 0.5. Both channel conversions now use `1 / 0x8000`. On the GIG fixture the render peak moves from `0.218` to `0.4359`.

## Evidence

- Bug site (before): `plugins/GigPlayer/GigPlayer.cpp:726` and `:735` used `1.0 / 0x10000` (verified by `sed -n 724,737p` on the unfixed tree).
- Fix (after): `plugins/GigPlayer/GigPlayer.cpp:726` and `:735` now use `1.0 / 0x8000`.
- Regression, reference comparison: `tests/src/plugins/PluginPortsMigrationTest.cpp:1587-1600` (new `gigplayer` branch in `migratedPluginsPreserveBehaviour`). The frozen reference still has the old half-amplitude render, so this asserts migrated == 2 × reference within `1e-6`.
- Regression, peak: `tests/src/plugins/PluginPortsMigrationTest.cpp:2397-2400` (in `slice8PluginsAreLiveAndExact`) asserts the GIG peak is above `0.35`.
- Inherited-file register: `tests/upstream-modifications.txt` (last line) records the `GigPlayer.cpp` change with a reason.

Scope note: the new assertions use the existing `PART_C_MIGRATED_gigplayer` host and the vendored `tests/data/gigproof/sine440.gig`. No new files were created, so `tests/fork-sources.txt` is unchanged. No control commands were added.

## RED / GREEN evidence

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-155-port.r1/logs`

| Step | Command (cwd noted) | EXIT | Result |
|---|---|---|---|
| Build targets, first try | repo root: `cmake --build build --target gigplayer PluginPortsMigrationReference PluginPortsMigrationTest -j2 > .../build-1-red.log 2>&1` | 2 | Environment failure, not code: ccache could not write its temp file under `/run/user/1000` (`build-1-red.log:27`). |
| Build targets, retry | repo root: `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p9-155 cmake --build build --target gigplayer PluginPortsMigrationReference PluginPortsMigrationTest -j2 > .../build-2-red.log 2>&1` | 0 | Built. |
| **RED** (test change, unfixed product) | `build/tests`: `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../test-1-red.log 2>&1` | 8 | Failed at `'scaledWorst <= 1e-6f'` (`gigplayer render is not twice the pre-fix reference (scaled max\|delta\|=0.218)`) and `'peak > 0.35f'` (`16-bit GIG sample peak was not normalized to full scale`, `peak=0.218`). Totals: 42 passed, 3 failed, 2 skipped. |
| Build zene + gigplayer + test (fix applied) | repo root: `CCACHE_TEMPDIR=... cmake --build build --target zene gigplayer PluginPortsMigrationTest -j2 > .../build-3-green.log 2>&1` | 0 | Built. |
| **GREEN** (fix applied) | `build/tests`: `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../test-2-green.log 2>&1` | 8 | Both GIG assertions pass (`peak=0.4359`). Totals: 44 passed, 1 failed, 2 skipped. The one failure is `nonRealtimeLadspaEffectsAreInvalid` (`SWH Decimator test fixture was not loaded`), which fails identically before and after the fix and is unrelated to GigPlayer. |
| 4b, patch saved | repo root: `git diff -- plugins/GigPlayer/GigPlayer.cpp > .../fix.patch` | 0 | Saved. |
| 4b, revert fix | repo root: `git apply -R .../fix.patch` | 0 | Reverted. |
| 4b, rebuild after revert | repo root: `CCACHE_TEMPDIR=... cmake --build build --target gigplayer -j2 > .../build-4-revert.log 2>&1` | 0 | Built. |
| **4b RED** (reverted) | `build/tests`: `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../test-3-revert.log 2>&1` | 8 | Same two GIG failures as the first RED run (`scaled max\|delta\|=0.218`, `peak > 0.35f`). Totals: 42 passed, 3 failed, 2 skipped. |
| 4b, re-apply fix | repo root: `git apply .../fix.patch` | 0 | Re-applied. |
| 4b, rebuild after re-apply | repo root: `CCACHE_TEMPDIR=... cmake --build build --target gigplayer -j2 > .../build-5-reapply.log 2>&1` | 0 | Built. |
| **4b GREEN** (re-applied) | `build/tests`: `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../test-4-reapply.log 2>&1` | 8 | Same as GREEN above: 44 passed, 1 failed (unrelated SWH case). |

Ctest family: `ctest -R '^GigPlayer$'` matches **0 tests** in this configuration (`ctest -N` lists 593 tests and no GigPlayer entry; the `tests/reference/GigPlayer` target is not registered here). The gig coverage that ran is `PluginPortsMigrationTest` (its GIG render case and `slice8PluginsAreLiveAndExact`). Logged in `test-5-gigfamily.log`.

Affected-family count: 1 registered ctest target (`PluginPortsMigrationTest`), 44 Qt cases passed after the fix.

## Gates

| Gate | Command | EXIT | Notes |
|---|---|---|---|
| Complexity ratchet | repo root: `bash tests/complexity-gate.sh --check > .../gate-1-complexity.log 2>&1` | 1 | 6 `REGRESSION` lines, all in files this change does not touch (for example `dotCentreAlong@tests/src/gui/XyPadPairTest.cpp`, `parseNotes@src/core/DawProjectReadTracks.cpp`). |
| File-length ratchet | repo root: `bash tests/file-length-gate.sh --check > .../gate-2-filelength.log 2>&1` | 1 | 14 `REGRESSION` lines, all for test or source files this change does not touch. `PluginPortsMigrationTest.cpp` grew 2787 → 2806 lines and is not flagged. |

Baseline comparison: I reverted the three changed files (`git apply -R` on a saved full patch), re-ran both gates (`gate-3-complexity-base.log` EXIT=1, `gate-4-filelength-base.log` EXIT=1), and re-applied the patch. The regression lines are the same 6 + 14 entries with and without this change. So the gates were already red on this line and this fix does not add to them. I did not re-anchor or edit any gate or baseline.

## Test-weakening check

The `migratedPluginsPreserveBehaviour` change replaces the exact checksum comparison for `gigplayer` with an exact 2× comparison against the frozen reference. This is required because the frozen reference embodies the old bug; the checksum equality cannot hold after the fix. The new check is still sample-exact (`1e-6` tolerance), and it fails on the unfixed code (RED above).

## Observation, not changed

`GigInstrument::loadSample()`'s 24-bit branch (`plugins/GigPlayer/GigPlayer.cpp:703` and `:716`) divides a 24-bit value packed into the top bits of an `int32_t` by `0x100000000`. By reading the code, full-scale 24-bit input comes out near 0.5, which looks like the same half-amplitude issue. I did not measure it and it is not in this finding, so I left it alone. It needs a decision from the owner (or a separate finding with a render check) before anyone changes it.

## Files in this commit

- `plugins/GigPlayer/GigPlayer.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-155-port.md`
