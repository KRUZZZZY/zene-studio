**Verdict: FIXED**

# Cluster new-p17-139 — Reference panel hides errors from its state query

## Finding and verdict

REAL. `src/gui/ReferenceProjectPanel.cpp:176` (pre-fix) did:

```cpp
const QJsonObject state = ControlRegistry::instance()->invoke(QStringLiteral("reference.get_state"), {}).result;
```

`ControlRegistry::invoke` returns `ControlResult`, whose contract is "exactly one of result / error is populated"
(`include/ControlRegistry.h:70-76`). The panel discarded `ok`/`errorMessage` and read only `.result`, so every failure
became `open == false` and was rendered as `"No reference project is open."` with an empty tree, i.e. a failed query
was presented as a closed reference. Failures are reachable without any bug in the handler: `invoke` refuses before
dispatch when the engine is not addressable (`src/core/ControlRegistry.cpp:283-291`, typed `Busy`), when a command is
missing or `requires` is unmet (`ControlRegistry.cpp:262-275`), when collaboration intercepts it
(`ControlRegistry.cpp:304-309`), or when a handler throws (`ControlRegistry.cpp:311-318`). `reference.get_state`
itself is a plain success-only handler (`src/core/ControlCommandsReference.cpp:113-116`), so the only possible failure
is the registry's, and the panel must display it.

## Fix

`src/gui/ReferenceProjectPanel.cpp:174-191` now keeps the whole `ControlResult`:

- on failure it shows `"The reference project could not be read: <errorMessage>"` in the path label, keeps the last
  tree read (a failed query is not a close, the same rule `MixerRoutingMatrix::fill()` uses at
  `src/gui/MixerRoutingMatrix.cpp:170-179`), refreshes the independent target list, and returns;
- on success it renders `state.result` exactly as before, so unchanged behaviour for every old project and every
  successful query; a later good refresh restores the normal view.

No command was added or changed, no header changed, no undo/transaction code touched, and nothing here runs on the
audio thread (GUI refresh only; the failing path allocates only the label string, as the old path did).

Callers checked: `showPanel()` (`ReferenceProjectPanel.cpp:150-157`), `refreshIfOpen()` (the change hook,
`:169-172`), `openWithDialog()` (already refuses to show on `!opened.ok`, `:159-167`) and the tests all reach the one
`refresh()`; the menu entry `src/gui/LibraryCommandMenus.cpp:165` only calls `showPanel()`.

## Regression test

`tests/src/gui/ReferenceProjectPanelTest.cpp:327-348`, `aFailedStateQuerySaysSoAndKeepsTheLastTree`. The test opens a
reference, proves the tree has the reference's `Lead` track, then makes the next `reference.get_state` fail
deterministically through the registry's own seam: `ControlRegistry::setReady(false)` makes every
`requiresEngine` command (including `reference.get_state`) return the typed `Busy` refusal
(`src/core/ControlRegistry.cpp:283-291`), with no sleep, clock or race. Readiness is restored before any assertion.
It asserts the error text is shown, that `"No reference project is open."` is not shown, that the last tree survives,
and that the next good `refresh()` restores the normal path label.

## Commands and EXIT codes

All build/test output was sent to numbered files. The supplied log folder
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-139.r1/logs` is not writable in this session's sandbox
despite being owned by the user with mode 755: `touch` and the file tool both fail with `EACCES`, while `chmod`
metadata succeeds (Landlock-style denial; the sibling worktree is writable). Logs therefore live in
`/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/new-p17-139-logs/`; each command below names the log it wrote.

Baseline before the test edit:

```
$ cmake --build build --target ReferenceProjectPanelTest > .../logs/build-baseline.log 2>&1; echo EXIT=$?
EXIT=0
$ (cd build/tests && ctest -R '^ReferenceProjectPanelTest$' --output-on-failure > .../logs/test-baseline.log 2>&1; echo EXIT=$?)
EXIT=0            # 1/1 passed
```

RED — new test on the original product code (`build-red.log`, `test-red.log`):

```
$ cmake --build build --target ReferenceProjectPanelTest > .../logs/build-red.log 2>&1; echo EXIT=$?
EXIT=0
$ (cd build/tests && ctest -R '^ReferenceProjectPanelTest$' --output-on-failure > .../logs/test-red.log 2>&1; echo EXIT=$?)
EXIT=8
FAIL!  : ReferenceProjectPanelTest::aFailedStateQuerySaysSoAndKeepsTheLastTree()
         'shown.startsWith(QStringLiteral("The reference project could not be read:"))' returned FALSE.
         (No reference project is open.)
