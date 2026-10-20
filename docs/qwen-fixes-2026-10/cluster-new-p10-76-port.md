# Cluster new-p10-76-port

**Verdict: FIXED**

The finding is REAL: `wasm.render_offline` accepted a malformed and unbounded
`input` array. The port of the accepted fix (branch `bugs/qwen-new-p10-76`,
commit `cbe3d1362` — which carried only the blocked report and an unexecuted
python regression) is re-applied here against the newest line, with a
regression test that runs in this configuration.

## What was wrong

- `src/core/ControlCommandsWasmRender.cpp:186-192` (before the fix) read the
  caller's array with a file-local loop:
  `out.push_back(static_cast<float>(value.toDouble()));`
  — no element check, no finiteness/range check, no length check. A string
  became `0`, a finite double outside the float range (`1e300`) became an
  infinity (an undefined conversion), and an array far longer than `frames`
  was reserved and copied whole.
- `src/core/ControlCommandsWasm.cpp:229-238` carried the SAME loop, so the
  sibling `wasm.process` had the same defect in its own copy.

## The fix

- NEW `include/ControlSampleArray.h` + `src/core/ControlSampleArray.cpp`: the
  control layer's ONE reader for a caller's sample array. The rules
  (`include/ControlSampleArray.h:28-36`):
  - the array may not be longer than the number of frames the call asks for
    (`src/core/ControlSampleArray.cpp:49-55`);
  - every element must be a JSON number
    (`src/core/ControlSampleArray.cpp:65-69`);
  - every value must be finite and within the float range
    (`src/core/ControlSampleArray.cpp:74-79`).
  A refused array is an `InvalidArgs` failure, and the samples are not written
  at all (`src/core/ControlSampleArray.cpp:50`, `:66`, `:75`; the samples are written only at `:81`).
- `src/core/ControlCommandsWasmRender.cpp:219` and
  `src/core/ControlCommandsWasm.cpp:307` read the argument through that
  reader (`readInputArgument`, `src/core/ControlSampleArray.cpp:85`), so
  `wasm.render_offline` AND its sibling `wasm.process` refuse the same arrays
  with the same typed refusal.
- The two commands' descriptions now state the contract
  (`src/core/ControlCommandsWasmRender.cpp:316-320`,
  `src/core/ControlCommandsWasm.cpp:374-378`).

No new command, no new parameter, no change to the audio thread, the undo
journal or the save format.

## The regression test

- NEW `tests/src/core/ControlSampleArrayTest.cpp` (registered in
  `tests/CMakeLists.txt:210-215`): ten slots over the reader — a string
  sample, a boolean sample, `1e300`, an infinity, an array longer than the
  frames, an array of exactly the frame count, an empty array, a negative
  sample, an absent `input` argument and a malformed one. It is deterministic
  (no clock, no sleep, no race): every input is a literal.
- The reader is pure Qt, so the test runs in EVERY configuration — including
  this one, where the wasmtime C API is absent and the `wasm.*` commands are
  not compiled at all (`build/CMakeCache.txt:800` `WANT_WASM:BOOL=OFF`,
  `src/core/CMakeLists.txt:99-107`).
