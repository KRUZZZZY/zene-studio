FIXED

# Cluster new-p12-80: `WaveSample::copyFrom` trusts the destination length for the source

Finding (Luna patrol, historical line 81): "copyFrom copies `m_length` samples from `graph->samples()` without checking the graph's current length."

## Verdict and evidence

**FIXED (latent).** The finding's line is wrong, and the bug itself is real.

- The finding's `plugins/Xpressive/ExprSynth.h:81` is the `WaveSample(const graphModel*)` constructor (`ExprSynth.h:78-83`). It sizes the table from `graph->length()` and copies that many samples, so it is consistent and not the bug.
- The bug is `WaveSample::copyFrom`, which was at `plugins/Xpressive/ExprSynth.cpp:718-725` on the base. Before the fix it did `memcpy(m_samples, graph->samples(), m_length * sizeof(float))`: it read the destination's length `m_length` from a source whose own length may be smaller. The graph's buffer is only guaranteed to hold `graph->length()` values (`Graph.cpp:478-486` sizes `m_samples` from the length; `Graph.cpp:503-515` `setLength` shrinks the logical length but keeps the buffer; `Graph.cpp:527-532` `setSamples` writes `length()` values).
- Reachability in production today: none of the Xpressive graphs or tables change length. `Xpressive.cpp:87-90` builds the graphs and `Xpressive.cpp:106-108` builds the tables all with `GRAPH_LENGTH`. No `setLength` call targets an Xpressive graph; the `setLength` callers are `DynamicsProcessorControls.cpp:132`, `WaveShaperControls.cpp:114`, `BitInvader.cpp:193-210` and `MultitapEchoControls.cpp:166-168`. The mismatch branch is therefore defensive for now, and the regression test is what proves the contract.

### Why the table is not reallocated

A resize inside `copyFrom` looked like the obvious fix, but it is unsafe. The `ExprSynth` constructor hands each expression a raw pointer and length per note (`ExprSynth.cpp:752-754`, `add_cyclic_vector("W1", m_W1->m_samples, m_W1->m_length, ...)`). A reallocation would free a buffer that a playing note still reads on the audio thread. So the table keeps its pointer and length for its lifetime, and the copy adapts to the graph inside the table.

### The fix

`plugins/Xpressive/ExprSynth.cpp:719-731`, under the existing `requestChangesGuard`:

- copy `min(m_length, graph->length())` samples, so the copy never reads past the graph's own length;
- zero-fill the rest of the table, so no stale values survive a copy from a shorter graph;
- the table pointer and length do not change.

A `<algorithm>` include was added (`ExprSynth.cpp:28`). No header, member, or layout change, so `PluginPortsMigrationTest` offsets are unaffected.

Design choices (for redirection): a shorter graph zero-fills the tail rather than refusing the copy, because refusing would silently drop the user's GUI edit. A longer graph truncates, which is what the old code did for that case.

## Regression tests

`tests/src/plugins/ExprSynthTest.cpp` (class `ExprSynthTimeTest`, existing ctest `ExprSynthTest`):

- `waveTableCopyFromShorterGraphStaysInGraphSamples` (lines 268-296): an 8-sample graph is shrunk to length 2 with `setLength(2)`; its buffer still holds `0.9` past index 2. The table of length 8 is pre-filled with `0.25`, then `copyFrom` runs. The test requires `m_length == 8`, indices 0-1 equal the graph's new samples, and indices 2-7 equal `0`. Without the fix, indices 2-7 take the stale `0.9`.
- `waveTableCopyFromLongerGraphKeepsTableLength` (lines 298-312): a longer graph; the table keeps length 4 and takes the first 4 samples. Passes before and after the fix; it guards the truncation path.

Determinism: no clock, sleep, thread or race. The test uses only the synchronous `graphModel::setSamples` and `setLength`, and the table's public `m_samples` and `m_length`. The unfixed code's stale read is within the graph's 8-value buffer, so the failure is a deterministic logical over-read, not dependent on undefined behaviour.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-80.r1/logs`.

