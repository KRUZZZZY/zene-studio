# Cluster new-p15-20 — `mts.master_set` records an undo step for an unchanged flag

Verdict: **FALSE-POSITIVE** on this branch (`bugs/qwen-new-p15-20`, base `050/m0-train-8`).

The finding says a successful `mts.master_set` that does not change the flag should return
without recording an undo step. The contract says the opposite: every successful mutating
command records exactly one step, and an unchanged flag is still a successful mutation. The
current code and its tests match the contract. No product change is made. This round adds one
test that pins the contract from the undo side.

## Evidence

**Code — the step is recorded unconditionally after a successful write**
(`src/core/ControlCommandsMts.cpp:318-349`):

- `:320-322` reads `enabled` and the current `wasEnabled`.
- `:324-328` calls `tuning->setMtsMaster(enabled, &error)`; a refusal returns before any step.
- `:329-330` comments that the flag pair is "ONE recorded action step".
- `:331-339` calls `control::addUndoStep` with no check of `enabled != wasEnabled`.

Disarming an already-disarmed master is therefore a successful write that records one step.
This is intended. The comment at `:329-330` and the transaction at `:340-347`
(`action checkpoint`) describe that shape.

**Contract — one record per successful mutating command**
(`docs/A16-REVERSIBILITY.md:219`): "One record per successful mutating command, built by the
registry from the handler's `__transaction` payload". The finding's fix would make a successful
call write no record, which breaks this rule.

**Existing test — the contract is pinned for one call**
(`tests/src/core/SessionTuningTest.cpp:1331-1337`): `:1331-1333` says "Disarming what was never
armed is idempotent - and as a SUCCESSFUL mutator it records exactly ONE undo step (A16)", and
`:1337` asserts `undoDepth == depthBefore + 1` for a single call.

**New test — each unchanged call is its own step, and undoing one is honest**
(`tests/src/core/SessionTuningTest.cpp:1339-1377`, `masterSetEachUnchangedCallIsItsOwnUndoStep`):

- Two identical `mts.master_set` calls each add one step (`undoDepth` +1, then +2).
- `control.undo` removes exactly one step, and the flag is still the value both calls asked for.
- It covers a case the existing test does not: a second unchanged call, and undo after it.

**Reversibility row** (`src/core/ControlReversibilityTableMts.cpp:111`): `mts.master_set` is
`RC::TrueInverse`, reversible, with no coalesce target. Two consecutive calls therefore cannot
merge into one step (`include/ControlReversibility.h:399`, `coalesces()` is false when the target
is empty), so the new test has no wall-clock dependence.

**Callers and siblings** (`rg setMtsMaster(` over `src`, `include`, `tests`): the only callers are
the handler at `src/core/ControlCommandsMts.cpp:325` and its two undo closures at `:334` and `:338`.
The definition is `src/core/SessionTuningMtsEsp.cpp:89`. No caller depends on the no-step
behaviour the finding asks for.

## Commands run

The log folder for this round is `new-p15-20.r2/logs`. The round-1 logs are in `new-p15-20.r1/logs`.
The ccache temp directory had to point to `/tmp` for the build, because the default
`/run/user/1000/ccache-tmp` is not writable from this sandbox. This is an environment override
only and no repo file changes.

Build and test (round 1 first, then this round):

| # | Command (run from the worktree unless noted) | EXIT | Result |
|---|---|---|---|
| build-1 (r1) | `cmake --build build --target zene SessionTuningTest -j4` | 2 | ccache `Permission denied` on `/run/user/1000/ccache-tmp`; no code error |
| build-2 (r1) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p15 cmake --build build --target zene SessionTuningTest -j4` | 0 | `Built target SessionTuningTest` |
| test-1 (r1) | `cd build/tests && ctest -R SessionTuningTest --output-on-failure` | 0 | `100% tests passed out of 1` |
| test-2 (r1) | `cd build/tests && ./SessionTuningTest masterSetReportsItsOwnMtsEspState` | 0 | `Totals: 3 passed, 0 failed` |
| build-green-1 (r2) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p15 cmake --build build --target SessionTuningTest -j4` | 0 | new test compiled, unchanged product code |
| test-green-direct-1 (r2) | `cd build/tests && ./SessionTuningTest masterSetEachUnchangedCallIsItsOwnUndoStep` | 0 | `Totals: 3 passed, 0 failed` (new case PASS) |
| test-green-1 (r2) | `cd build/tests && ctest -R SessionTuningTest --output-on-failure` | 0 | `100% tests passed out of 1` |

RED / GREEN for the new test (see the next section for the mutation):

