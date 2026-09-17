# MERGE-4-REPORT — the four platform-delta lanes into `release/0.3.0`

**What this is.** `030/wplat-lin`, `030/wplat-arm64`, `030/wplat-win` and `030/wplat-mac` —
four lanes that all branched from `eba78f8ff` (run 35126160372's head) and each fixed the
platform-dependent CI failures it owned — merged, one at a time, in the order lin, arm64, win, mac.

| item | value |
|---|---|
| base | `eba78f8ff91cc358b708c18505635a5115f16d97` |
| **merge tip** (pre-fix) | `e9a298b3f698f1dc468f2ee447b3503e8175aba6` |
| the lane-defect fix | `77f31b295058ff18e9754a9fb30bc420e2fe0959` |
| remote before | `product` `refs/heads/release/0.3.0` = `eba78f8ff91cc358b708c18505635a5115f16d97` |

The push carries this report, so the tip pushed is **this file's own commit** — its parent is
`77f31b295` (the `SafeStartTest` fix) and its grandparent is the merge tip `e9a298b3f`.

| lane | branch tip | merge commit |
|---|---|---|
| `030/wplat-lin` | `22f65d337b226c6a2d6e56fe142a07b6bbba50fc` | `e4a1e9bf2d2027513d9e37c2c7012a648f825fb6` |
| `030/wplat-arm64` | `fb7acd09ab889f1aacca6dff6269941bbdbf6a17` | `1ddc1c17da4f20bc1462e2c41f4ce689ddb6a8c0` |
| `030/wplat-win` | `fafd59dd394c0ead9bf1f4fd5fa0ec4873a27306` | `b22f984953e8a325a2a1e3f140b5876eb8184f10` |
| `030/wplat-mac` | `1e1838dfb344d69a6ba0e59a26407c185c1e70ca` | `e9a298b3f698f1dc468f2ee447b3503e8175aba6` |

All four branches are ancestors of the tip (`git merge-base --is-ancestor`, 4/4 OK), and
`git log --oneline eba78f8ff..HEAD` carries all four merge commits and every lane commit.

## Conflicts, and how each was resolved

Resolved from the index stages (`git show :2:` ours / `git show :3:` theirs), never by keeping markers.

1. **`RESUME-NOTES.md`** (add/add at the win merge, modify/modify at the mac merge) — one record,
   one `## ` section per lane (lin, win, mac), single H1, each lane's own text kept and its inner
   headings demoted exactly one level so the hierarchy reads. Nothing dropped (line-multiset check
   against the three lane copies: only heading lines differ, by that one `#`).
2. **`tests/data/vst3-chunk-probe/CMakeLists.txt`** (win's `WIN32` branch vs mac's `APPLE` +
   `WIN32` + `else`) — took mac's COMPLETE per-platform contract, which mirrors
   `tests/data/vst3-test-effect/CMakeLists.txt` (APPLE `Contents/MacOS/<stem>` + `Info.plist` +
   `macmain.cpp`; WIN32 `Contents/<arch>-win/<stem>.vst3` + `dllmain.cpp`; else the `-linux`
   tree). Win's WIN32 half is subsumed: the arch mapping is identical and its windows values are
   the ones in the merged file. Configure-checked here against a stubbed SDK —
   linux / `-DAPPLE=TRUE` / `-DWIN32=TRUE`, each `EXIT=0`, generated rules read back:
   `Contents/x86_64-linux/…so` + `linuxmain.cpp`; `Contents/MacOS/vst3-chunk-probe` +
   `Contents/Info.plist` (CFBundleExecutable = `vst3-chunk-probe`) + `macmain.cpp`;
   `Contents/x86_64-win/vst3-chunk-probe.vst3` + `dllmain.cpp`.
3. **`tests/src/core/SafeStartTest.cpp`** (lin's single-SIGSEGV reset + its evidence comment vs
   mac's generalized fix) — took mac's form, which is a superset of lin's semantics:
   `resetFatalSignalsToDefault()` sets `SIG_DFL` for SIGSEGV **and** SIGBUS/SIGILL/SIGFPE/SIGABRT/
   SIGTERM/SIGPIPE before the deliberate fault, so the child dies by the kernel's disposition
   (lin's assertion) and no inherited qtestlib handler can convert it; plus mac's `describeStatus()`
   wait-status decode and the merely-STOPPED-continue in `waitBounded()`. File is exactly 500 lines;
   `file-length-gate.sh --check` PASS.

Auto-merged but verified against **both** parents, so no side was silently dropped:

4. **`tests/CMakeLists.txt`** — lin's offscreen registration for `ClipLinkTest`,
   `ClipLinkPersistenceTest`, `ImportDetectionTest` and arm64's `MmpzGitDepthTest TIMEOUT 1800`
   both present; diff vs the arm64 branch is exactly lin's trio block, diff vs the pre-mac parent
   is empty. No markers, no duplicates.
5. **`tools/mmpz-git/tests/test_mmpz_git.py`** — arm64's `RENDER_TIMEOUT` + `run_bounded()` and its
   three call sites are present, AND mac's Python-side bound in `_loads` (no GNU `timeout`
   anywhere: `grep '"timeout"'` returns nothing). 964 lines = its `file-length-baseline-tools.tsv`
   entry, tools-scope gate PASS.
6. **`.github/workflows/build.yml`** — win's mingw `CCACHE_MAXSIZE: 2G` and its Free-disk step, and
   mac's `PYTHON3_EXECUTABLE` provisioning + `gtimeout` resolution in three backtrace steps.
   `yamllint` EXIT=0; `tests/release-staging-path-gate.sh` PASS.

## The one lane-owned defect that blocked the build (fixed minimally)

The first build of the merged tip failed:

```
tests/src/core/SafeStartTest.cpp:104:19: error: expected unqualified-id before 'public'
```

`030/wplat-mac` committed that file without compiling it (its own report says so: *"NOT compiled —
no Qt5/macOS here"*). `signals` is a Qt macro (`QtCore/qobjectdefs.h` → `public`) and QTest pulls
it in, so `const int signals[]` expanded to `const int public[]`. Fixed by renaming the table to
`fatalSignals` (3 sites, semantics and line count unchanged) in `77f31b295`, which rides this push.
Rebuild after the fix: EXIT=0.

## Build and test evidence on the merged tip (unpiped)

| step | command (from `zene-030`) | result |
|---|---|---|
| build | `cmake --build build -j4` | first **EXIT=2** (the defect above) → after the fix **EXIT=0** |
| the five, headless | `cd build/tests && env -u DISPLAY ctest -R 'ClipLinkTest\|ClipLinkPersistenceTest\|ImportDetectionTest\|ControlShutdownHookTest\|SafeStartTest'` | **5/5 passed, EXIT=0** (4.69 s) |
| meter budget | `ctest -R ControlMeterCommands` | **1/1, EXIT=0** (13.61 s) |
| plugin guards | `ctest -R 'ControlAutomationScriptTest\|ControlAutomationModesTest\|MpePlaybackTest'` | **3/3, EXIT=0** (4.48 s) |
| golden audio | `ctest -R GoldenAudioSelfTest` | registered as #163 → **1/1, EXIT=0** (0.40 s) |
| mmpz depth | `ctest -R MmpzGitDepthTest` | first **EXIT=8** — all five reds were `exit 127` + `libwasmtime.so: cannot open shared object file` (environment, not the tree) → with `LD_LIBRARY_PATH=<zene-030>/third_party/wasmtime/lib` **EXIT=0, Passed 93.73 s** |
| full sweep | `ctest -j4 --output-on-failure` | **213/213 passed, EXIT=0** (146.48 s) |

## Gate evidence

`bash tests/run-all-gates.sh` → **EXIT=3 = PASS-WITH-SKIPS**, the expected verdict: gate 2
(coverage) is the only skip and it needs `--with-coverage` by design.

* gate 1 ctest — 213/213, 549.00 s; gate 3 tautology PASS; gate 4 complexity PASS (fork + tools)
* gate 5 mutation (`src/core/RoutingGraph.cpp`) — **kill score 88.5% ≥ 80%**, and `git status`
  after the run shows no mutant left in the tree
* gate 6 upstream ledger PASS (422 changed paths declared, ledger holds 460 entries);
  gate 7 file length PASS (fork + tools); gate 8 duplication PASS (0.52% / 0.00%)
* gate 9 registration PASS (660 fork-NEW, 1104 inherited, 40 tooling, 0 stale);
  gate 10 test-source registration PASS; gate 11 evidence/size PASS; gate 12 RT-safety PASS

Run separately, all EXIT=0: `fork-sources-gate.sh`, `no-upstream-regression-gate.sh`,
`all-sources-reproduce.sh` (**REPRODUCES**), `unregistered-tests-gate.sh`,
`file-length-gate.sh --check` (fork and `--scope tools`), `complexity-gate.sh --check`.

### Manifest finding, recorded not touched

`tests/fork-sources.txt`'s own verify recipe does **not** reproduce — it reports
`tests/src/core/OutOfProcessHostSupport.h` as an entry the recipe's `awk` allow-list does not
emit. That is **pre-existing**: the same recipe run with the range's `HEAD` replaced by
`eba78f8ff` prints the identical single diff line and also exits 1. The entry-set gate
(`fork-sources-gate.sh`) passes, the file was not changed by any of the four lanes, and per the
merge policy a pre-existing red is recorded rather than re-anchored or weakened.

## What this push asks CI to prove (run #4)

The six red build-matrix jobs of run 35126160372 and the fixes that should clear them:
`linux-x86_64` (offscreen trio + SafeStart + shutdown hook), `linux-arm64` (meter budget, mmpz
depth bound, `TIMEOUT 1800`), `macos-arm64` + `macos-x86_64` (python provisioning, `gtimeout`
backtrace, vst3 bundle layout, `_loads` bound, SafeStart, golden-audio diagnostic), `mingw64`
(free disk + 2G ccache), `msvc-x64` (the three plugin-module guards + the vst3 fixture layout).
