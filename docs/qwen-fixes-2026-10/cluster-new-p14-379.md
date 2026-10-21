# Cluster new-p14-379: a refused module load says why in the WasmEffect status line

Verdict: **FIXED**

## Finding

`loadModule()` stores the reason of a refused load in `m_error`
(`plugins/WasmEffect/WasmEffectControls.cpp:78`), but `statusText()` never read
that field: it reported the worker's state only. The status line is the only
thing the WasmEffect dialog shows about a load
(`plugins/WasmEffect/WasmEffectControlDialog.cpp:102`, refreshed every 500 ms
by the dialog's timer at `WasmEffectControlDialog.cpp:75-81`), so the reason of
a refused load never reached the user.

This is REAL. On the original code, after `WasmEffectControls::loadModule()`
had refused a module, the status line still read:

```
No module loaded - dry passthrough
```

while the reason was recorded and readable only through `lastError()`
(`plugins/WasmEffect/WasmEffectControls.h:85-88`), which nothing in the dialog
uses. With a module already running, the refused load leaves the running
module alone (all or nothing, `plugins/WasmEffect/WasmEffect.cpp:101-108`) and
the worker stays `Ready`, so the status line read `gain.wasm: ready - ...` and
hid the refusal completely. The regression run recorded below shows exactly
that (`harness-test-red-3.log`): the status line did not contain the refusal
reason.

## Fix

`plugins/WasmEffect/WasmEffectControls.cpp` (`statusText()`):

- The state texts are collected into `status` instead of being returned from
  the switch, so the refusal reason can be added to them.
- After the state text is built, a recorded refusal is appended:
  `if (!m_error.isEmpty() && !status.contains(m_error)) status = tr("%1 - load
  failed: %2").arg(status, m_error);`
- The `status.contains(m_error)` guard keeps the line free of a duplicate when
  the worker's own error already names the same reason (the `Failed` state
  prints `m_effect->lastError()`, which is the same text for a load the worker
  could not host).
- The unreachable `return tr("unknown state")` after the switch became the
  switch's `default:` case, so the behaviour for an unknown worker state is
  unchanged.

The refusal reason is cleared where it already was cleared: a successful
`loadModule()` (`WasmEffectControls.cpp:83`) and a project load without a
module (`WasmEffectControls.cpp:169`). Nothing else in the file changed.

Scope of the change:

- `statusText()` has exactly one caller, the dialog's `refreshStatus()`
  (`WasmEffectControlDialog.cpp:96-103`), and the dialog is the only consumer
  of the text. No other caller of `statusText()`, `loadModule()` or `m_error`
  exists in the tree (`grep -rn "statusText\|lastError()\|loadModule" plugins
  src tests` -> only `WasmEffectControlDialog.cpp`, `WasmEffect.cpp` and the
  test file).
- The sibling path into the same function - `WasmEffectControls::loadSettings()`
  (`WasmEffectControls.cpp:160-191`), which loads a project module through
  `loadModule()` - now shows the same reason for a module a project names but
  the host cannot host. It clears `m_error` for a project without a module,
  exactly as before.
- The control-surface path `wasm.device_load`
  (`src/core/ControlCommandsWasmEdit.cpp:344-382`) refuses a module BEFORE the
  device is touched, so it never sets `m_error` and is unaffected.

## Regression test

`tests/src/wasm/WasmSandboxTest.cpp`: new slot
`p8_failedModuleLoadIsReportedInTheStatusText()`.

It writes a file that is not a wasm module into a `QTemporaryDir`, asks the
controls model to load it and asserts that the status line carries the refusal
reason - once with no module loaded, once with a working module running (the
status line has to name the reason AND still report the module that runs), and
once more after a successful load (the reason is gone again).

The test is deterministic: the refusal is produced by the host's own module
checks on a file that is not a wasm module, so there is no timing, no thread,
no sleep and no polling in it. The only external input is the fixture file the
test writes itself.

## Evidence

