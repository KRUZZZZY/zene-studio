# Cluster new-p11-1: streamed peer commands and journal fingerprints

Verdict: **FALSE-POSITIVE**

## Evidence

`src/core/CollabSessionSync.cpp:197-199` wraps a streamed command in `RemoteApplyScope` and invokes it through `ControlRegistry`. That scope suppresses command interception at `src/core/CollabCommandTap.cpp:61-65`; it does not suppress observation. `ControlRegistry::runHandler()` calls `CommandTap::observed()` at `src/core/ControlRegistry.cpp:363` after the handler. The observer at `src/core/CollabCommandTap.cpp:67-69` calls the installed observer at depth zero without checking `RemoteApplyScope`. `CollabSession::observe()` records the journal fingerprint at `src/core/CollabSessionSync.cpp:124-133` outside its host-only broadcast branch.

Therefore a streamed `track.rename` updates the peer fingerprint as the command completes. The subsequent journal poll sees no fingerprint change and does not request a resync. The suggested extra fingerprint update in `applyCommand()` would duplicate existing behavior.

A temporary loopback integration probe joined a peer, applied a streamed journaled `track.rename`, and checked for the local-edit resync notice. It passed against the original production code. I removed the probe because it did not fail before a production change and would not be a valid regression test.

## Evidence table

| Evidence | Exact command | Result |
| --- | --- | --- |
| RED | N/A; the finding is false, so no production change or RED test applies. | N/A |
| Original-code integration probe | `ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-1.r1/logs/test-1.log 2>&1; echo EXIT=$?` (run from `build/tests`) | `EXIT=0`; 1/1 CTest target passed with the temporary probe included. |
| GREEN | N/A; no production change was made. | N/A |
| Final affected-family run | `ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-1.r1/logs/test-2.log 2>&1; echo EXIT=$?` (run from `build/tests`) | `EXIT=0`; 1/1 CTest target passed. Verbose run reported 11 passed, 0 failed, 0 skipped. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-1.r1/logs/gate-complexity-1.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-1.r1/logs/gate-file-length-1.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p11-1.md` | Report only; no product or test files changed. |

## Build commands

- `cmake --build build --target CollabTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-1.r1/logs/build-2.log 2>&1; echo EXIT=$?` — `EXIT=2`. The temporary probe initially had unqualified test references; this was corrected before the probe run.
- `cmake --build build --target CollabTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-1.r1/logs/build-3.log 2>&1; echo EXIT=$?` — `EXIT=0`.
- `cmake --build build --target CollabTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-1.r1/logs/build-4.log 2>&1; echo EXIT=$?` — `EXIT=0` after removing the probe.
- `ctest -R '^CollabTransportTest$' -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-1.r1/logs/test-3.log 2>&1; echo EXIT=$?` (run from `build/tests`) — `EXIT=0`.

The fff grep tool was unavailable in this session, so the specified log folder was searched with `rg` for errors, failures, assertions, and test totals.
