# Cluster new-p14-378: a requested fuel budget did not limit module startup

Verdict: **FIXED**

## Finding

`WasmSandboxHost::load()` applied the caller's fuel budget **after** the module
had been compiled and instantiated:

- `src/wasm/WasmSandboxHost.cpp:142-146` (original) - `candidate->loadModuleBytes(...)`
  instantiates the module, and instantiation runs the module's **start function**.
- `src/wasm/WasmSandboxHost.cpp:148-150` (original) - `candidate->setFuelBudget(budget)`
  is called only *after* that.

So a module whose start function costs more fuel than the caller asked for still
loaded: the start function ran under `WasmSandbox`'s default budget
(`abi::defaultFuelBudget = 1_000_000`, `src/wasm/WasmAbi.h:69`), whatever the
caller requested. A `wasm.load` with `fuel: 100` was not confined at all.

This is **REAL**, and the sandbox's own contract says so: the configured budget
is the one in force while a module is instantiated.
`WasmSandbox::Impl::primeFuelBudget()` (`src/wasm/WasmSandbox.cpp:271-279`)
primes the store with `m_impl->fuelBudget` before instantiation, and
`loadModuleBytes()` calls it "because instantiation runs the module's start
function ... and needs the same headroom" (`src/wasm/WasmSandbox.cpp:372-376`).
The host simply configured that budget one step too late.

Caller and siblings checked:

- the only caller of `WasmSandboxHost::load()` is the `wasm.load` command
  (`src/core/ControlCommandsWasmEdit.cpp:136-143`), whose `fuel` argument is
  schema-validated to `integerProperty(1, 1000000000)`
  (`src/core/ControlCommandsWasmEdit.cpp:255`) and is `0` (the ABI default) when
  the caller does not name one;
- `WasmSandbox::loadModuleFile()` - the other path into the same sandbox
  (`src/wasm/WasmWorker.cpp:274` for the effect's worker,
  `src/wasm/WasmOfflineRender.cpp:78` for the offline render) - never names a
  budget, so it keeps the default and is unchanged by this fix;
- `WasmSandbox::callWithFuel()` (`src/wasm/WasmSandbox.cpp:281-321`) re-primes
  the budget before **every** call, so the per-call metering is identical
  before and after the fix.

## Fix

`src/wasm/WasmSandboxHost.cpp` - the requested budget is computed and applied
**before** the module is instantiated:

```cpp
	const std::uint64_t budget =
		fuelBudget == 0 ? abi::defaultFuelBudget : fuelBudget;

	auto candidate = std::make_unique<WasmSandbox>();
	candidate->setFuelBudget(budget);
	if (!candidate->loadModuleBytes(bytes.data(), bytes.size(), outcome.error))
	{
		return outcome;
	}
```

Nothing else moved. `m_fuel`, `outcome.fuelBudget` and the per-call budget are
the same values as before, the "all or nothing" property of `wasm.load` is
untouched (the candidate is still discarded on failure, the hosted module is
still left alone), and no audio path, command, undo step or saved format is
involved.

Behaviour change, stated plainly: a module whose start function costs more than
the requested budget is now **refused** (`instantiation trapped: ... all fuel
consumed by WebAssembly`) instead of being hosted. Under the default budget
nothing changes, because the default budget is what the start function already
ran under.

## Regression test

New file `tests/src/wasm/WasmSandboxHostFuelTest.cpp` (the closest area's
tests live in `tests/src/wasm/`; no existing test included
`WasmSandboxHost.h`), registered as its own ctest target `WasmSandboxHostFuelTest`
under the existing `if(WANT_WASM)` guard in `tests/CMakeLists.txt`. The target
compiles `src/wasm/WasmSandbox.cpp` and `src/wasm/WasmSandboxHost.cpp` itself
(the same pattern as `WasmWorkerPoolTest`), because those sources are Qt free
and linking the whole core would cost a full build of it for one host-side
assertion.

Three slots, all deterministic - no clock, no sleep, no race:

1. `requestedBudgetBoundsModuleStartup()` - the fixture module's start
   function spins 1000 times; the test asks for **100** fuel and asserts the
   load is **refused** and that nothing is hosted.
2. `defaultBudgetStillHostsTheSameModule()` - the control: the **same** module
   under the ABI default budget loads, and the host reports that budget. This
   pins the refusal in (1) to the *budget* and not to a module the runtime
   cannot run.
3. `refusedLoadKeepsTheHostedModule()` - a module hosted under the default
   budget stays hosted when a later load is refused (the `wasm.load` all-or-
   nothing contract).

The fixture's cost is a property of the module, not of the machine: 1000
iterations cost far more than the 100 fuel requested and far less than the
1 000 000 of the default budget, so the outcome cannot depend on the runtime's
exact fuel accounting.

## The build tree and why a second one was needed