Logs under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-379.r1/logs`.

### How the test was run (this lane's build tree has no wasm)

This lane's build tree was configured with `-DWANT_WASM:BOOL=OFF`
(`build/CMakeCache.txt:800`), the lane shim refuses `cmake` configuration
(`bunny-guard: configuring is the parent's job`, `cmake-wasm-1.log`, exit 97),
so the `WasmSandboxTest` ctest target does not exist in this lane and
`ctest -R '^WasmSandboxTest$'` finds 0 tests. The pinned wasmtime C API
(v48.0.1 - the version `scripts/fetch-wasmtime.sh` pins) was placed in the
worktree's gitignored `third_party/wasmtime` (copied from the programme's
`zene-cachyos` tree, which fetched it with that script), and the registered
test source was compiled and linked directly with the tree's own objects
(`harness-build-script.sh`, archived next to the logs). The harness compiles
exactly the sources the CMake target compiles
(`tests/CMakeLists.txt:3294-3302`: the test source, the three WasmEffect plugin
sources) plus the wasm core sources the `WANT_WASM=ON` build would add to
`lmmsobjs`, and links the tree's own `lmmsobjs` objects. The .wasm fixtures
were assembled with the tree's own `tools/wasm/wat2wasm.cpp`.

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Baseline gate (before the change) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `gate-baseline.log`: RESULT PASS |
| Build the harness (RED, original code) | `bash /tmp/wasm-harness-p14-379/build.sh <worktree> /tmp/wasm-harness-p14-379` | 0 | `harness-build-1.log` (first attempts failed to compile and are kept as `harness-build-1..6.log`; the script was corrected) |
| RED (original code) | `QT_QPA_PLATFORM=offscreen /tmp/wasm-harness-p14-379/WasmSandboxTest p8_failedModuleLoadIsReportedInTheStatusText` | 1 | `harness-test-red-3.log`: `FAIL! ... 'emptyStatus.contains(emptyError)' returned FALSE. (the status line hides the load failure: "No module loaded - dry passthrough" does not contain "module compile failed: failed to parse WebAssembly module ...` - Totals: 2 passed, 1 failed |
| Build the harness (GREEN, fix applied) | `bash /tmp/wasm-harness-p14-379/build.sh <worktree> /tmp/wasm-harness-p14-379` | 0 | `harness-build-7.log` |
| GREEN (fix applied) | `QT_QPA_PLATFORM=offscreen /tmp/wasm-harness-p14-379/WasmSandboxTest p8_failedModuleLoadIsReportedInTheStatusText` | 0 | `harness-test-green-3.log`: `PASS! ... p8_failedModuleLoadIsReportedInTheStatusText()` - Totals: 3 passed, 0 failed |
| Reversal | `git diff -- plugins/WasmEffect/WasmEffectControls.cpp > logs/fix.patch` then `git apply -R logs/fix.patch` | 0 | `fix.patch` (73 lines), patch removed |
| Reversal RED | rebuild + `WasmSandboxTest p8_failedModuleLoadIsReportedInTheStatusText` | 1 | `harness-build-11-reverse.log` (build 0), `harness-test-red-3.log`: the same failing assertion |
| Restore | `git apply logs/fix.patch` | 0 | patch restored |
| Restored GREEN | rebuild + `WasmSandboxTest p8_failedModuleLoadIsReportedInTheStatusText` | 0 | `harness-build-12-restore.log` (build 0), `harness-test-green-3.log`: 3 passed, 0 failed |
| Affected family (whole `WasmSandboxTest` binary) | `QT_QPA_PLATFORM=offscreen /tmp/wasm-harness-p14-379/WasmSandboxTest` | 1 | `harness-test-full-3.log`: 25 slots - 23 passed, 1 skipped (`g4_compiledCDemoModule`, no zig toolchain), 1 failed (`p5_pluginFactoryDiscoversPlugin`: the plugin module `libwasm_effect.so` is not built in this `WANT_WASM=OFF` tree - it fails identically with the fix reverted, `harness-test-full-1.log`) |
| ctest family (as registered) | `ctest -R '^WasmSandboxTest$' --output-on-failure` (from `build/tests`) | 0 | `ctest-wasm-1.log`: `No tests were found!!!` - 0 tests, the target is compiled out in this lane |
| Product build | `cmake --build build --target zene` | 0 | `build-zene-1.log`: built target zene (the changed file is inside the wasm plugin, which this tree does not build) |
| Gates (all) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `gate-1-final.log`: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length - all PASS, RESULT: PASS |

