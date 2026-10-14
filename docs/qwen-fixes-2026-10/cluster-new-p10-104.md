# Cluster new-p10-104: NEEDS-CONTEXT

## Verdict

The zero-frame interpolation issue is visible in the source, but this header has no active users in the worktree and cannot be compiled as written. Exercising it requires resolving unrelated broken class initializers first. I did not change production behavior or claim a regression result.

## Evidence

- [include/Delay.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-104/include/Delay.h:101) sets `m_delay` to zero and `m_fraction` to one for `setDelay(0)`. At line 117, interpolation therefore selects the next ring-buffer slot before this update writes the input at line 121.
- `CombFeedback`'s constructor initializes nonexistent `m_feedBack` at line 72; the declared member is `m_gain` at line 129. The attempted test build also diagnosed invalid initializers in neighboring templates at lines 143, 214–216, and 295.
- `rg -n '#include ["<]Delay.h[">]|include/Delay.h' --glob '*.{h,cpp}'` returned exit 1 after removing the temporary test include: no source file includes this header. `CombFeedback` appears only in its declaration and aliases in this header. The listed `MonoDelay` and `StereoDelay` references are separate classes and cannot exercise this template.

The behavior described by the patrol is plausible, but the test cannot reach it on this branch. Repairing all header compilation failures would expand this one-bug task and may expose additional API/design questions, so the finding needs context before a production change.

## Validation evidence

| Check | Exact command | Exit/result |
|---|---|---|
| RED build attempt | `cmake --build build --target zene DeviceDspTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-104.r1/logs/build-1-red.log 2>&1; echo EXIT=$?` | `2`; compilation stopped on invalid `Delay.h` member initializers before the regression could run. |
| RED ctest | Not run; the test executable did not build. | 0 tests executed. |
| CTest inventory | `ctest -N -R '^(DeviceDspTest|MonoDelay|StereoDelay|DelayControls)$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-104.r1/logs/test-discovery-1.log 2>&1; echo EXIT=$?` | `0`; one test was registered (`DeviceDspTest`), but its executable was missing. |
| GREEN test | Not run; no production fix was made. | N/A. |
| Fix-removal proof | Not run; there is no production patch to remove and reapply. | N/A. |
| Affected-family tests | `DeviceDspTest` | 0 executed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-104.r1/logs/gate-complexity-2.log 2>&1; echo EXIT=$?` | `0`. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-104.r1/logs/gate-file-length-2.log 2>&1; echo EXIT=$?` | `0`. |

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-104.md`
- `tests/upstream-modifications.txt` (registers this new finding record)
