# MERGE-6-REPORT — `030/attr-1` + `030/arch2-api` into `release/0.3.0`

**What this is.** Two lanes merged into the release line one at a time — `030/attr-1` first, then
`030/arch2-api`. Together they close the attribution defect on the shipped demos and split the
control registry out of the QtWidgets-linked core into its own static library with a registered
boundary test.

| item | value |
|---|---|
| base | `f16baff79fb536508dbb0d64c761816d623c20b5` |
| **merge tip** (pre-report) | `a039d262ec2c4a0d6bdfccc644f4c197b5e132b8` |
| remote before | `product` `refs/heads/release/0.3.0` = `f16baff79fb536508dbb0d64c761816d623c20b5` |

The push carries this report, so the tip pushed is **this file's own commit**; its parent is the
merge tip `a039d262e`.

| lane | branch tip | merge commit |
|---|---|---|
| `030/attr-1` | `93d8e9a1f0b8c12853066d9efcea9b87b4ffdbe8` | `f10c57ba0f68de53fa4bdc84da6076c944cee59f` |
| `030/arch2-api` | `b951dc2a90e798db57d42312702e58b370292daf` | `a039d262ec2c4a0d6bdfccc644f4c197b5e132b8` |

Both branches are ancestors of the tip (`git merge-base --is-ancestor`, 2/2 OK), and
`git log --oneline f16baff79..HEAD` carries both merge commits and every lane commit. Net diff
`f16baff79..HEAD`: **49 files changed, 1636 insertions(+), 45 deletions(-)**.

## Conflicts, and how each was resolved

**None — and none was expected.** The two branches touch fully disjoint file sets (verified before
merging with `git diff --name-only f16baff79...<branch>`): `030/attr-1` carries 41 paths (38
`data/projects/**/*.mmpz` demos, `tests/upstream-modifications.txt`, `docs/RENAME-COMPLETE.md`,
`docs/reports/ATTR1-REVERT-REPORT.md`), `030/arch2-api` carries 8
(`include/zene/api/ControlApi.h`, `src/CMakeLists.txt`, `src/core/CMakeLists.txt`,
`tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/fork-sources.txt`,
`tests/zene-api-boundary.py`, `docs/reports/ARCH2-BOUNDARY-REPORT.md`). The intersection is empty.
Both merges applied clean under the `ort` strategy.

Post-merge assertions, all run on the merge tip:

- `git merge-base --is-ancestor 030/attr-1 HEAD` → 0; `030/arch2-api HEAD` → 0.
- `git grep -n '^<<<<<<< \|^>>>>>>> '` over the whole tree → nothing; no conflict markers.
- `git rev-list --parents -n 1 HEAD` → `a039d262e f10c57ba0 b951dc2a9` — the tip is a true merge
  whose two parents are the first merge commit and the arch2-api lane tip.
- Content probe: `include/zene/api/ControlApi.h` (4995 bytes) and `tests/zene-api-boundary.py`
  (20057 bytes) are present at the tip; the 38 demo `.mmpz` blobs are the reverted upstream ones.
- The working tree is clean apart from the untracked nested worktree dirs (`warch2/`, `wattr/`,
  `wcrash/`), which belong to sibling lanes and were not touched.

## Local verification at the merge tip (`a039d262e`, zene-030, warm `build/`)

**Build.** `cmake -S . -B build` → **CMAKE_EXIT=0** (same cache, no options changed; the merged
`CMakeLists.txt` files reconfigured cleanly, 1.5 s configure / 2.7 s generate). Then
`cmake --build build -j4` → **BUILD_EXIT=0**, 18:01:55 → 18:05:25 (≈3.3 min, log
`/tmp/merge6-build.log`). The new `zene_api` static library compiled its **42 translation units**
(`build/src/CMakeFiles/zene_api.dir/**/*.o` = 42) and the rest of the tree relinked. Disk unchanged
by the build: **81 GB free** before and after.