- `tests/control-wasm-sandbox.py` carries the command-level coverage of the
  same rules (the accepted branch's cases, plus the sibling `wasm.process`),
  for a build with the sandbox compiled in. It is driven with an ASSEMBLED
  module (`<build>/wasm-modules/gain.wasm`), not the `.wat` example: the
  render's worker takes binaries, and the `.wat` text is the host's business
  (`wasm.load` assembles it). The device's own transcript refuses a `.wat` for
  the same reason (`tests/control-wasm-device.py:46`, `:101-104`), so the
  shared file loader must not start assembling one.

## Verification record

| Check | Exact command | Result |
|---|---|---|
| RED (the product change reverted) | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-76-port.r1/logs/fix.patch` → `cmake --build build --target zene ControlSampleArrayTest > logs/build-12.log 2>&1` → `ctest -R '^ControlSampleArrayTest$' --output-on-failure > logs/test-10.log 2>&1` | build exit 0, **ctest exit 8** — 6 of 12 slots FAIL (`a string sample was accepted`, `a sample outside the float range was accepted`, `more samples than frames were accepted`, ...) |
| GREEN (the fix re-applied) | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-76-port.r1/logs/fix.patch` → `cmake --build build --target zene ControlSampleArrayTest > logs/build-13.log 2>&1` → `ctest -R '^ControlSampleArrayTest$' --output-on-failure > logs/test-11.log 2>&1` | build exit 0, **ctest exit 0** — `1/1 Test #60: ControlSampleArrayTest ... Passed` |
| Affected family (this change) | `ctest -R '^(ControlSampleArrayTest\|ControlRegistryTest\|ControlResultSchemaTest\|ControlRegistryNestingTest\|ControlCommandsSnapshot)$' --output-on-failure > logs/test-6.log 2>&1` | **exit 0 — 5 tests, 5 passed** |
| Wider family (regression check) | `ctest -R "Control" --output-on-failure > logs/test-family-control.log 2>&1` | exit 8 — 115 tests: 52 passed, 63 failed (41 of them "Not Run": their executables are not built in this tree). The SAME 63 with the same reasons on the pristine tree (`logs/test-family-control-baseline.log`, the failing sets diff to zero lines) — they are this tree's missing plugins (`the LADSPA host plugin 'ladspaeffect' is not in this build`), the MCP bridge and a stale tutorial fixture, none of them this change. |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **exit 0 — all eight gates PASS** (`logs/gate-feature-check-2.log`) |
| Ratchet: complexity | `bash tests/complexity-gate.sh --check` | exit 1 — the six `REGRESSION` lines are in OTHER files (`tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`, `src/gui/ControlCommandsShortcuts.cpp`); none in this change's files. `handleRenderOffline` stays at its baseline CCN 12 (`logs/gate-complexity-3.log:58`) |
| Ratchet: file length | `bash tests/file-length-gate.sh --check` | exit 1 — the 14 `REGRESSION` lines are pre-existing files over 500 lines; none in this change's files |
| Command surface | `python3 tools/mcp-zene-control/snapshot_commands.py --socket <live> --lane <worktree>` | exit 0 — 828 commands, the same surface as before (the `wasm.*` group is compiled out in this configuration), snapshot regenerated live (`logs/snapshot-regen-3.log`) |
| The two wasm TUs | `g++ -fsyntax-only -std=c++20 -fPIC -I include -I src -I build/include -I build/src -I build <qt6> -Wall -Wextra <proxy TU>` | exit 0 — those two translation units are NOT compiled in this configuration (`WANT_WASM=OFF`), so the code changed in them is checked with a proxy that repeats the changed calls verbatim (the same names, namespaces and types): `logs/syntax-check-wasm-tus.log` |

The RED run is a real runtime failure, not a build failure: the patch
reversed is the product change alone
(`git diff -- src/core/ControlSampleArray.cpp src/core/ControlCommandsWasm.cpp
src/core/ControlCommandsWasmRender.cpp > logs/fix.patch`), the reader stays
compilable, and the test fails on its assertions.

The branch carries two behaviour-preserving steps before the fix, so that the
reversal above is the FIX and nothing else:

- `02ef44b8a` — the duplicated `jsonToFloats` becomes the shared reader
  (behaviour unchanged).
- `c034d4b92` — the argument lookup moves into the reader (behaviour
  unchanged). This also keeps `handleRenderOffline`'s cyclomatic complexity
  at its baseline: the refusal branch leaves the handler, so the complexity
  gate does not see a rise in a function that is already over the target.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES
  with it (exit codes).** RED: `ctest -R '^ControlSampleArrayTest$'` exit 8
  (6 failing slots, `logs/test-10.log`). GREEN: the same command exit 0
  (`logs/test-11.log`). The reversal is `git apply -R logs/fix.patch` and the
  re-application is `git apply logs/fix.patch`; both are logged above with
  their build exit codes (0 and 0).
- **The test is deterministic.** Every input is a literal (a string, `true`,
  `1e300`, `std::numeric_limits<double>::infinity()`, a two-element array);
  there is no clock, no sleep, no thread and no race. The seams are the
  reader's own arguments: the array, the frame count and the out-parameter.
- **The fix is at the cause and complete.** The cause was the conversion
  loop, and it existed in TWO copies: `wasm.render_offline`
  (`src/core/ControlCommandsWasmRender.cpp`) and its sibling `wasm.process`
  (`src/core/ControlCommandsWasm.cpp`). Both now read through the one reader
  (`include/ControlSampleArray.h`), so they cannot drift apart again. The
  wider type the value flows into was checked too: the samples are `float`s,
  hence the float-range rule (the same rule `wasm.set_param` already applies
  to its value, `src/core/ControlCommandsWasmEdit.cpp:200-206`).
  **Sibling commands in the changed command files**: the `wasm.*` group is
  `get_state`, `list`, `load`, `process`, `set_param`, `unload` (in
  `src/core/ControlCommandsWasm.cpp`) and `pool`, `render_offline` (in
  `src/core/ControlCommandsWasmRender.cpp`). `process` is the one that takes
  a caller's samples and it is fixed consistently with `render_offline` ✓;
  `get_state`, `list`, `load`, `set_param` and `unload` take no sample array
  and are untouched ✓; `pool` reports the pool's counters and is untouched ✓.
- **No collateral damage.** Old projects and saved data are untouched: the
  change adds no field, no parameter and no command, and the undo journal and
  the save format are not involved (the reader is a read-only argument
  check, and a refused call records nothing — it is a refusal, not an edit).
  The new limit is `input` length ≤ the `frames` the same call asks for, and
  it is checked against THAT call's own `frames` — no other caller can reach
  it: the only two call sites are the two commands above, and their
  pre-existing callers pass an input of exactly `frames` samples
  (`tests/control-wasm-sandbox.py:80-81`: `FRAMES = 8` with eight samples).
  An ABSENT `input` argument keeps the old behaviour (the command's own
  ramp), and an EMPTY array is silence, not a malformed call.
- **Realtime, undo and commands.** Nothing here is on the audio thread: the
  reader is called by two control commands, on the control thread, and it
  allocates one `std::vector<float>` for the caller's samples (the same
  allocation the old code made). No mutex, no wait, no logging, no signal.
  No new control command was added (the command surface is unchanged: 828
  commands before and after), so the `ControlCommandsSnapshot` canary does not
  move. The refused call is an `InvalidArgs` failure, which the registry
  records as no edit at all — one honest undo step is unaffected.
