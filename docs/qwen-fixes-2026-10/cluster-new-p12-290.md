FIXED

# Cluster new-p12-290: the editor frame crashes on a null query-interface output pointer

## Finding and verdict

Luna reported `plugins/Vst3Effect/Vst3EditorSession.cpp:124-140`: `HostPlugFrame::queryInterface()`
stores through `obj` without checking it, so a faulty VST3 editor that calls `queryInterface(iid, nullptr)`
on its frame crashes the host instead of getting `kInvalidArgument`.

The finding's line 124 has moved. On this branch the function is at
`plugins/Vst3Effect/Vst3EditorSession.cpp:138-155`. Before the fix it wrote `*obj` at lines 142, 146 and 150
with no null check. The verdict is FIXED, and the evidence for it is below.

## Evidence

- **The defect (before the fix):** `plugins/Vst3Effect/Vst3EditorSession.cpp:138-141` checked only the IID,
  then stored through `obj` at 142, 146 and 150. A null `obj` therefore dereferences null.
- **The contract:** the sibling `plugins/Vst3Effect/Vst3ComponentHandler.h:115-117` begins with
  `if (obj == nullptr) { return kInvalidArgument; }` and is covered by
  `tests/src/plugins/Vst3HostTest.cpp` `testComponentHandlerAnswersInterfaceQueries`
  (the case `queryInterface(IComponentHandler::iid, nullptr) == kInvalidArgument`).
  The frame is the only product `queryInterface` that lacked the guard. Repo-wide search:
  `rg -n "queryInterface\(" plugins src include tests` (the remaining hits are the caller side, the
  test fakes, and the vendored Carla SDK).
- **Reachability:** the editor view receives the frame through `IPlugView::setFrame()` and calls
  `queryInterface()` on it. That is a plug-in calling into the host, so the null store is a
  plug-in-triggered host crash.
- **The fix:** `plugins/Vst3Effect/Vst3EditorSession.cpp:140` adds
  `if (obj == nullptr) { return kInvalidArgument; }`, the same form as the sibling. No legitimate caller
  passes null: the product code never calls `queryInterface` on the frame (its queries are on the view at
  `Vst3EditorSession.cpp:249` and on the component at `Vst3Host.cpp:250`). Valid queries, and
  `kNoInterface` for unknown IIDs, behave exactly as before.
- **Scope:** main-thread editor path only. No audio thread, no allocation or lock, no undo, no control
  command. The fix is one line.
- **Not a sibling in other places:** the CLAP host has no frame-style `queryInterface` in product code (repo
  search above).

## The regression test

`tests/src/plugins/Vst3HostTest.cpp` - `Vst3HostTest::testEditorFrameRefusesANullOutPointer`, next to the
component-handler sibling. It uses two in-test fakes (`FrameProbeView`, `FrameProbeController`) and the
session's public API (`Vst3EditorSession::open/close`, `setWindowSystem(X11)`). It needs no real plug-in and
no timing, and it does not expose `HostPlugFrame`. It checks:

1. `frame->queryInterface(IPlugFrame::iid, nullptr)` and `queryInterface(FUnknown::iid, nullptr)` both return `kInvalidArgument`.
2. A valid `queryInterface(IPlugFrame::iid, &queried)` returns `kResultOk`, yields the frame, and the reference
   it takes is released by the caller.
3. An unknown IID (`IPlugView::iid`) returns `kNoInterface` and leaves `queried == nullptr`.

