# MERGE-5-REPORT — `030/mac-forkfix` + `030/mingw-disk2` into `release/0.3.0`

**What this is.** Two fix lanes, both branched from `8edfe30d5` (run #4's head), merged into the
release line one at a time — `030/mac-forkfix` first, then `030/mingw-disk2`. They carry the two
remaining red causes of run 35212797698: the macOS `SafeStartTest` crash child (CoreFoundation
aborts a fork-only child) and the mingw64 ENOSPC at 66 % (≈207 test targets × ≈0.75 GB DWARF
archives against a 145 GB runner volume).

| item | value |
|---|---|
| base | `8edfe30d581d53736e519deb653ca235f17853c2` |
| **merge tip** (pre-report) | `7f4a4576946fbd1cf4eb88043b8c3559fd980356` |
| remote before | `product` `refs/heads/release/0.3.0` = `8edfe30d581d53736e519deb653ca235f17853c2` |

The push carries this report, so the tip pushed is **this file's own commit**; its parent is the
merge tip `7f4a45769`.

| lane | branch tip | merge commit |
|---|---|---|
| `030/mac-forkfix` | `d117543f81ba4eacbbebba665ea70747b3f05380` | `d253aa5fc1816e7aeea6d9c44b779d68bde79a95` |
| `030/mingw-disk2` | `f5e9aeff261f85c22016274f8722babfebda99f0` | `7f4a4576946fbd1cf4eb88043b8c3559fd980356` |

Both branches are ancestors of the tip (`git merge-base --is-ancestor`, 2/2 OK), and
`git log --oneline 8edfe30d5..HEAD` carries both merge commits and every lane commit.

## Conflicts, and how each was resolved

**None.** Both merges were clean under the `ort` strategy — the two branches touch disjoint files
(`tests/src/core/SafeStartTest.cpp` + `docs/reports/WPLAT-MAC-REPORT.md` vs
`.github/workflows/build.yml` + `docs/reports/MINGW-DISK-ROUND2.md`) and neither touches a file the
tip changed under them.

Post-merge assertions, all run on the merge tip:

- `git merge-base --is-ancestor 030/mac-forkfix HEAD` → 0; `030/mingw-disk2 HEAD` → 0.
- `git grep -n '^<<<<<<< \|^>>>>>>> '` → nothing; the `^=======$` separator token also scanned
  over code/workflow/ledger files → nothing.
- Content probes: `tests/src/core/SafeStartTest.cpp` carries the `--safe-start-crash-child`
  protocol (fork **and**`exec`, 499 lines); the mingw job in `build.yml` caches
  `~/.cache/ccache` (line 841 — the directory ccache 4.9 actually uses) and configures with
  `-DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -DNDEBUG"` + the C equivalent, with `df -h` over every
  mount printed before and after the Free-disk step. The other six jobs' ccache paths are untouched.
- Trade-off inherited with `030/mingw-disk2`: mingw artifacts ship unsymbolized; msvc/msys2 unchanged.

## Local verification at the merge tip (`7f4a45769`, zene-030, warm `build/`)

**Build.** `cmake --build build -j4` → **EXIT=0** (log `/tmp/merge5-build.log`): the one changed
translation unit recompiled and 153 test executables relinked; 882 built-target lines, no errors,
~2.5 min.

**Focused tests.** `env -u DISPLAY ctest -R 'SafeStartTest|ControlShutdownHookTest'` from
`build/tests` → **2/2 passed, exit 0** (`ControlShutdownHookTest` 0.07 s, `SafeStartTest` 1.31 s —
the merged exec'd-child implementation).

**Full ctest.** `env -u DISPLAY ctest -j4 --output-on-failure`:

- First run: 208/213, exit 8. All five reds are one environmental cause, not a defect:
  `ControlSocketIntegration` (#166), `ControlExportSettings` (#170), `agent_surface` (#204),
  `RenderSoftwareTag` (#210), `MmpzGitDepthTest` (#212) — every one fails because the spawned
  `build/zene` binary cannot open `libwasmtime.so` (exit 127). On this box the library exists only
  at `third_party/wasmtime/lib/` and there is no `ld.so.conf.d` entry or `ldconfig` cache entry for
  it, so anything that execs the built binary needs `LD_LIBRARY_PATH` set locally (the CI build
  jobs' own test trees do not have this problem — run #4's linux-x86_64 measured 209/209).
- Re-run with
  `LD_LIBRARY_PATH=$PWD/third_party/wasmtime/lib`: **213/213 passed, 0 failed, exit 0**
  (log `/tmp/merge5-ctest2.log`).

**Gate suite.** `env -u DISPLAY bash tests/run-all-gates.sh` (unpiped, backgrounded):

- Run A, exactly as CI invokes it (no `LD_LIBRARY_PATH`): **EXIT=1** — gate 1 `ctest` FAIL, for the
  same five libwasmtime-loader reds alone; gate 2 SKIP (`--with-coverage` not passed); gates 3-12
  **PASS**, including gate 5 mutation (`src/core/RoutingGraph.cpp`, control 3/3, KILLED rows in
  log) and gate 4/7 ratchets. The tree was left clean by the suite itself (gate 5 restored
  `RoutingGraph.cpp`; `git status --porcelain` shows only the untracked nested worktree dirs).
- Run B, with `LD_LIBRARY_PATH` set and `--no-mutation` (gate 5's PASS is run A's evidence):
  **EXIT=3 — `RESULT: PASS-WITH-SKIPS`**, gate 1 `ctest` PASS 213/213; gate 2 SKIP
  (`--with-coverage` not passed) and gate 5 SKIP (`--no-mutation`), the only two skips.
- Net: no gate fails for any reason other than the local loader path, and with that path set the
  enforced-scope suite is PASS-WITH-SKIPS — the expected pre-push state.

Logs: `/tmp/merge5-build.log`, `/tmp/merge5-ctest.log`, `/tmp/merge5-ctest2.log`,
`/tmp/merge5-gates.log`, `/tmp/merge5-gates2.log`.

## What the next run must show

Run #5 (four workflows on this tip) is read job-by-job afterwards. Expectations from run #4's
verdicts: macos-arm64 + macos-x86_64 green on #48 (child now execs; the mac *Signal-death
backtrace* step still has to behave), mingw64 past 66 % to a full build (the new `df -h`
every-mount lines appear; the ccache step still MISSes once — the compile command changed — which
is expected, not a defect). If all seven build jobs go green, the previously-skipped
*release gate (green matrix required)* job runs for the first time.