**Focused tests.** From `build/tests`, unpiped, with `LD_LIBRARY_PATH=<tree>/third_party/wasmtime/lib`:
`ctest -R 'ZeneApiBoundary|ControlRegistryTest|ReversibilityContractTest|SafeStartTest'` →
**4/4 passed, exit 0** (ControlRegistryTest 1.42 s, SafeStartTest 1.28 s, ReversibilityContractTest
1.43 s, **ZeneApiBoundary 5.22 s** — the NEW test, registered as ctest #213).

**Full ctest.** `ctest -j4 --output-on-failure` (same env):
**214/214 passed, 0 failed, CTEST_EXIT=0**, total 144.58 s (log `/tmp/merge6-ctest-full.log`). This
is the expected 213 + 1: `ZeneApiBoundary` is the only test added by the arch2-api lane, and ctest
reports `Total Tests: 214`.

**Live instance re-check at the merged tip** (the arch2 lane's before/after proof, re-run here).
One headless instance booted with the tree's own harness (`tests/control_socket_harness.py`,
imported in place from a `/tmp` script — `/tmp/merge6-ids/check_ids.py`, the same shape as
`/tmp/crashproof/step_a.py`), driven over the control socket, then closed via `control.quit`:

- `control.ping` → `version` `0.2.1-alpha.612+a039d26` — the build under test is this merge tip
  (`a039d26`), not a stale binary.
- `control.commands_list` → **340 ids in 53 groups**, declared `count` field **340** (proto 1).
  Full group list: app, arrangement, audio, automation, bounce, browser, bus, chain, chord, clip,
  clock, comp, control, controller, crash, dawproject, detect, device, dsp, export, freeze, groove,
  interchange, link, mastering, meter, midi, mixer, modulator, note, oop, patcher, pdc, plugin,
  port, project, rack, record, render, revisions, roll, routing, safestart, scale, script, session,
  settings, telemetry, track, transport, vca, warp, wasm.
- `control.quit` → `quitting: true`, process exited **code 0** after 0.24 s, socket file gone.
- Verdict **PASS**; evidence `/tmp/merge6-ids/check_ids.json` and `.transcript.txt`.

Net: moving the registry core into `zene_api` changed **neither the command count nor the group
count** — the boundary work is invisible to the command surface it was carved out of.

**Gate suite.** `bash tests/run-all-gates.sh` (unpiped, backgrounded, with the loader path set so
gate 1 measures the suite rather than the local `libwasmtime` search path):

- **GATES_EXIT=3 — `RESULT: PASS-WITH-SKIPS`**, 18:15:50 → 18:33:18 (log `/tmp/merge6-gates.log`).
- Gate 1 `ctest` **PASS 214/214** (561.48 s serial); gates 3–12 **PASS**, including gate 5 mutation
  (`src/core/RoutingGraph.cpp`: 30 selected of 170 candidates, 23 KILLED / 3 survived / 4 invalid
  no-build → **kill score 23/26 = 88.5 %**, threshold 80 %) and gate 4/7 ratchets.
- Gate 2 `coverage` **SKIP** — `--with-coverage` was not passed; it is the only skip. Per the
  suite's own wording, exit 3 is an *incomplete* run, not a green one; it is nonetheless the
  expected pre-push state and the one CI's `static-gates` job reproduces.
- The suite left the tree clean: `git status --porcelain` after the run shows only the untracked
  nested worktree dirs (gate 5 restored `RoutingGraph.cpp`).

Note the difference from the MERGE-5 run: this run sets `LD_LIBRARY_PATH`, so gate 1 is a real
PASS over all 214 tests. Run *without* that variable, gate 1 fails for the five
`libwasmtime.so`-loader tests that MERGE-5 documented — an environmental search-path issue on this
box, not a defect in the tree (CI's own build jobs run 209/209 without it).

Logs: `/tmp/merge6-cmake.log`, `/tmp/merge6-build.log`, `/tmp/merge6-ctest-full.log`,
`/tmp/merge6-gates.log`, `/tmp/merge6-ids/`.

## What the next run must show

Run #6 (four workflows on this tip). Run #5 was fully green on `f16baff79` — 7/7 build jobs and all
four workflows (`linux-x86_64` 209/209, `linux-arm64` 206/206 in its ~2 h job, `msvc-x64` 161/161,
both macOS 206/206, `windows-arm64` green, `mingw64` a 100 % build). Expectations for this run are
the same shape with these deltas:

- **Test counts +1** wherever tests run: `ZeneApiBoundary` is newly registered, so the platform
  totals become 210 / 207 / 162 / 207 / 207.
- **`ZeneApiBoundary` on each platform.** It is a build-level include-closure and `nm` boundary
  check; on MSVC its include-closure and `nm` checks **SKIP by design** while the build-level
  include-path gate still applies — that is expected, not a defect.
- **mingw64 disk margin** — the DWARF-free `RelWithDebInfo` fix from `030/mingw-disk2` now carries
  one more target; the job's own `df -h` lines are the evidence to read.
- The honesty/version guards in the static-gates job must still agree with the tree.
