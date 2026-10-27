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
- `tests/src/core/Lv2StateTest.cpp:304-338` - `aFailedStateRestoreIsReported` covers the
  parse-failure report. It passes at HEAD and with the test change below.

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

## Test change (round 2)

Review asked for a test to be added or changed. I added one assertion to the existing
`aFailedStateRestoreIsReported` (`tests/src/core/Lv2StateTest.cpp:338`):

    QVERIFY(!lv2Controls->control(0)->restoreState(QString()));

It pins the refusal contract the caller depends on: an empty document is refused (`false`), so
the load's error path sees it. The test runs on the in-tree LV2 fixture instrument
(`tests/data/lv2-test-instrument`), which is built and loaded in this environment, so the test is
not skipped here.

No product change is part of this round. The mutation probe below was a temporary edit that was
reverted exactly; it is not a fix.

## Tests run

| Command (from the worktree unless noted) | Log (`new-p23-144.r2/logs`) | EXIT | Result |
| --- | --- | --- | --- |
| `CCACHE_TEMPDIR=$PWD/build/ccache-tmp cmake --build build --target Lv2StateTest -j4` (test edited, product unchanged) | `build-3.log` | 0 | built |
| `QT_QPA_PLATFORM=offscreen ./Lv2StateTest` (in `build/tests`) | `qtest-3.log` | 0 | Totals: 7 passed, 0 failed, 3 skipped |
| Mutation probe: `src/core/lv2/Lv2Proc.cpp:816` `return false` changed to `return true` (diff saved as `mutant-1.patch`) → `cmake --build ...` | `build-4-mutant.log` | 0 | built |
| `QT_QPA_PLATFORM=offscreen ./Lv2StateTest` against the mutant | `qtest-4-mutant.log` | **1** | `FAIL! : Lv2StateTest::aFailedStateRestoreIsReported() '!lv2Controls->control(0)->restoreState(QStringLiteral("this is not a state document"))' returned FALSE`; Totals: 6 passed, 1 failed, 3 skipped |
| Mutation reverted exactly (`git diff -- src/core/lv2/Lv2Proc.cpp` empty) → `cmake --build ...` | `build-5-restored.log` | 0 | built |
| `QT_QPA_PLATFORM=offscreen ./Lv2StateTest` after the revert | `qtest-5-restored.log` | 0 | Totals: 7 passed, 0 failed, 3 skipped |
| `ctest -R '^Lv2StateTest$' --output-on-failure` (from `build/tests`) | `test-5-ctest.log` | 0 | 100% tests passed, 1 of 1 |

Earlier round-1 runs at HEAD, for reference: `build-2.log` (EXIT 0), `test-2.log` (EXIT 0),
`qtest-1.log` (EXIT 0), in `new-p23-144.r1/logs`.

Per QTest function (after the test change): `aFailedStateRestoreIsReported` PASS,
`malformedStateInstanceAttributeIsRefused` PASS, `rejectsAnEventWhosePaddingDoesNotFit` PASS.
`aPluginsOwnStateRoundTrips`, `theStateElementIsWrittenExactlyWhenThereIsState` and
`aSecondInstanceIsFoundByTheFirstsUri` SKIP, because the environment has no installed LV2 effect
that implements the State interface. **The plug-in-refusal and success round-trip paths are
therefore not exercised by any test that ran here.**

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (log `gates-2.log`)
exited 1 before any gate ran. It tries to write its per-gate logs to
`zene-studio-program/zene-qwen-new-p23-144-feature-check/`. That folder is outside the write-jail,
and writing `fork-sources.log` there fails with `PermissionError`. The script was not changed and
I did not write into that folder. (The folder exists and was created by an earlier run, but it is
not a path this lane may write to.)

The same gate scripts were run directly from the worktree, with logs in the lane folder
(`new-p23-144.r2/logs`):

