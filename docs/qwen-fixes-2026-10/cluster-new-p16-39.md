# Cluster new-p16-39: invalid sample rates reach the renderer

**Verdict: BLOCKED** - the finding is REAL and the fix is in this commit. The reader-level regression is
proven RED and GREEN in this lane. The command-level regression tests (`wasm.render_offline`,
`wasm.process`) are written but were NOT built or run, because this lane's build tree is configured
with `WANT_WASM=OFF` (see "Blocker").

## Decision: REAL

Evidence at HEAD (`5c1c6715d9`) before this change:

- `src/core/ControlCommandsWasmRender.cpp:218-220` (HEAD): `sample_rate` is read with
  `toDouble()` and cast to `float` with no check. `1e300` becomes `+inf` and `-1` is passed on as a
  negative rate. Both reach `wasm::renderOffline` (`src/core/ControlCommandsWasmRender.cpp:234-235`) and
  from there the module's `f32` parameter.
- The schema has no bound: `src/core/ControlCommandsWasmRender.cpp:333` is `numberProperty()`, and
  `include/ControlVocabulary.h:62` has no range form of it.
- The same unchecked read is in the sibling command `wasm.process`:
  `src/core/ControlCommandsWasm.cpp:299-300` (HEAD), schema `src/core/ControlCommandsWasm.cpp:382`.
- The sample reader already applies this rule to samples (`src/core/ControlSampleArray.cpp:74`:
  "finite and within the float range"); the rate never did.

The finding's `line 225` is a comment inside `handleRenderOffline` in this tree; the unchecked read is
at `:218-220`.

## Change (verified by the parts below)

- `include/ControlSampleArray.h` and `src/core/ControlSampleArray.cpp`: new
  `control::readSampleRateArgument(args, fallback, &out)`, next to the sample reader so both sample-taking
  commands share one rule. Absent argument: `fallback` (48000, unchanged). Present: must be a JSON
  number, finite, `> 0`, `<= FLT_MAX`, and still `> 0` after the cast to float (so `1e-60` is refused).
  Anything else: `InvalidArgs`, and `out` is untouched.
- `src/core/ControlCommandsWasmRender.cpp` (`handleRenderOffline`) and `src/core/ControlCommandsWasm.cpp`
  (`handleProcess`): the raw read is replaced by that helper; a refusal is returned as `invalid_args`
  wrapped in the command's own message, the same shape the 'input' refusal uses.

Not changed, on purpose: the command schemas. Adding a bound to `numberProperty()` would move the
`ControlCommandsSnapshot` canary, and the handler check refuses the same inputs. No control command was
added. The command description text does not mention the refusal yet; that is a docs follow-up if the
reviewer wants it in the schema text.

Behaviour outside the bug: any rate in `(0, FLT_MAX]` is unchanged; the default is unchanged. The rates
now refused (zero, negative, NaN/inf, over float range, rounds to float zero, non-number) were all
rendered before, so no legitimate caller is affected.

