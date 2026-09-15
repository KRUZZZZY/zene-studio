# Lane 030/host-chunking-wasm — board task 657 (feature rows 82 and 73)

Worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/whost`,
base `release/0.3.0` @ `f611c888b`. Three commits, no merge, no push.

| commit | what |
|---|---|
| `2630a13ae` | `fix(host): process exactly the frames asked for, in chunks (CODE-4, feature row 82)` |
| `2b8a7ac0a` | `feat(wasm): one shared lane pool, real wake-ups, deterministic offline render (CODE-5, feature row 73)` |
| `cf3ec68ce` | `docs(limits): the CODE-4 and CODE-5 rows get their limitation lines, and the ledgers their entries` |

## What was measured, with the commands and their exit codes

Every command below was run in this worktree; the exit code is the command's own, unpiped.

### 1. Configure (the reference box's flags, plus the SDK/CLAP checkouts this box has)

```
$ cmake -S . -B build -DWANT_QT6=ON -DWANT_VST3=ON -DWANT_CLAP=ON \
    -DWANT_VST3_TEST_INSTRUMENT=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DUSE_COMPILE_CACHE=ON -DUSE_WERROR=OFF \
    -DLMMS_VST3_SDK_PATH=.../zene-030/build/vst3sdk -DLMMS_CLAP_PATH=.../zene-030/build/clap
CONFIGURE_EXIT=0            # "* WASM DSP sandbox : Enabled" (wasmtime fetched by
                            #  scripts/fetch-wasmtime.sh, FETCH_EXIT=0)
```

### 2. CODE-4 — the chunking contract, green

```
$ cmake --build build --target ClapHostTest Vst3HostTest Vst3ChunkProbeTest -j 2
BUILD_EXIT=0
$ cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R \
    "Vst3ChunkProbeTest|ClapHostTest|Vst3HostTest|WasmWorkerPoolTest"
CTEST_EXIT=0    # 100% tests passed, 0 tests failed out of 4
```

The key lines, from the tests' own output:

```
QINFO Vst3ChunkProbeTest::testChunkedProcessing() MEASURED chunk sequence for a 1061-frame
      request into a 512-frame block: 512, 512, 37
QINFO Vst3ChunkProbeTest::testChunkedProcessing() MEASURED same 1061 frames: one instance
      asked for 512-frame blocks and one for 1061-frame blocks, identical output
QINFO Vst3ChunkProbeTest::testChannelWithoutACallerBufferOverAFullRequest() MEASURED 641
      frames through 2 declared channels with one supplied: 2 blocks, no oversized call
QINFO ClapHostTest::testChunkedProcessing() MEASURED chunk sequence for a 1061-frame request
      into a 512-frame block: 512, 512, 37
```

### 3. CODE-4 — the red half (the chunk loop removed, rebuilt, restored)

With the chunk loop replaced by one call (the pre-fix behaviour) and nothing else changed:

```
$ ./Vst3ChunkProbeTest
FAIL!  : Vst3ChunkProbeTest::testChunkedProcessing() Compared values are not the same
   Actual   (state.blocks - beforeState.blocks): 1
   Expected (std::int32_t{3})                  : 3
