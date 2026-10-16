IMPLEMENTED
# Cluster Q050-145: Document Zene's current free and activation model

Round 2 (rework). The round-1 review points are all addressed on top of the round-1 commit:

- the `--version` policy-reporting promise is removed from README.md (and the new gate
  keeps it out permanently);
- the GPL is cited as the project's license while disclosing that it permits charging
  ("gratis or for a fee") and that the license settles nothing about tiers; the no-tier
  statement is grounded separately in current repository facts (the narrowed source search
  and the build/release documentation);
- the activation-search claim is narrowed to the scanned product-owned paths (`src/`,
  `include/`, `plugins/`, first-party C/C++) and the vendored exclusions are named
  explicitly, the same set `tests/all-sources.txt` excludes;
- both reports (this file and `docs/050-features/Q050-145.md`) are updated;
- the documentation assertion is now a committed, regression-tested static check:
  `tests/doc-pricing-model-gate.py`, registered as ctest `DocPricingModelGate` in
  `tests/CMakeLists.txt`, with a red control proving it fails when the wording drifts;
- the documentation assertion and the license/source checks were rerun (table below).

README.md's `## Free and activation model (current build)` states the current no-paid-tier /
no-registration / no-activation state, names the GPL-2.0-or-later license, discloses the
GPL's permit-to-charge clause as a licence fact only, grounds the no-tier statement in the
product-source search and the build/release repo facts, and names the GitHub Releases page
(release notes authored as `docs/RELEASE-NOTES-<version>.md`, per `docs/RELEASING.md`) as the
announcement location for any future policy change. No future pricing policy is promised.
No code, command, undo step or project format changed.

## Evidence table (commands run in this worktree; logs in
/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-145.r2/logs)

| Check | Exact command and result |
| --- | --- |
| License extract cited | `sed -n '21,36p' LICENSE.txt > .../01-license.txt`; **EXIT=0** |
| Narrow activation / licence-key source search (2908 product-owned C/C++ files after the vendored exclusions) | `git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' \| grep -vE '<exclusions>' \| xargs grep -HniE 'licen[cs]e[-_ ]?key\|activation[-_ ]?(code\|key\|fee)\|...\|trial[-_ ](period\|version\|mode)\b' > .../02-source-search-narrow.txt`; **EXIT=123 (xargs: no file had a match), 0 output lines** - zero product-licensing matches |
| Broader competitor words scoped | `tier\|premium` matches only `include/AudioInputPath.h:84` and `include/MultiTrackRecorder.h:86` (competitor comparisons), allowlisted in the gate |
| Documentation assertion (green) | `python3 tests/doc-pricing-model-gate.py > .../04-gate-green.txt`; **EXIT=0** |
| Documentation assertion (red control) | sed reintroduces the round-1 `--version` promise into the README section, rerun, then restore from backup; `> .../03-gate-red-control.txt`; **EXIT=1**; after restore **EXIT=0** |
| Regression-run of the static check | `cmake --build build --target zene -- -j4 > .../05-build-zene.log`; **EXIT=0**; then `ctest -R 'DocPricingModelGate\|DocFiguresGate' --output-on-failure` from build/tests, `> .../06-ctest-docgates.log`; **EXIT=0**, 2/2 passed (DocPricingModelGate #584) |
| Complexity gate (gate 4, red on train 5 by design) | `bash tests/complexity-gate.sh --check > .../07-gate-complexity.log`; **EXIT=1**, inherited; over-threshold list byte-identical to the `../zene-cachyos` baseline (same FAIL/OVER md5); no file of this unit listed |
| File-length gate (gate 7, red on train 5 by design) | `bash tests/file-length-gate.sh --check > .../07-gate-file-length.log`; **EXIT=1**, inherited; output identical to the baseline (same md5); no file of this unit listed |
| Fork-sources gate | `bash tests/fork-sources-gate.sh > .../08-gate-forksources.log 2>&1; rc=$?`; **EXIT=0** |
| All-sources reproduce | `bash tests/all-sources-reproduce.sh > .../09-gate-allsources.log 2>&1; rc=$?`; **EXIT=0** |
| No-upstream-regression gate | `bash tests/no-upstream-regression-gate.sh > .../10-gate-nourg.log 2>&1; rc=$?`; **EXIT=0** |
| Namespace check | `tests/scripted/check-namespace > .../11-gate-namespace.log 2>&1; rc=$?`; **EXIT=0** |
| No-tautology gate | `bash tests/no-tautology-gate.sh > .../12-gate-tautology.log 2>&1; rc=$?`; **EXIT=0** |

## Files committed (round 2)

- `README.md` (the rewritten free-and-activation section)
- `tests/doc-pricing-model-gate.py` (new durable static documentation check)
- `tests/CMakeLists.txt` (register ctest `DocPricingModelGate`)
- `tests/upstream-modifications.txt` (ledger entry with the reason)
- `docs/050-features/Q050-145.md`
- `docs/qwen-fixes-2026-10/cluster-Q050-145.md`
