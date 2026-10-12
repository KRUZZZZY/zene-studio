# Cluster new-p10-105

**Verdict: NEEDS-CONTEXT**

The finding describes a real hazard in `CombFeedback::setDelay()`: `include/Delay.h:101-105` converts the input to `int` without validating it, and `update()` uses the stored delay in its buffer index at `include/Delay.h:112-117`. A negative delay can produce an invalid read position; NaN reaches an undefined floating-point-to-integer conversion.

I could not run the required regression on this branch without broadening the change. The first direct test that included `Delay.h` failed to compile because the header has unrelated invalid constructor initializers: `m_feedBack` at lines 72, 143, 214 and 295, and nonexistent `m_delay`/`m_fraction` members at lines 215-216. The configured product target does not include this header, and a source search found no product call sites for `CombFeedback`. The suggested `MonoDelay` and `StereoDelay` files are reference plugin sources; the configured CTest list has no matching test names. Repairing these compile errors would change unrelated delay templates and still leave the behavior contract for an invalid call (for example, whether it preserves the previous delay) to establish. I therefore made no production change and removed the unbuildable test attempt.

## Evidence

| Check | Command | Exit / result |
| --- | --- | --- |
| RED build attempt | `cmake --build build --target DeviceDspTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r1/logs/build-2.log 2>&1; echo EXIT=$?` | `2`; compilation stopped on the `Delay.h` initializer errors, before CTest ran. |
| RED regression | `ctest -R '^DeviceDspTest$' --output-on-failure` | Not run: the target did not build. |
| GREEN regression | — | Not run; no passing regression result is claimed. |
| Affected-family test count | — | 0 CTest tests executed. |
| Reverse-fix proof | — | Not run; no production fix was made. |
| Product rebuild | `cmake --build build --target zene -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r1/logs/build-3.log 2>&1; echo EXIT=$?` | `0`; this target does not include `include/Delay.h`. |
| CTest inventory | `ctest -N > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r1/logs/test-1.log 2>&1; echo EXIT=$?` (run from `build/tests`) | `0`; 402 tests listed, no `MonoDelay`, `StereoDelay` or `DelayControls` test. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | `0`. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-105.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | `0`. |

**Files committed:** `docs/qwen-fixes-2026-10/cluster-new-p10-105.md` only. No production or test code was changed.
