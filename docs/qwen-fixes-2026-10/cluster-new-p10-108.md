# Cluster new-p10-108

**Verdict: BLOCKED**

The finding is supported by the current event handlers: `XyPad::mousePressEvent()` enables input for every button at [plugins/Delay/DelayControlsDialog.cpp:118](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-108/plugins/Delay/DelayControlsDialog.cpp:118), and `mouseMoveEvent()` updates both models at lines 144 and 149 while that flag is set. `mouseReleaseEvent()` clears the flag for every button at line 126.

I could not run the required regression. This build has no `DelayControlsDialog` ctest: `ctest -R '^DelayControlsDialog$' --output-on-failure` from `build/tests` reported “No tests were found!!!” (EXIT=0, zero tests, treated as an error by the project rules). The existing `XyPadPairTest` at `tests/src/gui/XyPadPairTest.cpp` tests the Rack XY pad and is not this widget. `tests/reference/Delay/DelayControlsDialog.cpp` is a frozen plugin source, not a test. Creating a focused test needs a CMake target and build regeneration, which conflicts with the explicit “Do not configure” constraint, so I made no production change.

## Evidence

| Check | Exact command | Result |
|---|---|---|
| RED regression | `ctest -R '^DelayControlsDialog$' --output-on-failure` (cwd: `build/tests`) | EXIT=0; zero tests ran, so this is not RED evidence |
| GREEN regression | Not run; no registered target | N/A |
| Affected-family test count | Same ctest command | 0 (invalid per the zero-tests rule) |
| Complexity gate | `bash tests/complexity-gate.sh --check` | EXIT=0 (`gate-3.log`) |
| File-length gate | `bash tests/file-length-gate.sh --check` | EXIT=0 (`gate-4.log`) |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p10-108.md` | Report only; no production or test file changed |

## Build attempts

The following exploratory builds used temporary edits to `tests/src/gui/XyPadPairTest.cpp`; those edits were reverted and are not part of the worktree diff. Each command was run from the worktree root and returned EXIT=2:

- `cmake --build build --target XyPadPairTest` — `build-1.log`: the test event used a deprecated Qt 6 constructor, promoted to an error.
- `cmake --build build --target XyPadPairTest` — `build-2.log`: compiling the Delay implementation into this target left its Qt meta-object vtables undefined.
- `cmake --build build --target XyPadPairTest` — `build-3.log`: CMake AutoMoc rejected the included Delay moc file because its header is not in the target sources.

The available tool list did not expose the requested `fff` log-search tool; build and test logs were searched with `rg` instead. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-108.r1/logs`.
