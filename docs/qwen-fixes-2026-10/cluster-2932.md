FALSE-POSITIVE

# Cluster 2932 - `render.stems` `tail_bars` cast before its range and integer form are checked

## Verdict

The finding says a call with `tail_bars: 1.9` or a huge positive number reaches the handler's `static_cast<int>` in `ControlCommandsRenderStems.cpp:133-142`. On this branch it does not. Every route into the handler runs the registered argument schema first, and that schema refuses a fraction, a value above 64 and a value above `INT_MAX` with `InvalidArgs` before the handler is called. No product code changed. The commit adds assertions for the finding's own inputs to an existing test.

Base: `5c1c6715d9` (`050/m0-train-19`). Branch: `bugs/qwen-2932`.

## Evidence (file:line)

| Claim | Where |
| --- | --- |
| Dispatch checks args against the schema before the handler | `src/core/ControlRegistry.cpp:277-281` (`validateArgs` returns `InvalidArgs`; the handler is only reached after it passes) |
| `integer` = a double equal to its floor, so `1.9` is refused | `src/core/ControlSchema.cpp:50-52` |
| `minimum` / `maximum` are checked on present values, so `65` and `3e9` are refused | `src/core/ControlSchema.cpp:84-98` |
| The `tail_bars` contract is `integerProperty(0, 64)` | `src/core/ControlCommandsRenderStems.cpp:494` (args), `:503` (result) |
| The handler's own check is negative-only, so it would not catch these values by itself | `src/core/ControlCommandsRenderStems.cpp:137-145` |
| The handler is registered only as `cmd.handler`, reached through `invoke` | `src/core/ControlCommandsRenderStems.cpp:507` |
| Direct `->handler(...)` callers do not reach `render.stems` | `src/core/ControlCommandsNoteAcross.cpp:52-58` (allowlist without `render.stems`), `:130` (validated at `:85`); `src/core/ControlCommandsRecordTools.cpp:66-69` (only `transport.punch_set` / `punch_clear`, `:107-114`); `src/core/ControlCommandsSessionPerform.cpp:267` (`open` only) |
| The CLI `exportstems --tail-bars` cannot take a fraction or a value past `INT_MAX` | `src/core/main.cpp:908-914`: `QString::toInt(&ok)` fails on `1.9` or on a value past `INT_MAX`, which becomes a usage error |

The stated failure path is therefore unreachable. The schema is the single enforcement point for these arguments, which is the contract the codebase states (`ControlRegistry.cpp:277`).

### Not changed, noted

- The CLI caps `--tail-bars` at `bars < 0` only (`src/core/main.cpp:910`). It has no 64 ceiling, while the control surface does. That is a different divergence, outside this finding, and changing it would change CLI behaviour, so it is left alone.
- The finding's suggested fix, a second `0..64` check in `parseStemArgs`, is not added. It would be dead code today, and it would copy the bound into a second place that can drift from the schema. If the owner wants defence in depth anyway, it is a small change at `ControlCommandsRenderStems.cpp:137-146`.

## Regression test

No RED run is possible here. A regression test fails only when the defect is reachable, and this one is not. The commit instead pins the contract the verdict relies on. In `tests/src/core/ControlVerbInverseTest.cpp`, `renderStemsRefusesBadArgumentsBeforeAnyRender` already refused `tail_bars: -1` with `InvalidArgs` and asserted that no output directory was created. It now also asserts, through `ControlRegistry::invoke`, that `1.9`, `3.0e9` and `65` are refused with `InvalidArgs` and a message naming `tail_bars`.

Why this is not vacuous: `parseStemArgs` (`:137-145`) checks only the sign. Without the schema, `1.9` would be accepted as 1, and `65` and `3.0e9` would pass the sign check and go on into the export. The `InvalidArgs` result for those inputs can therefore only come from the schema. This is reasoned from the code. I did not run that removal as a mutation.

Test file: `tests/src/core/ControlVerbInverseTest.cpp` is a fork-new file (`tests/fork-sources.txt:2409`), so no `upstream-modifications.txt` row is needed. No new control command, no change to `ControlCommandsSnapshot`, no realtime path.

## Commands and results

All builds ran `-j4` or less. The first build failed for an environment reason (below); the rest are real results.

| # | Command (run from the worktree unless noted) | EXIT | Log (LOG FOLDER) |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target ControlVerbInverseTest -j4 > build-1-baseline.log 2>&1` | 2 | `build-1-baseline.log` |
| | ccache could not write its temp file under `~/.local/share/zene-bunny-fix/dsh/...` (write-jail). Not a code failure. | | |
| 2 | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-2932 cmake --build build --target ControlVerbInverseTest -j4 > build-2-baseline.log 2>&1` (unmodified test) | 0 | `build-2-baseline.log` |
| 3 | `cd build/tests && ctest -R '^ControlVerbInverseTest$' --output-on-failure > test-1-baseline.log 2>&1` (unmodified test) | 0 | `test-1-baseline.log`: 1/1 passed |
| 4 | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-2932 cmake --build build --target ControlVerbInverseTest -j4 > build-3-pin.log 2>&1` (with the new assertions) | 0 | `build-3-pin.log` |
| 5 | `cd build/tests && ctest -R '^ControlVerbInverseTest$' --output-on-failure > test-2-pin.log 2>&1` | 0 | `test-2-pin.log`: 1/1 passed |
| 6 | `cd build/tests && ZENE_CONTROL_CHECK_RESULTS=1 ./ControlVerbInverseTest renderStemsRefusesBadArgumentsBeforeAnyRender -v2 > test-3-verbose.log 2>&1` | 0 | `test-3-verbose.log`: `Totals: 3 passed`; the loop's `QCOMPARE` and `QVERIFY` ran 3 times |
| 7 | `cd build/tests && ZENE_CONTROL_CHECK_RESULTS=1 ./ControlVerbInverseTest > test-4-full.log 2>&1` | 0 | `test-4-full.log`: `Totals: 10 passed, 0 failed`, 10 `PASS` lines |
| 8 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > gates-1.log 2>&1` | 1 | `gates-1.log`: `PermissionError`. The script writes its logs to `<worktree>-feature-check`, outside the write-jail. Nothing was checked by this run. |
| 9 | `python3 -I gate_wrapper.py <worktree> <LOG>/gates > gates-2-table.log 2>&1` | 0 | `gates-2-table.log` |

Run 9 uses `gate_wrapper.py`, a file in the LOG folder. It imports `feature_check.py` and runs the same gate list and the same comparison (`main()` logic), with per-gate output in `<LOG>/gates/`. It does not change the gate, its limits or its baselines. The base listing comes from the existing cache for `5c1c6715d9`. Result:

```text
feature-check zene-qwen-2932 (base 5c1c6715d, 1 changed files; logs in .../logs/gates)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

Product diff for the step-4b check: `git diff --name-only 5c1c6715d9 -- src include` is empty, so there is no product patch to revert (`fix.patch` not produced).

## Files committed

- `tests/src/core/ControlVerbInverseTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-2932.md`
