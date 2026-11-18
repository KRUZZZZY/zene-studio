# Cluster new-p8-48

**Verdict: FALSE-POSITIVE**

## Finding

The finding says `render.render` should be marked mutating and record a file
transaction because it writes its requested output path. The write is real, but
the command’s treatment of that output is an explicit A16 contract decision:
the render artefact is outside the project state and undo does not manage it.
The caller chooses the destination, including whether to overwrite an existing
file, and can render again if it wants a different artefact.

## File and line evidence

- `src/core/ControlCommandsProject.cpp:310-360` serialises the session to a
  temporary project, runs the CLI renderer with the requested output path in a
  child process, removes the temporary project, and leaves the session
  unchanged. Lines 355-392 report the resulting output file.
- `src/core/ControlCommandsProject.cpp:395-433` registers `render.render`
  without setting `cmd.mutating`. The default is `false` in
  `include/ControlRegistry.h:97`.
- `src/core/ControlReversibilityTablePassive.cpp:212-225` explicitly assigns
  `render.render` the `NotMutating` class and explains that the output artefact
  is not project state, that overwriting it is the caller’s decision, and that
  undo does not remove it. `docs/A16-REVERSIBILITY.md:158` records the same
  contract.
- `tests/src/core/ReversibilityContractTest.cpp:377-402` checks consistency
  between registered mutating commands and the table. The current registration
  and `NotMutating` row agree.

## Evidence and validation

| Item | Command / result |
|---|---|
| Handler and registration inspection | `rg -n -C 2 'render.render|cmd.mutating|QFile::remove\(tempProject\)' src/core/ControlCommandsProject.cpp` — EXIT=0 |
| Contract inspection | `rg -n -C 2 'R\("render\.render"|rendered file is an output' src/core/ControlReversibilityTablePassive.cpp` — EXIT=0 |
| Contract-test inspection | `rg -n -C 2 'theOnlyUnclassedMutatingCommandsAreTheRefusals|cmd->mutating' tests/src/core/ReversibilityContractTest.cpp` — EXIT=0 |
| Default metadata inspection | `rg -n -C 2 'bool mutating = false' include/ControlRegistry.h` — EXIT=0 |
| RED regression | Not applicable: finding is false-positive; no regression was added or run. |
| GREEN regression | Not applicable: no product change was made. |
| Affected-family test count | Not applicable: no test target was run. |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p8-48.md` |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-48.r1/logs/gate-5.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-48.r1/logs/gate-5.log; exit "$status"` — EXIT=0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-48.r1/logs/gate-6.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-48.r1/logs/gate-6.log; exit "$status"` — EXIT=0 |

No production or test files were changed. The only file committed for this
finding is this report.
