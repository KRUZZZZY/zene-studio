# The control transport on Windows: a named pipe behind the same contract (CODE-9, feature row 83)

`docs/CONTROL-SOCKET-PATH-SAFETY.md` covers the POSIX listener's lifecycle (what the path already
holds, whether it may be bound, which inode is unlinked). This document covers the **Windows half**:
what the transport is, what is identical to POSIX and what is deliberately not, and — because it
matters more than anything else in this file — **which half of the evidence is local and which half
is CI-only**.

## 1. What the transport is

`--control-socket <path>` on Windows listens on a **named pipe**. The path must name one:

```
\\.\pipe\zene-control          # accepted
zene-control                   # refused: invalid_args, the same typed line a launcher already reads
/tmp/zene.sock                 # refused: invalid_args (that is a POSIX path)
```

Everything above the kernel object is the POSIX surface, unchanged:

| | POSIX | Windows |
|---|---|---|
| transport | `AF_UNIX` socket, mode 0600 | named pipe (`\\.\pipe\<name>`) |
| framing | one JSON-RPC request line in, one response line out | **identical** |
| command ids | `control.ping`, `mixer.get_state`, … (the whole registry) | **identical** — the same `ControlRegistry` |
| refusals | `{"id":..,"ok":false,"error":{"kind":..,"message":..}}` | **identical**, including the over-cap sentence |
| start line | `control socket listening on <path>` on stdout | **identical** |
| opt-in | off unless the flag is given | **identical** |
| reachability | local only (nothing listens on the network) | local only: `PIPE_REJECT_REMOTE_CLIENTS` |
| ownership | file mode 0600 | the process's default DACL (creating user + local admins) |
| cleanup | the socket file is unlinked on exit (only if this instance bound it) | the name ceases to exist with the process |
| per-connection | non-blocking fd + `QSocketNotifier`, replies queued when the peer is slow | one thread per connection, blocking-ish OVERLAPPED pipe calls |

Where the code lives:

- `src/core/ControlServerWin32.cpp` — **new**; every line inside `#if defined(Q_OS_WIN)`. Implements
  `listenWin32()`, `closeWin32()`, the accept loop, the per-connection thread and the dispatch bridge.
- `src/core/ControlServerSocket.cpp` — the platform branches of `listen()` and `close()` hand the
  Windows case to those two functions. On POSIX the file's token stream is what it was (proof in §3).
- `include/ControlServer.h` — the Windows members and methods, inside `#if defined(Q_OS_WIN)`.
- `src/core/ControlServer.cpp` — **untouched**: the wire half (framing, `dispatchLine()`) is shared.
- `tests/control-named-pipe-smoke.py` — the smoke test, registered as `ControlNamedPipeSmoke`.

## 2. The design, and why each decision is what it is

**One thread per accepted connection.** The POSIX path is one event loop with a `QSocketNotifier` per
connection; a thread per connection is the same shape on Windows and it keeps every blocking pipe call
off the server's thread.

**Every wait is on an event this code owns, and every pipe operation is OVERLAPPED.** A blocking
`ConnectNamedPipe` or `ReadFile` can only be stopped by closing a handle another thread is inside,
which is undefined behaviour. Instead: `ConnectNamedPipe` is issued overlapped and the accept loop
waits on `{connectEvent, quitEvent}`; each connection's reads and writes are overlapped and wait on
`{ioEvent, quitEvent}`. `closeWin32()` sets the quit event and joins, so nothing is ever closed
underneath a thread.

**`dispatchLine()` always runs on the server's thread.** The registry, the engine and the journal are
served from one thread on POSIX (the event loop), and nothing about Windows makes them thread-safe. A
per-connection thread therefore *posts* the request to the server's thread and waits for the answer —
which is also why the answer is a `shared_ptr`: either side may finish first, and the queued call must
not touch freed memory when the client thread has already given up.

**That wait is bounded, and that is load-bearing.** `closeWin32()` joins every client thread. A wait
for an answer that can never arrive (the instance is closing) must therefore time out and drop the
connection, or the shutdown could not finish. Every 100 ms the waiting thread re-checks the quit flag;
after 30 s it gives up.

**The 1 MiB request-line cap is enforced identically.** Over the cap the connection is sent the same
`invalid_args` refusal the POSIX path sends and the rest of what the peer writes is read and
discarded; only that connection is dropped, the listener is not.

## 3. What is PROVEN locally, and how

The lane that wrote this had **no Windows toolchain**. Three claims can still be proven on a Linux
box, and were. All three are one command; the script is committed so the parent can re-run it:

```
$ bash tests/prove-posix-unchanged.sh
== compile check (borrowed flags, -fsyntax-only, -Werror is in the flags) ==
COMPILES   include/ControlServer.h (EXIT=0)
COMPILES   src/core/ControlServer.cpp (EXIT=0)
COMPILES   src/core/ControlServerSocket.cpp (EXIT=0)
COMPILES   src/core/ControlServerWin32.cpp (EXIT=0)

== POSIX token stream vs release/0.3.0 (-E, line markers dropped) ==
IDENTICAL  include/ControlServer.h (110366 token lines)
IDENTICAL  src/core/ControlServer.cpp (111426 token lines)
IDENTICAL  src/core/ControlServerSocket.cpp (111520 token lines)
SAME-FILE  src/core/ControlServer.cpp (bit-identical to release/0.3.0)

== the Windows TU on POSIX ==
GUARDED    src/core/ControlServerWin32.cpp (0 of its own symbols survive on POSIX)

POSIX-PROOF EXIT=0
```

