ALREADY-FIXED: the MultiTrackRecorder constructor clamps the route count to `MaxRoutes` (64) on this branch, so `MultiTrackRecorder(65)` builds 64 routes. The finding's "never enforces MaxRoutes" is stale.

# Cluster 4294 — Multi-track recorder accepts route counts above its declared cap

## Finding
`src/core/audio/MultiTrackRecorder.cpp:40` (historical line; the report says the constructor "never enforces `MaxRoutes`").

## Evidence (current tree, HEAD `5c1c6715d9`)

- **The clamp is present:** `src/core/audio/MultiTrackRecorder.cpp:40`
  `const auto routes = std::clamp(routeCapacity, 1, MaxRoutes);  // the documented bound (RecorderTaps' table)`
  Introduced by commit `57980beef4` (2026-10-04, "0.5.0 round 6"). `git merge-base --is-ancestor 57980beef4 HEAD` → ancestor (exit 0). `git blame` on lines 40 attributes the clamp to that commit.
- **The cap is declared:** `include/MultiTrackRecorder.h:90` `static constexpr int MaxRoutes = 64;` and `:92-93` the constructor contract "routes (bounded to 1..MaxRoutes)".
- **Only the constructor sizes the route list:** `src/core/audio/MultiTrackRecorder.cpp:41-44` (`reserve` + `push_back`) are the only growth sites for `m_tracks`. `rg -n "push_back|emplace|resize|reserve|m_tracks"` on the file shows no other growth site. `include/MultiTrackRecorder.h:136` declares `m_tracks` private.
- **Production callers pass the cap, not more:** `src/core/AudioEngine.cpp:491` `m_recorder(MultiTrackRecorder::MaxRoutes, ...)`.
- **The route table is bounded by the same constant:** `src/core/RecorderTaps.cpp:60-62` (`std::array<Entry, MaxRoutes>`), with index guards at `:90`, `:103`, `:109`.

## Runtime evidence
A temporary probe (`ProbeRouteCapClamp` in `tests/src/core/MultiTrackRecorderTest.cpp`, not committed, reverted afterwards) was run against the unmodified HEAD:

- `MultiTrackRecorder over(65)` → `trackCount() == MaxRoutes` (64)
- `MultiTrackRecorder huge(1000000)` → `trackCount() == MaxRoutes` (64)
- `MultiTrackRecorder exact(MaxRoutes)` → 64
- `MultiTrackRecorder zero(0)` → `trackCount() == 1`

Result: PASS (`test-3.log`, "11 passed, 0 failed"). The probe passes because the clamp is already on HEAD, so it is evidence of the current behaviour, not a RED/GREEN regression test. No product or test file is changed by this lane.

## Why no regression test or product change
Procedure step 2 says a not-real finding goes straight to the report. The behaviour the finding asks for (over-cap counts are clamped to `MaxRoutes`) is in place, and the existing family covers the constructor path. Adding a test that passes on HEAD would not be a regression proof, so none was committed.

## Commands run and exit codes (LOG FOLDER: `~/.local/share/zene-bunny-fix/qwen/runs/4294.r1/logs`)

