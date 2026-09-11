# PR #594 rebase onto post-alpha — Session View data layer

**Verdict: rebased, built and tested green on both gate states. Ready to push as a branch; nothing was pushed.**

| | |
|---|---|
| Worktree | `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-pr594` |
| Base | `post-alpha/v0.2` = `0c23587d254ff981d3881c42ef98ac4b0997a7e6` |
| Rebased branch | `post-alpha/pr594` (created at the base; see *Branch naming* below) |
| Commit | `ca5c19376ec3d76e883f1befcae429e1d6585672` — `feat(session): Session View data layer with versioned XML persistence (#594)` |
| Original | `e317f1062` on `feat/session-view-model` (untouched, not moved, not deleted) |
| Method | `git cherry-pick -x e317f1062` (single commit; `-x` added the `(cherry picked from commit e317f1062…)` trailer) |
| Diffstat vs base | 15 files changed, **1677 insertions(+)**, 0 deletions — identical to the original patch |

Branch naming: the worktree was checked out on `post-alpha/pr594-rebase`, and no branch `post-alpha/pr594` existed. The task named `post-alpha/pr594`, so it was created at `0c23587d2` and the work was done there; `post-alpha/pr594-rebase` was left untouched at `0c23587d2`.

## 1. Commit set (inventory, before touching anything)

```
$ git log --oneline post-alpha/v0.2..feat/session-view-model
e317f1062 feat(session): Session View data layer with versioned XML persistence (#594)
```

One commit, 15 files, 1677 insertions (CMake wiring, `include/SessionModel.h`, `include/Song.h`,
`src/core/{SessionModel,SessionClip,SessionModelPrivate.h,CMakeLists.txt,Song.cpp}`, `src/CMakeLists.txt`,
`src/lmmsconfig.h.in`, 2 tests + 3 repo list files).

## 2. Per-conflict decisions

One textual conflict; every other hunk auto-merged, and each auto-merged integration point was
re-read on the merits against current main (local architecture is the authority).

| # | File:line (result) | Conflict | Decision |
|---|---|---|---|
| 1 | `tests/upstream-modifications.txt:28-33` | `HEAD` had local entries (`plugins/Oscilloscope/OscilloscopeGraph.cpp`, `src/core/AudioBuffer.cpp`); the patch added `include/Song.h` in the same slot | **Union** — both kept. Dropping the local entries would turn them into undeclared divergence for gate 6 (they postdate `tests/gate-base.txt` = `01148947e`); dropping `include/Song.h` would equally fail the gate because the patch modifies an upstream-inherited file. Placed `include/Song.h` in the include/ group, then the local entries. Gate 6 re-run: **PASS, exit 0, 34 ledger files**. |

Auto-merged, checked on the merits (no local/incoming disagreement):

