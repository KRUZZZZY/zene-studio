# Cluster new-p15-20 — `mts.master_set` records an undo step for an unchanged flag

Verdict: **FALSE-POSITIVE** on this branch (`bugs/qwen-new-p15-20`, base `050/m0-train-8`).

The finding says a successful `mts.master_set` that does not change the flag should return
without recording an undo step. The contract says the opposite: every successful mutating
command records exactly one step, and an unchanged flag is still a successful mutation. The
current code and its regression test both match the contract. No product change is made.

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

**Test — the contract is pinned, and it asserts the finding's "bug" as correct**
(`tests/src/core/SessionTuningTest.cpp:1331-1337`):

- `:1331-1333` says: "Disarming what was never armed is idempotent - and as a SUCCESSFUL mutator
  it records exactly ONE undo step (A16)".
- `:1334-1336` runs `mts.master_set` with `enabled=false` on a fresh, never-armed master and
  requires `ok`.
- `:1337` asserts `QCOMPARE(undoDepth(), depthBefore + 1)`.

**Reversibility row** (`src/core/ControlReversibilityTableMts.cpp:111`): `mts.master_set` is
`RC::TrueInverse`, reversible. Nothing in it suggests a no-op exemption.

**Scope of the "unchanged" case** — `setMtsMaster` (`src/core/SessionTuningMtsEsp.cpp:89`)
always re-applies the state (`MTS_RegisterMaster` / `MTS_DeregisterMaster` plus the flag write).
No caller depends on the no-step behaviour the finding asks for.

## Commands run

Worktree and log folder as given. The ccache temp directory had to point to `/tmp` for
the build, because the default `/run/user/1000/ccache-tmp` is not writable from this sandbox.
This is an environment override only and no repo file changes.

| # | Command (run from the worktree unless noted) | EXIT | Result |
|---|---|---|---|
| build-1 | `cmake --build build --target zene SessionTuningTest -j4` | 2 | ccache `Permission denied` on `/run/user/1000/ccache-tmp` (log `build-1.log`); no code error |
| build-2 | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p15 cmake --build build --target zene SessionTuningTest -j4` | 0 | `Built target SessionTuningTest` (log `build-2.log`) |
| test-1 | `cd build/tests && ctest -R SessionTuningTest --output-on-failure` | 0 | `100% tests passed, 0 tests failed out of 1` (log `test-1.log`) |
| test-2 | `cd build/tests && ./SessionTuningTest masterSetReportsItsOwnMtsEspState` | 0 | `Totals: 3 passed, 0 failed` (log `test-2-direct.log`) |

Gates (`feature_check.py` could not run; see Self-check):

| Command | EXIT |
|---|---|
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 — `PermissionError` creating the sibling log dir `zene-qwen-new-p15-20-feature-check`, outside the write-jail (log `gates-1.log`) |
| `bash tests/fork-sources-gate.sh` | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `tests/scripted/check-namespace` | 0 |
| `bash tests/no-tautology-gate.sh` | 0 |
| `python3 tests/doc-figures-gate.py` | 0 |
| `bash tests/complexity-gate.sh --check` | 1 — 6 `REGRESSION` lines, all in files this branch did not change (`gate-complexity.log`) |
| `bash tests/file-length-gate.sh --check` | 1 — 15 `REGRESSION` lines, all in files this branch did not change (`gate-file-length.log`) |

Changed files against the base: `git diff --name-only $(git merge-base HEAD 050/m0-train-8)..HEAD`
gives 0 files, and `git status --porcelain` gives 0 entries. The feature_check rule flags only
ratchet items in changed files, so the two ratchets have nothing to flag here. I did not run the
baseline-subtraction step itself.

## RED / GREEN

No RED or GREEN cycle is run, because there is no fix to make the test fail against.
The existing test `masterSetReportsItsOwnMtsEspState` passes on the unchanged code (test-1 and
test-2 above). This is the expected result for a FALSE-POSITIVE. The test already enforces the
behaviour the finding calls a bug.

## Self-check

- **Commits on the branch / report present / working tree clean**: this report is the only
  change, committed with the message naming cluster new-p15-20. No product file is touched.
- **"Tests added or changed" FAIL**: deliberately not satisfied. A new test would only
  duplicate `SessionTuningTest.cpp:1331-1337`, which already pins the same contract. Adding a
  duplicate to satisfy the checker would be a test with no regression value.
- **"gates (feature_check.py)" FAIL**: the script exited 1 with a `PermissionError` because it
  writes its logs to `<worktree>-feature-check`, a sibling of the worktree, outside my write-jail.
  I ran each gate it would call by hand (table above). Six pass. The two ratchets exit 1 raw
  with pre-existing items, which feature_check would not flag because no changed file is involved.
  The harness itself has not been run to completion and should be run by someone with access to
  that sibling folder.
- **Regression test FAILS with the product change reverted / PASSES with it**: not applicable.
  There is no product change. The existing test passes on the code as it is (test-1, test-2).
- **Deterministic**: the existing test uses no timing, sleeps or races. It uses the library-absent
  branch, which refuses arming without side effects and then disarms on a clean song
  (`clearProject()`, `SessionTuningTest.cpp:1296`), and checks the undo depth against the value
  recorded before the calls.
- **Fix at the cause / callers and siblings**: no fix. The other `mts.*` table verbs record their steps
  the same way, unconditionally (`src/core/ControlCommandsMts.cpp:127-139`, `tableMutation`), which is
  consistent with the contract.
- **No collateral damage**: nothing changed, so no saved data or project load is affected.
- **ALREADY-FIXED / FALSE-POSITIVE evidence**: `src/core/ControlCommandsMts.cpp:318-349`,
  `docs/A16-REVERSIBILITY.md:219`, `tests/src/core/SessionTuningTest.cpp:1331-1337`.