Full product diff: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-39.r1/logs/fix.patch`.

## Evidence

Commands run from `build/tests` (or the worktree root where noted); logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-39.r1/logs/`.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Build wasm render test target | `cmake --build build --target ControlWasmRenderTest -j4` | 2 | `build-1-red.log` (`No rule to make target 'ControlWasmRenderTest'`) |
| Reconfigure with wasmtime | `cmake -S . -B build -DWASMTIME_ROOT=...` | 97 | `configure-1.log` (`bunny-guard: configuring is the parent's job`) |
| RED build of reader target (ccache tmp dir not writable) | `cmake --build build --target ControlSampleArrayTest -j4` | 2 | `build-2-green.log` |
| Build reader target, ccache off | `CCACHE_DISABLE=1 cmake --build build --target ControlSampleArrayTest -j4` | 0 | `build-3-green.log` |
| GREEN run, via ctest | `ctest -R '^ControlSampleArrayTest$' --output-on-failure` | 0 | `test-1-green.log` (100% of 1) |
| GREEN run, binary | `./ControlSampleArrayTest` | 0 | `test-2-green-direct.log` (`Totals: 15 passed, 0 failed`) |
| RED: reader mutated to HEAD behaviour (`if (inRange && false)`), rebuild | `CCACHE_DISABLE=1 cmake --build ... ControlSampleArrayTest -j4` | 0 | `build-4-red-mutant.log` |
| RED run | `ctest -R '^ControlSampleArrayTest$' --output-on-failure` | 8 | `test-3-red-mutant.log`: `FAIL! ... anInvalidSampleRateIsRefusedAndChangesNothing() '!result.ok' returned FALSE`, `ControlSampleArrayTest.cpp:214`, `Totals: 14 passed, 1 failed` |
| Fix restored (byte-identical to saved copy, `cmp`), rebuild | `CCACHE_DISABLE=1 cmake --build ... ControlSampleArrayTest -j4` | 0 | `build-5-green-restored.log` |
| GREEN run after restore | `ctest -R '^ControlSampleArrayTest$' --output-on-failure` | 0 | `test-4-green-restored.log` (100% of 1) |
| Syntax check, changed production files (configured flags + wasmtime headers) | `python3 syntax-check.py build <wt> <wasmtime>/include` | 0 for `ControlSampleArray.cpp` and `ControlCommandsWasmRender.cpp`; 1 for `ControlCommandsWasm.cpp` | `syntax-1.log` |
| The same error at HEAD | HEAD copy of `ControlCommandsWasm.cpp` with the same flags | 1 | `syntax-head.log` (`registerWasmEditCommands` not declared, line 415 at HEAD / 422 now) |
| Syntax check, test file | same flags, `ControlWasmRenderTest.moc` include removed in a temp copy | 0 | `syntax-test.log` |

Test counts: `ControlSampleArrayTest` ran 15 cases (init, cleanup, 10 pre-existing tests, 3 new), 0 failed
with the fix, 1 failed without it (the new refusal case). `ControlWasmRenderTest`: 0 executed (not built).

### Gates (run by hand; `feature_check.py` could not write its log folder)

`feature_check.py` writes its logs to `<worktree>-feature-check`, outside the write-jail, and stopped
with `PermissionError: ... zene-qwen-new-p16-39-feature-check` before running any gate. Each gate it runs
was run by hand from the worktree with the same command, logs in the LOG folder:

| Gate | EXIT | Log |
| --- | --- | --- |
| fork-sources | 0 | `gate-fork-sources.log` |
| all-sources-reproduce | 0 | `gate-all-sources-reproduce.log` |
| no-upstream-regression | 0 | `gate-no-upstream-regression.log` |
| check-namespace | 0 | `gate-check-namespace.log` |
| no-tautology | 0 | `gate-no-tautology.log` |
| doc-figures | 0 | `gate-doc-figures.log` |
| complexity (ratchet, raw) | 1 | `gate-complexity.log` |
| file-length (ratchet, raw) | 1 | `gate-file-length.log` |

The two ratchets are red on the train, as the brief says. None of their `REGRESSION:` lines names a
file this change touches. The regressions listed are `WasmSandbox.cpp`, `Vst3Host.cpp`, `TakeLane.cpp`,
`ArrangeToolsTest.cpp`, `CollabTransportTest.cpp`, `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`,
`ControlCommandsMixerStrip.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsSessionRecordLand.cpp`.
`handleRenderOffline` appears in the complexity table at 12 CCN (`gate-complexity.log:60`) and is not a
regression line; the edit replaces one `?:` with one `if`, so its CCN is unchanged. The ratchets could
not be compared against the base listing (that cache lives outside the jail), so this is a reading of
the raw output, not the script's classification.

## Blocker

