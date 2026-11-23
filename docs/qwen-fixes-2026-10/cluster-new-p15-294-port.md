FIXED

# cluster new-p15-294-port: Applying a snapshot can overwrite an unrelated project file

new-p15-294-port: FIXED

## Decision: REAL on this branch

Before this change, `CollabSession::applySnapshot` (src/core/CollabSessionSync.cpp:219-237 on the branch
base) wrote every received snapshot to a fixed name, `snapshotPathFor(m_client->hostAddress())`, which is
`<userProjectsDir>/collab/joined-<host>.mmpz` (old `snapshotPathFor`, src/core/CollabSessionSync.cpp:83-91).
It wrote through a `QSaveFile`, which replaces whatever is at that path. So a project the user had saved
there, or the copy left by another join to the same host (a second Zene instance or an earlier session),
was overwritten without any check.

The fix was made on `bugs/qwen-new-p15-294` (commits 5aa286cb5a..6f47f3bdc0) but was never part of this
branch: `git merge-base --is-ancestor 47cfb0c64b HEAD` reported NOT in HEAD. This job ports it. The product
patch applied cleanly (`git apply --check` EXIT 0). The test patch applied with
`--exclude=tests/upstream-modifications.txt`, and its six registration lines were appended to the current file.

## The fix

- include/CollabPolicy.h, src/core/CollabPolicy.cpp: `snapshotDestination()` names the copy
  `joined-<sanitised label, max 64>-<per-join token>.mmpz` and tries the bounded siblings `-2..-64`, never
  returning an existing path. `reserveSnapshotFile()` and `reserveSnapshotDestination()` create the name
  atomically with `QIODevice::NewOnly` (O_CREAT|O_EXCL). A name another process took after it was selected
  is skipped and left untouched.
- include/CollabSession.h, src/core/CollabSession.cpp: each join makes a 12-hex `m_snapshotToken` (QUuid).
  `m_snapshotPath` and `m_snapshotFile` hold the reservation for the whole join. `resetToIdle()` clears all three.
- src/core/CollabSessionSync.cpp: `applySnapshot` reserves on the first publication. Each later resync
  truncates and rewrites through the same handle. Before `project.open`, it checks that the path still holds
  exactly the bytes it wrote (`fileHoldsSnapshot`). If another file has replaced it, the handle is dropped
  and a fresh name is reserved, with at most 4 attempts. `QSaveFile` (replace-on-commit) is no longer used.

Callers and siblings: `applySnapshot` is the only writer of joined copies. It is reached only through
`m_client->setSnapshotSink` (src/core/CollabSession.cpp, `join`). `writeSnapshot` (host side) writes into a
`QTemporaryDir` and was not affected. This path does not run on the audio thread, adds no control commands,
and does not touch undo. Projects already on disk load as before, because only the name of a newly
received copy changes. An existing legacy `joined-<host>.mmpz` file is no longer overwritten.

## Regression tests

- tests/src/core/CollabPolicyTest.cpp: `snapshotDestinationNeverReplacesAnExistingFile`,
  `snapshotReservationNeverReplacesAFileCreatedAfterSelection` (plus sanitising, sibling-walk, exhaustion
  and creation cases).
- tests/src/core/CollabTransportTest.cpp / CollabTransportTestParts2.h:
  `applySnapshotUsesUniqueDestinationInSource` (source contract: no `snapshotPathFor` or `QSaveFile`;
  reservation goes through `reserveSnapshotDestination` and `m_snapshotFile`).
- tests/control-collab.py: the control-socket scenario from the original branch was ported. I did not run
  it this session.

## Commands (logs in ~/.config/kads/zene/state/runs/new-p15-294-port.r2/logs)

| step | command | EXIT |
|---|---|---|
| build | `cmake --build build --target zene CollabPolicyTest CollabTransportTest -j4 > build-1.log` | 0 |
| test | `ctest -R 'CollabPolicyTest\|CollabTransportTest' --output-on-failure > test-1.log` (100% tests passed out of 2) | 0 |
| 4b reverse | `git diff -- include/CollabPolicy.h include/CollabSession.h src/core/CollabPolicy.cpp src/core/CollabSession.cpp src/core/CollabSessionSync.cpp > fix.patch && git apply -R fix.patch` | 0 |
| red build | same build > build-2-red.log (`CollabPolicyTest.cpp:194:40: error: 'snapshotDestination' was not declared`) | 2 |
| red test | same ctest > test-2-red.log (`FAIL! CollabTransportTest::applySnapshotUsesUniqueDestinationInSource() ... snapshotPathFor helper is still present`; 50% passed, 1 failed of 2) | 8 |
| 4b restore | `git apply fix.patch` | 0 |
| green build | same build > build-3-green.log | 0 |
| green test | same ctest > test-3-green.log (100% tests passed out of 2) | 0 |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > gates-1.log` (all PASS, RESULT: PASS) | 0 |

Note on red: without the product change, CollabPolicyTest does not compile because the API it tests does
not exist. That is a genuine failure, but its ctest row in test-2-red.log ran the stale green binary. The
runtime red evidence is the CollabTransportTest failure.

## Self-check

- Verdict is backed by evidence from this session: the old `snapshotPathFor` + `QSaveFile` code was read
  at src/core/CollabSessionSync.cpp:219-237 before the port.
- Red/green: the EXIT codes in the table come from the logs listed.
- Registration: all product files are already in tests/fork-sources.txt (lines 1067-1068, 1674-1677). The
  upstream-modifications lines are carried over from the original branch, and the registration gates PASS.
- Realtime: none of the changed code runs on the audio thread (collab session, main thread).
- Unverified: tests/control-collab.py was not run here. TSan is not available on this box. The
  reservation race is handled by O_EXCL plus the byte check after writing, and is argued from the design only.

Delegation: Haiku ran every build and ctest step and the 4b reverse/restore sequence (build-1..3, test-1..3).
I did not use Sonnet: this job was a clean port of an existing reviewed fix (`git apply`), with no new test
or fix to design.

## Other findings

- None.