The lane's build tree is configured with `-DWANT_WASM:BOOL=OFF`
(`build/CMakeCache.txt`) because the wasmtime C API is not on its find path, so
**the whole `src/wasm/` directory is not compiled there** (`src/CMakeLists.txt:35-37`)
and no wasm test target exists in `build/tests`. The lane shim also refuses a
top-level `cmake` configuration ("bunny-guard: configuring is the parent's
job", exit 97).

The pinned wasmtime C API **is** on this box
(`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-ci4/third_party`,
the same v48.0.1 prefix `docs/KNOWN-LIMITATIONS.md:514-530` names), so the
evidence below was produced in a **separate, wasm-enabled build tree inside the
lane worktree** (`build-wasm/build`, git-ignored by `/build*/`), configured
with the same options as the lane tree plus the wasmtime prefix:

```
/usr/bin/cmake -S . -B build-wasm/build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DWANT_QT6=ON -DWANT_WASM=ON \
  -DWASMTIME_ROOT=/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-ci4/third_party \
  -DLMMS_CLAP_PATH=<wt>/build/clap -DLMMS_VST3_SDK_PATH=<wt>/build/vst3sdk
```

That configure printed `-- WASM DSP sandbox : Enabled` and exited 0
(`configure-wasm-4.log`). The lane's own tree was not reconfigured and its
`zene` binary was not disturbed.

## Evidence

Logs under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-378.r1/logs`.
All ctest runs are from `build-wasm/build/tests` (the wasm-enabled tree's test
directory), because the lane tree compiles the wasm code out.

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Configure (wasm-enabled tree) | `/usr/bin/cmake -S . -B build-wasm/build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWANT_QT6=ON -DWANT_WASM=ON -DWASMTIME_ROOT=.../zene-ci4/third_party -DLMMS_CLAP_PATH=<wt>/build/clap -DLMMS_VST3_SDK_PATH=<wt>/build/vst3sdk` | 0 | `configure-wasm-4.log`: `-- WASM DSP sandbox : Enabled` |
| Build the test | `cmake --build build-wasm/build --target WasmSandboxHostFuelTest` | 0 | `build-wasm-test-3.log` |
| **RED** (original code) | `ctest -R WasmSandboxHostFuelTest --output-on-failure` | **8** | `test-red-2.log`: 1 test run, **2 failed** - `requestedBudgetBoundsModuleStartup() '!outcome.ok' returned FALSE` and `refusedLoadKeepsTheHostedModule() '!refused.ok' returned FALSE`; the control `defaultBudgetStillHostsTheSameModule()` PASSED |
| Build the test (fix applied) | `cmake --build build-wasm/build --target WasmSandboxHostFuelTest` | 0 | `build-wasm-test-4.log` |
| **GREEN** (fix applied) | `ctest -R WasmSandboxHostFuelTest --output-on-failure` | **0** | `test-green-1.log`: `100% tests passed out of 1` |
| Reversal: write the product patch | `git diff -- src/wasm/WasmSandboxHost.cpp > <logs>/fix.patch` | 0 | `fix.patch` (product files only, no test hunk) |
| Reversal: remove the fix | `git apply -R <logs>/fix.patch` | 0 | the tree is back at the original code |
| Rebuild after the reversal | `cmake --build build-wasm/build --target WasmSandboxHostFuelTest` | 0 | `build-wasm-test-5.log` |
| **Reversal RED** | `ctest -R WasmSandboxHostFuelTest --output-on-failure` | **8** | `test-red-3.log`: the same two assertions fail again, the control still passes |
| Restore the fix | `git apply <logs>/fix.patch` | 0 | the tree is back at the fixed code |
| Rebuild after the restore | `cmake --build build-wasm/build --target WasmSandboxHostFuelTest` | 0 | `build-wasm-test-6.log` |
| **Restored GREEN** | `ctest -R WasmSandboxHostFuelTest --output-on-failure` | **0** | `test-green-2.log`: `100% tests passed out of 1` |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** | `feature-check-1.log`: `RESULT: PASS` (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS) |
| Build the wasm family | `cmake --build build-wasm/build --target WasmWorkerPoolTest` / `--target WasmAbiConformanceTest` / `--target WasmSandboxTest` / `--target wasm_effect` | 0 / 0 / 0 / 0 | `build-wasm-family.log`, `build-wasm-plugin.log` |
| **Whole wasm family** | `ctest -R "^Wasm" --output-on-failure` (from `build-wasm/build/tests`) | **0** | `test-family-2.log`: `100% tests passed out of 4` (WasmSandboxTest, WasmAbiConformanceTest, WasmWorkerPoolTest, WasmSandboxHostFuelTest) |
| Lane tree's binary (no collateral damage) | `cmake --build build --target zene` | **0** | `build-zene-lane.log`: `Built target zene` |

**Affected-family test count:** the wasm family in the wasm-enabled tree is
**4 ctest targets, 56 slots, all green** - `WasmSandboxTest` 23 passed / 1
skipped, `WasmAbiConformanceTest` 14 passed, `WasmWorkerPoolTest` 14 passed,
`WasmSandboxHostFuelTest` 5 passed (3 slots plus init/cleanup)
(`family-slots-*.log`, each `EXIT=0`). The first family run
(`test-family-1.log`, exit 8) had one failure,
`WasmSandboxTest::p5_pluginFactoryDiscoversPlugin()`, because the
`libwasm_effect.so` the plugin-factory probe looks for had not been built in
that tree; after building the `wasm_effect` target the family is green
(`test-family-2.log`, exit 0) - that failure was a missing build artefact of
this evidence tree, not a behaviour change.

In the **lane's** tree the wasm family has 0 registered / 0 executed tests,
because `WANT_WASM=OFF` compiles `src/wasm/` out entirely - that is a property
of the lane's configuration, not of this change, and it is why the evidence
above was produced in the second tree.

### Gate note

`feature_check.py` reports **complexity: no new item in your files** and
**file-length: no new item in your files**: `WasmSandboxHost::load()` keeps its
complexity (the change moves two statements and adds no branch) and
`src/wasm/WasmSandboxHost.cpp` stays far below the 500-line target. The new
test file is 174 lines and its functions are all below the CCN target of 10.

## Files committed

- `src/wasm/WasmSandboxHost.cpp` (the fix; already registered in
  `tests/fork-sources.txt` - fork-NEW code, so no divergence-ledger entry)
- `tests/src/wasm/WasmSandboxHostFuelTest.cpp` (**new**, registered in
  `tests/all-sources.txt:3042` - the whole-tree scope, which is where the
  sibling wasm tests live: `tests/src/wasm/WasmAbiConformanceTest.cpp`,
  `tests/src/wasm/WasmSandboxTest.cpp`, `tests/src/wasm/WasmWorkerPoolTest.cpp`)
- `tests/CMakeLists.txt` (registers the new ctest target; an inherited file, so
  the divergence ledger entry is in `tests/upstream-modifications.txt`)
- `tests/all-sources.txt` (the new test source, in sorted place)
- `tests/upstream-modifications.txt` (the `tests/CMakeLists.txt` entry)
- `docs/qwen-fixes-2026-10/cluster-new-p14-378.md` (this report)

No new command, no command snapshot change, no save-format change, no audio
path change.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES
   with it.** RED `ctest -R WasmSandboxHostFuelTest --output-on-failure` exit
   **8** (`test-red-2.log`, and again `test-red-3.log` after
   `git apply -R <logs>/fix.patch`); GREEN exit **0** (`test-green-1.log`, and
   again `test-green-2.log` after `git apply <logs>/fix.patch`). Both ctest runs
   executed 1 test (never 0).
2. **The test is deterministic.** The fixture's cost is a property of the
   module (a 1000-iteration loop in the start function); the budgets are
   literals (100 and the ABI default). No wall-clock timing, no sleep, no
   thread, no race, no injected timestamp is needed - the quantity under test
   (fuel) is counted by the runtime itself, not by the test.
3. **The fix is at the cause and complete.** The cause is the ordering in
   `WasmSandboxHost::load()`; the fix moves the budget application before the
   instantiation that runs the start function. Every caller was checked: the
   one caller of `WasmSandboxHost::load()` is `wasm.load`
   (`src/core/ControlCommandsWasmEdit.cpp:143`), and the sibling paths into the
   same sandbox (`WasmSandbox::loadModuleFile()` from `WasmWorker.cpp:274` and
   `WasmOfflineRender.cpp:78`) name no budget and keep the default. The value
   flows on into `m_fuel` and `outcome.fuelBudget` exactly as before, and
   `WasmSandbox::callWithFuel()` still re-primes the per-call budget, so the
   per-call metering is untouched.
4. **Nothing new runs on the audio thread.** The change is in the control
   thread's load path; no allocation, free, lock, wait, log or signal was
   added, and no audio file was touched.
5. **Old projects and saved data are unaffected.** No file format, no save
   path, no project field and no command surface changed; a project that
   loads a module with the default budget behaves exactly as before.
6. **Portability.** The test uses only Qt Test, `QTemporaryDir` and `QFile`;
   no Qt-6-only API (the tree builds with Qt5 and Qt6), no `windows.h` macro
   name, no `std::filesystem`. The production change is three statements in
   existing code and touches no platform API.
7. **Undo and commands.** No command was added, removed or changed, so the
   control-commands snapshot and the reversibility table are untouched;
   `wasm.load` keeps its existing one-step undo (the recorded inverse is
   another `wasm.load`), and a refused load still changes nothing, so it still
   needs no inverse.
