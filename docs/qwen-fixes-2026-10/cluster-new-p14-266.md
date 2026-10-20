# Cluster new-p14-266 — Invalid negative routing outputs accepted as "no output"

Verdict: **FIXED** (branch `bugs/qwen-new-p14-266`, base `050/m0-train-8`).

## Evidence

The finding (BUG 5, `src/core/RoutingGraph.cpp:146` in the historical tree) is real on the
current branch. The loader's output-node step accepted every negative integer as the saved
`-1` "none selected" sentinel:

- Before the fix, `src/core/RoutingGraph.cpp:146` was `if (output < 0) { return true; }`, so
  `output="-2"` passed validation and the staged graph was committed with no output node
  (`RoutingGraph::load`, `src/core/RoutingGraph.cpp:363-380`, commit at `:378`).
- The fix (`src/core/RoutingGraph.cpp:147-148`):

  ```cpp
  if (output == -1) { return true; }
  if (output < 0 || !idMap.contains(output)) { return false; }
  ```

  Only `-1` is the no-output sentinel. Every other negative value is refused, and a
  refused document leaves the target graph unchanged because `load()` commits only at `:378`.
- The explicit `output < 0` in the second line is deliberate. `loadNodes()`
  (`src/core/RoutingGraph.cpp:84`) accepts any integer node `id`, so `idMap` can
  hold a negative key. A bare `idMap.contains(output)` check would let `output="-2"`
  select a node that a malformed file gave `id="-2"`. Writers only save `-1` or
  non-negative ids (`src/core/RoutingGraph.cpp:336`, `m_outputNodeId`), so refusing all
  other negatives changes no saved project.
- `setOutputNode()` (`src/core/RoutingGraph.cpp:277-282`) already refuses unknown ids
  through `node(id)`, so the loader was the only path that let a negative through.
- Control-thread XML path only. Nothing in the audio path changes.

Regression test: `tests/src/core/RoutingGraphTest.cpp:470-475` adds an `output="-2"`
document to the rejected list of `LoadMalformedDocuments()`. The existing `-1` (valid
no-output) and `0` (valid node id zero) checks are unchanged (`:531-549`).