The lane build tree `build/` was configured with `WANT_WASM=OFF` (`build/CMakeCache.txt:842`), so
`ControlWasmRenderTest` is not a target and `zene` has no `wasm.*` commands compiled in. The wasmtime
C API exists read-only at
`zene-bunny-integration/third_party/wasmtime` (`include/wasmtime.h`, `lib/libwasmtime.so`), so the
parent could configure this tree with `-DWASMTIME_ROOT=<that path>` and then:

```
CMAKE_BUILD_PARALLEL_LEVEL=4 cmake --build build --target ControlWasmRenderTest
cd build/tests && ctest -R '^ControlWasmRenderTest$' --output-on-failure
```

I did not reconfigure: the `cmake` shim answers `configuring is the parent's job`. The ccache error
(`ccache: error: failed to create temporary file ... Permission denied`) also needs `CCACHE_DISABLE=1`
or a writable ccache temp dir; with it the reader target builds.

### Open for the command-level run

- `tests/src/core/ControlWasmRenderTest.cpp`: three new cases. `renderOfflineRefusesASampleRateThatIsNotAFiniteFloatRate`
  (`1e300`, `-1`, `0`, `1e-60`; expects `InvalidArgs`), `renderOfflineCarriesAFiniteSampleRateToTheRender`
  (`44100` accepted and echoed), and `processRefusesASampleRateThatIsNotAFiniteFloatRate` (loads the NaN
  module with `wasm.load`, then `wasm.process` with `1e300`). Expected RED on HEAD: the first and third
  return `ok`. Not run. The `wasm.load` invocation from a test is an assumption I have not checked.
- `ControlCommandsWasm.cpp`: the syntax check reports only `registerWasmEditCommands` (pre-existing at HEAD,
  see table). The file was therefore not fully compiled here, and the two call-site edits are checked
  by reading only.

## Self-check

- **RED/GREEN, reverted product change.** Reader level: proven. The reader was mutated to HEAD behaviour;
  `ControlSampleArrayTest` exit 8 (`test-3-red-mutant.log`), restored byte-identical, exit 0 (`test-4-green-restored.log`).
  The command-level test has no RED/GREEN evidence: it was not built. **Not proven** at command level.
- **Deterministic.** Yes. No clock, sleep or thread. Inputs are fixed JSON numbers and strings
  (`1e300`, `-1`, `0`, `1e-60`, `FLT_MAX*2`, `"48000"`); the reader test uses a sentinel (`12345.0f`) to show a refusal wrote nothing.
- **Fix at the cause and complete.** The unchecked read was at two sites, `ControlCommandsWasmRender.cpp:218`
  (render_offline) and `ControlCommandsWasm.cpp:299` (process). Both now call the one reader. Grep of the
  wasm command sources shows no other input read of `sample_rate` (the remaining hits are output-only,
  e.g. `ControlCommandsWasm.cpp:270`). The schema is unchanged on purpose, so the snapshot canary does not move.
- **No collateral damage.** An absent `sample_rate` still gives 48000. Any finite rate in `(0, FLT_MAX]` is
  passed through unchanged (`aFinitePositiveSampleRateIsRead`). The refused inputs were all rendered before
  this change. The one in-tree caller that sets a rate, `tests/control-wasm-sandbox.py:83,356`, sends
  44100, which is still accepted. The default (48000) is unchanged. Neither handler touches project data: each reads its
  arguments and the sandbox host and returns JSON (`handleRenderOffline`, `handleProcess`); `render_offline`'s
  description says "Writes nothing".
- **Verdict is not ALREADY-FIXED or FALSE-POSITIVE**, so no code citation is needed beyond the evidence above.
- **Gates.** The six simple gates exit 0 and the two ratchets exit 1 for reasons in other files (above). The
  script `feature_check.py` could not run from this write-jail, so the gates item of `submit_check.py` stays FAIL until
  the parent runs `feature_check.py` from outside it.
