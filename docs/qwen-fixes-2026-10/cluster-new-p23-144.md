# Cluster new-p23-144: Lv2Proc::restoreState reports success when the plug-in refuses the state

**Verdict: FIXED**

`restoreState()` used to return `true` whenever lilv could parse the document, because
`lilv_state_restore()` is `void` and discards the plug-in's `LV2_State_Interface::restore()` status.
A state the plug-in refused was therefore reported as loaded, and the processor stayed at its default.
The fix drives the plug-in's restore from the host, keeps its status, and returns `false` for any
non-success status. The existing load path (`Lv2ControlBase::loadSettings()`, cluster new-p17-83)
then reports the failure, naming the plug-in. The parse-failure path is unchanged.

Scope and limits are listed under "Open points" below.

## Evidence (after the fix)

- `src/core/lv2/Lv2Proc.cpp:835-847` - `retrieveStateProperty()`: the retrieve callback the plug-in
  receives; it serves the decoded properties by key.
- `src/core/lv2/Lv2Proc.cpp:851-854` - `mapPathUnchanged()`: the map-path feature. For a state read
  from a string (no directory, empty path map) lilv's `absolute_path` returns its input, so this is
  the same mapping (checked against lilv's `state.c` source, not a local build; see Open points).
- `src/core/lv2/Lv2Proc.cpp:876-899` - `decodeStateProperty()`: reads each object with
  `sratom_read()` into an LV2 forge, the same way lilv's `new_state_from_model` does; flags are
  `LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE`, or `LV2_STATE_IS_POD` for `atom:Path`.
- `src/core/lv2/Lv2Proc.cpp:902-926` - `decodeStateProperties()`: finds the `state:state` node and
  decodes every predicate/object of it.
- `src/core/lv2/Lv2Proc.cpp:929-945` - `setPrefix()` / `setStatePrefixes()`: the seven prefixes lilv
  declares when it reads a state string (atom, lv2, pset, rdf, rdfs, state, xsd). Without them a
  document lilv accepts (it uses `pset:` and `lv2:` names) is refused here.
- `src/core/lv2/Lv2Proc.cpp:948-966` - `decodeStateDocument()`: parses the document with serd/sord.
- `src/core/lv2/Lv2Proc.cpp:971-990` - `restorePluginState()`: calls the plug-in's
  `LV2_State_Interface::restore()` itself with the decoded properties, the host features and a
  map-path feature, and returns the plug-in's status. It returns `LV2_STATE_ERR_NO_FEATURE` when the
  plug-in has no state interface.
- `src/core/lv2/Lv2Proc.cpp:994-1011` - `Lv2Proc::restoreState()`: lilv still validates the document
  (so what counts as a state is unchanged); the properties are decoded; the restore runs under
  `requestChangeInModel()`; the function returns `status == LV2_STATE_SUCCESS`.
- `include/Lv2Proc.h:121-125` - the declaration's comment now states the refusal case.
- `src/core/lv2/Lv2ControlBase.cpp:237-249` - unchanged: a `false` from `restoreState()` is reported
  through `reportStateRestoreFailure()` (lines 257-264).
- Fixture: `tests/data/lv2-test-instrument/lv2-test-instrument.c:48-49` (keys) and `:99-100` (URIDs),
  `save()` and `restore()` below them, and `extension_data()` returning `s_state` for
  `LV2_STATE__interface`. `lv2-test-instrument.ttl` declares `lv2:extensionData state:interface`.
  `restore()` returns `LV2_STATE_ERR_UNKNOWN` for a state that names `urn:zene:test:refuse`, and
  `LV2_STATE_ERR_BAD_TYPE` when `urn:zene:test:accepted` does not arrive as the string it was saved as.
- Test: `tests/src/core/Lv2StateTest.cpp:387-450` - `aPluginRefusingAStateIsReported()`. It checks,
  in order: the accepted document restores (`true`); the plug-in's own save restores (`true`); a
  syntactically valid document for this plug-in that names the refused key returns `false`; the project
  load (`Song::errors()`) reports the refusal and names the plug-in.

## Test runs (logs in `new-p23-144.r3/logs`)

RED and GREEN for the regression. Each command ran from the worktree; `build/` is the configured tree.

| Step | Command | Log | EXIT | Result |
| --- | --- | --- | --- | --- |
| RED | build fixture + test, product unchanged (`CCACHE_TEMPDIR=$PWD/build/ccache-tmp cmake --build build --target Lv2StateTest -j4`) | `build-red-1.log` | 0 | built |
| RED | `./Lv2StateTest` in `build/tests` | `qtest-red-1.log` | **1** | `FAIL! aPluginRefusingAStateIsReported() '!proc->restoreState(documentWith(RefusedKey))' returned FALSE` |
| GREEN attempt | `cmake --build build --target zene Lv2StateTest -j4` | `build-green-1.log` | 0 | built, no warnings in changed files |
| GREEN attempt | `./Lv2StateTest` | `qtest-green-1.log` | 1 | `FAIL! ... 'proc->restoreState(saved)' returned FALSE` - the plug-in's own save was refused |
| Root cause | temporary `qWarning` of the saved document (removed afterwards) | `qtest-debug-1.log` | 1 | saved doc uses `pset:`/`lv2:` names; the decoder lacked lilv's prefixes |
| GREEN | after `setStatePrefixes()`; `cmake --build ... Lv2StateTest -j4` | `build-green-2.log` | 0 | built |
| GREEN | `./Lv2StateTest` | `qtest-green-2.log` | **0** | 8 passed, 0 failed, 3 skipped |

Family and final runs:

| Command | Log | EXIT | Result |
| --- | --- | --- | --- |
| `cmake --build build --target InstrumentLatencyPdcTest Lv2ViewBaseLifetimeTest Lv2InstrumentKeyRegressionTest PluginPortsMigrationTest Lv2StateTest -j4` | `build-family-1.log` | 2 | `make: No rule to make target 'Lv2InstrumentKeyRegressionTest'`: that name is a ctest entry, not a target (it runs `ControlDeviceCatalogueTest`); make stopped there |
| `cmake --build build --target ControlDeviceCatalogueTest -j4` | `build-family-2.log` | 0 | built |
| `ctest -R '^Lv2StateTest$' ...` (earlier run) | `test-family-1.log` | 8 | 3 passed, 2 not run (targets above) |
| `ctest -R '^Lv2InstrumentKeyRegressionTest$' ...` | `test-family-2.log` | 0 | 1 of 1 passed |
| final: `cmake --build build --target zene lv2instrument Lv2StateTest InstrumentLatencyPdcTest Lv2ViewBaseLifetimeTest ControlDeviceCatalogueTest -j4` | `build-final-1.log` | 0 | built, 0 warnings |
| final: `ctest -R '^(Lv2StateTest\|InstrumentLatencyPdcTest\|Lv2ViewBaseLifetimeTest\|Lv2InstrumentKeyRegressionTest)$'` | `test-final-1.log` | 0 | 4 of 4 passed |
| final: `./Lv2StateTest` | `qtest-final-1.log` | 0 | 8 passed, 0 failed, 3 skipped |

`PluginPortsMigrationTest` was not built or run: its source has no LV2 references (`rg -i lv2` finds
none), so this change does not reach its code path.

### Reversal proof on the final diff (the procedure's step 4b)

The product diff is `src/core/lv2/Lv2Proc.cpp` plus the comment in `include/Lv2Proc.h`. Saved as
`fix-final.patch` (240 lines).

| Step | Command | Log | EXIT | Result |
| --- | --- | --- | --- | --- |
| reverse | `git apply -R fix-final.patch` | - | 0 | product files back to HEAD |
| rebuild | `cmake --build build --target zene Lv2StateTest -j4` | `build-final-reversed.log` | 0 | built |
| run | `./Lv2StateTest` | `qtest-final-reversed.log` | **1** | `FAIL! aPluginRefusingAStateIsReported() ... returned FALSE` |
| restore | `git apply fix-final.patch` | - | 0 | fix back in place |
| rebuild | `cmake --build build --target zene Lv2StateTest -j4` | `build-final-restored.log` | 0 | built |
| run | `./Lv2StateTest` | `qtest-final-restored.log` | **0** | 8 passed, 0 failed |
| ctest | `ctest -R '^Lv2StateTest$' --output-on-failure` | `test-final-restored.log` | **0** | 1 of 1 |

The project-load assertion (`errors` naming the plug-in) comes after the refusal assertion, so the
reversed run stops at the first one. It is exercised in the restored run only.

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (log `gates-final.log`)
exits 1 before any gate: it writes its per-gate logs into
`zene-studio-program/zene-qwen-new-p23-144-feature-check/`, outside the write-jail (`PermissionError`
on `fork-sources.log`). The script was not changed, and that folder was not written. The same gate
scripts were run directly from the worktree (final run, logs `*-5.log`, `check-namespace-7.log`,
`doc-figures-6.log`):

| Gate script | Log | EXIT | Result |
| --- | --- | --- | --- |
| `bash tests/fork-sources-gate.sh` | `gate-fork-sources-5.log` | 0 | PASS |
| `bash tests/all-sources-reproduce.sh` | `gate-all-sources-5.log` | 0 | PASS |
| `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-5.log` | 0 | PASS |
| `tests/scripted/check-namespace` | `gate-check-namespace-7.log` | 0 | PASS (see note) |
| `bash tests/no-tautology-gate.sh` | `gate-no-tautology-5.log` | 0 | PASS |
| `python3 tests/doc-figures-gate.py` | `gate-doc-figures-6.log` | 0 | PASS |
| `bash tests/complexity-gate.sh --check` | `gate-complexity-5.log` | 1 | 7 `REGRESSION` lines, none naming a changed file |
| `bash tests/file-length-gate.sh --check` | `gate-file-length-5.log` | 1 | 22 `REGRESSION` lines, none naming a changed file |

The two ratchet counts (7 and 22) are unchanged from the previous round. The changed files are
`include/Lv2Proc.h`, `src/core/lv2/Lv2Proc.cpp`, `tests/data/lv2-test-instrument/lv2-test-instrument.{c,ttl}`,
`tests/src/core/Lv2StateTest.cpp` and this report. No `REGRESSION` line names any of them, so under the
script's rule (`feature_check.py` lines 84-87) those rows pass.

Note on `check-namespace`: the first version failed with `Expected } before #endif`. The checker
strips `//` comments without respecting string literals, so the `http://` URIs in a braced table
hid the closing brace. The table is now one call per prefix (`setPrefix`), and the anonymous
namespace is written as the repo's other files write it (`namespace` then `{`).

## Self-check

1. **The regression test fails with the product change reverted and passes with it.** Yes. The
   reversal proof above: reversed, `Lv2StateTest` exits **1** at the refusal assertion
   (`qtest-final-reversed.log`); restored, it exits **0** (`qtest-final-restored.log`,
   `test-final-restored.log`).
2. **The test is deterministic.** No clock, sleep, thread or race. The documents are literal Turtle
   strings built from fixed URIs (`urn:zene:test:lv2-instrument`, `urn:zene:test:accepted`,
   `urn:zene:test:refuse`). The fixture's decisions depend only on the keys it is given and the atom
   type and first byte of the accepted value. Nothing depends on the time or on the order of
   threads; the audio thread is not involved (main thread; `requestChangeInModel()` as before).
3. **The fix is at the cause, with callers and siblings checked.** The cause is that lilv hides the
   plug-in's status, so the host makes the call itself. Callers of `Lv2Proc::restoreState`
   (`rg -n restoreState\(` over `src include plugins tests`): the production caller is
   `src/core/lv2/Lv2ControlBase.cpp:242`, which already reports a `false`; the tests call it directly
   (`tests/src/core/Lv2StateTest.cpp`). `lilv_state_restore` is no longer called anywhere in `src`
   (`rg lilv_state_restore src plugins include`: only comments). `saveState()` is unchanged. The sibling
   paths - parse failure, empty payload, malformed instance attribute, the existing report
   (`aFailedStateRestoreIsReported`) - all still pass in the family run.
4. **No collateral damage.** Which state is applied is unchanged: the same lilv validation, the same
   sratom decoding as lilv's `new_state_from_model`, the same flags, the same map-path mapping for a
   string-loaded state, and the same host features plus the map-path feature. No limit or check on
   sizes or counts was added; the only new check is the plug-in's own status. Behaviour changes that
   are intended, and that a reviewer should know about:
   - a plug-in that returns a non-success status for a document that loads today now produces a load
     error (the state is still applied as before; the error is new). That is the finding's contract;
   - a document naming a plug-in without a state interface now reports a load error (it was silent).
     Zene never writes such a document, because `saveState()` returns nothing for those plug-ins.
5. **Verdict evidence (FIXED).** `src/core/lv2/Lv2Proc.cpp:994-1011` (the status is returned),
   `:971-990` (the plug-in's own restore), `Lv2ControlBase.cpp:237-249` (the report), and the test at
   `tests/src/core/Lv2StateTest.cpp:387-450` with the RED/GREEN runs above.

## Open points

- **Plug-in URI mismatch.** A document that names another plug-in is still not compared with
  `m_plugin` (`lilv_state_get_plugin_uri()` exists, `lilv.h:1522`). It is now handed to this plug-in's
  restore, which decides from its own keys. Whether to refuse such documents outright, and whether a
  document with no `appliesTo` is still accepted, is still a compatibility decision for the owner.
- **Decoder parity is tested only for a string property.** The fixture's accepted value is a
  `atom:String`. Other atom types (numbers, paths, vectors, objects) go through the same sratom path
  as lilv, but no test here decodes them.
- **Map-path parity is taken from lilv's source**, fetched from `lv2/lilv` `master`
  (`src/state.c`: `absolute_path` returns its input for a state with no directory and an empty
  path map). The installed lilv is 0.28.0; that source was not compared with it locally.
- **Skipped tests.** `aPluginsOwnStateRoundTrips`, `theStateElementIsWrittenExactlyWhenThereIsState`
  and `aSecondInstanceIsFoundByTheFirstsUri` still skip, because no installed LV2 effect implements
  the State interface here. The fixture's round trip (`aPluginRefusingAStateIsReported`) runs instead.
- **The gate script.** `feature_check.py` cannot run inside this lane's write-jail (see Gates).