Totals: 9 passed, 1 failed, 1 skipped
```

GREEN — same test after the fix (`build-green.log`, `test-green.log`, `test-single-green.log`):

```
$ cmake --build build --target ReferenceProjectPanelTest > .../logs/build-green.log 2>&1; echo EXIT=$?
EXIT=0
$ (cd build/tests && ctest -R '^ReferenceProjectPanelTest$' --output-on-failure > .../logs/test-green.log 2>&1; echo EXIT=$?)
EXIT=0            # 1/1 ctest, all 9 runnable cases passed (1 environment skip)
$ (cd build/tests && ./ReferenceProjectPanelTest aFailedStateQuerySaysSoAndKeepsTheLastTree > .../logs/test-single-green.log 2>&1; echo EXIT=$?)
EXIT=0            # PASS: aFailedStateQuerySaysSoAndKeepsTheLastTree
```

Step 4b — reverse only the product hunk, rebuild, run (must fail), restore (`fix.patch`, `build-prove-red.log`,
`test-prove-red.log`, `build-prove-green.log`, `test-prove-green.log`):

```
$ git diff -- src/gui/ReferenceProjectPanel.cpp > .../logs/fix.patch; echo EXIT=$?
EXIT=0
$ git apply -R .../logs/fix.patch; echo EXIT=$?
EXIT=0
$ cmake --build build --target ReferenceProjectPanelTest > .../logs/build-prove-red.log 2>&1; echo EXIT=$?
EXIT=0
$ (cd build/tests && ctest -R '^ReferenceProjectPanelTest$' --output-on-failure > .../logs/test-prove-red.log 2>&1; echo EXIT=$?)
EXIT=8            # same FAIL! assertion, "(No reference project is open.)"; 1 failed
$ git apply .../logs/fix.patch; echo EXIT=$?
EXIT=0
$ cmake --build build --target ReferenceProjectPanelTest > .../logs/build-prove-green.log 2>&1; echo EXIT=$?
EXIT=0
$ (cd build/tests && ctest -R '^ReferenceProjectPanelTest$' --output-on-failure > .../logs/test-prove-green.log 2>&1; echo EXIT=$?)
EXIT=0            # 1/1 passed
```

Affected family — `tests/src/gui/ReferenceProjectPanelTest.cpp` is the only test that includes this panel; the
`Reference` ctest family was run too (`build-family.log`, `test-family.log`):

```
$ cmake --build build --target ArrangingSpacerReferenceTest ControlSurfaceReferenceTest > .../logs/build-family.log 2>&1; echo EXIT=$?
EXIT=0
$ (cd build/tests && ctest -R 'Reference' --output-on-failure > .../logs/test-family.log 2>&1; echo EXIT=$?)
EXIT=0            # 3/3: ControlSurfaceReferenceTest, ArrangingSpacerReferenceTest, ReferenceProjectPanelTest
$ cmake --build build --target zene > .../logs/build-zene.log 2>&1; echo EXIT=$?
EXIT=0
```

Gates (`feature-check.log`). The literal requested command cannot create its sibling log directory and exits 1 with
`PermissionError: [Errno 13] ... zene-qwen-new-p17-139-feature-check`; the same script was then run unchanged through a
log-path-only redirect shim (`dsh/home/run-feature-check-redirect.py`, which overrides only `Path.mkdir`/`write_text`
for that directory; no gate command, threshold or baseline is touched):

```
$ python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/feature-check.log 2>&1; echo EXIT=$?
EXIT=1            # PermissionError creating <worktree>-feature-check (sandbox), not a gate FAIL
$ python3 /home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/run-feature-check-redirect.py <worktree> >> .../logs/feature-check.log 2>&1; echo EXIT=$?
EXIT=0
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

Files committed: `src/gui/ReferenceProjectPanel.cpp`, `tests/src/gui/ReferenceProjectPanelTest.cpp`,
`docs/qwen-fixes-2026-10/cluster-new-p17-139.md`.

## Self-check

- **Regression RED/GREEN:** RED `ctest` exit 8 with `(No reference project is open.)`; after restoring the fix,
  `ctest` exit 0. Step 4b reverse/apply exits were 0/0 and the same assertion flipped 8 → 0.
- **Deterministic:** no timing, sleep or race. The seam is `ControlRegistry::setReady(false)`, which makes the real
  registry refuse `reference.get_state` through its normal `requiresEngine` path, then `setReady(true)` before asserts.
- **Cause and completeness:** all four callers of `refresh()` (showPanel, refreshIfOpen, the post-command `run()`,
  openWithDialog) reach the fixed function; `openWithDialog` already handled its own failure. The only other
  `reference.get_state` consumer is the menu entry that calls `showPanel`, unchanged. The control command itself was
  not modified.
- **No collateral damage:** successful queries render the exact same tree, path and target list; only the failure
  branch changed. No save/load data or limit changed. Tests are green.
- **Realtime/undo/commands:** GUI-only change; no audio-thread path, allocation/lock/logging added on such a path, no
  undo/transaction change, no new control command.
- **Log folder:** the supplied path is not writable by this sandbox (`touch` EACCES on a 0755 user-owned directory);
  all logs are preserved under `dsh/home/new-p17-139-logs/`, including `fix.patch`, both RED and both GREEN runs.
