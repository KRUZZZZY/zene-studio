# Cluster new-p9-376 — FIXED

## Evidence

`RackZones::matchMask` now scans every zone and sets bits by matching chain. The layer rack has at most nine chains, so its note path can represent every matching layer regardless of zone count. `InstrumentLayers::layerMask` consumes those chain bits without allocating. See `include/RackZones.h:146`, `src/core/RackZones.cpp:153`, and `src/core/InstrumentLayers.cpp:282`.

The regression at `tests/src/core/InstrumentRoutingTest.cpp:210` creates 65 zones; only zone 64 matches key 80 and names chain 8. Before the fix, `zones.matchMask(80, 100)` returned 0 instead of 256 at line 217.

## Commands and results

| Check | Exact command | EXIT |
|---|---|---:|
| Baseline build (`build-2.log`) | `cmake --build build --target zene InstrumentRoutingTest` | 0 |
| RED on the original behavior (`test-3.log`; reproduced after reverse patch in `test-8.log`) | `ctest -R '^InstrumentRoutingTest$' --output-on-failure` | 8 |
| GREEN after restoring the fix (`test-9.log`) | `ctest -R '^InstrumentRoutingTest$' --output-on-failure` | 0 |
| Reverse the final product patch (`fix.patch`) | `git diff -- include/RackZones.h src/core/RackZones.cpp src/core/InstrumentLayers.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-376.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-376.r1/logs/fix.patch` | 0 |
| Rebuild with the fix removed (`build-8.log`) | `cmake --build build --target zene InstrumentRoutingTest` | 0 |
| Reapply the product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-376.r1/logs/fix.patch` | 0 |
| Rebuild with the fix restored (`build-9.log`) | `cmake --build build --target zene InstrumentRoutingTest` | 0 |
| Affected-family build (`build-10.log`) | `cmake --build build --target zene RackZonesTest RackZoneRoutingTest InstrumentRoutingTest InstrumentLayersCommandsTest` | 0 |
| Affected-family tests (`test-10.log`) | `ctest -R '^(RackZonesTest|RackZoneRoutingTest|InstrumentRoutingTest|InstrumentLayersCommandsTest)$' --output-on-failure` | 0; 4/4 CTest targets passed |
| Complexity gate (`gate-5.log`) | `bash tests/complexity-gate.sh --check` | 0 |
| File-length gate (`gate-6.log`) | `bash tests/file-length-gate.sh --check` | 0 |

The initial `InstrumentLayersCommandsTest` attempt exited 0 but skipped its layer command slot because this build exposes no loadable instrument (`test-2.log`); it was not used as RED evidence. `cmake --build build --target all --parallel 8` exited 97 because `bunny-guard` disallows the `all` target; the explicit affected-family build above succeeded.

## Files committed

- `include/RackZones.h`
- `src/core/RackZones.cpp`
- `src/core/InstrumentLayers.cpp`
- `tests/src/core/InstrumentRoutingTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-376.md`
