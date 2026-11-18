# Cluster new-p9-25-port

Verdict: **FIXED** (re-applied by hand on `bugs/qwen-new-p9-25-port`, based on the newest line `050/m0-train-5`). The two gates are red, the same as on clean `HEAD` (see Gates below).

Source commit (accepted on the older line, not merged here): `9f354367b fix: validate collaboration sequences (cluster new-p9-25)`. It was read with `git show` and re-written against the current code; it was not cherry-picked, merged or rebased.

## Finding and evidence

Before the fix, `src/core/CollabClient.cpp` converted the JSON `seq` value straight to `quint64` in two places:

- `onSnapshot`, `src/core/CollabClient.cpp:250` at HEAD (`m_seq = static_cast<quint64>(...toDouble())`)
- `onCommand`, `src/core/CollabClient.cpp:260` at HEAD (`const quint64 sequence = static_cast<quint64>(...toDouble())`)

Converting a double that is negative, non-integral, non-finite or ≥ 2^64 to an unsigned 64-bit integer is undefined behaviour. The same check was already missing on this branch, so the verdict is not ALREADY-FIXED.

## Change

- `src/core/CollabClient.cpp:28` adds `#include <cmath>`.
- `src/core/CollabClient.cpp:48-60` adds `readSequence()` in the anonymous namespace. It accepts only a JSON double that is finite, integral, ≥ 0 and < 2^64.
- `onSnapshot` (`src/core/CollabClient.cpp:252-258`) and `onCommand` (`src/core/CollabClient.cpp:282-288`) call `readSequence()` before the project is opened or the command sink runs. On failure they set a state message and call `requestResync()`, then return. `m_seq` is only assigned after validation, as before.
- Regression tests: `tests/src/core/CollabTransportTest.cpp`, helper `snapshotFrame()` and class `CraftedPeer` in the anonymous namespace, plus `rejectsNegativeSnapshotSequence()` and `rejectsOutOfRangeCommandSequence()` (the crafted peer sends `seq = -1.0` in a snapshot and `seq = 1e300` in a command).

No new files were added, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are unchanged. No control commands were added.

## Build and test ledger

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-25-port.r1/logs`. The log search used `rg`/Grep (the fff grep tool was deferred and not loaded).

| Step | Command (cwd: worktree root unless noted) | Log | Exit |
|---|---|---|---:|
| build attempt 1 | `cmake --build build --target zene CollabTransportTest -j2 > LOGS/build-1.log 2>&1; echo EXIT=$?` | build-1.log | 2 (environment: ccache could not create a temp file under `/run/user/1000`; no compile error) |
| build RED | `CCACHE_TEMPDIR=/tmp/ccache-tmp-kruzzzzy cmake --build build --target zene CollabTransportTest -j2 > LOGS/build-2.log 2>&1; echo EXIT=$?` | build-2.log | 0 |
| **RED** | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure > LOGS/test-1.log 2>&1; echo EXIT=$?` | test-1.log | **8** |
| build GREEN | same build command, `build-3.log` | build-3.log | 0 |
| **GREEN** | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure > LOGS/test-2.log 2>&1; echo EXIT=$?` | test-2.log | **8** (see note 1) |
| proof: patch saved | `git diff -- src/core/CollabClient.cpp > LOGS/fix.patch` | — | 0 |
| proof: revert | `git apply -R LOGS/fix.patch` | — | 0 |
| proof: build reverted | `CCACHE_TEMPDIR=... cmake --build build --target zene CollabTransportTest -j2 > LOGS/build-4.log 2>&1` | build-4.log | 0 |
| **proof RED** | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure > LOGS/test-3.log 2>&1` | test-3.log | **8** |
| proof: reapply | `git apply LOGS/fix.patch` | — | 0 |
| proof: build fixed | `CCACHE_TEMPDIR=... cmake --build build --target zene CollabTransportTest -j2 > LOGS/build-5.log 2>&1` | build-5.log | 0 |
| **proof GREEN** | `cd build/tests && ctest -R '^CollabTransportTest$' --output-on-failure > LOGS/test-4.log 2>&1` | test-4.log | **8** (see note 1) |
| gate: complexity | `bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1` | gate-1.log | **1** |
| gate: file length | `bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1` | gate-2.log | **1** |
| gate baseline on clean HEAD | same two gate commands run inside a `git archive HEAD` copy in `/tmp` | gate-head-complexity.log, gate-head-filelength.log | **1**, **1** |

Notes:

1. **Affected family:** `CollabTransportTest`, 25 QtTest cases executed in each run (non-zero, so the ctest selection is real).
   - RED (test-1): 22 passed, 3 failed. The failures are `rejectsNegativeSnapshotSequence` (`host.requestedResync()` returned FALSE, `tests/src/core/CollabTransportTest.cpp:709`), `rejectsOutOfRangeCommandSequence` (same message, `:733`), and `throttledUntrackedEditIsRetried` (tempo 163, expected 140, `:646`).
   - GREEN (test-2 and test-4): 24 passed, 1 failed. Both new cases pass. The only failure is `throttledUntrackedEditIsRetried`, which is **pre-existing**: it fails identically on the unfixed code (test-1, line 25), and it runs before the new cases in declaration order. This change does not touch the engine-backed throttle path. It is not fixed here; it is outside this bug.
2. The RED and the GREEN are the same test file. Only the production patch was reverted for the RED proof, using the saved `fix.patch`. The reapplied patch is identical to the committed one.

## Gates

Both required gates exit 1 on the working tree, and they also exit 1 on a clean `HEAD`. Their regression lists are identical to `HEAD`'s, except for one line:

- complexity: same set of functions over CCN 10 on `HEAD` and on the working tree; none is in `CollabClient.cpp` (`gate-1.log` lines 97-102 are all other files).
- file length: same set on both, except `tests/src/core/CollabTransportTest.cpp`, which is not in `tests/file-length-baseline.tsv`. Under the gate's counting rule it measured **541 code lines on HEAD** (already over 500) and **632** with this change. That file is flagged both before and after; the fix adds 91 code lines to it.

I did not change any gate limit or baseline. Clearing these red gates means splitting pre-existing over-limit files, which is outside this bug. Options for the owner: move the two new cases into their own test file (under 500 lines, with CMake and `fork-sources.txt` registration) so that `CollabTransportTest.cpp` stays at 541, or leave them in the existing file as this report does.

## Files committed

- `src/core/CollabClient.cpp`
- `tests/src/core/CollabTransportTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-25-port.md`

Build note: `CCACHE_TEMPDIR` was set to `/tmp/ccache-tmp-kruzzzzy` for the build commands. It is an environment workaround only and is not part of the code change.
