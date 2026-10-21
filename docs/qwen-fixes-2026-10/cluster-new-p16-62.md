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

Completion check (`submit_check.py new-p16-62`, saved as submit-check-1.log): commits, clean tree, report and test-changed items PASS; the **gates item FAILS** (see point 6). Answers to the ASK items:

1. **RED/GREEN with the product change reverted and restored.** Reverted with `git apply -R fix.patch` (exit 0), rebuilt (build-2-red.log, exit 0), `ctest -R '^DrumSynthTest$'` exit 8 with six `FAIL!` lines and `Totals: 8 passed, 6 failed` (test-2-red.log). Restored with `git apply fix.patch` (exit 0), rebuilt (build-3-green.log, exit 0), ctest exit 0, `100% tests passed out of 1` (test-3-green.log). The reverted state is the pre-854ada329 DrumSynth.cpp, so this is the proof for the fix.
2. **Deterministic.** No clock, sleep, thread or random input. Every value is a literal in the test: the tone fixture text is written to a `QTemporaryDir` file, rendered in-process through `DrumSynth::GetDSFileSamples(path, samples, 1, 44100)`, and the int16 vectors are compared exactly. The non-finite input and its expected reference (default F1 or literal `0` level) are both fixed strings.
3. **At the cause, callers and siblings checked.** No product change in this cluster; the fix is `854ada329`. Its checks cover every route a float takes from a `.ds` file to a sample:
   - profile floats: all `GetPrivateProfileFloat` callers (`src/core/DrumSynth.cpp:417, 430, 432, 435, 470, 475, 476, 491, 502, 503, 544, 558`) go through the `isfinite` check at `:334`;
   - envelope values: the only parser is `GetEnv` → `storeEnvelopeValue` (`:161-170`), and every stored point passes `:167`;
   - generated samples: the two sample conversion sites (`:939` bit-depth, `:969` output clipping) are guarded.
   `GetPrivateProfileInt` is not affected: an integer parse cannot yield NaN or infinity.
4. **No collateral damage.** A non-finite value falls back to the documented default (profile → `def`; envelope → `0`, the existing malformed-token behaviour). Finite values are untouched, and no new limit or check was added. Existing projects load the same: `DrumSynthLevelTest`, `DrumSynthTest` and `SampleBufferResampleTest` all pass (test-4-family.log, `100% tests passed out of 3`). Trailing-text parsing (`200abc`) is unchanged; see Not changed above.
5. **ALREADY-FIXED evidence.** `src/core/DrumSynth.cpp:334` (profile floats), `:167` (envelope), `:939` and `:969` (samples); `git merge-base --is-ancestor 854ada329 HEAD` exit 0.
6. **Gates.** The unmodified `feature_check.py` exits 1 here: it creates `<worktree>/../zene-qwen-new-p16-62-feature-check/` outside the write-jail (`PermissionError`). It needs to run from an environment that can write beside the worktree. The same gate logic, with only the log folder redirected to the run folder, exits 0 with all rows PASS (gate-1-feature-check.log). This item should be re-run from an environment that can write beside the worktree.