| # | Command | EXIT | Log |
|---|---|---|---|
| 1 | `ctest -R '^(MultiTrackRecorderTest\|ControlRecordingRoutesTest\|InputChannelMeterTest)$'` from `build/tests` (binaries not yet built) | 8 (all "Not Run") | `test-1.log` |
| 2 | `cmake --build build --target zene MultiTrackRecorderTest ControlRecordingRoutesTest InputChannelMeterTest -j4` | 2 (ccache could not write its temp file under the shared `ccache.conf` `temporary_dir`) | `build-1.log` |
| 3 | Same build with `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-bunny/tmp-4294` (env override; the shared config was not edited) | 0 | `build-2.log` |
| 4 | `ctest -R '^(MultiTrackRecorderTest\|ControlRecordingRoutesTest\|InputChannelMeterTest)$'` from `build/tests` | 0 (3/3 passed: ControlRecordingRoutesTest, MultiTrackRecorderTest, InputChannelMeterTest) | `test-2.log` |
| 5 | Probe build: `cmake --build build --target MultiTrackRecorderTest -j4` (with probe) | 0 | `build-3.log` |
| 6 | `ctest -R '^MultiTrackRecorderTest$' -V` from `build/tests` (with probe) | 0 (11 passed, including ProbeRouteCapClamp) | `test-3.log` |
| 7 | `python3 .../qwen/feature_check.py <worktree>` | 1 — it aborted before running any gate: it creates its log folder at `<worktree>/../zene-qwen-4294-feature-check`, which is outside the write-jail (PermissionError). Not a gate result. | (stdout only) |
| 8 | The same gate commands run directly from the worktree (`bash tests/fork-sources-gate.sh`, `bash tests/all-sources-reproduce.sh`, `bash tests/no-upstream-regression-gate.sh`, `tests/scripted/check-namespace`, `bash tests/no-tautology-gate.sh`, `python3 tests/doc-figures-gate.py`, `bash tests/complexity-gate.sh --check`, `bash tests/file-length-gate.sh --check`) | fork-sources 0, all-sources-reproduce 0, no-upstream-regression 0, check-namespace 0, no-tautology 0, doc-figures 0, complexity 1, file-length 1 | `gate-*.log` |

Gate table (feature_check rules applied by hand):

| Gate | Raw exit | Result |
|---|---|---|
| fork-sources | 0 | PASS |
| all-sources-reproduce | 0 | PASS |
| no-upstream-regression | 0 | PASS |
| check-namespace | 0 | PASS |
| no-tautology | 0 | PASS |
| doc-figures | 0 | PASS |
| complexity (ratchet) | 1 | PASS by rule: the branch's changed set is empty (`merge-base HEAD 050/m0-train-19` = HEAD, so `git diff --name-only` is empty), so none of the 6 listed `REGRESSION` items is in a changed file (`gate-complexity.log`) |
| file-length (ratchet) | 1 | PASS by rule: same empty changed set; none of the 22 listed items is in a changed file (`gate-file-length.log`) |

The two ratchet exits are the expected red state on the train. They are not caused by this lane; nothing in this lane changes a file.

## Self-check

`submit_check.py` output (`submit-1.log`, EXIT=1) before this section and the commit existed: FAIL commits on the branch (nothing since base), FAIL working tree clean (this report untracked), FAIL gates (feature_check.py), PASS report, PASS tests (claim-only closure).

- **Regression test fails with the change reverted and passes with it (exit codes):** Not applicable: this verdict has no product change to revert. The over-cap probe passes on HEAD (`test-3.log`, exit 0, 11 passed), which is the expected state for an already-fixed behaviour. No RED/GREEN pair exists, so none is claimed. The probe is not committed.
- **Deterministic:** The probe uses only constructor arguments (65, 1000000, MaxRoutes, 0). No timing, sleeps, threads or races. It was reverted and is not in the commit.
- **Fix at the cause, all callers checked:** No fix. Callers checked: `src/core/AudioEngine.cpp:491` (passes `MaxRoutes`), `src/core/AudioInputPath.cpp:153-160` (`recordRouteCapacity()` returns `MaxRoutes`), the default `NumTracks = 2` (`include/MultiTrackRecorder.h:76`). `m_tracks` grows only in the constructor (`src/core/audio/MultiTrackRecorder.cpp:41-44`).
- **No collateral damage:** Nothing changed. The existing clamp does not reject any legitimate caller: every production caller passes `MaxRoutes` or `NumTracks` (2).
- **ALREADY-FIXED evidence (file:line / contract):** `src/core/audio/MultiTrackRecorder.cpp:40` clamps to `[1, MaxRoutes]`; `include/MultiTrackRecorder.h:90,92-93` declares the cap and the "bounded to 1..MaxRoutes" contract; the clamp came in with `57980beef4` and is an ancestor of HEAD.
- **Gates item (FAIL in submit_check):** `feature_check.py` cannot run in this lane: it writes logs to `<worktree>/../zene-qwen-4294-feature-check`, outside the write-jail (PermissionError, see command 7). The gate commands were run directly; six pass and the two ratchets are red by rule, with no item in a changed file (table above). This FAIL is environmental, not a gate result, and I did not work around the jail.
