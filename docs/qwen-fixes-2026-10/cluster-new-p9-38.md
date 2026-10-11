# Cluster new-p9-38: undoing `clip.trim` and auto-resize

**Verdict: FALSE-POSITIVE**

The reported behavior is not present on this branch. `clip.trim` checkpoints the clip before changing its edges and auto-resize flag (`src/core/ControlCommandsClipTrim.cpp:182-195`). Its recorded inverse contains `op` and `args`, but no `applies: command` field (`src/core/ControlEditSupport.cpp:481-492`). `control.undo` defaults a missing `applies` field to `journal`, dispatches an inverse only when it explicitly says `command`, and otherwise unwinds the `ProjectJournal` step (`src/core/ControlCommandsControl.cpp:330-336, 411-417`). It therefore does not invoke `clip.trim` again.

The clip checkpoint includes the old `autoresize` value: the trim mechanism documents that it is serialized and loaded unconditionally (`src/core/ControlCommandsClipTrim.cpp:80-83`). Existing regression coverage verifies auto-resize is true before trimming and true after one `control.undo` (`tests/src/core/ControlVerbInverseTest.cpp:182-215`). No product or test changes were needed.

## Evidence

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED attempt | No RED command applies: the finding is false on the unchanged branch, and the existing regression passes. | N/A | No production fix was made. |
| GREEN baseline | From `build/tests`: `ctest -R '^ControlVerbInverseTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-38.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | `ControlVerbInverseTest` passed; 1/1 affected-family ctest passed. |
| Test target build | `cmake --build build --target ControlVerbInverseTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-38.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | Affected test target rebuilt before ctest. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-38.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-38.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | Passed. |

There is no fix patch or reverse-patch run because no production change was warranted. The explicit file committed for this finding is `docs/qwen-fixes-2026-10/cluster-new-p9-38.md`.
