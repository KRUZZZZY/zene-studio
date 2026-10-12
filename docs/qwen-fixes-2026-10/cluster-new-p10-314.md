# Cluster new-p10-314

**Verdict: FALSE-POSITIVE (for branch `bugs/qwen-new-p10-314`).**

The finding names `src/gui/PhaseScopeView.cpp:188`, but that file is not present
or tracked in this checkout. Searches of the GUI, tests, headers, plugins, and
the CMake test/build registration files found no `PhaseScopeView` or phase-scope
reference. There is no reported refresh path here to change or test.

The closest existing UI component is `src/gui/widgets/Oscilloscope.cpp:156-199`.
Its `paintEvent()` draws the audio waveform for `DEFAULT_CHANNELS`; it has no
mixer channel list or refresh method. This does not implement the phase-scope
channel selector described in the finding.

## Verification

| Evidence | Command/result |
| --- | --- |
| Source tracked | `git ls-files --error-unmatch src/gui/PhaseScopeView.cpp` — exit 1; path is not tracked. |
| Source exists | `test -e src/gui/PhaseScopeView.cpp` — exit 1; path is absent. |
| Source/test references | `rg -n "PhaseScopeView|phase.?scope" src/gui tests include plugins` — exit 1; no matches. |
| Build/test registration references | `rg -n "PhaseScopeView|phase.?scope" src/gui/CMakeLists.txt src/CMakeLists.txt tests/CMakeLists.txt` — exit 1; no matches. |
| Nearest existing UI code | `nl -ba src/gui/widgets/Oscilloscope.cpp | sed -n '150,205p'` — exit 0; lines 156-199 paint fixed waveform channels. |
| Regression RED | Not applicable: classification found no affected source or behavior in this branch, so step 2 skips regression creation and execution. |
| Regression GREEN | Not applicable for the same reason. |
| Affected-family test count | Not applicable: no PhaseScope test family exists; no ctest command was run. |
| Complexity gate | See exact logged command below — exit 0. |
| File-length gate | See exact logged command below — exit 0. |

Exact gate commands (each command's final process exit code was the gate code):

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-314.r1/logs/gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-314.r1/logs/gate-3.log; exit "$rc"
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-314.r1/logs/gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-314.r1/logs/gate-4.log; exit "$rc"
```

Both exact command invocations returned exit code 0.

The gate commands were run with output redirected to new files in the requested
log folder. The `fff` grep tool was not available in this session; the logs
were checked with `rg` instead. Each of these searches returned exit 1 because
there were no matches:

```text
rg -n -i error "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-314.r1/logs"
rg -n -i FAILED "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-314.r1/logs"
rg -n -i Assertion "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-314.r1/logs"
```

No production or test files were changed. The only file committed for this
finding is this report: `docs/qwen-fixes-2026-10/cluster-new-p10-314.md`.
