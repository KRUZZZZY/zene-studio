# Cluster 5009 — Active partial requests can time out

Verdict: FALSE-POSITIVE

## What the code does (mechanism confirmed)

- `src/core/ControlServer.cpp:345-348` — `ControlServer::onClientReadable` refreshes
  `it->deadlineMs` only inside `if (buffer.isEmpty())`, i.e. only after a completed
  exchange; a partial read leaves the deadline untouched.
- `src/core/ControlServer.cpp:309-320` — `retireIdleClients` drops any client whose
  `deadlineMs <= now`; the sweep timer is 1000 ms (`src/core/ControlServer.cpp:148`).
- `include/ControlServer.h:93` — `ClientIdleTimeoutMs = 30000`.

So a client streaming one request line in chunks, each within 30 s but totalling more,
is indeed dropped mid-line. The finding describes the code accurately.

## Why it is a false positive anyway

That behaviour is the established design contract, not an oversight: the deadline is an
absolute per-request-line anti-stall policy — a peer gets 30 s to complete a request
line, and only a completed exchange refreshes it. The committed ratchet asserts exactly
the finding's scenario as CORRECT behaviour:

- `tests/control-surface-hardening.py:281-324` (`check_idle_deadline`, BUG-SRV-2b): the
  `stalled` peer sends a partial line at t=0 and more bytes at t=20 (still no newline)
  and the test requires it to be retired by the deadline at t=35 — "A PARTIAL read does
  not extend it; a completed exchange does." That is the finding's own scenario, and the
  test demands the drop.
- `tests/CMakeLists.txt:4215-4217` (registration of `ControlSurfaceHardening`): "a
  request line carries an absolute 30 s deadline that a partial read does NOT extend
  while a completed exchange does."
- Introduced by commit `848041d92` "fix(control): validate channel targets, request ids
  and client bounds; prove BUG-CTL-2/3, BUG-SRV-1/2".

The proposed fix ("refresh the deadline whenever bytes are read") would flip that
committed assertion: the stalled peer's t=20 chunk would push its expiry to t=50, it
would still be alive at t=35, and `check_idle_deadline` would fail
("the stalled peer was not retired by the 30s deadline"). Rewriting that assertion to
match the finding would weaken a committed test, which the rules forbid. No code was
changed.

## Commands run (verbatim, with observed EXIT codes)

From `build/tests` (LOGS = /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5009.r1/logs):

```
ctest -R ControlSurfaceHardening --output-on-failure > "$LOGS/test-1.log" 2>&1   # EXIT=0 (1/1 passed, 38.10 s)
ctest -R ControlSurfaceHardening --output-on-failure > "$LOGS/test-2.log" 2>&1   # EXIT=0
```

From the worktree root:

```
bash tests/complexity-gate.sh --check > "$LOGS/gate-1.log" 2>&1                  # COMPLEXITY_EXIT=0
bash tests/file-length-gate.sh --check >> "$LOGS/gate-1.log" 2>&1                # LENGTH_EXIT=0
```

The committed ratchet passes on the current, unfixed code: the tested behaviour is the
intended one. No regression test was written because there is no bug to reproduce; the
existing test already pins the behaviour the finding would change.

Commit: report only (this file). No source change, no test change.
