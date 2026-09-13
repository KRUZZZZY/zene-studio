# Integration run: 0.2.1 wave-2 merge train

Branch `integrate/0.2.1-wave2`, base `ec07dd3a0` (= `main` = tag `v0.2.1-alpha`), worktree
`zene-pa-integrate2`. Unpushed, untagged.

Merge tip (four merges): `d2b3f5b94`. Gate-6 adjudication: `93b4d4961`. The evidence commits
follow those two; `git -C lmms log --oneline integrate/0.2.1-wave2` names the tip on disk, which
is the commit that carries this report (plus the final-tip gate re-run after it).

## Merge order, one commit each

| # | lane branch | tip | merge commit |
|---|---|---|---|
| 1 | `fix/automation-touch-race` | `1950e266c` | `078feeb08` |
| 2 | `fix/control-shutdown` | `7c557ef1d` | `4ae96c1e2` |
| 3 | `fix/control-arm64-cluster` | `762bc2c57` | `2dedc34dd` |
| 4 | `fix/vst3-macos-msvc-tests` | `e54e88545` | `d2b3f5b94` |

## Conflicts, measured rather than assumed

* `tests/upstream-modifications.txt` (edited by lanes 1 and 2) did **not** conflict: lane 1's
  hunks fall on the `Song.h` / `AutomatableModel.cpp` / `Song.cpp` entries and lane 2's on the
  `src/gui/MainWindow.cpp` entry — different entries, so both lanes' reason text auto-merged
  into their own lines. Verified by content on the merged file, not by the absence of a
  conflict: `grep -c automation-touch-race` = 3, lane 2's `auto-save recovery: a clean quit
  clears recover.mmp` = 1.
* `tests/control-shutdown.py` **was** edited by two lanes (lane 2 and lane 3) — the brief's
  premise that they touched different `control-*.py` files does not hold. It is the only
  conflict (3 regions, `tests/control-shutdown.py:31,46,77`) and is resolved in
  `4ae96c1e2..2dedc34dd` to lane 2's side of all three, with the import union where the two
  compose (lane 2's `PING_TIMEOUT` line; lane 3's `STARTUP_BOUND`/`Blocked` are referenced only
  by the loop body that was not taken, so importing them would leave dead names).
  Decided by the merged file itself, not by preference: the call site — outside the conflict
  and so already merged — passes four arguments, and lane 3's own harness
  (`tests/control_socket_harness.py:428` `wait_ready`, `STARTUP_BOUND = READY_TIMEOUT` at
  `:67`) uses exactly lane 2's full-budget retry. Lane 3's `control-shutdown.py` hunk was the
  "one socket read decides" shape lane 3 had already replaced in its own harness.
* `tests/CMakeLists.txt` was touched by no lane (checked against all four tips) and
  did not conflict.

## Gate 6 failure found on the merged tip (lane-introduced, carried in unchanged)

`run-all-gates.sh` first returned **exit 1**: Gate 6 flagged nine lane-2 evidence files
(`docs/control-shutdown-logs/*.{txt,log}`) as *VIOLATION: undeclared change to
upstream-inherited code*. The gate allows `tests/**` whatever the file is and `*.md` anywhere,
but a non-`.md` file under `docs/` must be declared in the divergence ledger.

Measured as lane-introduced, not a merge artifact: lane 2's **own** worktree fails Gate 6 the
same way (`zene-fix-shutdown`, exit 1, the same 9 paths) while lanes 1, 3 and 4 exit 0. Fixed
by moving the evidence to `tests/control-shutdown-logs/` (bytes unchanged — the same home lane
3's and lane 4's evidence already use) and following the ledger's reference to the old path,
rather than adding nine ledger entries claiming an inherited change upstream never had. No
gate, test, ratchet or assertion was changed. The failing run's log is kept as
`run-all-gates-BEFORE-FIX-gate6-fail.log`.

## Verification on the merged/final tip — every exit code unpiped

| command | exit |
|---|---|
| `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DUSE_WERROR=ON -DWANT_QT6=ON -DWANT_VST3=ON -DWANT_CLAP=ON -DLMMS_VST3_SDK_PATH=<pinned> -DLMMS_CLAP_PATH=<pinned>` | 0 |
| `cmake --build build -j4` | 0 |
| `ctest --output-on-failure` from `build/tests` (final tip) | 0 — 92/92, 151.5 s |
| `bash tests/run-all-gates.sh` | 3 — 10/10 ran gates PASS, gate 2 (coverage) SKIP by design |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `bash tests/unregistered-tests-gate.sh` | 0 |
| `bash tests/fork-sources-gate.sh` | 0 |
| `python3 -c 'yaml.safe_load(.github/workflows/build.yml)'` | 0 — 6 jobs |
| `yamllint .github/workflows/build.yml` | 0 |
| lines over 120 chars in `.github/workflows/build.yml` | 0 |

`ctest` and `run-all-gates.sh` (which runs `ctest` itself) were never run concurrently.

## Product-hunk review (every auto-merged `src/` + `include/` hunk was read)

Eight product files changed, the only four lanes' sets, and no two lanes edited the same
function. Findings, named even where judged harmless:

1. `src/core/Song.cpp:161` (with `:109`) — `s_automationCacheSong` is one static slot set
   unconditionally in `Song::Song()` and nulled unconditionally in `Song::~Song()`. With two
   overlapping `Song` lifetimes the later constructor displaces the pointer and the earlier
   destructor nulls it, so a still-live song silently loses the hook and its last-frame cache
   can again name a destroyed model. Latent: nothing constructs a second song (the rigs all use
   `Engine::getSong()`), so no test or path exercises it today. An
   `if (s_automationCacheSong == this)` guard on both sides would close it.
2. `src/core/Song.cpp:175` — `m_oldAutomatedValues.remove(model)`, reached from
   `AutomatableModel::~AutomatableModel()` (`src/core/AutomatableModel.cpp:86`), can run on the
   GUI thread while `processAutomations()`/`stop()` walk and assign the same `QMap` on the
   render thread (`src/core/Song.cpp:517,525,807,812`). Nothing locks it (`m_tracksMutex` is the
   only mutex in the file). Lane 1's design, present in its own tree, not merge-introduced: it
   replaces a deterministic use-after-free with an unconditional cross-thread `QMap` mutation.
3. `src/gui/MainWindow.cpp:250` — `aboutToQuit` → `sessionCleanup` duplicates the
   `closeEvent` call at `:1459` on a window-close quit. Harmless: `sessionCleanup()` is
   `ProjectRecovery::removeRecovery()` + `setSession(Normal)`, both idempotent.
4. `include/ControlServer.h:71` — at the `MaxQueuedReplyBytes` cap the append returns false and
   `dispatchPendingLines` drops the client without the drain-read used on the over-cap
   request-line path, so a still-writing peer meets EPIPE instead of a quiet drain. Reachable
   only by a peer that stopped reading; not a merge artifact.

## Brief deltas worth knowing

Beyond the file lists given: lane 3 also touches `include/ControlServer.h`,
`src/core/ControlServerSocket.cpp`, `tests/control-absent-socket.py`,
`control-headless-no-audio-device.py`, `control-no-audio-device.py`, `control-readiness.py`;
lane 4 also touches `tests/src/core/PhaseDSidechainTest.cpp`. The workflow carries **six**
per-job "Crash evidence" steps, not three, and has no reference to `tests/evidence/`.

## unverified

CI only: the macOS/Windows rows (VST3 fixture entry point, MSVC scan-filter module names), the
linux-arm64 engine-start timing the control lanes fix, Qt5 (`WANT_QT6=OFF`) behaviour of the
`aboutToQuit` shutdown route, the whole-tree ratchet scope (`--whole-tree`) and the coverage
gate (skipped here via exit 3). Local build is Linux x86_64 Qt6 `RelWithDebInfo`.