| Gate script | Log | EXIT | Result |
| --- | --- | --- | --- |
| `bash tests/fork-sources-gate.sh` | `gate-fork-sources-3.log` | 0 | PASS |
| `bash tests/all-sources-reproduce.sh` | `gate-all-sources-3.log` | 0 | PASS |
| `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-3.log` | 0 | PASS |
| `tests/scripted/check-namespace` | `gate-check-namespace-3.log` | 0 | PASS |
| `bash tests/no-tautology-gate.sh` | `gate-no-tautology-3.log` | 0 | PASS |
| `python3 tests/doc-figures-gate.py` | `gate-doc-figures-4.log` | 0 | PASS |
| `bash tests/complexity-gate.sh --check` | `gate-complexity-3.log` | 1 | 7 `REGRESSION` lines, none naming a changed file |
| `bash tests/file-length-gate.sh --check` | `gate-file-length-3.log` | 1 | 22 `REGRESSION` lines, none naming a changed file |

The two ratchets exit 1 on items that already exist on the train. The branch's changes against
its base are `tests/src/core/Lv2StateTest.cpp` and the report
(`git diff --name-only 639e1d23e..HEAD`), and no `REGRESSION` line names either file. Under the
script's own rule (`feature_check.py` lines 84-87) those two rows PASS. The automated pre-submit
gate row also passed in the review round.

## Self-check

Round 2 answers, against the pre-submit check that failed on "tests added or changed".

1. **Regression test fails with the product change reverted and passes with it (RED/GREEN).**
   There is no product fix, so there is no product change to revert. The closest evidence is the
   mutation probe: with `Lv2Proc.cpp:816` mutated to `return true` (the behaviour the finding
   describes), `Lv2StateTest` exits **1** (`qtest-4-mutant.log`), and the failing assertion is the
   existing one at `Lv2StateTest.cpp:336`. With the line restored, it exits **0** (`qtest-5-restored.log`).
   The new assertion at line 338 sits after line 336, so it is not the one that trips in that
   probe. It also would not trip on an empty-document mutation, because lilv returns `nullptr` for
   an empty string, so the return value is `false` either way. It is a pin on the contract, not a
   RED test for a bug.
2. **The test is deterministic.** It uses no clock, sleep, thread or race. Its inputs are literal
   strings (`QString()`, `"this is not a state document"`) and the fixture instrument loaded from
   the fixed URI `urn:zene:test:lv2-instrument`. Its outcome depends only on `restoreState`'s return value.
3. **Fix at the cause; callers and siblings checked.** No fix was made. Callers of
   `Lv2Proc::restoreState` (`rg -n "restoreState\("` over `src include plugins tests`): the
   production caller is `src/core/lv2/Lv2ControlBase.cpp:242`, and the direct test calls are
   `tests/src/core/Lv2StateTest.cpp:197`, `:201`, `:336` and `:338`. The production caller already
   reports a refused payload (`Lv2ControlBase.cpp:237-249`). The sibling `saveState` guards with
   `lilv_plugin_has_extension_data` (`Lv2Proc.cpp:792-793`); `restoreState` does not. That is a
   difference, but it changes nothing for the verified cases, and I did not change it.
4. **No collateral damage.** No product behaviour changed, so old projects and saved data load as
   before. The only change is one added assertion in an existing test, which strengthens it. No
   limit or check was added.
5. **Verdict evidence (the claim is not ALREADY-FIXED or FALSE-POSITIVE).** The evidence is the
   file:line citations in "Evidence": `Lv2Proc.cpp:810-823` (restore call and its void result),
   `/usr/include/lilv-0/lilv/lilv.h:1637-1638` (`void lilv_state_restore`), and
   `Lv2ControlBase.cpp:237-264` (the existing report path). The verdict is NEEDS-CONTEXT, so the
   claim-only exemption in `submit_check.py` does not apply, and the test change above is what
   addresses the "tests added or changed" row.
