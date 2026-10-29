ALREADY-FIXED — cluster new-p22-76 (BUG 16 "sections allow negative start/end ticks"): the core and the schema already refuse a negative span on section.add and section.set; this commit adds a guard test that pins the refusal at the command boundary. No product code changed.

Branch `bugs/qwen-new-p22-76`, base `7641e9493d` (= FIX-BASE `050/m0-train-18`).

## Verdict and evidence

The finding says `addSection` passes `toInt()` values straight to `ArrangementSections::add` with no non-negativity check, so `section.add start=-5 end=-1` creates a negative-tick section. On this branch that does not happen, for three independent reasons:

| # | Where | What it shows |
|---|---|---|
| 1 | `src/core/ControlVocabulary.cpp:117-120` | `tickProperty()` is `integerProperty(0, 0x7fffffff)`: the JSON schema for every tick argument has `minimum` 0 and `maximum` 0x7fffffff. `section.add` (`src/core/ControlCommandsSections.cpp:156-160`) and `section.set` (`:161-163`) take their `start`/`end` from this span. |
| 2 | `src/core/ControlSchema.cpp:87-91` | the validator enforces `minimum` ("… is below the minimum …") and `src/core/ControlRegistry.cpp:277-280` runs `validateArgs` before the handler, returning `InvalidArgs`. A negative `start` or `end` never reaches `addSection`/`setSection` over the wire. |
| 3 | `src/core/ArrangementSections.cpp:60` | `add()` already refuses `end <= start \|\| start < 0` and returns -1; `addSection` maps -1 to `InvalidArgs` (`src/core/ControlCommandsSections.cpp:102`). Commit `8b0f0905d8` ("a replacement section accepted a span add() would refuse") put the same check into `replace()` at `src/core/ArrangementSections.cpp:71`. |

The existing tests already pin the core rules: `tests/src/core/ArrangeToolsTest.cpp:379` (`section.add` with end before start → `InvalidArgs`) and `aSectionReplacementRefusesAnInvalidSpan` (`:384-405`, including a `start = -100` replacement that is refused and leaves the stored section alone).

What was missing was a test that drives the *negative* span through the command layer for both `section.add` and `section.set`. Added: `aNegativeSectionSpanIsRefusedOverTheCommands` at `tests/src/core/ArrangeToolsTest.cpp:405-420` (comment 405-406, function 407-420). It asserts `InvalidArgs` for `section.add` (start -5, end -1), for `section.set` with `start=-5`, and for `section.set` with `end=-1`, and that the stored section stays at [0, 768).

## Sibling check (not changed, not reachable)

`src/core/ControlCommandsSections.cpp:117` (`setSection`) checks only `changed.end <= changed.start`, and `:119` ignores the bool returned by `replace()`. A negative `start` that got past the schema would therefore return `ok` with the section unchanged instead of an error. The schema's `minimum` 0 means no wire input reaches that line, so I did not change it: the finding's bug does not occur, and changing it would be outside the finding's scope. It is worth a follow-up hardening if the handler is ever reached from code that skips the schema.

## Commands and results

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-76.r1/logs/`.

| Step | Command (run from the worktree / `build/tests` as stated) | Log | EXIT |
|---|---|---|---|
| a | `cmake --build build --target zene ArrangeToolsTest -j4` | build-1.log | 2 — ccache could not create its temp file under `dsh/home` (permission). Environment, not code. Discarded. |
| b | `ctest -R '^ArrangeToolsTest$'` from `build/tests` | test-1.log | 8 — run against the missing binary from (a). Discarded. |
| c | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp-p22 cmake --build build --target zene ArrangeToolsTest -j4` | build-2.log | **0** (no `ccache: error` lines) |
| d | `ctest -R '^ArrangeToolsTest$' --output-on-failure` from `build/tests` | test-2.log | **0** — `1/1 Test #297: ArrangeToolsTest … Passed` |
| e | `build/tests/ArrangeToolsTest aNegativeSectionSpanIsRefusedOverTheCommands -v2` | test-3-negative.log | **0** — `PASS aNegativeSectionSpanIsRefusedOverTheCommands()`, `Totals: 3 passed, 0 failed` |
| f | `python3 …/qwen/feature_check.py <worktree>` as written | gate-1.log | 1 — `PermissionError` creating the sibling folder `<worktree>-feature-check` outside this lane's write jail. The gate never ran. |
| g | same gate code via `/home/kruzzzzy/.cache/zene-tmp/p22-gate/run_gate.py` (imports `feature_check.py` unchanged, sends its log folder to the LOG FOLDER) | feature-check-1.log | **0** — `RESULT: PASS` (8 rows, all PASS) |
| h | the raw gate scripts, one at a time: `fork-sources-gate.sh`, `all-sources-reproduce.sh`, `no-upstream-regression-gate.sh`, `no-tautology-gate.sh`, `doc-figures-gate.py`, `tests/scripted/check-namespace` | gate-2..gate-6 (and check-namespace in the earlier loop) | 0 each |
| i | `bash tests/complexity-gate.sh --check` | gate-7-complexity.log | 1 — the listed regressions are in other files (WasmSandbox, XyPadPairTest, DawProjectReadTracks, …). None in `ArrangeToolsTest.cpp`. Feature check treats this as no new item in changed files. |
| j | `bash tests/file-length-gate.sh --check` | gate-8-file-length.log | 1 — lists `new file over 500 lines: tests/src/core/ArrangeToolsTest.cpp (531)`. The file was already over 500 at base (642 physical lines at `7641e9493d`). That key is in the cached base listing `gate-baselines/7641e9493d….json`, so feature_check (step g) does not count it as new in a changed file. The added test is 16 lines. |