FAIL!  : Vst3ChunkProbeTest::testChannelWithoutACallerBufferOverAFullRequest()
         'output[frame] == 0.5f' returned FALSE. (frame 0 of the caller's channel holds 0.25, not 0.5)
Totals: 4 passed, 2 failed, 0 skipped, 0 blacklisted, 7ms
free(): invalid size
Received signal 6 (SIGABRT)        # the over-run in the allocator
TEST_EXIT=134
```

Run alone, the second case aborts with `double free or corruption (!prev)`. Restoring the chunk loop:
`BUILD_EXIT=0`, `VST3_EXIT=0`, `Totals: 6 passed, 0 failed`.

### 3b. The regression this lane introduced and fixed (found by the parent's own bar)

`Vst3InstrumentTest::testDistinctInputGivesDistinctOutput` failed on the first version of the chunk
loop, and passed with the pre-fix host (checked by reverting `Vst3Host.{h,cpp}` to `2630a13ae~1` and
rebuilding: `PRE_FIX_TEST_EXIT=0`, `Totals: 11 passed`). Cause: the chunk slicer dropped MIDI events
whose sample offset is at or beyond the end of the request, where the pre-chunking host clamped them
into the last sample (`std::clamp(frameOffset, 0, frames)`) - a note-off written at the block boundary
stopped releasing the note. Fixed in `f84f4d168` by delivering those events with the LAST chunk at that
chunk's end offset, the same behaviour the clamp had; the rule is now written in
`Vst3Host.h`'s over-run/tail rule. After the fix, from the same build tree:
`Vst3InstrumentTest EXIT=0 (11 passed)`, `Vst3HostTest EXIT=0 (9 passed)`,
`Vst3ChunkProbeTest EXIT=0 (6 passed)`, `ClapHostTest EXIT=0 (12 passed)`,
`WasmWorkerPoolTest EXIT=0 (7 passed)`.

### 4. CODE-5 — the pool, the wake-ups and the determinism verdict, green

```
$ ./WasmWorkerPoolTest
MEASURED 3 hosted workers on 8 shared lane(s) (pool bound 8)
MEASURED wake-ups 5 (suppressed 0), parks 47, blocks 5
MEASURED floor (same build, run to run, inline control pair): 0 of 8192 frames differ, max |d| 0;
         subject (through the pool): 0 of 8192 frames differ, max |d| 0
MEASURED 0.25-scale vs 0.75-scale stimulus: 8160 frames differ, max |d| 0.249023
Totals: 7 passed, 0 failed, 0 skipped, 0 blacklisted, 213ms
TEST_EXIT=0
```

The determinism verdict is a measured comparison: the floor comes from two inline (no-pool) renders
in the same call, the subject from the pooled renders, and `deterministic` is the subject within the
floor in both differing frames and largest absolute difference. `docs/RENDER-DETERMINISM.md` is the
recorded contract this follows; no hash identity and no fixed threshold is used anywhere.

### 5. The core translation units compile

Compiled individually through `make -f src/CMakeFiles/lmmsobjs.dir/build.make …` (the whole core is
not built in this lane — see "not verified"):

```
core/PluginHostChunking.cpp                    EXIT=0
core/ControlCommandsHostChunking.cpp           EXIT=0
core/ControlReversibilityTableHostChunking.cpp EXIT=0
core/ControlCommandsWasmRender.cpp             EXIT=0
core/ControlReversibilityTableWasmRender.cpp   EXIT=0
core/ControlReversibilityTable.cpp             EXIT=0
core/ControlRegistryRegistrations.cpp          EXIT=0
core/ControlCommandsWasm.cpp                   EXIT=0
wasm/WasmWorker.cpp                            EXIT=0
wasm/WasmWorkerPool.cpp                        EXIT=0
wasm/WasmOfflineRender.cpp                     EXIT=0
```

### 6. Gates (all unpiped, run on the final tree, after `1771d2bfd`)

| gate | exit | result |
|---|---|---|
| `tests/file-length-gate.sh --check` | **1** | REGRESSION: `plugins/Vst3Effect/Vst3Host.cpp` grew 800 → 891 lines; `plugins/ClapEffect/ClapHost.cpp` 953 → 1001. Ratchet re-anchor NOT done (owner directive for this pass). |
| `tests/complexity-gate.sh --check` | **1** | three new functions over the target — `lmms::wasm::renderOffline` CCN 22, the fixture's `ChunkProbe::process` CCN 21, `lmms::vst3::HostedPlugin::Impl::runChunk` CCN 14 — plus `wasm::WasmWorker::run`, whose baseline entry is now dead weight because the polling loop it measured is gone. Baseline NOT edited (owner directive); splitting those functions is the non-ratchet fix a later pass should take. |
| `tests/no-tautology-gate.sh` | 0 | PASS |
| `tests/duplication-gate.sh` | 0 | PASS (0.31 %, budget 5 %) |
| `tests/fork-sources-gate.sh` | 0 | PASS — 446 fork-NEW, 1060 inherited, 0 stale |
| `tests/no-upstream-regression-gate.sh` | 0 | PASS — every change to upstream-inherited code declared |

## What is NOT verified in this lane

* **No full build, no full `ctest`, no `tests/run-all-gates.sh`, no live `--control-socket`
  transcript.** The core is not built in this worktree, so `plugin.host_chunking`, `wasm.pool` and
  `wasm.render_offline` have been proven to *compile* and their rows/registration to *link*, but no
  running instance has answered them. The three ids need a live-instance snapshot refresh in
  `tools/mcp-zene-control/zene_control/commands_snapshot.json` (derived file — not hand-edited here).
* **No third-party plug-in has ever been hosted** — the CODE-4 proof is against the in-tree MIT
  fixtures by construction (none is installable on this box).
* The `wasm.pool`/`wasm.render_offline` A16 rows are guarded by `#ifdef LMMS_HAVE_WASM`; a build
  without wasmtime compiles an empty table, which was compile-checked with the macro defined only.
