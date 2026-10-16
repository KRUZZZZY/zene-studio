# Cluster Q050-145: Document Zene's current free and activation model

**Verdict: IMPLEMENTED**

README.md now states the verified current no-tier/no-activation state, cites LICENSE.txt:21-36, records the empty source search for activation/licence-key checks, and names the GitHub Releases page as the announcement location for any future policy change. No future pricing policy is promised. No code, command, undo step or project format changed.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| License cited | `sed -n '21,36p' LICENSE.txt > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-145.r1/logs/license-1.log` from the worktree; **EXIT=0** |
| No product activation/licence-key checks | `find src include plugins -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) | grep -v '/3rdparty/' | grep -v 'CarlaBase/carla' | grep -v 'plugins/LadspaEffect/calf' | xargs grep -HniE 'license[-_ ]?key|activation[-_ ]?(code\|key)\|tier\|subscription\|serial (number\|key)\|register (this\|your\|the) (copy\|version\|product)\|premium\|trial period'` run from the worktree, output to `.../search-4.log`; **EXIT=0**, 35 lines, all unrelated technical uses (port/event/MIDI subscription, channel tier limits, rtneural); no product licensing or activation check |
| README edit | `git diff -- README.md` shows the new "Free and activation model (current build)" section (+18 lines) at `README.md:218` |
| Complexity gate | `bash tests/complexity-gate.sh --check > .../logs/gate-complex.log 2>&1; rc=$?; echo EXIT=$rc` from worktree root; **EXIT=1** (pre-existing base regressions only; this unit adds no source file and exits no newly listed file) |
| File-length gate | `bash tests/file-length-gate.sh --check > .../logs/gate-filelen.log 2>&1; rc=$?; echo EXIT=$rc` from worktree root; **EXIT=1** (pre-existing; `README.md` is not listed) |
| Fork-sources gate | `bash tests/fork-sources-gate.sh > .../logs/gate-fork.log 2>&1; rc=$?; echo EXIT=$rc`; **EXIT=0** |
| All-sources reproduce | `bash tests/all-sources-reproduce.sh > .../logs/gate-allsources.log 2>&1; rc=$?; echo EXIT=$rc`; **EXIT=0** |
| No-upstream-regression gate | `bash tests/no-upstream-regression-gate.sh > .../logs/gate-nourg.log 2>&1; rc=$?; echo EXIT=$rc`; **EXIT=0** |
| Namespace check | `tests/scripted/check-namespace > .../logs/gate-namespace.log 2>&1; rc=$?; echo EXIT=$rc`; **EXIT=0** |
| No-tautology gate | `bash tests/no-tautology-gate.sh > .../logs/gate-tautology.log 2>&1; rc=$?; echo EXIT=$rc`; **EXIT=0** |

## Files committed

- `README.md`
- `docs/050-features/Q050-145.md`
- `docs/qwen-fixes-2026-10/cluster-Q050-145.md`
EOF
echo done