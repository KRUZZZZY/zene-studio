# cluster new-p8-180 — Restarting a map can accept stale NAT-PMP replies

Verdict: FIXED

## Finding
`src/core/PortMappingPmp.cpp` `PmpMapper::map()` replaced the mapper's state without stopping the
running timer or separating replies from a prior, still-pending attempt. NAT-PMP replies carry no
transaction id, so a delayed reply from the previous attempt could be consumed according to the new
attempt's `m_step` and finish it with stale data.

## Evidence
- `src/core/PortMappingPmp.cpp:64` (`map()`): no `m_timer->stop()`, no socket reset; the same bound
  socket keeps receiving the previous attempt's replies.
- `src/core/PortMappingPmp.cpp:153-169` (`handlePmp`): a NAT-PMP map reply (opcode 130) is accepted
  purely by `m_step`, with no way to tell which attempt it belongs to.
- `include/PortMapping.h:98-100`: single shared `m_socket`/`m_timer` across attempts.

## Fix
In `map()`, when an attempt is still pending (`m_step != Step::Idle`), stop the timer and close the
socket so the following `bind(AnyIPv4, 0)` gets a fresh local port: replies still on the wire for the
previous attempt's requests aim at the old port and become unroutable, so the new attempt can only
consume its own replies. The normal (idle) path is unchanged.

```cpp
if (m_step != Step::Idle) { m_timer->stop(); m_socket->close(); }
if (m_socket->state() != QAbstractSocket::BoundState) { m_socket->bind(QHostAddress(QHostAddress::AnyIPv4), 0); }
```

## Regression test
Added slot `restartMapIgnoresStaleReply` in `tests/src/core/PortMappingCodecTest.cpp` (existing area
test; both files already registered in `tests/fork-sources.txt`). The fake gateway can hold replies
back; attempt 1 (port 41740) is driven to the PmpMap step, then `map(41741,...)` restarts while its
map reply is still outstanding. The stale reply (external 40000 for 41740) is released into the
restarted attempt before its own reply (external 40001 for 41741). The gateway's map reply now
derives external = 40000 + (internal - 41740), byte-identical for the recorded 41740.

## Commands and exit codes (logs in this run's log folder)
- RED on original code: `cmake --build build --target PortMappingCodecTest > LOGS/build-5.log 2>&1` EXIT=0
- `cd build/tests && ctest -R PortMappingCodecTest --output-on-failure > LOGS/test-4.log 2>&1` EXIT=8
  - `FAIL!: restartMapIgnoresStaleReply()  Actual (result.externalPort): 40000  Expected: 40001`
  - 1 ctest test executed; 12 QtTest slots: 11 passed, 1 failed.
- Fix applied to `src/core/PortMappingPmp.cpp`.
- GREEN: `cmake --build build --target PortMappingCodecTest > LOGS/build-6.log 2>&1` EXIT=0
- `cd build/tests && ctest -R PortMappingCodecTest --output-on-failure > LOGS/test-5.log 2>&1` EXIT=0
  - 1 ctest test executed; 12 QtTest slots passed.
- Proof the test catches the bug:
  - `git diff -- src/core/PortMappingPmp.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` (reverted)
  - `cmake --build build --target PortMappingCodecTest > LOGS/build-7.log 2>&1` EXIT=0
  - `cd build/tests && ctest -R PortMappingCodecTest --output-on-failure > LOGS/test-6.log 2>&1` EXIT=8 (FAIL, same 40000 vs 40001 assertion)
  - `git apply LOGS/fix.patch` (re-applied)
  - `cmake --build build --target PortMappingCodecTest > LOGS/build-8.log 2>&1` EXIT=0
  - `cd build/tests && ctest -R PortMappingCodecTest --output-on-failure > LOGS/test-7.log 2>&1` EXIT=0 (PASS)
- Gates: `bash tests/complexity-gate.sh --check` EXIT=0; `bash tests/file-length-gate.sh --check` EXIT=0
- `cmake --build build --target zene > LOGS/build-9.log 2>&1` EXIT=0

## Files committed
- `src/core/PortMappingPmp.cpp` (fix)
- `tests/src/core/PortMappingCodecTest.cpp` (regression slot + held-reply gateway support)
- `docs/qwen-fixes-2026-10/cluster-new-p8-180.md` (this report)
