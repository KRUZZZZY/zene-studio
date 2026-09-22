# S7 baseline — before the lane-upconvert change (040/arch4-s7, base 1253ca7d9)

Everything here was measured on the UNMODIFIED base tree (branch `040/arch4-s7`,
`git status --porcelain` empty at the time of each run), so the post-change runs
have something to be compared against. Exit codes are unpiped
(`cmd > log 2>&1; echo EXIT=$?`).

## Configure + build (tools/local-ci.sh --jobs 2, build dir `build-ci`)

```
provision EXIT=0   (VST3 SDK 3.8.1_build_84 MIT, CLAP 1.2.10)
configure EXIT=0   (log: build-ci/configure.log)
build EXIT=2       (log: build-ci/build.log)  <- /usr/bin/ld: final link failed:
                                                 No space left on device
local-ci overall exit=1
```

The box's single 286G filesystem was at 100% when the full-suite build died at
52% (transcript tail: `build-localci-baseline.stdout`). Free space was restored
(the 2G ccache was cleared, and sibling lane `zene-712` shrank its own build dir
42G -> 4.5G while the measurement ran); the build was restarted and **scoped
down to the proof set** — `build-ci/tests` alone had reached **33G at 66%**
(static RelWithDebInfo test binaries; a full 231-test build is not feasible on
this disk while sibling lanes hold 46G+). The full-suite sweep is therefore
recorded as **not run here** (unverified list), and validation is the contract's
proof set: fixture identity, the comping pair, the new upconversion test, and
the gates.

Built on the base tree: `build-ci/zene` (269448056 bytes) and the comping pair
`TakeLaneTest`, `TakeLaneCompTest`.

## Proof 2 baseline — comping tests, pre-change (ctest from `<build>/tests`)

```
$ cd build-ci/tests && QT_QPA_PLATFORM=offscreen ctest -R "TakeLane" --output-on-failure
    1/2 Test #114: TakeLaneCompTest ... Passed 1.47 sec
    2/2 Test #115: TakeLaneTest ....... Passed 1.45 sec
    100% tests passed, 0 tests failed out of 2
EXIT=0
```

Registered-test census on the base tree: `ctest -N` -> **Total Tests: 231**.

## Proof 1 baseline — ARCH-4 S0 fixture gate, pre-change binary

`python3 verification/arch4-s0-gate.py --tree <tree> --binary <tree>/build-ci/zene`
(full output committed as `docs/s7-logs/s0-fixture-gate-BEFORE.md`):

```
corpus     PASS 38 .mmpz + 4 .mmp (want 38 + 4)
container  PASS 38/38 .mmpz decompress->compress byte-identical
verbatim   PASS 41/42 exact; 1 not-exact (all documented: ['tests/emptyproject.mmp'])
cli-verify PASS `mmpz_git.py verify` over 38 .mmpz -> exit 0
daw-raw    PASS 5 runs -> 5 distinct raw streams (raw byte-identity is NOT the oracle)
daw-canon  PASS 5 runs -> 1 distinct canonical form(s) (canonical IS the oracle)
daw-corpus PASS 38 upgraded twice; 0 canonical mismatch, 0 errors; 38/38 differ raw
RESULT: PASS
EXIT=0
```
