# Cluster new-p23-144: Lv2Proc::restoreState reports success when lilv_state_restore fails

**Verdict: NEEDS-CONTEXT**

The finding's mechanism does not exist: `lilv_state_restore()` returns `void`, so there is no
result to check. The failure it describes for an unparseable document is already reported
(cluster new-p17-83). What is left - a document that parses but that the plug-in refuses to
apply - cannot be observed through lilv's API, and fixing it is a design decision, so no product
code was changed.

## Evidence

- `src/core/lv2/Lv2Proc.cpp:810-823` - `restoreState()` returns `false` for an empty document
  (line 812) and when `lilv_state_new_from_string()` returns `nullptr` (line 816). It then calls
  `lilv_state_restore()` (line 819) and returns `true` (line 822). The restore call produces no
  value that the function could check.
- `/usr/include/lilv-0/lilv/lilv.h:1637-1638` - `LILV_API void lilv_state_restore(...)`. The
  installed library is `/usr/lib/liblilv-0.so.0.28.0`. The finding's option "check the
  lilv_state_restore result" cannot be implemented against this API.
- `/usr/include/lilv-0/lilv/lilv.h:1522` - `lilv_state_get_plugin_uri()` exists, but
  `restoreState()` never compares the document's plug-in URI with `m_plugin`. The lilv doc comment
  on `lilv_state_restore` says the state "must apply to the correct plugin". What lilv does with a
  document that names another plugin is **not verified** here.
- `src/core/lv2/Lv2ControlBase.cpp:237-249` - `loadSettings()` already reports a refused
  non-empty `<lv2state>` payload through `reportStateRestoreFailure()` (lines 257-264), which calls
  `Song::collectError()`. This is the new-p17-83 fix.
- `tests/src/core/Lv2StateTest.cpp:304-336` - `aFailedStateRestoreIsReported` covers the
  parse-failure report. It passes at HEAD (see the test run below).

## Why no fix was made

1. **The stated mechanism is false.** `lilv_state_restore()` has no return value, so "treat the
   failed restore as failure" has no failure signal to read from it.
2. **The plug-in-refusal case is not visible to the host through lilv.** lilv applies the state
   and does not return the plug-in's own status to the caller. Observing a refusal would mean the
   host calling the plug-in's `LV2_State_Interface::restore` itself, which reimplements lilv's
   restore. That is not a small change at this call site.
3. **A narrower host-visible check exists, but it changes what loads.** Comparing
   `lilv_state_get_plugin_uri()` with `lilv_plugin_get_uri(m_plugin)` would refuse documents that
   name another plugin. Whether to refuse them, and whether a document with no `appliesTo` is
   still accepted, is a compatibility decision for the owner. It was not made here, because the
   brief says not to invent a behaviour change.

## Tests run

No product file changed, so there was no RED/GREEN cycle and no regression test was added.
The existing `Lv2StateTest` was run to check the already-fixed parse-failure path.

| Command (from the worktree unless noted) | Log | EXIT |
| --- | --- | --- |
| `cmake --build build --target Lv2StateTest -j4` | `build-1.log` | 2 (ccache temp dir under `zene-bunny-fix/dsh` denied; not a code error) |
| `CCACHE_TEMPDIR=$PWD/build/ccache-tmp cmake --build build --target Lv2StateTest -j4` | `build-2.log` | 0 |
| `cd build/tests && ctest -R '^Lv2StateTest$' --output-on-failure` | `test-2.log` | 0 (1 of 1 ctest entry passed) |
| `QT_QPA_PLATFORM=offscreen ./Lv2StateTest` (in `build/tests`) | `qtest-1.log` | 0 (Totals: 7 passed, 0 failed, 3 skipped) |

