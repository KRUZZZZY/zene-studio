# Cluster new-p8-292 — FIXED

## Finding and fix

The finding is real. `CollabSession::pollJournal()` used to consume a changed fingerprint before checking the three-second auto-resync interval. A throttled edit was therefore invisible to later timer polls.

`CollabSession::trackUntrackedEdit()` now records an eligible joined-peer edit as pending. `pollJournal()` keeps checking that pending request on subsequent timer ticks and sends it when the interval expires. Applying a full snapshot or leaving the session clears pending state. The regression uses a joined session, waits for the first resync snapshot to apply, then creates a second untracked journal edit inside the throttle interval.

## File and line evidence

- `include/CollabSession.h:113,132`: helper declaration and pending state.
- `src/core/CollabSessionSync.cpp:187-190`: snapshot application clears pending state.
- `src/core/CollabSessionSync.cpp:223-252`: eligible edit tracking and retry after the interval.
- `src/core/CollabSession.cpp:147`: leaving the session clears pending state.
- `tests/src/core/CollabTransportTest.cpp:251,297`: end-to-end regression and its failing assertion; the original code saw `snapshotsSent` equal to 2 instead of 3.

## Validation evidence

All build and test output was redirected to new files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs`. Commands below show the exact shell command and its recorded exit code. Ctest commands ran from `build/tests`.

| Check | Command | Exit |
| --- | --- | ---: |
| RED on original implementation | `ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/test-1-red.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/test-1-red.log; exit "$rc"` | 8 |
| Final build | `cmake --build build --target zene CollabPolicyTest CollabTransportTest CollabPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/build-8-final.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/build-8-final.log; exit "$rc"` | 0 |
| Final affected-family ctest | `ctest -R Collab --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/test-8-final-family.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/test-8-final-family.log; exit "$rc"` | 0 |

The affected family ran **4 ctest tests, 4 passed**: `CollabPolicyTest`, `CollabTransportTest`, `CollabPanelTest`, and `ControlCollab`. The RED assertion was `Actual (snapshotsSent): 2; Expected (3): 3`.

The final patch-removal proof used:

1. `git diff -- include/CollabSession.h src/core/CollabSession.cpp src/core/CollabSessionSync.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/fix.patch` — exit 0.
2. `cmake --build build --target zene CollabTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/build-6-proof-red.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/build-6-proof-red.log; exit "$rc"` — exit 0.
3. `ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/test-6-proof-red.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/test-6-proof-red.log; exit "$rc"` — exit 8 with the same assertion failure.
4. `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/fix.patch` — exit 0.
5. `cmake --build build --target zene CollabTransportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/build-7-proof-green.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/build-7-proof-green.log; exit "$rc"` — exit 0.
6. `ctest -R '^CollabTransportTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/test-7-proof-green.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/test-7-proof-green.log; exit "$rc"` — exit 0.

Final ratchet commands, run again after this report was added:

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/gate-9-final.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/gate-9-final.log; exit "$rc"` — exit 0.
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/gate-10-final.log 2>&1; rc=$?; echo "EXIT=$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-292.r1/logs/gate-10-final.log; exit "$rc"` — exit 0.

The first complexity check returned exit 1 because `pollJournal()` reached CCN 11; extracting `trackUntrackedEdit()` brought the final function complexity within the unchanged limit. The file-length gate passed.

The requested `fff` grep interface was not exposed in this environment (`type -a fff` found no executable and no `fff` tool was available), so log searches used `rg` for `error`, `FAILED`, and `Assertion`. The only `FAILED` matches were the intentional RED runs; no `Assertion` matches were found.

## Files in the commit

- `include/CollabSession.h`
- `src/core/CollabSession.cpp`
- `src/core/CollabSessionSync.cpp`
- `tests/src/core/CollabTransportTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-292.md`
