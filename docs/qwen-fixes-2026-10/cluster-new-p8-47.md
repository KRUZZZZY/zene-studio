# Cluster new-p8-47

**Verdict: FALSE-POSITIVE**

The finding says `project.export_midi` should be marked mutating and should record a file-level transaction. The branch's A16 contract explicitly defines this command as `not_mutating`: its output is a caller-owned artefact, while the session remains unchanged. The same contract makes this distinction for `interchange.smf_export`, another Standard MIDI File writer, and other output-artifact commands. In this contract, transaction tracking covers session/project changes; it does not capture or restore caller-owned output files. Changing this command alone would contradict the existing contract and neighboring exporters.

## Evidence

| File and lines | Evidence |
| --- | --- |
| `src/core/ControlCommandsProjectLifecycle.cpp:211-235` | The command description says the session is not modified; the handler writes the MIDI output at the caller's path and returns its path and byte count. |
| `src/core/ControlReversibilityTableShell.cpp:69-72` | The contract classifies `project.export_midi` as `RC::NotMutating` and explains that it writes at the caller's path without changing session state. |
| `src/core/ControlReversibilityTableInterchange.cpp:59-63,82-88` | The table says external filesystem output is `not_mutating` when session state is unchanged, and applies that rule to `interchange.smf_export` with the caller-owned MIDI file. |
| `src/core/ControlReversibilityTableDevices.cpp:101-102` | `track.preset_save` follows the same rule for a file output that leaves the project unchanged. |
| `src/core/ControlRegistry.cpp:355-363` | The registry records transactions only for successful changed commands declared mutating. That behavior matches the explicit classification above. |

## Verification

No regression was added or run: step 2 determined the finding is not a bug under the existing contract. No ctest command was run, so there is no RED/GREEN result or affected-family test execution count to report (0 tests executed; this is not a zero-test ctest run).

| Check | Exact command | Exit |
| --- | --- | --- |
| RED regression | Not run; false-positive finding | N/A |
| GREEN regression | Not run; no fix applied | N/A |
| Affected-family tests | No ctest command run | 0 executed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-47.r1/logs/gate-1-complexity.log 2>&1; gate_code=$?; printf 'EXIT=%s\n' "$gate_code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-47.r1/logs/gate-1-complexity.log; printf 'EXIT=%s\n' "$gate_code"; exit "$gate_code"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-47.r1/logs/gate-2-file-length.log 2>&1; gate_code=$?; printf 'EXIT=%s\n' "$gate_code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-47.r1/logs/gate-2-file-length.log; printf 'EXIT=%s\n' "$gate_code"; exit "$gate_code"` | 0 |
| Log search | `rg -n 'EXIT=|error|FAILED|Assertion' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-47.r1/logs/gate-1-complexity.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-47.r1/logs/gate-2-file-length.log` | 0 |
| `fff` availability | `command -v fff` | 1 (unavailable; `rg` used to search logs) |

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p8-47.md` (this report)
