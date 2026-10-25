FALSE-POSITIVE

# Cluster 4823 — Invalid port direction loses its validation error

## Decision

`writableModel` does discard the direction-specific `ControlResult` into a local `ignored` value at `src/core/ControlCommandsPorts.cpp:201-204`. That error is not lost on the registered `port.set_pin` request path: its schema at `src/core/ControlCommandsPorts.cpp:345-355` restricts `direction` to `in` and `out`. `ControlSchema.cpp:102-109` rejects values outside an enum, `ControlSchema.cpp:168-175` applies that check while validating each property, and `ControlRegistry.cpp:277-281` returns `InvalidArgs` before it dispatches the handler (`ControlRegistry.cpp:293-309`). The handler is registered only at `ControlCommandsPorts.cpp:367`.

`handlePortSetPin` is the only caller of `writableModel` (`ControlCommandsPorts.cpp:250`); its subsequent `matrixForDirection` call passes the caller's `error` (`:252`). The sibling `port.get_state` path does not call `writableModel`. Therefore a public `port.set_pin` request with `direction: "sideways"` gets the registry's typed `invalid_args` response before the local-result code can run. The finding describes a real local error-handling smell, but not a reachable command bug.

The existing socket test includes the invalid-direction request at `tests/control-ports-commands.py:202-203`. The attempted CTest run did not reach `check_validation`: `main` first calls `check_builtin_matrix` at `:306`, which stopped because this build has no loadable built-in effect. No product change or regression test was made, following the false-positive path in the procedure.

## Verification

| Check | Exact command | Result |
|---|---|---|
| Rebuild | `cmake --build build --target zene` (worktree root) | Exit 0; `logs/build-1.log` |
| Port command family | `ctest -R ControlPortsCommands --output-on-failure` (`build/tests`) | Exit 8; 1 test executed. The harness failed at “this build ships no loadable built-in effect” before the malformed-direction assertion; `logs/test-1.log`. |
| Initial feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-4823` | Exit 1. `fork-sources` and `all-sources-reproduce` failed because `tests/all-sources.txt` contained 1,148 vendored sfizz entries excluded by its own regeneration recipe; `logs/feature-check-2.log`. |
| Regenerate scope list | `{ git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)'; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py; } | LC_ALL=C sort -u > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4823.r1/logs/all-sources-generated-1.txt` | Exit 0. Retained the manifest header and replaced only its generated entry list; no product source changed. |
| Final registration gates | `bash tests/fork-sources-gate.sh`; `bash tests/all-sources-reproduce.sh` (worktree root) | Both exit 0; `logs/gate-sources-2.log` and `logs/gate-reproduce-1.log`. |
| Final feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-4823` | Exit 0; all eight gates pass in `logs/feature-check-5.log`. |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-4823 4823` | Exit 0; `RESULT: PASS - submit` in `logs/submit-check-1.log`. |
| Regression RED/GREEN | Not applicable: FALSE-POSITIVE; no product fix or new regression test. | The attempted family test was blocked by the missing built-in effect prerequisite above. No RED or GREEN result is claimed. |

Final feature-gate table:

| Gate | Result |
|---|---|
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS (no new item in changed files) |
| file-length | PASS (no new item in changed files) |

## Self-check

- Regression test fails with product change reverted and passes restored: not applicable; the verdict is FALSE-POSITIVE and there is no product change. The only attempted family run was `ctest -R ControlPortsCommands --output-on-failure` (exit 8) and stopped before `check_validation` because no loadable built-in effect was available.
- Deterministic regression test: none added. No timing, sleeps, races, or test seams were introduced.
- Cause, callers, and siblings: `writableModel` has one caller (`handlePortSetPin`, `ControlCommandsPorts.cpp:250`); that handler is only registered behind the schema-validating `ControlRegistry::invoke` path. The only other direction-matrix call in that handler passes `&error` (`:252`), while `port.get_state` does not use `writableModel`.
- Collateral effects and saved projects: no production or serialization code changed. `tests/all-sources.txt` was regenerated from its own declared recipe to restore the registration gate; it is test-scope metadata and does not affect project loading or audio behavior.
- FALSE-POSITIVE evidence: `port.set_pin` declares the enum at `ControlCommandsPorts.cpp:345-355`; enum validation and pre-handler `InvalidArgs` return are at `ControlSchema.cpp:102-109`, `ControlSchema.cpp:168-175`, and `ControlRegistry.cpp:277-281`.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-4823.md`
- `tests/all-sources.txt` (regenerated scope entries only, to make the required registration gate pass)
