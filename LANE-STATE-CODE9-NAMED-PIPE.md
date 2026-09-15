# LANE STATE — CODE-9, the Windows named-pipe control transport (task #671, feature row 83)

Branch `030/code9-named-pipe`, worktree `…/zene-030/wpipe`. Continuation of `52533045e`; this pass adds
5 commits on top of it (the checkpoint commit's unreviewed WIP was **read, revised and partly
reverted** — see §2).

## 1. What is in the tree now

| file | state | what it carries |
|---|---|---|
| `src/core/ControlServerWin32.cpp` | **new** (~475 lines) | The Windows transport: `listenWin32()`, `closeWin32()`, the accept loop, one thread per connection, the bounded dispatch bridge. Every line inside `#if defined(Q_OS_WIN)`. |
| `src/core/ControlServerSocket.cpp` | edited (+15 lines, 484 → 499) | The platform branches of `listen()` and `close()`. On POSIX the token stream is unchanged (proof §4). |
| `include/ControlServer.h` | edited | Windows members/methods, inside `#if defined(Q_OS_WIN)`. On POSIX the token stream is unchanged (proof §4). |
| `src/core/ControlServer.cpp` | **reverted to `release/0.3.0`** | The wire half is shared and must not move. The WIP's guard reshuffle is gone; the file is bit-identical to `release/0.3.0`. |
| `src/core/CMakeLists.txt` | edited | Names the new source in `LMMS_SRCS` (an explicit list, not a glob). |
| `tests/control-named-pipe-smoke.py` | **new** | The Windows smoke test; dual-transport so it can be run by hand on POSIX too. |
| `tests/CMakeLists.txt` | edited | Registers the ctest `ControlNamedPipeSmoke` under `if(WIN32 AND PYTHON3_EXECUTABLE)`; `find_program(PYTHON3_EXECUTABLE)` gains `python3.exe`/`python` fallbacks; the stale "the server refuses on Windows" comment is corrected. `CONTROL_SUITE_AVAILABLE`'s logic is **untouched**. |
| `tests/prove-posix-unchanged.sh` | **new** | The local proof, runnable: borrowed real flags, `-fsyntax-only` on the four TUs, `-E` token-stream comparison vs `release/0.3.0`. |
| `docs/CONTROL-NAMED-PIPE.md` | **new** | The transport, what is identical/absent vs POSIX, what is proven locally, what is CI-only, and the next action. |
| `docs/KNOWN-LIMITATIONS.md`, `docs/RELEASE-NOTES-v0.3.0-alpha.md` | edited | The absence line and the bounds (one section each, appended). |
| `src/core/main.cpp` | edited (5 lines) | `--control-socket` help text + parse comment name the pipe. Strings/comments only. |
| `tests/fork-sources.txt`, `tests/upstream-modifications.txt` | edited | The new file registered; the three touched inherited files' entries carry the CODE-9 reason. |

**Ids / A16 bookkeeping:** this is a **transport**, not a command group — the surface's ids and the
reversibility table are the ones already in the tree, served over a second kernel object. **No new
command id and no new A16 row exists or is needed**, and nothing in `ControlReversibilityTable*.cpp`
was touched. The smoke test reads the id list **off the pipe** and checks it (283 ids on the
`release/0.3.0` tip; the assertion is on the stable subset it lists, not on a frozen count).

## 2. What was done to the WIP this lane inherited

- The WIP's `ControlServer.h` declarations (`listenWin32`, `closeWin32`, `win32AcceptLoop`,
  `win32ClientLoop`, `Win32Client`, …) were kept as the *shape* and rewritten where the design below
  needed it: the dispatch bridge is a **queued** call with a bounded wait, not a
  `BlockingQueuedConnection` (a blocking wait would make a shutdown that joins the waiting threads
  unable to finish).
- `ControlServer.cpp`'s interim guard reshuffle (`#if defined(Q_OS_UNIX)` + trailing `Q_UNUSED(fd)`)
  was **reverted**. It was not needed for the Windows build and it is what made a byte-for-byte
  POSIX-unchanged claim unprovable. The file is now bit-identical to `release/0.3.0`.
- `Client`'s Windows branch was dropped (Windows has no fd map; the transport is a thread per pipe).

## 3. The design, in one line each

One thread per accepted connection; **every** wait is on an event this code owns and every pipe
operation is OVERLAPPED (so `close()` never closes a handle a thread is inside); `dispatchLine()`
**always** runs on the server's thread through a queued call (the surface, the engine and the journal
are single-threaded on POSIX too); the client thread's wait for that answer is **bounded (30 s, quit
polled every 100 ms)** so a closing instance can always join its threads; the 1 MiB line cap and the
over-cap refusal sentence are identical to POSIX; `PIPE_REJECT_REMOTE_CLIENTS` is the local-only flag.

