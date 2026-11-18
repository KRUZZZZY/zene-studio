# Cluster new-p8-51: arm-track undo classification

Verdict: **FIXED**

`record.arm_track` creates a take file outside the project journal. Disarming
stops writing and retires the recovery journal, but retains the WAV, so it cannot
be a reversible inverse. The production fix already on this branch marks the
transaction `irreversible`, records no command inverse, and points to explicit
disarm plus manual keep/delete as the fallback.

## Evidence

- `src/core/ControlCommandsRecordingRoutes.cpp:201` records the arm as
  non-reversible with an empty inverse operation; line 203 names the retained-file
  fallback.
- `src/core/ControlReversibilityTableRecording.cpp:165` classifies
  `record.arm_track` as irreversible and explains why disarming cannot restore
  the filesystem.
- `tests/src/core/ControlRecordingRoutesTest.cpp:98` verifies typed undo refusal;
  line 104 verifies the take remains on disk.
- `tests/control-record-inputs.py:439` now requires the transaction to be
  `irreversible`, `reversible: false`, and have an empty inverse operation.
  Its separate `track.set_arm` snapshot and inverse assertion remains at line
  455.
- The contract row in `docs/RECORD-INPUTS.md:72` and command descriptions in
  `docs/CAPABILITIES-0.3.0.md:1121` and `:1123` describe the retained take.
  `tools/mcp-zene-control/zene_control/commands_snapshot.json` was regenerated
  from the live registry and carries the updated arm and disarm descriptions.

## Verification

All ctests ran from `build/tests`. The affected recording/reversibility family
contains four tests: `ControlRecordingRoutesTest`, `ReversibilityContractTest`,
`ReversibilityUndoTest`, and `ControlRecordInputs`.

| Phase | Exact command | Exit | Result |
|---|---|---:|---|
| Build | `cmake --build build --target zene ControlRecordingRoutesTest ReversibilityContractTest ReversibilityUndoTest` | 0 | All affected targets built. |
| Sensitivity RED build, production patch reversed | `cmake --build build --target zene ControlRecordingRoutesTest` | 0 | Rebuilt the original reversible behavior. |
| Sensitivity RED | `ctest -R '^ControlRecordingRoutesTest$' --output-on-failure` | 8 | 1 test failed at `!undo.ok`: undo claimed to reverse the retained take. |
| Sensitivity GREEN build, production patch restored | `cmake --build build --target zene ControlRecordingRoutesTest` | 0 | Rebuilt the fixed behavior. |
| Sensitivity GREEN | `ctest -R '^ControlRecordingRoutesTest$' --output-on-failure` | 0 | 1/1 test passed. |
| Recording/reversibility family | `ctest -R 'ControlRecordInputs|ControlRecordingRoutesTest|ReversibilityContractTest|ReversibilityUndoTest' --output-on-failure` | 0 | 4/4 tests passed, including `ControlRecordInputs`. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | Passed. |

An interim complexity-gate run returned EXIT 1 after I temporarily added the
pre-existing socket test to `tests/fork-sources.txt`; that made the gate classify
its existing over-limit helpers as newly added. I removed that inventory-only
edit, changed no gate or baseline, and reran both gates successfully on the final
files.

For sensitivity, the source-only patch was captured with
`git show --format= HEAD -- src/core/ControlCommandsRecordingRoutes.cpp src/core/ControlReversibilityTableRecording.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-51.r2/logs/fix.patch`
(EXIT 0), reversed with
`git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-51.r2/logs/fix.patch`
(EXIT 0), and restored with
`git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-51.r2/logs/fix.patch`
(EXIT 0). Only the two production source files were in that patch.

The snapshot capture ran the built `zene` in the socket harness, then invoked
`python3 tools/mcp-zene-control/snapshot_commands.py --socket /tmp/zctl-run-7268fihu/zene.sock --lane /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-51`
(EXIT 0); it wrote 809 commands across 76 groups. Build and test logs are under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-51.r2/logs`.

## Files committed in the follow-up

- `docs/CAPABILITIES-0.3.0.md`
- `docs/RECORD-INPUTS.md`
- `docs/qwen-fixes-2026-10/cluster-new-p8-51.md`
- `tests/control-record-inputs.py`
- `tools/mcp-zene-control/zene_control/commands_snapshot.json`
