# Cluster new-p9-212: integer and enum LADSPA control ranges

**Verdict: FALSE-POSITIVE**

## Evidence

`src/core/LadspaControl.cpp:69-74` passes the integer and enum range as `(max, min)`. For a port with bounds 0–10, that call is `setRange(10, 0, 1)`. `AutomatableModel::setRange` in `src/core/AutomatableModel.cpp:393-399` stores those arguments, swaps them when the stored minimum exceeds the maximum, then calculates the range. The resulting model bounds are 0–10, so the reported control can represent the plugin's advertised range. The step passed for this case is 1.

No production fix or regression test was added: a test asserting the model range is 0–10 would pass before and after changing the argument order, so it would not prove a regression. No test was run. The CTest registration search found `PluginPortsMigrationTest`; it found no tests named `LadspaControlDialog`, `LadspaControls`, or `LadspaWidgetFactory`.

## Evidence table

| Check | Result |
|---|---|
| RED | Not run; the finding is a false positive and no regression test applies. |
| GREEN | Not run; no production change or regression test. |
| Affected-family test count | No CTest suite run. The registration search returned `PluginPortsMigrationTest` only among the queried names. |
| Range implementation | `EXIT=0` |
| Test registration | `EXIT=0` |
| Complexity gate | `EXIT=0` on both runs. |
| File-length gate | `EXIT=0` on both runs. |
| Log scans for `error`, `FAILED`, `Assertion` | Each returned `EXIT=1` (no matches). |

### Exact commands

```text
nl -ba src/core/AutomatableModel.cpp | sed -n '388,408p'; echo EXIT=$?
rg -n '^add_test\((LadspaControlDialog|LadspaControls|LadspaWidgetFactory|PluginPortsMigrationTest)' build/tests/CTestTestfile.cmake; echo EXIT=$?
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-212.r1/logs/gate-1.log 2>&1; echo EXIT=$?
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-212.r1/logs/gate-2.log 2>&1; echo EXIT=$?
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-212.r1/logs/gate-3.log 2>&1; echo EXIT=$?
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-212.r1/logs/gate-4.log 2>&1; echo EXIT=$?
rg -n -i 'error' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-212.r1/logs; echo EXIT=$?
rg -n 'FAILED' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-212.r1/logs; echo EXIT=$?
rg -n 'Assertion' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-212.r1/logs; echo EXIT=$?
```

The requested `fff` grep tool was not available in this environment, so the log scans used `rg`.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p9-212.md`
