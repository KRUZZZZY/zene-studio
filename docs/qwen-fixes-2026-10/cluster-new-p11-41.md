# Cluster new-p11-41

Verdict: **BLOCKED**

## Finding assessment

The reported behavior is already implemented on this branch. `note.remove` calls
`MidiClip::removeNote()` at `src/core/ControlCommandsNotes.cpp:229-231`. That
method calls `notesChanged()` after erasing the note at
`src/tracks/MidiClip.cpp:229-246`; `notesChanged()` calls
`ClipLinks::mirrorContent(this)` whenever the clip is linked at
`src/tracks/MidiClip.cpp:183-187`.

The existing control-surface test covers the described path:
`tests/src/core/ClipLinkTest.cpp:310-335` removes a note from one linked clip,
then checks the other member's note count and remaining key. No product or test
change was warranted, so no RED regression run was applicable.

The classification is blocked only at the required commit gate: both mandatory
ratchet checks fail on existing worktree regressions outside this finding. The
complexity gate lists six new over-target functions in unrelated files. The
file-length gate lists fourteen files over 500 code lines, including the
unchanged `tests/src/core/ControlEditCommandsTest.cpp` at 511 lines. The gate
baselines were not modified or re-anchored.

## Verification evidence

| Step | Exact command | Result |
|---|---|---|
| Build existing linked-clip test | `cmake --build build --target ClipLinkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-41.r1/logs/build-2.log 2>&1` | EXIT=0 |
| Existing behavior test (run from `build/tests`) | `ctest -R '^ClipLinkTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-41.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | EXIT=0; 1/1 CTest target passed |
| RED regression | Not applicable: branch already propagates this removal and no patch was made | N/A |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-41.r1/logs/complexity-gate-3.log 2>&1` | EXIT=1 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-41.r1/logs/file-length-gate-3.log 2>&1` | EXIT=1 |

The recorded file contains the exact blocking gate output. The report is the
only file committed for this cluster; no production source or test was changed.