## 4. Evidence (real output, exit codes read unpiped)

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

```
$ python3 <borrowed-flag syntax check> src/core/main.cpp     # the help-text edit
=== src/core/main.cpp -> EXIT=0
```

The smoke test's run above is **over the POSIX transport** (the path's shape selects it); it is
evidence that the *test* is right, not that the pipe works. The binary used is the merge-train tree's
`…/zene-030/build/zene`, borrowed read-only — no build was made in this lane.

## 5. The VERBATIM replacement row text for `docs/FEATURE-LIST-0.3.0.md`

`docs/FEATURE-LIST-0.3.0.md` lives on `030/audit` and was **NOT edited**. Replace row 83 (section 10,
"Plugin hosting") with:

```
| 83 | `CODE-9` — Windows named-pipe control transport | the whole control surface, **unchanged — no new ids and no new A16 rows**: this is a second transport behind the same contract, and its smoke test reads the id list off the pipe (283 ids on the `release/0.3.0` tip, checked against the ids the surface has always carried) | **in the tree** *(landed since the audit)* — `--control-socket <path>` on Windows now listens on a named pipe (`\\.\pipe\<name>`) instead of refusing, and serves the **identical** line-delimited JSON-RPC surface: the same framing, the same command ids, the same typed refusals, the same `control socket listening on <path>` start line, the same 1 MiB request-line cap and the same over-cap refusal sentence. New `src/core/ControlServerWin32.cpp` (every line inside `#if defined(Q_OS_WIN)`: `listenWin32`, `closeWin32`, the accept loop, one thread per connection, a bounded dispatch bridge that always runs `dispatchLine()` on the server's thread); `ControlServerSocket.cpp`'s platform branches hand the Windows case to it and report its refusal through the **same** `fail` lambda the POSIX path uses; `src/core/ControlServer.cpp` (the wire half) is **bit-identical** to `release/0.3.0`. Local-only by construction: the pipe is created with `PIPE_REJECT_REMOTE_CLIENTS`, so a client cannot reach it over `\\<host>\pipe\...`. **The Windows half's verdict is CI-only evidence** — the lane had no Windows toolchain, so the `msvc-x64` job's new `ControlNamedPipeSmoke` ctest (`tests/control-named-pipe-smoke.py`, registered under `if(WIN32 AND PYTHON3_EXECUTABLE)`) is where the transport is compiled and run. What IS proven locally: the three control translation units preprocess **token-for-token** to `release/0.3.0`, and the new Windows TU preprocesses to nothing on POSIX (`bash tests/prove-posix-unchanged.sh`, EXIT=0). Stated limits (`docs/KNOWN-LIMITATIONS.md`, `docs/CONTROL-NAMED-PIPE.md`): no `chmod`/inode equivalent (the pipe carries the process's default DACL), one thread per connection retained until the listener closes, a peer that stops reading a reply blocks that connection's thread (there is no write notifier), and **no local execution of the Windows half at all** | verdict Group A #15; `BACKLOG` § Change-plan register, `CODE-9` |
```

## 6. Remaining acceptance list

| item | state |
|---|---|
| local build + ctest of this worktree | **not run** — no build tree in the lane (owner directive: land the feature, fix builds in the fix-up pass) |
| `tests/prove-posix-unchanged.sh` | **EXIT=0** (§4) |
| `tests/control-named-pipe-smoke.py` over the POSIX transport | **EXIT=0** (§4) |
| `src/core/main.cpp` compiles under the borrowed flags | **EXIT=0** (§4) |
| `ControlNamedPipeSmoke` on the `msvc-x64` job | **not yet run** — this is the Windows verdict, and the single next action (§7) |
| `ControlCommandsSnapshot` | expected red in this lane: the snapshot is derived and regenerated once at a merge |
| `tests/run-all-gates.sh` | not run (its gate 1 needs a build); the cheap gates were run individually |

## 7. The next exact action

Watch the next `msvc-x64` run for **`ControlNamedPipeSmoke`** in `build/tests`:

- **green** → the Windows verdict exists, row 83 is done on this branch;
- **red** → fix the compile/behaviour errors it reports **in `src/core/ControlServerWin32.cpp`
  only** (nothing else in the control surface is in scope), then re-run
  `bash tests/prove-posix-unchanged.sh` — it fails loudly if an edit escapes the `Q_OS_WIN` guards.
