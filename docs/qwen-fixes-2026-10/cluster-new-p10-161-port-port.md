# Cluster new-p10-161-port-port

Verdict: **FIXED**

`GigInstrument::addSamples()` now accumulates the release-trigger flag across regions with logical OR instead of overwriting it, so a later region without a release-trigger dimension cannot cancel release samples established by an earlier region.

## Evidence

| File | Evidence |
| --- | --- |
| `plugins/GigPlayer/GigPlayer.cpp:857` | `gignote.release = gignote.release || dim.release;` retains the flag across regions. |
| `tests/src/plugins/PluginPortsMigrationTest.cpp` | New slot `gigPlayerKeepsReleaseTriggerAcrossRegions()` renders a note on the release-trigger fixture and asserts a nonzero release mean. |
| `tests/upstream-modifications.txt` | Records the inherited production change and regression. |
| `tests/data/gigproof/sine440.gig` | Updated fixture (from commit d7261f3d3) with a release-trigger dimension in the first region and a later non-release region. |
| `tests/data/gigproof/make_gig.cpp` | Merged generator preserving ping-pong/keyboard fixtures plus the release-trigger fixture. |
| `tests/data/gigproof/README.md` | Updated provenance and hash (5382e8b48923d6154c6a7f9b43dc997cdbd27af2ae254b63951b3eed55fc4c11). |

## Verification

Test and build output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-161-port-port.r1/logs`.

| Check | Command | Exit |
| --- | --- | ---: |
| Initial RED before the product fix (`test-revert.log`) | `QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest gigPlayerKeepsReleaseTriggerAcrossRegions` (from `build/tests`) | 1 |
| Final GREEN (`test-reapply.log`) | same | 0 |
| Reverse-patch RED (`test-revert.log`) | same, with `fix.patch` applied in reverse | 1 |
| Re-apply GREEN (`test-reapply.log`) | same, with `fix.patch` restored | 0 |
| Affected family (`test-family.log`) | `QT_QPA_PLATFORM=offscreen ctest -R 'PluginPortsMigrationTest'` (from `build/tests`) | 8 (one pre-existing unrelated LADSPA failure: `nonRealtimeLadspaEffectsAreInvalid`) |
| Build (`build-2.log`) | `cmake --build build --target PluginPortsMigrationTest gigplayer` | 0 |
| Build zene (`build-zene.log`) | `cmake --build build --target zene` | 0 |
| Feature check (`gate.log`) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py ...` | 0 |

Reverse-patch proof: `git diff -- plugins/GigPlayer/GigPlayer.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` (exit 0); `git apply .../logs/fix.patch` (exit 0).

## Self-check

- Regression RED/GREEN: RED `test-revert.log` exit 1 (assertion `releaseMean > 0.05` false); GREEN `test-reapply.log` exit 0. Reverse-patch RED exit 1, re-apply GREEN exit 0.
- Determinism: no wall-clock, sleeps or races; the test uses the vendored `sine440.gig` fixture with a release-trigger dimension and renders a fixed `frames * 2` note plus one period, reading the deterministic release probe mean.
- Cause and completeness: the only overwrite site is `plugins/GigPlayer/GigPlayer.cpp:857` inside `GigInstrument::addSamples()`; `gignote.release` starts `false` in the `GigNote` constructor and is now OR-accumulated across every region in the `while (pRegion != nullptr)` loop; the `wantReleaseSample != true` guard is preserved, and `getDimensions()` sets `dim.release` per region. No other caller mutates the flag.
- Collateral damage: no new commands, limits or allocations; saved projects load unchanged because only flag accumulation changed. The fixture change keeps the normal (KeyDown) path on the first region's zone 0 sine sample, so the slice-8 sample-exact comparison still passes at the 2x scale.

## Files committed

- `plugins/GigPlayer/GigPlayer.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/data/gigproof/sine440.gig`
- `tests/data/gigproof/make_gig.cpp`
- `tests/data/gigproof/README.md`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-161-port-port.md`
