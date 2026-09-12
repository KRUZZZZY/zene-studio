# Evidence for the v0.2.0-alpha CI failures

Everything here was produced while fixing the tag `v0.2.0-alpha` of
`KRUZZZZY/zene-studio`. Nothing is a summary of a log — the logs and the
harnesses that produced the verdicts are in this directory.

## What failed, and where the evidence is

| source | ref | result |
|---|---|---|
| `build.yml` run [34708003925](https://github.com/KRUZZZZY/zene-studio/actions/runs/34708003925) | tag `v0.2.0-alpha`, commit `b099fd6cb41394ebf364c755b227ee9980e24970` | **7/7 jobs failed** |
| `checks.yml` run [34708003952](https://github.com/KRUZZZZY/zene-studio/actions/runs/34708003952) | same commit | **1/3 jobs failed** (`scripted-checks`, at the `Run check-namespace` step); `shellcheck` and `yamllint` passed |

The seven `build.yml` job logs are `*.clean.log` (ANSI escape sequences
stripped). They were fetched per job, because `gh run view --log` refuses while
a run is still in progress and the per-job endpoint serves a job as soon as that
job is complete:

```sh
R=KRUZZZZY/zene-studio
gh api repos/$R/actions/runs/34708003925/jobs --jq '.jobs[] | "\(.id) \(.name): \(.conclusion)"'
gh api repos/$R/actions/jobs/<job-id>/logs --allow-escape-sequences > <job>.log
sed -E 's/\x1b\[[0-9;]*[mGKHF]//g; s/\^\[\[[0-9;]*m//g' <job>.log > <job>.clean.log
```

## The harnesses (re-run any of them; each exits 0 only when every expectation holds)

Each `run.sh` is self-contained: it writes its own probe sources, synthetic
headers and probe projects into `out/gen/` (gitignored) and prints a verdict
table. That keeps `tests/` free of standalone translation units that nothing
builds, while leaving the probe sources readable in the script itself.

| harness | reproduces | proves |
|---|---|---|
| `qdebug-probe/run.sh` | `ConfigManager.cpp:700` "invalid use of incomplete type 'class QDebug'" | the missing `<QDebug>` include is what the error turns on (red/green, one probe, one compilation difference) |
| `fenv-arch-check/run.sh` | `include/fenv.h:20` "no member named '__control' in 'fenv_t'", as the macos-arm64 job reported it | the arm64 branch's bit arithmetic (against Darwin's own `fenv_t`/`FE_*`/`__fpcr` declarations) and that the x86_64 branch is textually unchanged |
| `cmake-probes/run.sh` | `ClapHeaders.cmake:45` (mingw64) and `InstallTargetDependencies.cmake:37` "Not a target: lmms" (msvc-x64, windows-arm64) | the pre-fix module fails under the MinGW find policy / with `TARGETS lmms`, the fixed module does not |

Their outputs are the `run.log` files beside them.

## Local build, test and gate logs

- `provision.log` — `.github/workflows/provision-plugin-hosting-deps.sh build` run on this box (exit 0; VST3 SDK `v3.8.1_build_84` and CLAP `1.2.10` fetched).
- `configure.log` — `cmake -S . -B build -DWANT_QT6=ON -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DWANT_VST3=ON -DWANT_CLAP=ON` (exit 0).
- `build.log` — `cmake --build build -j2`, the last line carrying its exit code.
- `ctest.log` — `ctest` from `build/tests` (the top-level build directory has no `CTestTestfile.cmake` and would report 0 tests).
- `check-namespace.log` — `python3 tests/scripted/check-namespace`, the four errors it still reports.

## Deliberately not copied here

Apple's macOS SDK `fenv.h` (`MacOSX11.3.sdk/usr/include/fenv.h`) was read to fix
the polyfill but is not redistributed: it carries Apple's own licence. The
per-architecture declarations the fix relies on are quoted in
`include/fenv.h`'s comments and in the synthetic header that
`fenv-arch-check/run.sh` generates, with the SDK path and the section they came
from named there. The one VST3 SDK excerpt kept here —
`vst3-module_mac.mm.txt`, the `#error` that requires ARC — is MIT (VST3 >= 3.8).