Affected-family test count: `ArrangeToolsTest` = 1 ctest test, executing 3 QTest functions (`initTestCase`, the new guard, `cleanupTestCase`), 0 failed.

Note on the first gate loop: a zsh word-splitting slip produced exit 127 for some scripts in one earlier attempt; those logs were overwritten by the explicit `bash …` runs in step h. The 127 logs were not used as evidence.

## Red/green status

No product file changed, so there is no RED state for this lane. The new test is a guard: it passes on the current code (step e), and it passes on a branch where the fix is already in. I did not run a revert experiment, because reverting the schema or core checks would be a change outside this finding. The test is sensitive on the `section.set` path: without the schema `minimum` on `tickProperty()`, `setSection` would return `ok` (end -1 > start -5 passes its pre-check, and `replace()`'s refusal is ignored), which the test catches.

## Files committed

- `tests/src/core/ArrangeToolsTest.cpp` (new test method only; no other test changed)
- `docs/qwen-fixes-2026-10/cluster-new-p22-76.md` (this report)

No test file is new, so no registration in `tests/fork-sources.txt` / `tests/upstream-modifications.txt` is needed.

## Self-check

Answers to the completion-check questions (`submit_check.py new-p22-76`, saved in `submit-check-1.log`):

- **Regression test FAILS with the product change reverted and PASSES with it:** not applicable as a RED/GREEN pair, because this branch has no product change to revert. The new test passes on the current code (ArrangeToolsTest step e, EXIT 0; step d, EXIT 0). It is a guard that pins existing behaviour, not regression proof. Its sensitivity to the `section.set` path (removing the schema `minimum` on `tickProperty()` would make `setSection` return `ok`) is an analysis, not a run.
- **Deterministic:** yes. No timing, sleeps or threads. The test uses only the registry (`run()`) and the singleton `ArrangementSections` with fixed ticks (-5, -1, 0, 768), and clears the store at the end.
- **Fix at the cause and complete / siblings:** no fix was needed. Callers checked: `addSection` (`ControlCommandsSections.cpp:94-105`) and `setSection` (`:107-122`), both behind the schema. The `setSection` pre-check gap (`:117` checks only `end <= start`; `:119` ignores `replace()`'s bool) is not reachable over the wire and is recorded above, not changed.
- **No collateral damage:** no product code changed, so saved projects, `ArrangementSections::load()` (`ArrangementSections.cpp:155-158`), and every other caller behave exactly as before. The test file is the only file changed.
- **ALREADY-FIXED evidence:** `src/core/ControlVocabulary.cpp:117-120` (tick schema minimum 0), `src/core/ControlSchema.cpp:87-91` (minimum enforced), `src/core/ControlRegistry.cpp:277-280` (refusal before the handler), `src/core/ArrangementSections.cpp:60` and `:71` (core refusal), `tests/src/core/ArrangeToolsTest.cpp:379` and `:384-405` (existing core tests).

Other checks:

- **Undo / reversibility:** unchanged. Refused calls never reach `recorded()`, so they add no undo step.
- **Audio thread:** nothing touched.
- **Portability:** the test uses only existing helpers.
- **Gates:** `feature_check.py` could not run in the lane because it creates `<worktree>-feature-check` next to the worktree, outside the write jail (PermissionError, gate-1.log, EXIT 1). The same script logic run with its log folder redirected to the LOG FOLDER returned `RESULT: PASS` (feature-check-1.log, EXIT 0). The `submit_check.py` gate row therefore fails for this environment reason only and needs the sibling folder to be writable to pass as written.
