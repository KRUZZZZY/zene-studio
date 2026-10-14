# Cluster new-p9-565

**Verdict: FALSE-POSITIVE**

## Finding review

The claimed null target dereference is not present on this branch.

- `src/core/AutomationClip.cpp:159-170`: `AutomationClip::firstObject()` returns the first target only when the vector is nonempty and its `QPointer` is live. Otherwise it returns a static `FloatModel` with the clip defaults (0–1). The function does not return null.
- `include/AutomationClip.h:149-156`: `getMin()` and `getMax()` dereference the non-null result of `firstObject()`.
- `src/gui/clips/AutomationClipView.cpp:110-128`: disconnect captures the current range before erasing a target. If that was the last target, scaling reads the default range through `getMin()`/`getMax()`.
- `src/gui/clips/AutomationClipView.cpp:271-272`: painting also reads range values through `firstObject()`, which supplies the same default model for an empty clip.

No product change or regression test was added: a test for this claimed crash would pass on the original branch, so it would not be a valid RED regression.

## Verification evidence

| Check | Exact command | Result |
|---|---|---|
| Source-path evidence | `nl -ba src/core/AutomationClip.cpp | sed -n '155,176p'; nl -ba include/AutomationClip.h | sed -n '145,159p'; nl -ba src/gui/clips/AutomationClipView.cpp | sed -n '104,130p'; nl -ba src/gui/clips/AutomationClipView.cpp | sed -n '265,278p'` | EXIT=0 |
| RED regression | Not applicable; finding is false on the original branch | Not run |
| GREEN regression | Not applicable; no fix or regression was warranted | Not run |
| Affected-family test count | No affected test family; ctest was not invoked | N/A (0 executed) |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-565.r1/logs/gate-3.log 2>&1; gate_result=$?; echo EXIT=$gate_result; exit "$gate_result"` | EXIT=0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-565.r1/logs/gate-4.log 2>&1; gate_result=$?; echo EXIT=$gate_result; exit "$gate_result"` | EXIT=0 |

The fff grep tool was not available in this session. Gate logs were searched with these commands; each returned EXIT=1, meaning no match:

- `rg -n -i 'error' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-565.r1/logs`
- `rg -n -i 'FAILED' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-565.r1/logs`
- `rg -n -i 'Assertion' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-565.r1/logs`

The source-path evidence command exited 0. No build or test command was run because the reported behavior is already safe in the inspected code.

Files committed: docs/qwen-fixes-2026-10/cluster-new-p9-565.md.
