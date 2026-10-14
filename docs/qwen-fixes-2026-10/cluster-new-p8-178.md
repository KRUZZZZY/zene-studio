# Cluster new-p8-178 — Audition outside note rows

Verdict: FALSE-POSITIVE

Evidence: `NoteCanvas::pressAudition` at `src/gui/noteeditor/NoteCanvasInput.cpp:172-176`
passes `m_pressKey` to `NoteEditor::auditionPress` when there is no note under the pointer. A
negative value can therefore reach that method, but `src/gui/noteeditor/NoteEditor.cpp:371-379`
returns for `key < 0` before calling `Piano::handleKeyPress`. The invalid key is not sent to the
instrument. During a drag, `NoteCanvas::scrubTo` at `src/gui/noteeditor/NoteCanvasGestures.cpp:151-159`
also ignores negative keys. The reported invalid audition does not occur. Returning from the
canvas handler before setting `Drag::Audition` would also prevent a drag that starts in blank space
from auditioning after entering a valid row.

Regression: RED/GREEN not applicable because the finding is false-positive; no test/build command
was run.
Affected-family test count: 0 (no family run).

Gates:

- `bash tests/complexity-gate.sh --check` — EXIT=0; output: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-178.r1/logs/gate-1.log`.
- `bash tests/file-length-gate.sh --check` — EXIT=0; output: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-178.r1/logs/gate-2.log`.

The session did not expose the requested `fff` grep tool and no `fff` executable was installed;
the gate logs were searched with `rg -n -i 'error|FAILED|Assertion|EXIT='` instead.

Files in commit: `docs/qwen-fixes-2026-10/cluster-new-p8-178.md` (report only).
