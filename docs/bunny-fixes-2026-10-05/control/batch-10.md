# Control batch 10 — Luna finding triage

Six findings against the control-surface infrastructure. One was fixed; two were
false or already handled; three are real mechanisms whose deterministic failing
test could not be built headlessly in this lane and are left with their evidence.

### Cluster 5021 — Missing stem models report a negative byte count

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlStemModel.cpp:73` and `src/core/ControlStemSupport.cpp:184`
both insert `static_cast<double>(info.size())` / `static_cast<double>(modelInfo.size())`.
The finding rests on `QFileInfo::size()` returning `-1` for a nonexistent file. In
Qt 6 that is no longer true — it returns 0. A probe case
(`testMissingModelReportsZeroBytes`, redirecting `LMMS_STEM_MODEL_DIR` at an empty
`QTemporaryDir`) reported `model_present:false` with `model_bytes:0` and `bytes:0`.
The instrumentation was removed and the test file reverted; no change remains.

Commit: none

Test: none (probe reverted)

Red/green: probe run only, no committed regression.

### Cluster 2208 — Effect undo can dereference a destroyed effect chain

Verdict: ALREADY-FIXED

Evidence: `src/core/ControlStructuralSupport.cpp:397` `recordedChainFinder(EffectChain*)`
returns a closure that guards with `QPointer<EffectChain>` (line 400) or re-resolves
through `resolveChainTarget(args, ...)` (line 401). `journalEffectRemoval` (line 440)
and `journalEffectLoad` (line 465) capture that finder, never a raw pointer, and the
comments say so. The finding describes the pre-`b81a13ec8` code.

Commit: b81a13ec8 (on this branch, before this batch)

Test: none added

Red/green: none

### Cluster 5007 — Oversized complete request lines bypass the line limit

Verdict: FIXED

Evidence: `readChunk` (`src/core/ControlServer.cpp:260-271`) stops appending once the
BUFFER passes `MaxRequestLineBytes`, which bounds the buffer, not the line. A valid
request padded past the cap inside its JSON and then terminated was already complete
at that moment, so `dispatchPendingLines` handed it to dispatch and it ran as a real
command — the cap exists to refuse exactly that. Same gap in
`ControlServer::win32ServeBuffer` (`src/core/ControlServerWin32.cpp:404`). Both now
check `line.size()` against `MaxRequestLineBytes` before dispatch and refuse in the
never-ended line's own vocabulary.

Commit: c975eac22

Test: `control-oversized-request-line.py::case_oversized_complete_line`
("an over-cap COMPLETE request line is refused, not dispatched"). Its own file because
`control-socket-path-safety.py` already sits at the per-file length ratchet; registered
in `tests/CMakeLists.txt` (LMMS via `ControlOversizedRequestLine`) and
`tests/fork-sources.txt`.

Red/green: with the POSIX check short-circuited in-tree (`if (false && ...)`, stash is
guard-blocked): `cmake --build build --target zene` EXIT=0, then
`QT_QPA_PLATFORM=offscreen python3 tests/control-oversized-request-line.py build/zene`
EXIT=1 — "no refusal arrived for an over-cap request line ... the line was buffered
instead of capped". Restored: build EXIT=0, same harness EXIT=0, and
`QT_QPA_PLATFORM=offscreen python3 tests/control-socket-path-safety.py build/zene`
EXIT=0 with all 9 cases passing. Both gates EXIT=0.

### Cluster 5020 — A single oversized transaction exceeds the byte cap

Verdict: REAL-UNTESTED

Evidence: `ControlRegistry::trimTransactions` (`src/core/ControlTransactions.cpp:64-70`)
stops the byte branch at `m_transactions.size() > 1`, so one retained record can
exceed `MaxTransactionBytes` (262144, `include/ControlReversibility.h:439`). The loop
is correct as written and deliberate: the comments at 61-63 state the newest record is
never dropped, because `control.undo` must always be able to read it. The mechanism is
real but I could not show it is reachable. Every before-state producer is capped well
below the total: `ControlSnapshotLimit` = 65536 (`include/ControlDeviceSupport.h:203`)
and `StructuralSnapshotLimit` = 65536 (`include/ControlStructuralSupport.h:83`), so
before plus inverse descriptor for one command sits far under 256 KiB, and
`src/core/ControlCommandsGrooveEdit.cpp:55-80` deliberately avoids unbounded clip note
lists. Auditing all 302 `__transaction` producers for an uncapped before-state was out
of scope for the time budget. The finding's suggested fix ("refuse or explicitly
truncate") is also a design change: it contradicts the never-drop-newest contract that
`ReversibilityContractTest::transactionRecordStatesItsBounds` and the undo path rely on.
Left alone per rule 2.

Commit: none

Test: none

Red/green: none

### Cluster 5009 — Active partial requests can time out

Verdict: REAL-UNTESTED

Evidence: `ControlServer::onClientReadable` (`src/core/ControlServer.cpp:345-348`)
refreshes `it->deadlineMs` only inside `if (buffer.isEmpty())`, while `retireIdleClients`
(`src/core/ControlServer.cpp:310-323`) drops any client whose `deadlineMs <= now` with
`ClientIdleTimeoutMs` = 30000 (`include/ControlServer.h:93`). A client sending a valid
line in chunks, each within 30s but totalling more, is dropped mid-line. The mechanism
is as described and the fix is one line. A deterministic headless RED is not practical
here: it needs >30s of wall clock inside a socket harness whose own timeouts are tuned
for seconds, and the result would be a slow timing test rather than a behavioural one.
Not changed without a failing test, per rule 2.

Commit: none

Test: none

Red/green: none

### Cluster 5012 — Concurrent startup can unlink a live control socket

Verdict: REAL-UNTESTED

Evidence: `openBoundSocket` (`src/core/ControlServerSocket.cpp:82-85`) does
`if (unlinkStale) { ::unlink(address.sun_path); }` and then binds, while the liveness
classification happened earlier and separately in `mayBindPath` (called at
`src/core/ControlServerSocket.cpp:407`). Between the two, another process can bind that
path, so the stale verdict is applied to a live socket. The finding's fix (make removal
conditional on the inode still matching) is correct in principle. A deterministic RED
needs a genuine two-process interleaving with no injection point between `mayBindPath`
and `unlink`, which this lane cannot schedule reliably headlessly. Left alone per rule 2.

Commit: none

Test: none

Red/green: none

Leftovers: 5020, 5009 and 5012 are real code shapes left unfixed and unblocked — 5020
needs an audit of all 302 `__transaction` producers for an uncapped `before` plus a
decision on whether exceeding the total cap should refuse or truncate (a design change
against the never-drop-newest contract); 5009 needs a timeout override or a fake clock to
make the idle deadline testable in seconds; 5012 needs an injection point between
`mayBindPath` and `unlink` (or a second-process race harness) to make the race
deterministic. None is realtime-path work, so all three are fixable in a later lane.