* `CMakeLists.txt:110` `OPTION(WANT_SESSION_VIEW … OFF)`; `:147` `IF(WANT_SESSION_VIEW)` block. Local `VERSION_MAJOR/MINOR` = `0`/`1` (product alpha) preserved — the only content difference from the PR tip in this file.
* `include/Song.h:38,44-46,334-339,474-476` — gated include/accessor/member appended below main's lines; `Song.h` is unchanged by the 37 local commits in that region.
* `src/core/Song.cpp:920-922, 1140-1147, 1256-1263` — verified in place: `clearProject()`, the `loadProject()` element chain **after** main's newer `else if("keymaps")` branch and **before** the GUI branch, and `saveProjectFile()` after `saveKeymapStates()` / before `m_savingProject = false`. Local `scales`/`keymaps` branches kept; the session branch is additive.
* `src/CMakeLists.txt:121-128`, `src/core/CMakeLists.txt:20-25,173`, `src/lmmsconfig.h.in:42` — additive; local context intact.
* `tests/CMakeLists.txt` — `SessionModelTest` appended to `LMMS_TESTS` after main's stem block; main's `RemotePluginClientE2ETest` registration and Part C blocks untouched.
* `tests/fork-sources.txt` — 4 new entries added; main's `tools/local-ci.sh` entry kept.
* `tests/src/core/ProjectVersionTest.cpp`, `tests/src/core/SessionModelTest.cpp`, the four `Session*` sources — applied byte-identically to the PR tip (that file is unchanged since the PR's base commit).

Byte-identity check: **11 of the 15 files are byte-identical to the PR tip**; the 4 that differ
(`CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/upstream-modifications.txt`)
differ **only** by content main gained after the PR was written (alpha version numbers, the remote-plugin
E2E tests, `tools/local-ci.sh`, the local ledger entries).

## 3. Build and test — `WANT_SESSION_VIEW=ON`

```
cmake -S . -B build -DWANT_QT6=ON -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DUSE_COMPILE_CACHE=ON -DWANT_SESSION_VIEW=ON
  → configure exit 0 (log /tmp/kos_cfg_on.log); build/lmmsconfig.h:42 `#define LMMS_HAVE_SESSION_VIEW`
cmake --build build -j6 > build.log 2>&1; echo BUILD_EXIT=$?
  → BUILD_EXIT=0 (recorded in build.log; preserved as build-on.log)
cd build/tests && ctest --output-on-failure > ctest.log 2>&1; echo CTEST_EXIT=$?
  → CTEST_EXIT=0   ·   100% tests passed, 0 tests failed out of 26   ·   Total Test time (real) = 33.02 sec
    (log preserved as build/tests/ctest-on.log; SessionModelTest #24/26 Passed 1.51 sec)
```

`WANT_SESSION_VIEW` is a real option in this tree (`CMakeLists.txt:110`) — grepped before use.

## 4. Build and test — `WANT_SESSION_VIEW=OFF`

Same build directory, **explicit reconfigure** (a second build dir was not created — ~25 GB is shared
across lanes):

```
cmake -S . -B build … -DWANT_SESSION_VIEW=OFF   → exit 0 (/tmp/kos_cfg_off.log)
  → build/CMakeCache.txt `WANT_SESSION_VIEW:BOOL=OFF`; build/lmmsconfig.h:42 `/* #undef LMMS_HAVE_SESSION_VIEW */`
cmake --build build -j6 > build.log 2>&1        → 577 translation units rebuilt; every target `Built`
cd build/tests && ctest --output-on-failure > ctest.log 2>&1; echo CTEST_EXIT=$?
  → CTEST_EXIT=0   ·   100% tests passed, 0 tests failed out of 25   ·   Total Test time (real) = 38.51 sec
    (log = build/tests/ctest-off.log; `grep -c SessionModelTest CTestTestfile.cmake` = 0)
```

Build-log caveat, stated plainly: the OFF build was launched as a background wrapper that appends its
own `BUILD_EXIT=` line; it was **SIGTERMed (exit 143) after reaching 100 %** and before writing that line
(`build.log` carries the note). Verification instead: the immediate re-run `cmake --build build -j6`
did **zero** compile/link steps and exited **0**, proving the tree was complete and consistent.

Flag-OFF proof beyond test registration: `nm -C build/lmms | grep -c -i 'SessionModel'` = **0**
(sanity check: the same command finds `Song::saveProjectFile`), i.e. the OFF binary contains no session
code at all. The OFF build dir still holds stale ON-phase artifacts (`build/src/CMakeFiles/lmmsobjs.dir/core/SessionModel.cpp.o`,
`build/tests/SessionModelTest`) — CMake removes targets, not old object files; they are not linked and not registered.

## 5. Test-count reconciliation (26/25 vs the recorded 31/30)

The recorded baseline was measured on an older base (`48fed8644`) with different configure flags:
`-DWANT_STEM_SPLIT=ON -DLMMS_CLAP_PATH=<vendored clap>` (`lanes/594-EVIDENCE.md` §4). Name-level diff:

| Contribution | Old baseline (48fed8644) | This run (0c23587d2, prescribed flags) |
|---|---|---|
| Base `LMMS_TESTS` list | 22 | 23 (`+ RemotePluginClientE2ETest`) |
| `Stem*` tests (`LMMS_HAVE_STEM_SPLIT`) | +3 | 0 (`WANT_STEM_SPLIT=OFF`, undefined in `lmmsconfig.h`) |
| `Clap*` tests (`if(TARGET lmms_clap)`, `tests/CMakeLists.txt:491`) | +3 | 0 (no CLAP path passed) |
| `PluginPortsMigrationTest`, `AudioPluginTest` | +2 | +2 |
| `SessionModelTest` (ON only) | +1 | +1 |
| **Total** | **31 ON / 30 OFF** | **26 ON / 25 OFF** |

So the ON−OFF delta the PR introduces is exactly 1 in both configurations: the count difference is
configure scope + main's evolution, **not rebase loss** — every test the patch adds or touches is
present and green. Independent cross-check at the identical base commit: sibling lane
`zene-pa-gatedebt` (commit `0c23587d2`, no session view) registers exactly those **25** names; its
siblings show the same 25 plus their own lane test (`zene-pa-lufs` 26, `zene-pa-scan` 26).

## 6. Repo gates (read-only scripts, run from the worktree)

| Gate | Result |
|---|---|
| `tests/no-upstream-regression-gate.sh` | **PASS, exit 0** — 34 declared files; `include/Song.h`, `src/core/Song.cpp`, `src/lmmsconfig.h.in` all honor the change |
| `tests/no-tautology-gate.sh` | PASS, exit 0 |
| `tests/file-length-gate.sh` | PASS, exit 0 (new files 118–331 lines, under the 500 limit) |
| `tests/complexity-gate.sh` | PASS, exit 0 |
| `tests/duplication-gate.sh` | PASS, exit 0 (104 fork sources, 1.16 % duplicated) |

`complexity-gate.sh` rewrites `tests/complexity-baseline.tsv` as a side effect of its ratchet; that
rewrite was **reverted** (`git checkout --`) and is not part of the branch.

## 7. Dropped from the original patch

**Nothing.** 15/15 files, 1677/1677 inserted lines, 0 deletions, identical to `e317f1062`.
Two observations, both follow-ups rather than losses:

* The new sources are registered in `tests/fork-sources.txt` (fork scope, which the gates above scan)
  but **not** in `tests/all-sources.txt`, the whole-tree inventory that postdates the PR. Consequence:
  a gate run with `--scope all` skips them. Whether to add them (and to the `*-baseline-all.tsv` files)
  is the parent's call; no gate failed without it.
* Pre-existing, unchanged from the original evidence: `.mpt` templates are not covered by a round-trip
  test, and nothing consumes launch/quantisation/follow-action fields yet (data layer only).

## 8. Push command (exact — **not run**)

```sh
git -C /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-pr594 push product post-alpha/pr594:refs/heads/post-alpha/pr594
```

* `product` = `https://github.com/KRUZZZZY/zene-studio.git`. It does not touch `origin` (LMMS/lmms)
  or `messmerd`, and it does not move `feat/session-view-model` (the PR #5 branch stays as it is).
* Updating PR #5 itself would mean a force-push onto `feat/session-view-model`; that is deliberately
  **not** in this command.
* If the parent wants PR #5 to point at this branch, do the remote-side branch move deliberately, and
  rebase onto `product/main` first if a `main` push is also intended (`product/main` is protected).

## 9. Not proven / residual risk

* The old-flags comparison (31/30) was **reconciled name-by-name, not re-measured** — I did not rebuild
  with `WANT_STEM_SPLIT=ON` + a real CLAP path.
* The ON-phase `build/lmms` binary was replaced by the OFF rebuild, so the ON symbol check is
  behavioural (26/26 ctest incl. `SessionModelTest`) rather than a `nm` on the ON binary.
* The build directory is left in the **OFF** state (targets, cache and `lmmsconfig.h`); the ON logs live
  in `build-on.log`, `build/tests/ctest-on.log`.
* No engine, clock domain or UI exists (by scope) — unit/round-trip evidence only.

Logs: `build.log` (OFF build + the SIGTERM note), `build-on.log` (ON build, ends `BUILD_EXIT=0`),
`build/tests/ctest.log` = `ctest-off.log`, `build/tests/ctest-on.log`, `/tmp/kos_cfg_{on,off}.log`.