| # | Command | EXIT | Result |
|---|---|---|---|
| build-red-1 (r2) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p15 cmake --build build --target SessionTuningTest -j4` with the mutation | 0 | mutated product file compiled |
| test-red-direct-1 (r2) | `cd build/tests && ./SessionTuningTest masterSetEachUnchangedCallIsItsOwnUndoStep` with the mutation | **1** | `FAIL! ... masterSetEachUnchangedCallIsItsOwnUndoStep()` at `SessionTuningTest.cpp:1359`: `Actual undoDepth 0, Expected depthBefore + 1 = 1` |
| test-red-full-mutated (r2) | `cd build/tests && ./SessionTuningTest` with the mutation | **2** | `FAIL!` at `SessionTuningTest.cpp:1337` (existing case) and `:1359` (new case); `Totals: 23 passed, 2 failed` |
| build-green-2 (r2) | rebuild after restoring the product file | 0 | restored code |
| test-green-direct-2 (r2) | `./SessionTuningTest masterSetEachUnchangedCallIsItsOwnUndoStep` after restore | 0 | `Totals: 3 passed, 0 failed` |
| test-green-2 (r2) | `ctest -R SessionTuningTest` after restore | 0 | `100% tests passed out of 1` |
| build-family (r2) | `cmake --build build --target SessionTuningPublicationTest SessionTuningMenuTest -j4` | 0 | both family targets built |
| test-family-2 (r2) | `cd build/tests && ctest -R SessionTuning --output-on-failure` | 0 | `100% tests passed out of 3` (SessionTuningTest, SessionTuningPublicationTest, SessionTuningMenuTest) |

The first family run (test-family-1, EXIT 8) had two targets "Not Run" because I had built only
`SessionTuningTest` at that point. test-family-2 is the valid run.

Gates. `feature_check.py` still cannot run from this sandbox, because it writes its logs to the
sibling folder `zene-qwen-new-p15-20-feature-check`, which is outside the write-jail. I ran each
gate command it calls by hand, from the worktree, on the final tree:

| Command | EXIT |
|---|---|
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 (`PermissionError` on the sibling log dir; round-1 log `gates-1.log`) |
| `bash tests/fork-sources-gate.sh` | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `tests/scripted/check-namespace` | 0 |
| `bash tests/no-tautology-gate.sh` | 0 |
| `python3 tests/doc-figures-gate.py` | 0 |
| `bash tests/complexity-gate.sh --check` | 1 — `REGRESSION` lines are pre-existing; none names `SessionTuning` |
| `bash tests/file-length-gate.sh --check` | 1 — `REGRESSION` lines are pre-existing; none names `SessionTuning` |

Changed files against the base: the only change to this branch's tree is
`tests/src/core/SessionTuningTest.cpp` (39 added lines, no deletions). The product file
`src/core/ControlCommandsMts.cpp` is identical to HEAD (sha256 `52a78edc080d…`). The feature_check
rule flags only ratchet items in changed files, and no ratchet log names the test file. I did not
run the baseline-subtraction step itself.

## RED / GREEN

The "product change" for this round is the finding's proposed fix, applied only as a temporary
mutation to prove the test discriminates. It is not part of the commit:

    -	control::addUndoStep(
    +	if (enabled != wasEnabled) control::addUndoStep(

Steps and exit codes: mutation applied (`/tmp/p15-ControlCommandsMts.orig.cpp` holds the
original) → build EXIT 0 → new test EXIT 1 (FAIL at `:1359`) → full binary EXIT 2 (FAIL at `:1337`
and `:1359`) → file restored from the copy, `git diff --quiet` EXIT 0, sha256 matches HEAD →
rebuild EXIT 0 → new test EXIT 0 → ctest EXIT 0 → family EXIT 0.

The mutation was a single-line guard over the recorded step, so it makes the finding's described
behaviour exact (no step for an unchanged flag). The new test and the existing test both fail under
it, and both pass on the real code.

## Self-check

- **Commits on the branch / report present / working tree clean**: the round-1 commit `10e4d5fd1`
  holds the report. This round adds the test and an updated report, committed together. The
  product file is unchanged from HEAD.
- **"Tests added or changed"**: now satisfied. `masterSetEachUnchangedCallIsItsOwnUndoStep` is
  added to `tests/src/core/SessionTuningTest.cpp` (lines 1339-1377). Round 1 declined to add a test
  because it would duplicate `:1331-1337`. The reviewer asked for one, and the new case is not a
  duplicate: it checks two calls in a row and undo after the second, which `:1331-1337` does not.
- **"gates (feature_check.py)"**: feature_check.py still cannot run from this sandbox (see above).
  Each gate it calls was run by hand on the final tree (table above). Six pass. The two ratchets
  exit 1 with pre-existing items that name no file this branch changed.
- **ASK 1 — the regression test FAILS with the product change reverted and PASSES with it**: the
  product "change" is the finding's fix, applied as a mutation. Under it the new test exits 1
  (`:1359`), and the full binary exits 2 with the existing test also failing (`:1337`). With the
  real code the new test exits 0 (direct and ctest), and the family exits 0 (3 of 3). Exit codes are
  in the tables above.
- **ASK 2 — deterministic**: no timing, sleeps or races. The coalescing window cannot merge the two
  calls, because the `mts.master_set` row declares no coalesce target
  (`ControlReversibilityTableMts.cpp:111`, `ControlReversibility.h:399`). The only environmental
  input is `SessionTuning::mtsEspLibraryPresent()` (`src/core/SessionTuningMtsEsp.cpp:52`), a fixed
  property of the install and not of time. On this box the library is absent, so the test runs the
  disarm-when-disarmed path. The library-present path (arm, then arm again) has the same shape and
  is written into the same test, but it is NOT executed here because `libMTS.so` is not installed.
- **ASK 3 — the fix is at the cause and complete**: no product fix is made, because the behaviour is
  the contract. The callers of `setMtsMaster` are checked (`ControlCommandsMts.cpp:325, 334, 338`).
  The sibling `mts.*` table verbs record their steps unconditionally too
  (`ControlCommandsMts.cpp:127-139`, `tableMutation`), which is consistent.
- **ASK 4 — no collateral damage**: no product code changed, so no saved project or old save loads
  differently. The added test function touches only the in-memory undo stack of its own fixture,
  which the existing test also resets with `clearProject()`.
- **ASK 5 — FALSE-POSITIVE evidence**: `src/core/ControlCommandsMts.cpp:318-349` (records the
  step for every successful call), `docs/A16-REVERSIBILITY.md:219` (one record per successful
  mutating command), `tests/src/core/SessionTuningTest.cpp:1331-1337` (existing contract pin) and
  `:1339-1377` (new contract pin).