**(a) The three control translation units still compile under the fork's real flags.** The flags are
borrowed from a sibling tree's `compile_commands.json` — the flags the product build actually uses,
`-Werror` included — because this lane has no build tree of its own (a build is ~25 GB and the owner
directive for this pass forbids spending the window on one). Generated headers (`lmms_export.h`, the
AUTOMOC includes) are the one thing still read out of that tree; pass the built tree as `$1`.

**(b) The POSIX path is byte-for-byte unchanged.** Not "the diff looks small": the token stream the
compiler sees on POSIX is compared against `release/0.3.0`. Each `c++ -E` run is normalised by
dropping line markers, and the old revision is preprocessed out of tree so its own quoted includes
resolve to its own copies. `src/core/ControlServer.cpp` is additionally **byte-identical** to
`release/0.3.0`: the interim revision on this branch had reshuffled its guards, and that reshuffle was
reverted, which is what makes the token-stream proof possible rather than merely plausible.

**(c) The smoke test itself runs**, over the POSIX transport, against a real built binary — the
transport is chosen by the shape of the path (`\\.\pipe\...` → named pipe, anything else → AF_UNIX),
so the framing, the bounds and the expectations are executable where the pipe is not:

```
$ python3 tests/control-named-pipe-smoke.py <built zene> /tmp/zene-smoke-run/zene.sock
PASS: the instance listens on /tmp/zene-smoke-run/zene.sock and a client can connect to it
PASS: the start line is the shared one ('control socket listening on <path>')
PASS: control.ping answers over the pipe (pong, proto 1)
PASS: two requests in one write -> two whole reply lines, in order (no spliced line)
PASS: control.commands_list carries 283 ids over the pipe, including all 7 checked
PASS: a malformed request line is refused with the typed error shape (id -1, invalid_args)
PASS: an unknown command id is refused typed (not_found)
PASS: a request line past the 1048576-byte cap is refused with the same invalid_args line the POSIX path sends
PASS: the listener survives an over-cap connection: a fresh one pings
PASS: a path that is not \\.\pipe\<name> is refused at start-up with the typed line (exit 1)
PASS: control.quit answers over the pipe
PASS: the instance exits after control.quit (code 0)
PASS: the control socket is gone after the instance exits
================ SUMMARY ================
ControlNamedPipeSmoke: PASS
SMOKE_EXIT=0
```

(The transcript prints "over the pipe" in every line because that is the wording of the claims it
checks; run on POSIX by hand it drives the AF_UNIX socket. `283 ids` is the release/0.3.0 tip's own
count, read off the wire; the test asserts the stable id subset it lists, not a frozen number.)

## 4. What is NOT proven here: the Windows half is CI-only evidence

**No part of `ControlServerWin32.cpp` has ever been compiled or executed on the machine that wrote
it.** There is no MSVC, no MinGW and no Qt-for-Windows here. Therefore:

- the named-pipe transport is **compiled and run only by CI** — the `msvc-x64` job (`.github/workflows/build.yml`)
  builds it and `ctest` runs `ControlNamedPipeSmoke` (registered under `if(WIN32 AND PYTHON3_EXECUTABLE)`
  in `tests/CMakeLists.txt`);
- **a green run of that ctest is the Windows verdict**, and it is the first thing anyone should look
  at before believing this feature works;
- if that ctest is red, the failure is in the Windows half by construction: the POSIX half cannot be
  the cause (§3(b) proves it is not).

Known Windows-side risks, named rather than hidden:

1. **First compile.** The file has never seen a compiler. Expect ordinary first-build errors
   (a cast, a `DWORD`/`int`, a flag spelling) — they are cheap and local, and none of them can affect
   POSIX.
2. **`PIPE_REJECT_REMOTE_CLIENTS` / `WaitNamedPipeW` / `CancelIoEx` availability** assumes the Vista+
   SDK the CI uses; the Windows floor of this project is above that.
3. **Runner facts.** Whether the offscreen QPA plugin is present on the runner, and whether `python3`
   is on its PATH (the registration is conditional on exactly that) — the smoke test retries without
   the offscreen platform if the plugin is missing, and skips (exit 77) if it is not given a binary.

## 5. The single next action

Watch the next `msvc-x64` run for `ControlNamedPipeSmoke`. If it is green, the Windows half's verdict
exists and row 83 is done; if it is red, fix the errors it reports **in `ControlServerWin32.cpp`
only** — nothing else in the control surface is in scope, and the POSIX proof in §3 must stay true
(after any edit, re-run it; it fails loudly if a change escapes the `Q_OS_WIN` guards).
