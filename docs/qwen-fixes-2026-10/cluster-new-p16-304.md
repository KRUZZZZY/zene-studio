Verdict: FALSE-POSITIVE

new-p16-304: FALSE-POSITIVE

Finding: "Negative sample rates bypass the setter's zero check" (`include/BasicFilters.h`, reported near line 350).

Evidence:

- The stated mechanism cannot happen on this type. `sample_rate_t` is `std::uint32_t` ([include/LmmsTypes.h:42](../../include/LmmsTypes.h:42)), so no sample rate stored by `BasicFilters` can ever be negative. A call such as `setSampleRate(-1)` converts `-1` to `4294967295` at the call site (standard unsigned conversion). The setter then accepts it because its only rejected value is zero ([include/BasicFilters.h:341-349](../../include/BasicFilters.h:341)). The finding's "negative value stored and inverted" is not reachable; the only way to get the wrapped value is a caller passing an out-of-range integer, and no production caller does.
- Every production value reaching the setter is a non-negative engine rate. The one production caller is `LOMMEffect::changeSampleRate()` at [plugins/LOMM/LOMM.cpp:76-81](../../plugins/LOMM/LOMM.cpp:76): `m_sampleRate` is assigned from `Engine::audioEngine()->outputSampleRate()` (a `sample_rate_t`) and passed to `m_ap.setSampleRate(...)` ([plugins/LOMM/LOMM.cpp:81](../../plugins/LOMM/LOMM.cpp:81)). The `float` member `m_sampleRate` ([plugins/LOMM/LOMM.h:66](../../plugins/LOMM/LOMM.h:66)) is only ever assigned from that unsigned value, so it is never negative. Inside the header, the sub-filter propagation at [include/BasicFilters.h:353](../../include/BasicFilters.h:353) forwards the same value. `rg -n setSampleRate` finds no call of the `BasicFilters` setter in `plugins/DualFilter/`; `StereoLinkwitzRiley` (LOMM's other filters, [plugins/LOMM/LOMM.h:68-72](../../plugins/LOMM/LOMM.h:68)) uses the separate `float` overload at [include/BasicFilters.h:69](../../include/BasicFilters.h:69).
- The constructor and setter differ only for zero, and that asymmetry is the tested contract. On an unsigned type the constructor's `_sample_rate > 0 ? _sample_rate : 1` ([include/BasicFilters.h:296](../../include/BasicFilters.h:296)) is just `!= 0`, so it substitutes only for zero. The setter ignores zero and keeps the current rate ([include/BasicFilters.h:343-348](../../include/BasicFilters.h:343)). `tests/src/core/BasicFiltersSampleRateTest.cpp:52-63` pins that: `setSampleRate(0)` must be rejected, and the filter must then produce output identical to an untouched filter. The finding's "constructor substitutes a safe value for nonpositive rates" describes that zero-only substitution, not a negative-value path.
- An out-of-range wrapped value such as `4294967295` produces a finite `m_sampleRatio` (about `2.3e-10`), so it does not produce NaN or infinity. The API documents no upper bound on sample rates, so adding one (or changing the parameter to a signed type, which would change every caller) would invent a contract and change behaviour outside this bug. Not done.
- The suggested fix ("validate as positive at the API boundary") cannot be written on a `uint32_t` parameter. Making it signed would change the signature for all callers. That is an API redesign, not a bug fix, so it is out of scope here.

Regression test: none written. Procedure step 2 routes a not-real finding to the verdict without a RED/GREEN cycle, and the claimed mechanism has no defect to reverse. The existing guard for this setter is `BasicFiltersSampleRateTest` (below). No product file changed, so step 4b (`git diff` / `git apply -R` proof) does not apply.

Commands run, with EXIT codes. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-304.r1/logs/`.

| Evidence | Command | Result |
| --- | --- | --- |
| Build of the existing family target, first attempt | `cmake --build build --target BasicFiltersSampleRateTest -j4 > .../logs/build-1.log 2>&1; echo EXIT=$?` | EXIT=2. ccache could not create a temp file in `dsh/home/ccache-tmp-r6` (Permission denied, `build-1.log:18-19`). Environment problem, not a code error. |
| Same target, ccache disabled for this command | `CCACHE_DISABLE=1 cmake --build build --target BasicFiltersSampleRateTest -j4 > .../logs/build-2.log 2>&1; echo EXIT=$?` | EXIT=0; target linked |
| Existing family test (run from `build/tests`) | `ctest -R '^BasicFiltersSampleRateTest$' --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?` | EXIT=0; 1/1 tests run and passed (`Test #18: BasicFiltersSampleRateTest ... Passed`) |
| Fork-sources gate | `bash tests/fork-sources-gate.sh > .../logs/gate-fork-sources.log 2>&1` | EXIT=0 |
| All-sources reproduce gate | `bash tests/all-sources-reproduce.sh > .../logs/gate-all-sources-reproduce.log 2>&1` | EXIT=0 |
| No-upstream-regression gate | `bash tests/no-upstream-regression-gate.sh > .../logs/gate-no-upstream-regression.log 2>&1` | EXIT=0 |
| Check-namespace gate | `tests/scripted/check-namespace > .../logs/gate-check-namespace.log 2>&1` | EXIT=0 |
| No-tautology gate | `bash tests/no-tautology-gate.sh > .../logs/gate-no-tautology.log 2>&1` | EXIT=0 |
| Doc-figures gate | `python3 tests/doc-figures-gate.py > .../logs/gate-doc-figures.log 2>&1` | EXIT=0 |
| Complexity ratchet (raw) | `bash tests/complexity-gate.sh --check > .../logs/gate-complexity.log 2>&1` | EXIT=1; 6 `REGRESSION:` lines, none in a file this branch changes (see Self-check) |
| File-length ratchet (raw) | `bash tests/file-length-gate.sh --check > .../logs/gate-file-length.log 2>&1` | EXIT=1; 22 `REGRESSION:` lines, none in a file this branch changes (see Self-check) |
| `feature_check.py` (the required single gate) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-1.log 2>&1` | EXIT=1 with a traceback before any gate ran: `PermissionError` creating `<worktree>-feature-check/`, a log folder outside the worktree in `projects/zene-studio-program/`, which this lane may not write (`gates-1.log`). The six simple gates and two ratchets above were run by hand with the same commands the script uses (`feature_check.py:SIMPLE` / `RATCHET`). |

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-new-p16-304.md` only. No product or test file changed.

## Self-check

- Verdict is supported by file:line evidence: the type (`LmmsTypes.h:42`), the setter (`BasicFilters.h:341-349`), the constructor (`BasicFilters.h:296`), the production caller (`LOMM.cpp:76-81`), and the existing test (`BasicFiltersSampleRateTest.cpp:52-63`).
- Build and test evidence is recorded above with EXIT codes; the single build failure was an environment ccache permission error, and it was resolved with `CCACHE_DISABLE=1`.
- Regression test FAILS-with-revert / PASSES-with-fix (question 1): not applicable. No product change was made, so there is nothing to revert, and no new test was added. The existing `BasicFiltersSampleRateTest` passes on the unchanged tree (EXIT=0).
- Deterministic test (question 2): not applicable, no test added.
- Fix at the cause and complete (question 3): not applicable, no fix. The callers checked are `LOMM.cpp:76-81` (the only production caller of the `BasicFilters` setter) and the internal propagation at `BasicFilters.h:353`. Both pass non-negative engine rates.
- No collateral damage (question 4): nothing changed, so saved projects and existing behaviour are untouched.
- FALSE-POSITIVE evidence (question 5): `LmmsTypes.h:42` (`sample_rate_t` is `uint32_t`), `BasicFilters.h:341-349` (setter), `BasicFilters.h:296` (constructor, zero-only on an unsigned type), `LOMM.cpp:76-81` (production caller), `BasicFiltersSampleRateTest.cpp:52-63` (pinned zero contract).
- `feature_check.py` did not run to completion (PermissionError creating `<worktree>-feature-check/` outside the write-jail; see the table). This is why the submit check's gates item fails. The manual run of each of its checks is recorded above. Its ratchet items can only fail for a file the branch changed, and this branch changes only this report. The 28 `REGRESSION:` lines (6 complexity, 22 file-length) name no such file, so the ratchets are expected to pass. The base-branch baseline was not computed, so this is inferred, not observed.