Per QTest function: `aFailedStateRestoreIsReported` PASS, `malformedStateInstanceAttributeIsRefused`
PASS. `aPluginsOwnStateRoundTrips`, `theStateElementIsWrittenExactlyWhenThereIsState` and
`aSecondInstanceIsFoundByTheFirstsUri` SKIP, because this environment has no installed LV2 effect
that implements the State interface. **The plug-in-refusal path is therefore not exercised by any
test that ran here.**

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exited 1 before running
any gate. Its log is `gates-1.log`. It tries to create `<worktree>/../<name>-feature-check`, which
is `zene-studio-program/zene-qwen-new-p23-144-feature-check`, and that directory is outside the
write-jail (`PermissionError`). The script was not changed.

The same gate scripts were run directly from the worktree, with logs in the lane folder:

| Gate script | Log | EXIT | Result |
| --- | --- | --- | --- |
| `bash tests/fork-sources-gate.sh` | `gate-fork-sources-2.log` | 0 | PASS |
| `bash tests/all-sources-reproduce.sh` | `gate-all-sources-2.log` | 0 | PASS |
| `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-2.log` | 0 | PASS |
| `tests/scripted/check-namespace` | `gate-check-namespace-2.log` | 0 | PASS |
| `bash tests/no-tautology-gate.sh` | `gate-no-tautology-2.log` | 0 | PASS |
| `python3 tests/doc-figures-gate.py` | `gate-doc-figures-2.log` | 0 | PASS |
| `bash tests/complexity-gate.sh --check` | `gate-complexity-2.log` | 1 | 7 `REGRESSION` lines, none in a changed file |
| `bash tests/file-length-gate.sh --check` | `gate-file-length-2.log` | 1 | 22 `REGRESSION` lines, none in a changed file |

The two ratchet gates exit 1 on items that already exist on the train. The branch has no product
changes against its base, so `git diff --name-only <base>..HEAD` is empty and no item is in a file
the branch changed. Under the script's own rule (`feature_check.py`, lines 84-87) those two rows
PASS. The `REGRESSION` lines do not mention `lv2`.

## Self-check

`submit_check.py` (log `submit-check-1.log`, EXIT 1) reported:

- PASS commits on the branch; PASS working tree clean; PASS report present.
- FAIL **tests added or changed**. This is expected for this verdict. No product file changed, so
  no regression test was added. The claim-only exemption in `submit_check.py:94-97` applies only to
  ALREADY-FIXED or FALSE-POSITIVE, and this verdict is NEEDS-CONTEXT. A test that pins the
  unverified behaviour for a document that names another plugin would be an invented contract, so
  none was added. The owner decision in "Why no fix was made" (3) comes first.
- FAIL **gates (feature_check.py)**. This is an environment failure, not a code failure:
  `feature_check.py` cannot create `zene-studio-program/zene-qwen-new-p23-144-feature-check`
  (PermissionError, `gates-1.log`), which is outside the write-jail. The equivalent per-gate runs are
  in the Gates table above: six PASS, and the two ratchets exit 1 on items this branch does not touch.

ASK items:

1. **Regression test fails with the fix reverted and passes with it (RED/GREEN).** Not applicable.
   There is no product change to revert. The existing `aFailedStateRestoreIsReported` passes at HEAD
   (`qtest-1.log`, EXIT 0), which covers the parse-failure part only.
2. **Test is deterministic.** No test was added or changed.
3. **Fix at the cause, every caller checked.** No fix was made. The callers of
   `Lv2Proc::restoreState` were checked (`rg -n "restoreState\("` over `src include plugins tests`):
   production caller `src/core/lv2/Lv2ControlBase.cpp:242`, and direct test calls at
   `tests/src/core/Lv2StateTest.cpp:197`, `:201` and `:336`. The caller already reports a refused
   payload (`Lv2ControlBase.cpp:237-249`).
4. **No collateral damage.** No behaviour changed, so old projects and saved data load as before.
5. **Verdict evidence (the claim is not ALREADY-FIXED or FALSE-POSITIVE).** The evidence is the
   file:line citations above: `Lv2Proc.cpp:810-823` (restore call and its void result),
   `/usr/include/lilv-0/lilv/lilv.h:1637-1638` (`void lilv_state_restore`), and
   `Lv2ControlBase.cpp:237-264` (the existing report path).