Matrix covered: normal path (1-3), null out-pointer (1, the bug), reconfiguration (the session is opened and
closed, which runs the frame's drop/release path), audio-thread constraints (none: main-thread test),
platform (the test uses `PluginWindowSystem::X11` explicitly, so it runs on every platform that compiles
`Vst3HostTest`; it does not depend on the window system of the running machine).

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-290.r1/logs/`. Each build was run
from the worktree root and each ctest from `build/tests`.

| # | Step | Command | EXIT | Log |
|---|------|---------|------|-----|
| 1 | Build, first try | `cmake --build build --target Vst3HostTest -j4` | 2 | `build-1.log` (environment, not code: ccache could not create its temp file in the sandbox `dsh` directory; "Permission denied") |
| 2 | Build with the ccache temp dir moved to `$TMPDIR` (environment only, no config change) | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp cmake --build build --target Vst3HostTest -j4` | 0 | `build-2.log` |
| 3 | **RED** (fix absent) | `ctest -R Vst3HostTest --output-on-failure` | **8** | `test-1.log`: `Received signal 11 (SIGSEGV) ... accessing address 0x0` in `lmms::vst3::HostPlugFrame::queryInterface`, called from `Vst3HostTest::testEditorFrameRefusesANullOutPointer`; "1 tests failed (SEGFAULT)" |
| 4 | Fix applied; rebuild | `CCACHE_TEMPDIR=... cmake --build build --target Vst3HostTest -j4` | 0 | `build-3.log` |
| 5 | **GREEN** | `ctest -R Vst3HostTest --output-on-failure` | **0** | `test-2.log`: 100% passed |
| 6 | **4b: revert the product fix** | `git diff -- plugins/Vst3Effect/Vst3EditorSession.cpp > logs/fix.patch`; `git apply -R logs/fix.patch`; rebuild; `ctest -R Vst3HostTest -V` | 0; 0; 0; **8** | `fix.patch`, `build-4.log`, `test-3.log`: SIGSEGV again in `HostPlugFrame::queryInterface`, test FAILED |
| 7 | **4b: reapply the fix** | `git apply logs/fix.patch`; rebuild; `ctest -R Vst3HostTest -V` | 0; 0; **0** | `build-5.log`, `test-4.log`: `PASS : testEditorFrameRefusesANullOutPointer()`; `Totals: 23 passed, 0 failed` |
| 8 | Product and editor-family targets rebuilt | `CCACHE_TEMPDIR=... cmake --build build --target zene vst3effect Vst3EditorTest -j4` | 0 | `build-6.log` |
| 9 | Editor family (other targets that compile `Vst3EditorSession.cpp`) | `ctest -R Vst3EditorTest -V` | 0 | `test-5.log`: `Totals: 7 passed, 0 failed` |

**Affected families:** `Vst3HostTest` = 1 ctest test, 23 QTest functions, 23 passed (step 7).
`Vst3EditorTest` = 1 ctest test, 7 functions, 7 passed (step 9). The RED run (step 3) and the reverted
run (step 6) both failed with the same crash, so the regression test is proven to catch the bug.

## Gates

`feature_check.py` could not run to completion from this sandbox:

| # | Command | EXIT | Log |
|---|---------|------|-----|
| 10 | `python3 .../qwen/feature_check.py <worktree>` | 1 | `gates-1.log`: `PermissionError` creating `<worktree>-feature-check` beside the worktree (outside the write-jail). The script stopped before running any gate. |

To cover the same ground I ran the six simple gate scripts it lists, each directly in the worktree and
unmodified, plus the two ratchets:

| # | Command | EXIT | Log |
|---|---------|------|-----|
| 11 | `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources.log` |
| 12 | `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-reproduce.log` |
| 13 | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-regression.log` |
| 14 | `tests/scripted/check-namespace` | 0 | `gate-check-namespace.log` |
| 15 | `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology.log` |
| 16 | `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures.log` |
| 17 | `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity.log` (red on the train by design; 6 items) |
| 18 | `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length.log` (red on the train by design; 23 lines) |

The ratchets exit 1, which the brief says is the train's state. Neither log has a `REGRESSION` line for
`Vst3EditorSession.cpp` or `Vst3HostTest.cpp`: the complexity items are in other files, and the file-length
items name other files. The baseline comparison that `feature_check.py` does (`base_listings`) needs a
base checkout inside `zene-cachyos` and a cache outside the write-jail, so I did not run it. The "no new
item in our files" verdict for the ratchets rests on reading those logs, not on the script's own comparison.

## Files committed

- `plugins/Vst3Effect/Vst3EditorSession.cpp` - the null guard in `HostPlugFrame::queryInterface`.
- `tests/src/plugins/Vst3HostTest.cpp` - the regression test and its two fakes.
- `tests/upstream-modifications.txt` - a reason for the inherited test file's new cluster.
- `docs/qwen-fixes-2026-10/cluster-new-p12-290.md` - this report.

Nothing else was changed. `git status` was clean apart from these before the commit.

## Notes

- The finding said "No existing test includes this file's header". That is out of date: `Vst3HostTest` already
  compiles `Vst3EditorSession.cpp`, and `Vst3EditorTest` exercises the editor through a real fixture. Neither
  called the frame with a null out-pointer.
- No new control commands, no `tests/fork-sources.txt` change (the product file is already registered).

## Self-check

Verdict is FIXED, so no ALREADY-FIXED or FALSE-POSITIVE evidence is needed.

- **The regression test fails with the change reverted and passes with it.** RED: step 3, `ctest -R Vst3HostTest`
  EXIT 8, `test-1.log` (SIGSEGV in `HostPlugFrame::queryInterface`). Reverted again under 4b: step 6, EXIT 8,
  `test-3.log`. GREEN: step 5, EXIT 0, `test-2.log`; step 7, EXIT 0, `test-4.log` (`PASS` for the test, 23 passed).
- **Deterministic.** The test has no timing, sleeps, threads or races. The seam is the in-test
  `FrameProbeView`, which keeps the frame that `setFrame()` gives it, and `FrameProbeController`, which returns
  that view from `createView()`. The window system is injected with `setWindowSystem(PluginWindowSystem::X11)`
  instead of read from the running platform. The parent is the stand-in value `reinterpret_cast<void*>(1)`,
  which the fake view never dereferences.
- **At the cause and complete.** The guard sits before both branches of `queryInterface`, so the IPlugFrame,
  FUnknown and unknown-IID paths all get it. The sibling `Vst3ComponentHandler::queryInterface` already has it
  (`plugins/Vst3Effect/Vst3ComponentHandler.h:116`). The repo search found no other frame-style
  `queryInterface` in product code (`plugins`, `src`, `include`). Product code never queries the frame; its
  queries are on the view (`Vst3EditorSession.cpp:249`) and on the component (`Vst3Host.cpp:250`).
- **No collateral damage.** The only behaviour change is that a null `obj` now returns `kInvalidArgument` instead
  of crashing. Valid queries still return the frame with a reference taken, and unknown IIDs still return
  `kNoInterface` with a null output (both asserted in the test). No limit or check was added, so none can hit
  a caller that legitimately exceeds it. No project, save or saved-data path is touched. The editor family
  (`Vst3EditorTest`, 7 passed, step 9) still passes.
- **Gates: still FAIL in this sandbox.** `feature_check.py` exits 1 with a `PermissionError` when it creates
  `<worktree>-feature-check` beside the worktree, which is outside the write-jail. The six simple gate scripts
  pass when run directly (steps 11-16), and the two ratchets exit 1 with no reported items in the changed files
  (steps 17-18). The ratchets' base comparison did not run. Someone who can write beside the worktree should run
  `feature_check.py` once to confirm the table.
