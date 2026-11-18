Verdict: FALSE-POSITIVE

new-p21-157: setup() does not reset `prepared` — FALSE-POSITIVE

Evidence: the finding says `setup()` leaves `prepared` at `true`. On this branch (`bugs/qwen-new-p21-157`, HEAD `5c1c6715d9`) `sfz::FilterHolder::setup()` clears it at the end of the function, [FilterPool.cpp:61](plugins/SfzPlayer/sfizz/src/sfizz/FilterPool.cpp:61) (`prepared = false;`, under the comment "Disable smoothing of the parameters on the first call"). The sibling `reset()` also clears it at [FilterPool.cpp:22](plugins/SfzPlayer/sfizz/src/sfizz/FilterPool.cpp:22). The guard that consumes the flag is [FilterPool.cpp:99-102](plugins/SfzPlayer/sfizz/src/sfizz/FilterPool.cpp:99): on the first `process()` after a `setup()`, `filter->prepare(...)` is called with the current block's modulated cutoff, resonance and gain, so the first block of a new note is not prepared from stale values.

Both lines were added with the vendored file in `39f2979295` (`feat(instruments): the built-in SFZ Player on sfizz`), and `git log -S "prepared = false;"` on the file shows no commit that removed it, so the missing reset described in the finding is not a state this file has been in on this branch.

Callers: `Voice.cpp:511` calls `setup()` for every region filter at note start (`impl.filters_[i].setup(region, i, ...)`), so every new note re-arms the prepare path. `Voice.cpp:1719-1720` calls `filter.reset()` on the voice reset path, which also clears `prepared`. No caller reaches `process()` after `setup()` with `prepared` still true.

Contract check: the existing family test `tests/src/plugins/FilterPoolTest.cpp` (header: "a reset filter holder bypasses until its next setup") exercises the setup→process path and passes on the unmodified tree (below). No production code was changed, so there is no RED/GREEN pair to produce for this verdict; a regression test would pass without any fix and would not prove anything about this finding.

Commands run (from the worktree unless noted; logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-157.r1/logs/`):

| Step | Command | EXIT |
| --- | --- | --- |
| Build, first attempt | `cmake --build build --target FilterPoolTest -j4 > .../build-1.log 2>&1` | 2 (ccache could not create a temp file under `dsh/home/ccache-tmp-r6`: Permission denied, in `OpcodeCleanup.cpp.o`; environment issue, not a code result) |
| Test, first attempt | `cd build/tests && ctest -R '^FilterPoolTest$' --output-on-failure > .../test-1.log 2>&1` | 8 ("Could not find executable", because the build did not finish) |
| Build, retry | `CCACHE_DISABLE=1 cmake --build build --target FilterPoolTest -j4 > .../build-2.log 2>&1` | 0 |
| Test | `cd build/tests && ctest -R '^FilterPoolTest$' --output-on-failure > .../test-2.log 2>&1` | 0 (`100% tests passed out of 1`, 0.38 s) |
| Gate wrapper | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../gates-1.log 2>&1` | 1, before any gate ran: it creates `<worktree>/../<name>-feature-check`, which is outside the write-jail (Permission denied) |
| fork-sources | `bash tests/fork-sources-gate.sh > .../gate-fork-sources.log 2>&1` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh > .../gate-all-sources-reproduce.log 2>&1` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > .../gate-no-upstream-regression.log 2>&1` | 0 |
| check-namespace | `tests/scripted/check-namespace > .../gate-check-namespace.log 2>&1` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh > .../gate-no-tautology.log 2>&1` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py > .../gate-doc-figures.log 2>&1` | 0 |
| complexity | `bash tests/complexity-gate.sh --check > .../gate-complexity.log 2>&1` | 1 (6 `REGRESSION` lines, all in files this branch does not change) |
| file-length | `bash tests/file-length-gate.sh --check > .../gate-file-length.log 2>&1` | 1 (22 `REGRESSION` lines, all in files this branch does not change) |

Gate note: the wrapper's ratchet rule only fails items in files the branch changed. The branch has 0 changed files against `FIX-BASE` `050/m0-train-19` (`git diff --name-only <merge-base>..HEAD` returns 0 lines), and this commit adds only this report under `docs/`. So the complexity and file-length items cannot be in a changed file. I did not recompute the base listings the wrapper caches in `~/.local/share/zene-bunny-fix/qwen/gate-baselines`, because that path is outside the write-jail.

Files committed: `docs/qwen-fixes-2026-10/cluster-new-p21-157.md` only. No product or test file was changed.

## Self-check

- Regression test fails with the product change reverted and passes with it: not applicable. There is no product change. The only regression coverage is the existing `FilterPoolTest`, which passes on the unmodified tree (`test-2.log`, EXIT 0, `100% tests passed out of 1`). A new test would pass without a fix and prove nothing.
- Deterministic: the existing `FilterPoolTest` uses no wall-clock timing, sleeps or races. It sets up a holder, runs `process()` on fixed input buffers and compares the output with `std::equal` (`tests/src/plugins/FilterPoolTest.cpp:21-46`). I did not add any test.
- Fix at cause and complete: no fix. The callers that matter are checked: `Voice.cpp:511` (`setup` at note start) and `Voice.cpp:1719-1720` (`reset` on voice reset). Both keep `prepared` consistent with the contract in `FilterPool.cpp:61` and `:22`.
- No collateral damage: nothing changed, so saved data and other callers are unaffected.
- FALSE-POSITIVE evidence: `FilterPool.cpp:61` (`prepared = false;` at the end of `setup()`), present since `39f2979295`. The finding's premise does not hold on this branch.
- Gates: six of eight pass. Complexity and file-length exit 1 on items in files this branch does not change. The gate wrapper itself exits 1 before running any gate, because it cannot create its log folder outside the write-jail.
- Not done: the wrapper's base-listing comparison (outside the jail), and therefore the `gates (feature_check.py)` row of `submit_check.py` stays FAIL. That row is an environment limit, not a code result.
