FALSE-POSITIVE

## Finding

`ControllerRackView::moveUp` / `moveDown` call `m_controllerViews.first()` / `.last()` without an emptiness guard, so the finding claims they are undefined behaviour when a stale signal arrives with no views present.

## Evidence and decision

The defect is not reachable on this branch. `moveUp` and `moveDown` only run when a live `ControllerView` that is a member of `m_controllerViews` emits, and the list is non-empty whenever that happens.

- **Only wiring into the slots.** `src/gui/ControllerRackView.cpp:172-173` connects `ControllerView::movedUp` / `movedDown` to `moveUp` / `moveDown`. Nothing else connects to them.
- **Only emitters.** `src/gui/ControllerView.cpp:117` and `:119` are the only emitters. Their callers are the two `QAction`s at `src/gui/ControllerRackView.cpp:187-197`, parented to the view, and the context menu at `src/gui/ControllerView.cpp:156-164`, a `CaptionMenu` parented to the view. Both are destroyed with the view.
- **Membership invariant.** `m_controllerViews` gains a view only at `src/gui/ControllerRackView.cpp:200` (`append` in `addController`). It loses one only at `:226-227` (`erase` in `removeController`), and `:229` deletes that view immediately. A grep of `m_controllerViews` across `src/` finds no other site (`ControllerRackView.cpp:145-227`, plus the deferred-removal check at `:183`).
- **Consequence.** A view that can emit `movedUp` or `movedDown` is always in the list. So `first()` and `last()` never see an empty vector, and `std::find` always returns a valid iterator, which means `std::next` never gets `end()`.
- **Deferred removal does not reach the slots.** The deferred path at `src/gui/ControllerRackView.cpp:178-185` calls `deleteController` (`:121-141`), which calls `song->removeController`. That emits `controllerRemoved`, and `removeController` erases and deletes the view synchronously. It never calls `moveUp` or `moveDown`.
- **The destructor is silent.** `ControllerView::~ControllerView` (`src/gui/ControllerView.cpp:90-96`) only deletes the sub-window and does not emit.

`first()` / `last()` on an empty `QVector` is undefined behaviour in release builds, where `Q_ASSERT` compiles out. The code does not reach that state today, though. A guard would be a reasonable hardening if the wiring ever changes to pass in a view that is not a member. It is not a bug fix for any reachable path, so I did not change product code and did not add a regression test.

## Regression test

None. The finding is not reachable, so there is no product change for a test to fail without. A test that calls `moveUp` on an empty rack would exercise undefined behaviour the wiring never produces, so writing one would test a scenario that cannot occur.

## Commands run and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-51.r1/logs`.

| Step | Command | EXIT |
| --- | --- | --- |
| Gate script, as the procedure specifies | `python3 .../qwen/feature_check.py <worktree>` | 1 (`feature-check-1.log`). It stopped before running any gate: it tries to create `<worktree>/../zene-qwen-new-p23-51-feature-check`, which is outside the write-jail (`PermissionError`). |
| fork-sources | `bash tests/fork-sources-gate.sh` (from worktree) | 0 (`gate-fork-sources-2.log`) |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 (`gate-all-sources-2.log`) |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 (`gate-no-upstream-2.log`) |
| check-namespace | `tests/scripted/check-namespace` | 0 (`gate-check-namespace-2.log`) |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 (`gate-no-tautology-2.log`) |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 (`gate-doc-figures-2.log`) |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (`gate-complexity-2.log`, 7 REGRESSION lines) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (`gate-file-length-2.log`, 22 REGRESSION lines) |

These are the same commands `feature_check.py` runs, executed from the worktree with logs in the LOG FOLDER. I could not run the script itself, so its comparison step against the base was not run.

The complexity and file-length REGRESSION lines name `WasmSandbox.cpp`, `Vst3Host.cpp`, `DawProjectReadTracks.cpp`, `ControlCommands*.cpp`, `TakeLane.cpp`, `XyPadPairTest.cpp` and various `tests/src/*` files. None is in `src/gui/ControllerRackView.cpp` or any file this branch changes. This commit adds only `docs/qwen-fixes-2026-10/cluster-new-p23-51.md`, and `git diff --name-only <merge-base>..HEAD` on the branch before this commit is empty. Under the script's rule, which fails a ratchet only on items in changed files, these rows would be PASS, but that is my inference from the diff, not a result the script reported.

No test was built or run for this cluster. Nothing was built and no product code changed.

## Self-check

Ran `submit_check.py <worktree> new-p23-51` (`submit-check-1.log`, EXIT=1). Results: PASS commits on the branch, PASS working tree clean, PASS report present, PASS tests (claim-only, see below). FAIL gates (feature_check.py), explained below.

- **The regression test fails with the product change reverted and passes with it.** Not applicable. The verdict is FALSE-POSITIVE, so there is no product change to revert and no test was added.
- **The test is deterministic.** Not applicable, for the same reason.
- **The fix is at the cause and complete.** Not applicable: no fix. Callers and siblings checked for the reachability claim: the two signal connections (`src/gui/ControllerRackView.cpp:172-173`), the emitters (`src/gui/ControllerView.cpp:117,119`), the only `m_controllerViews` mutations (`src/gui/ControllerRackView.cpp:200`, `:226-227`, `:229`), and the deferred-removal path (`:178-185`).
- **No collateral damage.** Not applicable. No product code changed, so saved data and existing behaviour are untouched.
- **FALSE-POSITIVE evidence.** The code citations are in the Evidence section above: `src/gui/ControllerRackView.cpp:172-173`, `:187-197`, `:200`, `:226-229`, `:178-185`; `src/gui/ControllerView.cpp:90-96`, `:117-121`, `:156-164`.
- **Gates (FAIL, environment blocker).** `feature_check.py` exits 1 before running any gate because it creates its log folder at `<worktree>/../zene-qwen-new-p23-51-feature-check`, which is outside the write-jail (`PermissionError`, `feature-check-1.log`). I ran the same gate commands from the worktree, logging to the LOG FOLDER. The six simple gates exit 0. The two ratchets (complexity, file-length) exit 1 with REGRESSION lines, none in a file this branch changes. Clearing this FAIL needs a run of `feature_check.py` from outside the jail, or the log path changed to a writable location. I did not work around it by writing outside the worktree.
