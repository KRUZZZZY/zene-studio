# Cluster new-p12-92 — Full-scale input meter gap

**Verdict: FALSE-POSITIVE for this branch (finding absent/not applicable).**

The reported `src/gui/InputLevelMeter.cpp:123` is absent from both the base and target trees. The input-level path available here is the read-only `meter.input` command: `inputLevel()` takes left and right peak readings and returns their dB values and clipped flag (`src/core/ControlCommandsMixerTools.cpp:348-355`); its registration explicitly says “Read only” (`:507-508`). There is no input-meter widget `paintEvent()` to fix or exercise in this target.

The previous round changed a different display. `LargeMeters::paintEvent()` reads the master channel's RMS output (`src/gui/MixerWindows.cpp:83-84`), and its regression inspected that dialog. Those changes do not address this input-meter finding, so I reverted the `MixerWindows.cpp` change and the corresponding `MixerModesTest.cpp` test. No input-meter regression or product fix was added because the reported path is absent.

## Evidence

| Check | Exact command and working directory | Exit | Result |
|---|---|---:|---|
| Input-meter RED regression | Not run; no input-meter widget or affected test target exists in this branch. | N/A | No valid branch-local regression can exercise the reported `paintEvent()`. |
| Input-meter GREEN regression | Not run for the same reason. | N/A | No input-meter product change was made. |
| Source presence check | `git cat-file -e HEAD:src/gui/InputLevelMeter.cpp 2>/dev/null; printf 'HEAD lookup exit=%s\n' "$?"; git cat-file -e 'HEAD^:src/gui/InputLevelMeter.cpp' 2>/dev/null; printf 'HEAD^ lookup exit=%s\n' "$?"` (worktree root) | 0 | Both lookups printed exit 128, confirming the source is absent at the target and base revisions. |
| Historical unrelated RED regression (`build/tests`) | `ctest -R MixerModesTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-92.r1/logs/test-5.log 2>&1; status=$?; echo EXIT=$status` | 8 | This reversed the Large Meters master-output fix; it is not evidence for the input-meter finding. |
| Historical unrelated GREEN regression (`build/tests`) | `ctest -R MixerModesTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-92.r1/logs/test-6.log 2>&1; status=$?; echo EXIT=$status` | 0 | The same Large Meters master-output test passed with that fix; it is not evidence for the input-meter finding. |
| Affected-family test count | N/A | N/A | No InputLevelMeter test family exists here. The unrelated historical `MixerModesTest` run had 14 QtTest cases. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-92.r2/logs/gate-5.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-92.r2/logs/gate-5.log; exit "$status"` (worktree root) | 0 | Passed after the final report edit. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-92.r2/logs/gate-6.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-92.r2/logs/gate-6.log; exit "$status"` (worktree root) | 0 | Passed after the final report edit. |

The unrelated product and test changes were reverted with `git restore --source=HEAD^ -- src/gui/MixerWindows.cpp tests/src/gui/MixerModesTest.cpp` (exit 0).

## Files committed

- `src/gui/MixerWindows.cpp` — revert the unrelated master-output meter change.
- `tests/src/gui/MixerModesTest.cpp` — remove the regression for that unrelated display.
- `docs/qwen-fixes-2026-10/cluster-new-p12-92.md` — record that the input-meter finding is absent from this branch.
