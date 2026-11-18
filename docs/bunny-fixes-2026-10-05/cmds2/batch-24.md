# cmds2 batch-24 — WASM control group (5 findings)

Lane: `ControlCommandsWasm*.cpp`. Verdict counts: 3 FALSE-POSITIVE, 2 REAL-UNTESTED,
0 fixed, 0 duplicates.

THE BUILD TREE HAS NO WASM IN IT, which decided this batch. `build/CMakeCache.txt`
records `WASMTIME_ROOT:PATH=` empty and `Wasmtime_LIBRARY:FILEPATH=Wasmtime_LIBRARY-NOTFOUND`,
so `CMakeLists.txt:1015-1021` turned `WANT_WASM` off at configure time, `src/core/CMakeLists.txt:89`
never appended the four `core/ControlCommandsWasm*.cpp` translation units, and
`tests/CMakeLists.txt:3237` never registered `ControlWasmSandbox` / `ControlWasmDevice`.
Confirmed from the artefacts: `ctest -N` lists no WASM test, `strings build/zene` contains
zero occurrences of `wasm`, and there is no `ControlCommandsWasmEdit.cpp.o` anywhere under
`build/`. The `wasm.*` commands therefore do not exist in the binary these findings are about.
No deterministic failing test can be produced here, so per the lane procedure no product
code was touched for the two real findings.

That said, three findings are decided outright by the schema layer, which is compiled
unconditionally, so the verdicts below stand on their own.

### Cluster 4950 — Negative WASM fuel can overflow during conversion
Verdict: FALSE-POSITIVE
Evidence: `src/core/ControlCommandsWasmEdit.cpp:245` declares `fuel` as
`integerProperty(1, 1000000000)`, and `ControlRegistry::invoke()` runs
`validateArgs(cmd->argsSchema, args)` at `src/core/ControlRegistry.cpp:251` BEFORE it
dispatches to any handler. `ControlSchema.cpp:87-91` (checkRange) rejects anything below the
declared minimum, and `ControlSchema.cpp:47-51` (typeMatches, `integer`) additionally rejects
a non-integral double. `fuel: -1` therefore never reaches the `static_cast<std::uint64_t>` at
`ControlCommandsWasmEdit.cpp:137`; it is refused typed as `invalid_args` first. Every wire
entry point funnels through `invoke()` (`ControlServer.cpp:512`, `ScriptCommandBridge.cpp:327`,
the undo inverse dispatcher at `ControlCommandsControl.cpp:336`), so there is no bypass.
Commit: none
Test: none
Red/green: none (refusal is decided by already-passing schema validation; the handler is
unreachable with an out-of-range value)

### Cluster 4951 — `wasm.set_param` clamps a negative index to slot zero
Verdict: FALSE-POSITIVE
Evidence: `src/core/ControlCommandsWasmEdit.cpp:277-278` declares `index` as
`integerProperty(0, static_cast<int>(wasm::abi::maxParams) - 1)`, so `index: -1` is rejected
by the same `validateArgs`/`checkRange` path at `ControlRegistry.cpp:251` and
`ControlSchema.cpp:87-91` before `handleSetParam` runs. The `std::max(index, 0)` at
`ControlCommandsWasmEdit.cpp:198` is therefore dead defensiveness for wire traffic, not a live
clamp - and it is documented as such: the command's own description string at
`ControlCommandsWasmEdit.cpp:272-274` promises "never clamped silently". The existing
`tests/control-wasm-sandbox.py:269-271` check ("an index past the 16 slots is invalid_args")
covers the upper bound of the same enforcement.
Commit: none
Test: none
Red/green: none

### Cluster 4948 — `wasm.render_offline` can allocate an enormous ramp for invalid frame counts
Verdict: FALSE-POSITIVE
Evidence: `src/core/ControlCommandsWasmRender.cpp:313` declares `frames` as
`integerProperty(1, 4194304)`, enforced by `validateArgs` at `ControlRegistry.cpp:251` /
`ControlSchema.cpp:87-91` before `handleRenderOffline` is entered. `frames: -1` is refused as
`invalid_args`, so `rampInput(frames)` at `ControlCommandsWasmRender.cpp:175-182` is only ever
reached with a count inside the declared range and the multi-gigabyte cast at
`ControlCommandsWasmRender.cpp:203` cannot see a negative value.
Commit: none
Test: none
Red/green: none

### Cluster 4953 — An unreadable WASM directory is reported as an empty module list
Verdict: REAL-UNTESTED
Evidence: `src/core/ControlCommandsWasm.cpp:147-153` - `collectModules` returns -1 only when
`QDir::exists()` is false, then filters entries with `QDir::Files | QDir::Readable`. A directory
that exists but cannot be read passes the exists check and yields an empty `entryInfoList`, so
`handleList` at `ControlCommandsWasm.cpp:167-180` takes the success path with `count: 0`. The
function's own doc comment at `ControlCommandsWasm.cpp:145-146` promises "-1 when root is not a
READABLE directory", and the refusal text at `ControlCommandsWasm.cpp:170-174` says "no readable
directory at '%1'", so the code contradicts its own contract. Real, and the fix is a
readability check alongside the exists check. Not changed: the wasm.* commands are not compiled
into `build/zene` (see the header of this report), so no regression test can be run RED here.
Commit: none
Test: none (would belong in tests/control-wasm-sandbox.py alongside check_list_refusals)
Red/green: none

### Cluster 2153 — Undoing a WASM load or unload loses the previous fuel budget
Verdict: REAL-UNTESTED
Evidence: `src/core/ControlCommandsWasmEdit.cpp:76-104` - `loadTransaction` records only
`before.path`, and the recorded inverse at `:87-89` is `wasm.load` with `path` alone.
`unloadTransaction` at `:106-121` likewise records only the dropped path. Neither snapshot
carries `WasmSandboxHost::fuelBudget()` (`src/wasm/WasmSandboxHost.h:115`), so replaying the
inverse calls `host.load(path, 0)` and `WasmSandboxHost.cpp:148-149` substitutes
`abi::defaultFuelBudget` (1000000, `src/wasm/WasmAbi.h:69`). A module that was loaded with a
custom budget therefore comes back on the default one. The command's mechanism string at
`:98-102` already discloses that the inverse restores only the PATH and not the module's
memory or its 16 slots, and fuel is not in that list - so this is a gap in the snapshot, not a
documented limitation. Not changed for the same reason as Cluster 4953: unreachable in this
build tree.
Commit: none
Test: none (would belong in tests/control-wasm-sandbox.py next to check_rehost_undo, which
already asserts the cold-instantiation part of the same inverse)
Red/green: none

## Leftovers
Two real defects are recorded and left unfixed pending a WASM-ON build tree: the readability
check in `ControlCommandsWasm.cpp:150` and the fuel budget in the load/unload transaction
snapshots at `ControlCommandsWasmEdit.cpp:76-121`. Both need `WASMTIME_ROOT` pointed at a
wasmtime C API install before they can be proved RED, and each needs its regression added to
`tests/control-wasm-sandbox.py` (registered only under `IF(WANT_WASM)`, tests/CMakeLists.txt:3237).
Both gates pass on the untouched tree: `bash tests/complexity-gate.sh --check` EXIT=0,
`bash tests/file-length-gate.sh --check` EXIT=0. No test executable was built or run for this
batch, since none of the five findings is reachable in this tree.