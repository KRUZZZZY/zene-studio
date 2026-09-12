# Release-prep evidence — Zene Studio 0.2.0-alpha

Committed evidence for the `post-alpha/release-prep` lane (base `post-alpha/integration` @ `34c1f4f86`).
Every file here was produced by a command that is quoted in `docs/RELEASE-PREP-0.2.0.md` §8, with the
exit code read unpiped. Logs live here rather than in `/tmp` on purpose (the release checklist's
`B2.9`: the integration verification of record cited eight transient `/tmp` paths and they are gone).

**Nothing here is a substitute for the freeze re-runs.** These are this lane's measurements on its own
base; the release is verified against the frozen tree and the published artefacts, not against this.

| file | what it is |
|---|---|
| `01-version-untagged-ci-flags.txt` | `build/zene --version` with the CI flag set (no version override) |
| `02-version-0.2.0-alpha.txt` | the same binary reconfigured with the repo's documented `-DFORCE_VERSION=internal` |
| `03-configure-forceversion-internal.txt` | the configure banner for that build: `Project version : 0.2.0-alpha` |
| `04-honesty-gate-header-only.txt` | `release-honesty-gate.sh --header build/lmmsversion.h` → **exit 0, 6/6 PASS** |
| `05-honesty-gate-with-artifacts.txt` | the same plus `--artifacts build` → exit 1, 1 of 6 FAIL (`vst3instrument`) |
| `06-release-version-gate.txt` | the new version gate on this tree → exit 0 |
| `07-test-release-version-gate.txt` | that gate's red/green harness: 8 controls, all as declared |
| `08-fork-sources-gate.txt` | Gate 9 → exit 0 |
| `09-no-upstream-regression-gate.txt` | Gate 6 → exit 1; the one violation, `tools/ncpu-shim.c`, is pre-existing |
| `10-run-all-gates-summary.txt` | `run-all-gates.sh --no-mutation` summary (exit 1: gate 6) |
| `11-ctest.txt` | `ctest` from `build/tests` → 100% passed, 0 failed, of 47 |
| `12-gate-runner-unbound-array.txt` | the `set -u` empty-array defect in `run-all-gates.sh`, before and after |
| `13-local-ci-summary.txt` | `tools/local-ci.sh` step exits, ctest totals and the per-job coverage lines |