| # | Command (run in the worktree unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target ExprSynthTest -j4 > .../build-1-red.log 2>&1` | 2 | ccache could not create its temp file under `zene-bunny-fix/dsh/...` (Permission denied). Build infrastructure, not code. |
| 2 | `CCACHE_TEMPDIR=<run>/ccache-tmp cmake --build build --target ExprSynthTest -j4 > .../build-2-red.log 2>&1` | 0 | Test compiled with the RED tests and the unfixed product code. |
| 3 | `cd build/tests && ctest -R '^ExprSynthTest$' --output-on-failure > .../test-1-red.log 2>&1` | **8 (RED)** | `FAIL! ... waveTableCopyFromShorterGraphStaysInGraphSamples() 'wave.m_samples[i] == 0.0f' returned FALSE. (table sample 2 was copied from past the graph's length)`; `Totals: 7 passed, 1 failed` |
| 4 | `CCACHE_TEMPDIR=... cmake --build build --target ExprSynthTest -j4 > .../build-3-green.log 2>&1` | 0 | With the fix. |
| 5 | `cd build/tests && ctest -R '^ExprSynthTest$' --output-on-failure > .../test-2-green.log 2>&1` | **0 (GREEN)** | `100% tests passed out of 1` (the family runs 8 slots in one ctest entry, all passing) |
| 6 | 4b: `git diff -- plugins/Xpressive/ExprSynth.cpp > .../fix.patch` | 0 | |
| 7 | 4b: `git apply -R .../fix.patch` | 0 | |
| 8 | 4b: `cmake --build build --target ExprSynthTest -j4 > .../build-4-nofix.log 2>&1` (with `CCACHE_TEMPDIR`) | 0 | |
| 9 | 4b: `ctest -R '^ExprSynthTest$' ... > .../test-3-nofix.log` | **8 (must fail)** | same `FAIL!` line as RED; `7 passed, 1 failed` |
| 10 | 4b: `git apply .../fix.patch` | 0 | |
| 11 | 4b: rebuild as step 8 (`build-5-refix.log`) | 0 | |
| 12 | 4b: `ctest -R '^ExprSynthTest$' ... > .../test-4-refix.log` | **0 (must pass)** | `100% tests passed out of 1` |
| 13 | Family: `cmake --build build --target zene` (`build-7-zene.log`) | 0 | product `zene` rebuilt with the fixed `ExprSynth.cpp` |
| 14 | Family: `cmake --build build --target ExprSynthAllocationTest` (`build-8-alloc.log`) | 0 | |
| 15 | Family: `cd build/tests && ctest -R '^(ExprSynthTest\|ExprSynthAllocationTest)$' ... > .../test-6-family.log` | 0 | `100% tests passed out of 2` |
| 16 | `cmake --build build --target zene ExprSynthAllocationTest ExprSynth -j4` (`build-6-family.log`) | 2 | `No rule to make target 'ExprSynth'`: there is no `ExprSynth` target. `ctest -N` shows no `^ExprSynth$` ctest in this tree, only `ExprSynthTest` (#582) and `ExprSynthAllocationTest` (#586). The family for that name does not exist here; the two real families were run as steps 13-15. |
| 17 | Gate: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`gate-1-feature-check.log`) | **1** | `PermissionError` creating `<worktree-parent>/zene-qwen-new-p12-80-feature-check`, which is outside the write jail. Not a gate result. |
| 18 | Gate, same checks: `python3 -I .../runs/new-p12-80.r1/gate-driver.py <worktree> <logs>/gate-feature-check > gate-2-feature-check.table` | **0** | The driver loads `feature_check.py` and repeats its `main()` with the log folder moved into the run folder. The commands are the same. Table below. |

Gate table (step 18):

| Gate | Result |
| --- | --- |
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS (no new item in your files) |
| file-length | PASS (no new item in your files) |
| **RESULT** | **PASS** |

Summary for the evidence table: RED = step 3, exit 8; GREEN = step 5, exit 0; 4b without fix = step 9, exit 8; 4b with fix = step 12, exit 0; affected family = 2 ctest entries (`ExprSynthTest` with 8 slots, `ExprSynthAllocationTest`), 2/2 passed; gates = step 18 exit 0 (step 17 exit 1 is the jail-blocked log folder, not a gate failure).

Files committed: `plugins/Xpressive/ExprSynth.cpp`, `tests/src/plugins/ExprSynthTest.cpp`, `tests/upstream-modifications.txt` (inherited-file registration for the `ExprSynth.cpp` change), `docs/qwen-fixes-2026-10/cluster-new-p12-80.md`. `ExprSynthTest.cpp` is already listed in `tests/fork-sources.txt:2673`, so it needed no new entry.

Not changed: `tests/reference/Xpressive/ExprSynth.h` has its own inline `copyFrom` with the same unchecked `memcpy`. The tree's CMake has no target built from it (no `reference/Xpressive` entry in `tests/CMakeLists.txt`, and no ExprSynth ctest), so it is not product code and is left alone.

## Self-check

1. **Regression FAILS with the product change reverted and PASSES with it:** step 3 (RED, exit 8, before the fix was written) and step 9 (reverted via `git apply -R`, exit 8), against step 5 and step 12 (fix applied, exit 0). The `FAIL!` line is the same in both reverted runs.
2. **Deterministic:** no wall clock, sleep, thread or race in the new slots. The test uses synchronous `graphModel::setSamples` and `setLength`, and the table's public `m_samples` and `m_length`. The graph buffer is 8 values and the table is 8, so the old code's stale read stays within bounds, and the failure is a logical over-read, not undefined behaviour.
3. **Fix is at the cause and complete:** the only writer of table samples is `copyFrom`. All production callers (`Xpressive.cpp:206-208` load, `698-704` smoothChanged, `717-723` graphDrawn) use Xpressive graphs and tables, which are all `GRAPH_LENGTH`, so the copy stays equal-length on every reachable path. The mismatch branch is defensive (no `setLength` call targets an Xpressive graph; see the reachability note above). The per-note captured pointer and length (`ExprSynth.cpp:752-754`) is why the table length is not changed. The header's graph constructor (`ExprSynth.h:78-83`) was checked and is consistent.
4. **No collateral damage:** equal-length copies produce the same bytes as before (the fill range is empty). Longer graphs truncate as before. Only a shorter graph's tail changes, from stale graph data to zero. No save-format or header change, and no new limit or check, so no caller can be rejected. Saved Xpressive projects load as before, since the graph and table lengths are unchanged.
5. **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE:** the bug is real in the code as written. The finding's line number is the wrong place, which is noted above.
6. **Realtime, undo, commands:** `copyFrom` runs on the GUI or load thread under the existing change guard. The change adds `std::min` and `std::fill` only, no allocation, no logging, no new control command, and no undo change.
7. **Gate-tool limitation (open):** the official `feature_check.py` and `submit_check.py` write their logs to `<worktree>/../<name>-feature-check`, outside the write jail, so they cannot finish from this lane (step 17 exit 1). The same checks were run from the run folder (step 18, exit 0). The owner should run `feature_check.py` once from a writable location to confirm.