- **If the verdict is ALREADY-FIXED or FALSE-POSITIVE: the code or the
  contract that shows it.** Not applicable: the verdict is FIXED, and the
  code that was wrong is quoted above with its file:line
  (`src/core/ControlCommandsWasmRender.cpp:186-192` and
  `src/core/ControlCommandsWasm.cpp:229-238` before this change).

## Files in this change

- `include/ControlSampleArray.h` — NEW, the shared reader's declaration and
  the rules.
- `src/core/ControlSampleArray.cpp` — NEW, their one implementation.
- `src/core/ControlCommandsWasmRender.cpp` — `wasm.render_offline` reads the
  argument through the reader; its description states the contract.
- `src/core/ControlCommandsWasm.cpp` — the sibling `wasm.process` does the
  same; its description states the contract.
- `src/core/CMakeLists.txt` — the new TU is compiled in every configuration.
- `tests/src/core/ControlSampleArrayTest.cpp` — NEW, the regression test.
- `tests/CMakeLists.txt` — registers it.
- `tests/control-wasm-sandbox.py` — the command-level cases (a build with the
  sandbox compiled in).
- `tests/fork-sources.txt`, `tests/all-sources.txt` — the new files'
  registration.
- `tools/mcp-zene-control/zene_control/commands_snapshot.json` — regenerated
  live (the surface is unchanged).
- `docs/qwen-fixes-2026-10/cluster-new-p10-76-port.md` — this report.

## Note for the next reader (the merge train's stated blocker)

The merge train recorded that the accepted branch's checks "pass the .wat
EXAMPLE" while "the render path compiles raw bytes (bad magic number)". That
is true of this tree as well: `WasmSandbox::loadModuleFile`
(`src/wasm/WasmSandbox.cpp:423-439`) hands the file's bytes to the compiler
unchanged, so a `.wat` fails to compile, and the render's worker takes
binaries for the same reason the device's worker does. Making that loader
assemble a `.wat` would break the device's own tested refusal
(`tests/control-wasm-device.py:101-104`: "a module the device cannot host is
refused, typed"), so this port does NOT change it: the ported regression
drives the render with the assembled demo module instead, and the `.wat`
support of the render path is left as it was — a separate change, with its
own contract, not this bug's.
