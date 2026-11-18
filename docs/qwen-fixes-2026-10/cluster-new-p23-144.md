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
- `src/core/lv2/Lv2Proc.cpp:852-864` - `mapPathUnchanged()` and `freePathUnchanged()`: the state
  path features. For a state read from a string (no directory, empty path map), lilv's
  `absolute_path` returns its input; returned path memory is freed through `state:freePath`.
- `src/core/lv2/Lv2Proc.cpp:886-909` - `decodeStateProperty()`: reads each object with
  `sratom_read()` into an LV2 forge, the same way lilv's `new_state_from_model` does; flags are
  `LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE`, or `LV2_STATE_IS_POD` for `atom:Path`.
- `src/core/lv2/Lv2Proc.cpp:912-948` - `decodeStateProperties()`: selects the same `pset:Preset`
  subject Lilv parses, then decodes its `state:state` properties. An unrelated `state:state` triple
  elsewhere in the Turtle document is ignored.
- `src/core/lv2/Lv2Proc.cpp:951-968` - `setPrefix()` / `setStatePrefixes()`: the seven prefixes lilv
  declares when it reads a state string (atom, lv2, pset, rdf, rdfs, state, xsd). Without them a
  document lilv accepts (it uses `pset:` and `lv2:` names) is refused here.
- `src/core/lv2/Lv2Proc.cpp:970-988` - `decodeStateDocument()`: parses the document with serd/sord.
- `src/core/lv2/Lv2Proc.cpp:994-1020` - `restorePluginState()`: calls the plug-in's
  `LV2_State_Interface::restore()` itself with decoded properties, the host features, and Lilv's
  `state:mapPath` / `state:freePath` features when the host did not already provide them. It returns
  the plug-in's status and returns `LV2_STATE_ERR_NO_FEATURE` when the plug-in has no state interface.
- `src/core/lv2/Lv2Proc.cpp:1024-1040` - `Lv2Proc::restoreState()`: lilv still validates the document
  (so what counts as a state is unchanged); the properties are decoded; the restore runs under
  `requestChangeInModel()`; the function returns `status == LV2_STATE_SUCCESS`.
- `include/Lv2Proc.h:121-125` - the declaration's comment now states the refusal case.
- `src/core/lv2/Lv2ControlBase.cpp:237-249` - unchanged: a `false` from `restoreState()` is reported
  through `reportStateRestoreFailure()` (lines 257-264).
- Fixture: `tests/data/lv2-test-instrument/lv2-test-instrument.c:48-50` (keys) and `:101-106` (URIDs),
  `save()` and `restore()` below them, and `extension_data()` returning `s_state` for
  `LV2_STATE__interface`. `lv2-test-instrument.ttl` declares `lv2:extensionData state:interface`.
  `restore()` returns `LV2_STATE_ERR_UNKNOWN` for a state that names `urn:zene:test:refuse`, and
  `LV2_STATE_ERR_BAD_TYPE` when `urn:zene:test:accepted` does not arrive as the string it was saved as.
- Test: `tests/src/core/Lv2StateTest.cpp:388-465` - `aPluginRefusingAStateIsReported()`. It checks,
  in order: the accepted document restores (`true`); the plug-in's own save restores (`true`); a
  syntactically valid document for this plug-in that names the refused key returns `false`; the project
  load (`Song::errors()`) reports the refusal and names the plug-in; unrelated state triples do not
  affect the selected preset; an `atom:Path` property restores through both path features.

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

### QA follow-up on the final tree

The final build (`cmake --build build --target zene lv2instrument lv2-test-instrument Lv2StateTest
ControlDeviceCatalogueTest -j12`) exited 0. From `build/tests`,
`ctest -R '^(Lv2StateTest|Lv2InstrumentKeyRegressionTest)$' --output-on-failure` exited 0 (2/2).
With both product files reset to the branch base, the same targets built (EXIT 0) and that CTest
exited 8 at `!proc->restoreState(documentWith(RefusedKey))`; after restoring the product changes,
the targets built again (EXIT 0) and CTest passed (EXIT 0). The final requested `feature_check.py`
run exited 0, with all six gates passing and no new complexity or file-length items.

## Gates

The lane's initial `feature_check.py` invocation exited 1 because its write-jail blocked its sibling
log directory. QA reran the requested command on the final tree; it exited 0 with all gates passing
and no new complexity or file-length items. The same gate scripts had also been run directly from the
worktree (logs `*-5.log`, `check-namespace-7.log`, `doc-figures-6.log`):

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
   type and first byte of the accepted value, plus a fixed `atom:Path` value. Nothing depends on the time or on the order of
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
   sratom decoding as lilv's `new_state_from_model`, the same flags, the same path mapping for a
   string-loaded state, and the host features plus the mapPath/freePath features Lilv adds. No limit or check on
   sizes or counts was added; the only new check is the plug-in's own status. Behaviour changes that
   are intended, and that a reviewer should know about:
   - a plug-in that returns a non-success status for a document that loads today now produces a load
     error (the state is still applied as before; the error is new). That is the finding's contract;
   - a document naming a plug-in without a state interface now reports a load error (it was silent).
     Zene never writes such a document, because `saveState()` returns nothing for those plug-ins.
5. **Verdict evidence (FIXED).** `src/core/lv2/Lv2Proc.cpp:1024-1040` (the status is returned),
   `:994-1020` (the plug-in's own restore and path features), `Lv2ControlBase.cpp:237-249` (the
   report), and the test at `tests/src/core/Lv2StateTest.cpp:388-465` with the RED/GREEN runs above.

## Open points

- **Plug-in URI mismatch.** A document that names another plug-in is still not compared with
  `m_plugin` (`lilv_state_get_plugin_uri()` exists, `lilv.h:1522`). It is now handed to this plug-in's
  restore, which decides from its own keys. Whether to refuse such documents outright, and whether a
  document with no `appliesTo` is still accepted, is still a compatibility decision for the owner.
- **Decoder parity is tested for strings and paths.** The fixture round-trips an `atom:String` and
  separately restores an `atom:Path` value through both path features. Other atom types (numbers,
  vectors, objects) go through the same sratom path as lilv, but no test here decodes them.
- **Map-path parity is taken from lilv's source**, fetched from `lv2/lilv` `master`
  (`src/state.c`: `absolute_path` returns its input for a state with no directory and an empty
  path map). The installed lilv is 0.28.0; that source was not compared with it locally.
- **Skipped tests.** `aPluginsOwnStateRoundTrips`, `theStateElementIsWrittenExactlyWhenThereIsState`
  and `aSecondInstanceIsFoundByTheFirstsUri` still skip, because no installed LV2 effect implements
  the State interface here. The fixture's round trip (`aPluginRefusingAStateIsReported`) runs instead.
