# Cluster new-p9-394-port: malformed routing graph output

**Verdict: ALREADY-FIXED** (production code). This commit adds the regression coverage that the port job was missing.

## What the port was supposed to bring

Source commit `7312fe5d1` ("fix: validate routing graph output", on `bugs/qwen-new-p9-394`) added a file-local `loadOutput()` so that `RoutingGraph::load()` rejects a missing or nonnumeric `output` attribute instead of reading it as `0`. It is not an ancestor of this branch (`git merge-base --is-ancestor 7312fe5d1 HEAD` → not an ancestor).

## Evidence that the newest line already has the fix

The newest line carries the same behaviour from commit `03169b9b6` ("fix(routing-graph): a malformed graph connection number silently became zero", Luna cluster 5293, 2026-10-06). That commit added `applyOutputNode()` in the same place the old fix used `loadOutput()`:

- `src/core/RoutingGraph.cpp:141-150`: `applyOutputNode()` reads the attribute with `toInt(&parsed)`, returns `false` when `parsed` is false (line 145), keeps an explicit `-1` as "no output" (line 146), and rejects an id missing from `idMap` (line 147).
- `src/core/RoutingGraph.cpp:376`: `load()` calls `applyOutputNode()` before moving the loaded graph into place.
- `git blame -L 140,150 src/core/RoutingGraph.cpp` attributes lines 140-150 to `03169b9b6`.

So a missing or nonnumeric `output` already fails to load, and an explicit `-1` still loads with no output. Nothing in production code needed changing.

## What was missing: regression coverage

The newest line's `RoutingGraphTest::LoadMalformedDocuments` had no case for the `output` attribute itself (only for `output="9"` and connection attributes), so nothing guarded this behaviour. This commit adds:

- `tests/src/core/RoutingGraphTest.cpp:470-474`: missing `output` attribute, must not load (node 0 exists).
- `tests/src/core/RoutingGraphTest.cpp:475-479`: `output="invalid"`, must not load.
- `tests/src/core/RoutingGraphTest.cpp:525-543`: explicit `output="-1"` loads with no output; explicit `output="0"` still selects node 0.

No production file, header, or new test file changed, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are untouched.

## RED / GREEN proof

The production code already has the fix, so the usual "revert the fix commit" step does not apply. Instead the RED state was produced by a temporary mutation that removes the parse check, reproducing the pre-fix behaviour where `toInt()` turned a missing or invalid `output` into `0`:

- Mutation: delete `src/core/RoutingGraph.cpp:145` (`if (!parsed) { return false; }`). Patch saved as `logs/mutation-revert-parse-check.patch`. Pristine copy saved as `logs/RoutingGraph.cpp.pristine`.
- The mutation was reversed with `git apply -R` and `cmp` confirmed the file matches the pristine copy. `git diff -- src/core/RoutingGraph.cpp` is empty.

## Evidence table

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-394-port.r1/logs/`.

| Step | Exact command | Exit |
|---|---|---:|
| Build with mutation, attempt 1 (ccache could not write `/run/user/1000/ccache-tmp`, sandbox permission) | `cmake --build build --target zene RoutingGraphTest -j2 > build-1.log 2>&1; echo EXIT=$?` | 2 |
| Build with mutation, attempt 2 (ccache temp dir redirected to /tmp) | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp cmake --build build --target zene RoutingGraphTest -j2 > build-2.log 2>&1; echo EXIT=$?` | 0 |
| **RED**: test with mutation | `cd build/tests && ctest -R '^RoutingGraphTest$' --output-on-failure > test-1.log 2>&1; echo EXIT=$?` | **8** |
| RED assertion | `test-1.log:16-17`: `FAIL! : RoutingGraphTest::LoadMalformedDocuments() '!graph.load(document.documentElement())' returned FALSE.` at `tests/src/core/RoutingGraphTest.cpp:519` | - |
| Restore: reverse mutation patch | `git apply -R logs/mutation-revert-parse-check.patch` | 0 |
| Rebuild after restore | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp cmake --build build --target zene RoutingGraphTest -j2 > build-3.log 2>&1; echo EXIT=$?` | 0 |
| **GREEN**: test after restore | `cd build/tests && ctest -R '^RoutingGraphTest$' --output-on-failure > test-2.log 2>&1; echo EXIT=$?` | **0** (1/1 passed) |
| Family, first run (two binaries not built in this tree: "Not Run") | `cd build/tests && ctest -R '^RoutingGraph' --output-on-failure > test-3.log 2>&1; echo FAMILY_EXIT=$?` | 8 (1/3 passed; 2 Not Run) |
| Build the two missing family binaries | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp cmake --build build --target RoutingGraphLiveTest RoutingGraphScheduleTest -j2 > build-4.log 2>&1; echo BUILD_EXIT=$?` | 0 |
| **Affected family** (`RoutingGraphLiveTest`, `RoutingGraphScheduleTest`, `RoutingGraphTest`) | `cd build/tests && ctest -R '^RoutingGraph' --output-on-failure > test-4.log 2>&1; echo FAMILY_EXIT=$?` | **0** (3/3 passed) |
| Complexity ratchet | `bash tests/complexity-gate.sh --check > gate-1.log 2>&1; echo COMPLEXITY_EXIT=$?` | **1** (see below) |
| File-length ratchet | `bash tests/file-length-gate.sh --check > gate-2.log 2>&1; echo FILELENGTH_EXIT=$?` | **1** (see below) |

Test count: the affected family ran 3 tests (`RoutingGraphLiveTest`, `RoutingGraphScheduleTest`, `RoutingGraphTest`); the RED/GREEN target `RoutingGraphTest` ran 1 test.

## Gate failures (not caused by this commit)

Both ratchets exit 1 with `check mode: baseline not written`. Neither gate was re-anchored and no baseline was touched.

- Complexity: 6 `REGRESSION` lines, all in files this commit does not touch (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`). The only `RoutingGraph.cpp` entries (`connect`, `rebuildPlan`) are listed as grandfathered, not regressions. `RoutingGraphTest::LoadMalformedDocuments` does not appear in the output.
- File length: 14 `REGRESSION` lines for new files over 500 lines (`SmfInterchangeReader.cpp`, `TakeLane.cpp`, several `tests/src/core/*Test.cpp`, and others). `RoutingGraphTest.cpp` does not appear.

I did not run the gates against HEAD, so "pre-existing on the base line" is an inference from the file list, not a measured result. Fixing these is outside this single-bug port and would mean touching unrelated files, so they are left for the owner's gate work.

## Observations (not changed)

- `tests/src/core/RoutingGraphTest.cpp` is not listed in `tests/fork-sources.txt` on this line (the source commit `7312fe5d1` had added it). Left unchanged because the file is pre-existing here and this is outside the port.
- `ctest` against the build tree needed `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp` for builds; the default ccache temp dir under `/run/user/1000` is not writable in this sandbox.
