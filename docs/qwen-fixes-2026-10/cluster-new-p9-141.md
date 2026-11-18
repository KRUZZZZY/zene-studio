# Cluster new-p9-141

Verdict: NEEDS-CONTEXT

## Finding evidence

The finding is real. In [ExprSynth.cpp](../../plugins/Xpressive/ExprSynth.cpp),
`LastSampleFunction` stores `history_size` unchanged and initializes
`m_pivot_last` from `history_size - 1` at lines 153-159. For zero, that
unsigned subtraction underflows and the sample array has length zero.
`setLastSample()` writes `m_samples[m_pivot_last]` at line 173. `ExprFront`
passes its `last_func_samples` argument through at lines 527-532, and
`ExprFront::evaluate()` calls `setLastSample()` at lines 593-599.

## Why the fix is blocked

The supplied test locations are frozen reference plugin sources, not test
cases. `tests/CMakeLists.txt:1533` includes `tests/reference/Xpressive` in the
`PART_C_REFERENCE_MODULES` plugin list. The configured CTest manifest has no
`ExprSynth` or `Xpressive` test, and the build has no `ExprSynth` target.
Adding a deterministic regression requires registering a runnable test target;
the worktree instructions prohibit configuring this build. No production fix
was applied because the required RED/GREEN proof cannot be run under those
constraints.

## Evidence

Commands below were run from the stated directories. Logs are under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-141.r1/logs`.

| Check | Exact command | Exit / result |
| --- | --- | --- |
| RED regression | Not run: no registered regression target is available. | N/A |
| GREEN regression | Not run: no registered regression target is available. | N/A |
| ExprSynth test discovery (`build/tests`) | `ctest -N -R '^ExprSynth$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-141.r1/logs/exprsynth-discovery-1.log 2>&1; echo EXIT=$?` | Exit 0; `Total Tests: 0` |
| Xpressive test discovery (`build/tests`) | `ctest -N -R '^Xpressive$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-141.r1/logs/xpressive-discovery-1.log 2>&1; echo EXIT=$?` | Exit 0; `Total Tests: 0` |
| ExprSynth build target (worktree root) | `cmake --build build --target ExprSynth > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-141.r1/logs/build-exprsynth-target-1.log 2>&1; echo EXIT=$?` | Exit 2; no rule for target `ExprSynth` |
| Affected-family test count | Both named CTest queries above | 0 tests |
| Complexity gate (worktree root) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-141.r1/logs/gate-complexity-1.log 2>&1; echo EXIT=$?` | Exit 0 |
| File-length gate (worktree root) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-141.r1/logs/gate-file-length-1.log 2>&1; echo EXIT=$?` | Exit 0 |

Files committed: `docs/qwen-fixes-2026-10/cluster-new-p9-141.md`.
