# Release verification — 0.2.0-alpha

- worktree: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-integration
- commit verified: 6b593df4167a94fea4427f07dc0a9e6fe53759ad
- branch: post-alpha/integration
- date: 2026-09-12T14:49:23+01:00
- jobs: 4

| result | step |
|---|---|
| PASS |  provisioning |
| PASS |  configure |
| PASS |  build |
| PASS |  ctest-off |
| PASS |  reconfigure-on |
| PASS |  build-on |
| PASS |  ctest-on |
| PASS |  reconfigure-back-off |
| PASS |  build-back-off |
| FAIL |  gate-fork-sources (exit 1) |
| PASS |  gate-upstream-regression |
| PASS |  gate-tautology |
| PASS |  gate-complexity-fork |
| PASS |  gate-file-length-fork |
| PASS |  gate-duplication |
| SKIP |  gate-complexity-all (optional, exit 1) |
| SKIP |  gate-file-length-all (optional, exit 1) |
| FAIL |  gate-all (exit 1 — a gate inside the runner failed; log: docs/release-verification-0.2.0-alpha/gate-all.log) |
| SKIP |  gate-coverage (no instrumented tracefile at build/coverage/coverage-fork.info; needs Debug+WANT_COVERAGE=ON — see docs/COVERAGE-RUN.md) |
| PASS |  honesty-guard |
| PASS |  render-editing_note_volumes.mmp |
| PASS |  render-DirtyLove.mmpz |
| PASS |  render-Root84-TrancyLoop.mmpz |

pass=18 fail=2 skip=3

Note: a SKIP is not a pass. The gates that need a build report what they measured;
the whole-tree scopes are reported rather than gated on, and QA-GATES.md records why.