**Affected-family test count:** the `WasmSandboxTest` family has 0 registered /
0 executed ctest tests in this lane (the target is compiled out with
`WANT_WASM=OFF`). The same registered test source was built and executed
directly: 25 slots, of which the new one is the regression and 23 others pass,
1 is skipped for a missing zig fixture and 1 (`p5_pluginFactoryDiscoversPlugin`)
fails only because the wasm plugin module is not built in a wasm-off tree.

**Complexity:** `statusText()` was CCN 8 before the change and is CCN 10 after
it (`python3 -m lizard plugins/WasmEffect/WasmEffectControls.cpp`), still at
the target of 10 - no new item in the complexity ratchet. The switch's
`default:` case keeps the "unknown state" text without adding a branch.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with
   it.** RED `harness-test-red-3.log` exit 1 (the status line hid the
   refusal), GREEN `harness-test-green-3.log` exit 0. The reversal was done
   with `git apply -R logs/fix.patch` on the product file only (the test file
   stayed), rebuilt and re-run: exit 1 again; then `git apply logs/fix.patch`,
   rebuilt, re-run: exit 0.
2. **The test is deterministic.** The refusal comes from the host's own module
   checks applied to a file the test writes itself ("this is not a wasm
   module"), so the reason is a parse error, not a race. No wall-clock timing,
   no sleep, no polling, no thread and no injected timestamp is involved; the
   module that must keep running is loaded synchronously through
   `controls()->loadModule()`. The wasm worker's own load seam
   (`WasmWorker::setModuleLoadTestHook`, used by `p7`) is not needed here.
3. **The fix is at the cause and complete.** The cause is that `statusText()`
   ignored `m_error`; it now reports it, and the clearing of `m_error` after a
   successful load (`WasmEffectControls.cpp:83`) and after a project load
   without a module (`WasmEffectControls.cpp:169`) was already in place and was
   checked. Every caller was checked: `statusText()` has one caller (the
   dialog's `refreshStatus()`), `m_error` is written in exactly one place
   (`loadModule()`), and the sibling path into the same function
   (`loadSettings()`) goes through the same `loadModule()`. The control-surface
   command `wasm.device_load` refuses a module before the device is touched and
   never sets `m_error`.
4. **No collateral damage.** With no recorded refusal the status line is
   byte-for-byte the text it was before (the same five state strings, the same
   "quarantined" text, the same "unknown state" fallback). Nothing is saved or
   loaded differently: `m_error` is not part of `saveSettings()`, so old
   projects and saved data are unchanged, and a project that names a module the
   host cannot host now merely shows the reason in the dialog. No limit or
   check was added that another caller could trip over.
5. **Realtime, undo and commands.** `statusText()` runs on the GUI thread (the
   dialog's 500 ms timer), never on the audio thread; the change adds no
   allocation, lock, wait, I/O or logging to any audio path - the only touched
   function is a pure string builder. No undo step was added or changed, and no
   control command was added, removed or modified (the `wasm.*` command group
   and the commands snapshot are untouched).
6. **Portability.** The change uses only `QString`, `tr()` and `QFileInfo`, all
   already used in the same file; no Qt-6-only API, no `windows.h` macro name
   and no `std::filesystem` was introduced. The plugin's dialog path is the
   same on every platform.
7. **The "ALREADY-FIXED / FALSE-POSITIVE" question does not apply** - the
   verdict is FIXED, and the code that shows the bug is quoted above
   (`WasmEffectControls.cpp:78` writes `m_error`, the original `statusText()`
   never read it, and the RED run shows the status line without the reason).

## Files committed

- `plugins/WasmEffect/WasmEffectControls.cpp` (already registered in
  `tests/fork-sources.txt:1278`)
- `tests/src/wasm/WasmSandboxTest.cpp` (already registered in
  `tests/all-sources.txt:3042`; no new test file, no registration change)
- `docs/qwen-fixes-2026-10/cluster-new-p14-379.md` (this report)

No new product file and no inherited-file edit, so no registration-file change
was required. The worktree's `third_party/wasmtime` (the pinned wasmtime C
API used to build the harness) is gitignored and is not part of the commit.
