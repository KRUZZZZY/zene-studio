# Cluster new-p10-317

Verdict: **FIXED**

The public constructor in `include/PeakController.h:45` defaults its effect pointer to
`nullptr`. `src/core/PeakController.cpp:58-66` now guards all effect-specific signal
connections, and `src/core/PeakController.cpp:85` guards the coefficient read. When
there is no effect, the existing null path fills the output with zero at
`src/core/PeakController.cpp:122`.

The regression at `tests/src/core/ControllerSurfaceTest.cpp:118-122` constructs a
controller through the default argument, advances one frame, and checks that it returns
silence. Before the fix it segfaulted in the constructor. After the fix was reversed, the
same rebuilt test segfaulted again.

The three named `tests/reference/PeakControllerEffect` files are reference plugin
sources and are not registered CTest targets in this tree. The regression therefore
uses the existing registered `ControllerSurfaceTest`, which initializes the engine and
exercises controller behavior.

## Evidence

| Check | Exact command / result |
|---|---|
| RED on original code | `ctest -R '^ControllerSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/test-1.log; exit $status` — EXIT=8; `ControllerSurfaceTest` segfaulted in `PeakController::PeakController`. |
| GREEN with fix restored | `ctest -R '^ControllerSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/test-5.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/test-5.log; exit $status` — EXIT=0; 100% passed. |
| Reversal proof | The patch was reversed, `zene` and `ControllerSurfaceTest` rebuilt, and the identical ctest command then exited 8 with a segfault; the patch was reapplied and the test passed again. |
| Affected family count | 1 registered CTest case matched and executed: `ControllerSurfaceTest` (CTest test #40). |
| Complexity gate | `bash tests/complexity-gate.sh --check` — EXIT=0. |
| File length gate | `bash tests/file-length-gate.sh --check` — EXIT=0. |
| Explicit files committed | `src/core/PeakController.cpp`; `tests/src/core/ControllerSurfaceTest.cpp`; `tests/upstream-modifications.txt`; `docs/qwen-fixes-2026-10/cluster-new-p10-317.md`. |

## Build and test command record

Build commands ran from the worktree root. Each exited 0:

```sh
cmake --build build --target zene ControllerSurfaceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/build-1.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/build-1.log; exit $status
cmake --build build --target zene ControllerSurfaceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/build-2.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/build-2.log; exit $status
cmake --build build --target zene ControllerSurfaceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/build-3.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/build-3.log; exit $status
cmake --build build --target zene ControllerSurfaceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/build-4.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/build-4.log; exit $status
cmake --build build --target zene ControllerSurfaceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/build-5.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/build-5.log; exit $status
```

CTest commands ran from `build/tests`:

```sh
ctest -R '^ControllerSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/test-1.log; exit $status
```

EXIT=8 (original code; constructor segfault).

```sh
ctest -R '^ControllerSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/test-2.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/test-2.log; exit $status
```

EXIT=8 (first fix run; the test had not yet advanced the controller frame, so the
assertion observed the base class's cached initial value, 0.5). The test was corrected
to advance a frame before checking the null-effect output.

```sh
ctest -R '^ControllerSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/test-3.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/test-3.log; exit $status
```

EXIT=0 (fixed code).

```sh
ctest -R '^ControllerSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/test-4.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/test-4.log; exit $status
```

EXIT=8 (after the production patch was reversed and rebuilt; constructor segfault).

```sh
ctest -R '^ControllerSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/test-5.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/test-5.log; exit $status
```

EXIT=0 (production patch reapplied and rebuilt).

The reversal proof commands both exited 0:

```sh
git diff -- src/core/PeakController.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/fix.patch
```

Both gates ran from the worktree root and exited 0:

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/gate-1.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/gate-1.log; exit $status
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/gate-2.log 2>&1; status=$?; echo EXIT=$status >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-317.r1/logs/gate-2.log; exit $status
```
