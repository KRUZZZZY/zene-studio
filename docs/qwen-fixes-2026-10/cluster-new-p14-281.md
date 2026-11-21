FALSE-POSITIVE

# Cluster new-p14-281 — "Failed script runs are counted as both fires and errors"

## Verdict

FALSE-POSITIVE. The overlap is the documented contract of `ScriptClock`'s counters, not a defect. A failed run is
counted once in `fires` (a dispatch that completed) and once in `errors` (a dispatch that failed), and the code,
the schema description and the test suite all say so. No product file was changed.

## Evidence (file:line on branch `bugs/qwen-new-p14-281`, base `2da2ed356`)

1. **The contract is stated in the header.** `include/ScriptClock.h:137` documents `fires` as
   "dispatches that completed (Ok or an error)" and `include/ScriptClock.h:138` documents `errors` as
   "dispatches that failed". A failed dispatch is by definition in both, so the counters overlap by design.
2. **The code does what the header says.** `src/core/ScriptClock.cpp:383-384` increments `fires` for every
   dispatch, before the result is examined. `:385` (`Ok`) keeps it as a fire. `:386-393` (`Busy`) undoes the
   increment (`entry.fires--`, `:390`) and counts `m_busySkips`, because a busy boundary never ran. `:394` counts
   `errors` only for the remaining failure cases. So an error run is in `fires` and in `errors`, a busy skip is in
   neither, and an Ok run is only in `fires`.
3. **The read-back reports the same contract.** `src/core/ScriptClock.cpp:464-465` (per schedule) and `:471-479`
   (aggregate `counters`) report `fires` and `errors` as separate fields with no sum. The command description at
   `src/core/ControlCommandsLivecode.cpp:325-326` lists `fires, errors, budget_exceeded, ...` as the aggregate
   counters.
4. **The test suite pins the overlap.** `tests/src/core/ScriptClockTest.cpp:527` asserts `errors == 1` for one
   failing run (`lc-t-masq`), and the same test does not assert `fires == 0` for it.
5. **No product consumer adds the two.** `grep -rn busy_skips src include tools` (excluding 3rdparty) finds only
   the producer (`ScriptClock.cpp:481`), the description (`ControlCommandsLivecode.cpp:326`) and the generated
   snapshot. The Python livecode test (`tests/control-livecode-commands.py:201-223`, `:323`, `:336`) reads each
   counter on its own.

### One thing that is actually inaccurate (not changed)

The only place that adds the two counters is a test helper: `tests/src/core/ScriptClockTest.cpp:81-87`
(`hookRuns` = `fires + errors + busy_skips`). Its comment (`:77-80`) says "every path out of a fire moves one of
the three", which is wrong for the error path, because an errored run moves `fires` and `errors` (two of the
three). So `hookRuns` counts an errored run twice. Its only use I read is the zero-delta check at
`ScriptClockTest.cpp:500` (`QCOMPARE(hookRuns(clock) - before, 0)`), which is unaffected by the double count, so no
assertion today gives a wrong result. Fixing the helper or its comment is a test-only cleanup outside this finding;
it was left alone rather than changing a test in a bug-fix lane.

## Commands run and results

Each log is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-281.r1/logs/`.

| # | Command (cwd: worktree unless stated) | Log | EXIT |
|---|---|---|---|
| 1 | `cmake --build build --target ScriptClockTest -j4` | `build-1.log` | 2 (ccache could not write `/run/user/1000/ccache-tmp`: permission denied; no code error) |
| 2 | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14 cmake --build build --target ScriptClockTest -j4` | `build-2.log` | 0 |
| 3 | from `build/tests`: `ctest -R '^ScriptClockTest$' --output-on-failure` | `test-1.log` | 0 (1/1 tests run, `100% tests passed`) |
| 4 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `gates-1.log` | 1: `PermissionError` creating `<worktree>-feature-check/` (sibling of the worktree, outside the write-jail). Nothing was run by it. See the substitution below. |

Gate substitution for row 4: I ran the same commands that `feature_check.py` defines (`SIMPLE` and `RATCHET`),
from the worktree, with logs in the LOG folder:

| Gate | Command | Log | EXIT |
|---|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | `gate-fork-sources.log` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | `gate-all-sources-reproduce.log` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-regression.log` | 0 |
| check-namespace | `tests/scripted/check-namespace` | `gate-check-namespace.log` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | `gate-no-tautology.log` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | `gate-doc-figures.log` | 0 |
| complexity | `bash tests/complexity-gate.sh --check` | `gate-complexity.log` | 1 (red on the train on purpose) |
| file-length | `bash tests/file-length-gate.sh --check` | `gate-file-length.log` | 1 (red on the train on purpose) |

The two ratchet gates fail only on base-train items, not on this branch. The `REGRESSION:` lines in
`gate-file-length.log` name other files (for example `src/core/TakeLane.cpp`, `tests/src/core/RackMacrosTest.cpp`).
The branch's changed files are this report only, so none of the items can be "new in your files". The
comparison against the base listing was not run, because `feature_check.py` computes it with a
`git worktree add` in the shared `zene-cachyos` repository, which is outside the write-jail. So the
PASS/FAIL row for these two gates is not verified by the script, and the exit codes above are the only
evidence.

## Test change (contract pin, not a fix)

`tests/src/core/ScriptClockTest.cpp:527-531` (in `aLogLineCannotMasqueradeAsABudgetFailure`) gained one
assertion after the existing `errors == 1`: `QCOMPARE(fires, 1)` for the same single failed run, with a comment
citing the header contract. The existing assertions are unchanged; nothing was weakened.

| # | Command | Log | EXIT |
|---|---|---|---|
| 5 | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14 cmake --build build --target ScriptClockTest -j4` (after the test edit; log shows `ScriptClockTest.cpp.o` recompiled) | `build-3.log` | 0 |
| 6 | from `build/tests`: `ctest -R '^ScriptClockTest$' --output-on-failure` | `test-2.log` | 0 (1/1 tests run, `100% tests passed`) |

RED/GREEN: there is no product change to revert, so the no-fix/fix proof does not apply. The new assertion
passes on the unchanged code because it pins current behaviour that the header documents. It has not been
shown to fail under a mutation of `ScriptClock.cpp`; that was not run, because it would change product code
for a verdict that needs no change.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p14-281.md` (this report)
- `tests/src/core/ScriptClockTest.cpp` (one added `QCOMPARE` and its comment)

No product (`src/`, `include/`) file and no gate file changed.

## Self-check

- **Regression test fails with the change reverted and passes with it:** not applicable as a RED/GREEN proof,
  because the verdict is FALSE-POSITIVE and no product change exists. The pinned assertion passes on the
  current code (test-2.log, EXIT 0), which is the expected result for a documented contract.
- **Deterministic:** yes. The test uses the existing scripted schedule on the `transport` hook, with
  `pollOnce()` and `playSong()` driving one fire, and no sleeps or wall-clock timing. The same run already
  asserts `errors == 1` at the same line.
- **Fix at the cause and complete:** not applicable; there is no defect to fix. The callers that read the
  counters were checked: the per-schedule and aggregate read-backs (`src/core/ScriptClock.cpp:464-479`), the
  command description (`src/core/ControlCommandsLivecode.cpp:325-326`) and the Python livecode test
  (`tests/control-livecode-commands.py:201-223`, `:323`, `:336`), which read each counter separately.
- **No collateral damage:** no product code changed, so no saved data or old project path is affected.
- **Verdict evidence:** `include/ScriptClock.h:137-138` (contract), `src/core/ScriptClock.cpp:383-394`
  (code), `tests/src/core/ScriptClockTest.cpp:527` (pinned), as listed above.
- **Gates (feature_check.py):** the script itself could not run; it exits 1 with `PermissionError` creating its
  log folder beside the worktree, which is outside the write-jail. The same six simple gates and two ratchet
  commands were run directly (table above): six exit 0, complexity and file-length exit 1 on base-train items
  that are not in this branch's changed files. The base-comparison step was not run (it needs a shared cache
  and a worktree outside the jail). This is a gap, and the submit check's gates row will remain FAIL for
  this reason.
- **Observation not changed:** `tests/src/core/ScriptClockTest.cpp:77-87` (`hookRuns`) sums
  `fires + errors + busy_skips`, which counts an errored run twice, and its comment at `:78-80` is inaccurate
  for the error path. It only feeds the zero-delta check at `:500`, so no current assertion is wrong. Not
  changed here.