## Commands and results

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-266.r1/logs`.

Environment notes (not product results):
- The first build attempt failed with `ccache: failed to create temporary file for
  /run/user/1000/ccache-tmp/...: Permission denied` (`build-red-1.log`, EXIT=2). Every later
  build used `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14-266` so ccache can write its temp files.
  Make also printed `jobserver unavailable: using -j1`; builds ran serially, within -j4.
- `feature_check.py` could not run here: it creates `<worktree>-feature-check/` next to the
  worktree (`PermissionError`, the parent directory is outside the write-jail) and caches
  baselines under the qwen directory. Its gate commands were run one by one from the
  worktree instead (see gates below).

### RED (regression before the fix)

| Step | Command | EXIT |
| --- | --- | --- |
| Test edited, product fix not yet applied | `cmake --build build --target RoutingGraphTest -j4 > build-red-2.log` (with `CCACHE_TEMPDIR`) | 0 |
| RED | `cd build/tests && ctest -R '^RoutingGraphTest$' --output-on-failure > test-red-1.log` | **8** |

RED output (`test-red-1.log`): `FAIL! : RoutingGraphTest::LoadMalformedDocuments() '!graph.load(document.documentElement())' returned FALSE.` at `RoutingGraphTest.cpp(525)`. `load()` accepted the new `output="-2"` document. The earlier documents were all refused, so the failing one is the new `-2` case.

### GREEN (with the fix)

| Step | Command | EXIT |
| --- | --- | --- |
| Build zene + test | `cmake --build build --target zene RoutingGraphTest -j4 > build-green-1.log` | 0 |
| Family run, siblings not yet built (`Not Run`) | `cd build/tests && ctest -R '^RoutingGraph' ... > test-green-1.log` | 8 (not a result: 2 of 3 not built) |
| Build siblings | `cmake --build build --target RoutingGraphLiveTest RoutingGraphScheduleTest -j4 > build-green-2.log` | 0 |
| GREEN, family | `cd build/tests && ctest -R '^RoutingGraph' --output-on-failure > test-green-2.log` | **0** |

`test-green-2.log`: `RoutingGraphLiveTest Passed`, `RoutingGraphScheduleTest Passed`,
`RoutingGraphTest Passed`; `100% tests passed out of 3`. Affected family: **3 tests executed,
3 passed.**

### Step 4b (the test catches the bug, both directions)

| Step | Command | EXIT |
| --- | --- | --- |
| Save product diff | `git diff -- src/core/RoutingGraph.cpp > logs/fix.patch` | 0 |
| Revert product fix | `git apply -R logs/fix.patch` | 0 |
| Rebuild | `cmake --build build --target RoutingGraphTest -j4 > build-4b-revert.log` | 0 |
| Must FAIL | `cd build/tests && ctest -R '^RoutingGraphTest$' ... > test-4b-revert-RED.log` | **8** (`RoutingGraphTest.cpp(525)`) |
| Re-apply fix | `git apply logs/fix.patch` | 0 |
| Rebuild | `cmake --build build --target RoutingGraphTest -j4 > build-4b-fix.log` | 0 |
| Must PASS | `cd build/tests && ctest -R '^RoutingGraphTest$' ... > test-4b-fix-GREEN.log` | **0** |

### Gates (feature_check.py commands, run one by one from the worktree)

Logs: `logs/gate-<name>.log`.

| Gate | Command | EXIT | Result |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | PASS |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | PASS |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | PASS |
| check-namespace | `tests/scripted/check-namespace` | 0 | PASS |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | PASS |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | PASS |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | 6 `REGRESSION` lines, none in a changed file: `dotCentreAlong`, `parseNotes`, `adjust`, `setStrip`, `rotateProjectRevision`, `importedOverrides`. The `RoutingGraph.cpp` rows (`connect` CCN 15, `rebuildPlan` CCN 15) are in the table, not regressions. **PASS by the feature-check rule.** |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | 15 `REGRESSION` lines, none in a changed file (`RoutingGraph.cpp` and `RoutingGraphTest.cpp` are not listed). **PASS by the feature-check rule.** |

The changed files are `src/core/RoutingGraph.cpp` and `tests/src/core/RoutingGraphTest.cpp`.
Neither is a new file, so no `tests/fork-sources.txt` entry is needed.
`RoutingGraph.cpp` was added by the fork (`fadd04a83`), so it is not an upstream file and
needs no `tests/upstream-modifications.txt` entry.

## Not changed (noticed, out of scope)

`loadNodes()` (`src/core/RoutingGraph.cpp:84`) accepts a negative node `id`. Writers never
produce one, and the finding does not ask for it. It is a separate defect, so it is not
changed here. The output check now refuses a negative `output` either way.

## Files committed

- `src/core/RoutingGraph.cpp` (product fix, `applyOutputNode`)
- `tests/src/core/RoutingGraphTest.cpp` (regression document in `LoadMalformedDocuments`)
- `docs/qwen-fixes-2026-10/cluster-new-p14-266.md` (this report)

## Self-check

- **Regression fails with the product change reverted, passes with it (exit codes).** Yes.
  RED: `test-red-1.log` EXIT=8, `RoutingGraphTest.cpp(525)`. Revert proof: `test-4b-revert-RED.log`
  EXIT=8 at the same line, after `git apply -R logs/fix.patch`. With the fix re-applied,
  `test-4b-fix-GREEN.log` EXIT=0 (1/1 passed). Family `^RoutingGraph`: EXIT=0, 3/3 passed.
- **Deterministic (no timing, sleeps or races; seam or injected values).** Yes. The test
  parses fixed XML byte literals (`RoutingGraphTest.cpp:470-475`) into a `QDomDocument`
  and calls `RoutingGraph::load()`. No timer, sleep, thread or seam. The fix is
  control-thread parsing only.
- **Fix at the cause; every caller and sibling checked.** Yes.
  - The only path into `applyOutputNode()` is `RoutingGraph::load()` (`src/core/RoutingGraph.cpp:364-380`, call at `:377`).
  - `rg` finds no production caller of `RoutingGraph::load` under `src/` or `include/`. Its callers are the tests (`RoutingGraphTest.cpp:351, 357, 525, 539, 548`).
  - `setOutputNode()` (`src/core/RoutingGraph.cpp:277-282`) already refuses unknown ids through `node(id)`, so the loader was the only path that let a negative through.
  - The saver writes only `m_outputNodeId` (`src/core/RoutingGraph.cpp:337`), which is `-1` or a valid id, so the saved form never contains another negative.
  - Sibling not changed: `loadNodes()` (`src/core/RoutingGraph.cpp:84`) accepts a negative node `id`. This is a separate defect outside the finding. Noted above, not changed.
- **No collateral damage.** Yes. Saved documents contain `output="-1"` or a non-negative
  id, and both still load exactly as before. The `-1` and `0` cases are kept
  (`RoutingGraphTest.cpp:531-549`). No limit or check was added beyond refusing negative
  values other than `-1`.
- **Verdict evidence (ALREADY-FIXED / FALSE-POSITIVE).** Not applicable. The verdict is FIXED.
- **Gates (feature_check.py): not a clean run.** `feature_check.py` crashed before running any
  gate with `PermissionError` on `…/zene-qwen-new-p14-266-feature-check` (`logs/gates-1.log`).
  It writes its logs next to the worktree, outside the write-jail. Its gate commands were run
  one by one from the worktree instead (table above): six PASS, complexity and file-length
  EXIT=1, with no `REGRESSION` line in a changed file, so both pass by the feature-check rule.
  `submit_check.py` reports the gates row FAIL for the same crash (`logs/submit-check-1.log`).
  The owner or the fix loop should re-run `feature_check.py` where it can write its logs.
