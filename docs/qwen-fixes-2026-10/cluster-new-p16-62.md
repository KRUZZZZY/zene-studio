# Cluster new-p16-62: DrumSynth float settings accept NaN and infinity

**Verdict: ALREADY-FIXED**

The reported defect (non-finite `.ds` float settings reaching the oscillator) is closed on this branch. The fix landed in commit `854ada329` (cluster new-p10-138-port-port), which is an ancestor of `HEAD` (`git merge-base --is-ancestor 854ada329 HEAD` → exit 0). This cluster adds the regression coverage the finding's own inputs were missing.

## Evidence

- `src/core/DrumSynth.cpp:334`: `GetPrivateProfileFloat` rejects non-finite results: `if (sscanf(tmp, "%f", &f) != 1 || !std::isfinite(f)) { f = def; }`. `F1=nan`, `F1=inf` and `F1=-inf` now fall back to the default, the same as a missing key.
- `src/core/DrumSynth.cpp:167`: `storeEnvelopeValue` stores 0 for non-finite envelope tokens (the finding's "envelope parsing has the same gap").
- `src/core/DrumSynth.cpp:939` and `:969`: generated samples are guarded with `!std::isfinite(DF[j])` before the integer conversion and before clipping/output, so a non-finite value cannot become a corrupted sample.
- Blame: lines 167 and 334 come from `854ada329`; the existing test for that commit is `SampleBufferResampleTest::aNonfiniteDrumSynthLevelUsesTheDefaultGain`, which covers `Level=nan` only.
- Gap found: the finding's exact input (`F1=nan`) and a non-finite envelope level had no test. This cluster adds them to `tests/src/core/DrumSynthTest.cpp`: `rejectsNonfiniteToneFrequency` (`nan`, `inf`, `-inf`) and `rejectsNonfiniteEnvelopeLevel` (`nan`, `inf`, `-inf`). Each renders a file with the non-finite value and compares it with a file using the default (F1 omitted) or a literal `0` level.

### Not changed: "complete numeric parse"

The finding also asks to require a complete numeric parse. That is not done here, on purpose. `sscanf` still accepts trailing text (`F1=200abc` parses as 200). That does not cause the reported NaN/inf harm: a non-finite value is caught by `isfinite` on whatever `sscanf` stored, and a failed conversion (`!= 1`) falls back to the default. Requiring a full parse would change how existing `.ds` files with trailing text load, which is a behaviour change outside this bug, so it is left as an open question for the owner.

## Commands and results

Run from `zene-qwen-new-p16-62`. ccache needed `CCACHE_TEMPDIR=/tmp/ccache-tmp-p16-62` (its default temp dir under `/run/user/1000` is not writable here).

| Step | Command | EXIT | Log |
|---|---|---:|---|
| Save reverse of the fix | `git diff 854ada329^ 854ada329 -- src/core/DrumSynth.cpp > <logs>/fix.patch` | 0 | (38-line patch) |
| Remove the fix (RED state) | `git apply -R <logs>/fix.patch` | 0 | |
| Build, first try | `cmake --build build --target zene DrumSynthTest -j4` | 2 | build-1-red.log (ccache permission error, not a code error) |
| Build (RED) | same, with `CCACHE_TEMPDIR` set | 0 | build-2-red.log |
| RED test | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure` | 8 | test-2-red.log: 6 `FAIL!` (all six new cases: `render(nonfinitePath) == expected` returned FALSE); `Totals: 8 passed, 6 failed` |
| Restore the fix | `git apply <logs>/fix.patch` | 0 | |
| Build (GREEN) | same build command | 0 | build-3-green.log |
| GREEN test | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure` | 0 | test-3-green.log: `100% tests passed out of 1` |
| Build family | `cmake --build build --target DrumSynthLevelTest SampleBufferResampleTest -j4` | 0 | build-4-family.log |
| Family test | `cd build/tests && ctest -R '^(DrumSynthTest\|DrumSynthLevelTest\|SampleBufferResampleTest)$' --output-on-failure` | 0 | test-4-family.log: `100% tests passed out of 3` |
| Gates (direct) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | Could not run: it creates `<worktree>/../zene-qwen-new-p16-62-feature-check/` (outside the write-jail), so `mkdir` raised `PermissionError`. Nothing was run. |
| Gates (same logic, log dir redirected) | `python3 -I /tmp/fc-p16-62/feature_check_logdir.py <worktree>` | 0 | gate-1-feature-check.log: all rows PASS, `RESULT: PASS` |

The `/tmp` copy of `feature_check.py` differs from the original in only two lines: `ROOT` is pinned to the shared qwen folder (so `FIX-BASE` and the existing `gate-baselines` cache still resolve), and the log folder is the run folder. Gate limits and baselines are unchanged. The baseline cache for `b61e5f965` already existed, so the ratchet comparison only read it.

Gate rows (redirected run): fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS (no new item in changed files), file-length PASS (no new item in changed files).

## Files committed

- `tests/src/core/DrumSynthTest.cpp` (regression tests; `DrumSynthTest.cpp` is already in `tests/fork-sources.txt`, so no registration change)
- `docs/qwen-fixes-2026-10/cluster-new-p16-62.md`

No product code changed in this cluster. `src/core/DrumSynth.cpp` is unchanged from HEAD after the RED/GREEN cycle (`git status` shows only the test file before this report).

## Self-check

- The RED result is real: with `854ada329`'s `DrumSynth.cpp` hunks reverted, the six new cases fail (test-2-red.log, exit 8); with them restored, all pass (test-3-green.log, exit 0).
- Family: `DrumSynthTest`, `DrumSynthLevelTest`, `SampleBufferResampleTest` 3/3 pass (test-4-family.log, exit 0).
- Test is deterministic: no timing, sleeps or races; it compares int16 render vectors from `QTemporaryDir` fixtures.
- Gates: pass on the redirected run (gate-1-feature-check.log, exit 0). The unmodified script cannot run in this write-jail (see table).
- Open: the "complete numeric parse" half of the finding is not implemented, for the reason above.
