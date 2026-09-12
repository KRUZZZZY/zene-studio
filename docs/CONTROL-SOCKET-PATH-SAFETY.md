# `--control-socket <path>` destroyed the file at that path: the root cause, the refusal, and the proof

**Verdict.** Confirmed, reproduced on the release configuration, and fixed. `zene --control-socket <path>`
**unlinked whatever already existed at `<path>` and bound a socket in its place**, silently: a user who
pointed it at a project file (`--control-socket ~/song.mmp` is the obvious mistake) lost that file's
contents, and got no diagnostic at all. The line was `::unlink(address.sun_path);` inside
`ControlServer::openBoundSocket()` — unconditional, before every `bind()`, in the file that the release
advertises as its headline agent surface. The bind site now classifies the path with `lstat()` **and a
liveness probe** and refuses, typed, when the path holds anything that is not a socket nothing is listening
on; it names the offending path on stderr in the protocol's own wire shape; a genuinely stale socket is
still replaced, and now says so; and on exit the server unlinks only the socket it actually bound. Two
sibling defects from the same report — a partial write that could leave half a JSON line on the wire, and a
per-client request buffer with no bound — are fixed with it.

The independent fuzz audit of the same surface (`zene-ctrl-fuzz`, `docs/CONTROL-SURFACE-FUZZ.md`, in flight
while this was written) had already reproduced the reported defect and two faults of the same family: two
instances on one path (F1) and a live *third-party* socket being replaced (F2). Both are closed here, with
tests, because the rule that closes them is the same probe. Its other findings are recorded in §8 with its
reproductions, so the next release inherits them explicitly.

Nothing was pushed and no tag exists; only this worktree was touched.

---

## 1. The defect, and the exact line

```
$ printf 'IMPORTANT PROJECT DATA\n' > /tmp/socktest/song.mmp
$ QT_QPA_PLATFORM=offscreen ./build/zene --control-socket /tmp/socktest/song.mmp
# process starts normally, prints nothing about the file
$ ls -l /tmp/socktest/song.mmp
  -rw-rw-r-- 0 bytes   kind=socket        <-- the file's contents are GONE, replaced by a socket
```

At the release tip this repo built (`post-alpha/integration`, `ba24a9578`), `src/core/ControlServer.cpp`
read:

```
139	::fcntl(fd, F_SETFD, FD_CLOEXEC);
140
141	// A stale socket file left by a crashed instance would make bind() fail.
142	::unlink(address.sun_path);
143	if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
```

The comment is right about the problem it was solving — a socket file left behind by a `SIGKILL`ed run
does make `bind()` fail with `EADDRINUSE`, and nothing else at that path can be bound over without
removing it — and wrong about the remedy: it removed *the path*, not *the stale socket*. `unlink(2)` on a
regular file destroys its last name; the following `bind()` then creates a new inode of type socket at the
same name. The file's contents are unrecoverable, the replacement is zero bytes, and the process prints
nothing (the successful path prints `control socket listening on <path>`, which reads like a normal start).

The blast radius is whatever the caller names: a project (the mistake above), a file reused from a previous
run's path, any asset the user or an agent owns. It is a data-loss defect on the surface the release
advertises as its headline feature, which makes "silently" the worse half of it.

## 2. The rule, and why those two error kinds

`listen()` now classifies the path with **`lstat()`** — never `stat()` — before any fd is created, and acts
on what is actually there:

| What is at the path | What happens | Type on the wire |
|---|---|---|
| nothing (`ENOENT`) | `bind()`, and **no `unlink()` at all** | — |
| a socket nothing is listening on | it is a stale socket: `unlink()`, `bind()`, **and a log line** naming it | — |
| a socket something IS listening on | **refuse**: touch nothing, start nothing | `refused` |
| a directory | refuse: nothing is deleted, and nothing is started | `invalid_args` |
| anything else: regular file, symlink, FIFO, device | **refuse**: no unlink, no bind, no server | `refused` |
| `lstat()` failed for another reason (`EACCES`, …) | refuse (conservative: what is there cannot be known) | `refused` |

Both kinds are already in the surface's closed set (`include/ControlRegistry.h:47`:
`not_found | requires | invalid_args | busy | refused | irreversible` — unchanged by this fix); the choice
is which one carries the right information to the caller.

**A file/symlink/FIFO/device is `refused`**, and the message names the type and the path:

```
control socket: {"error":{"kind":"refused","message":"refusing to use /tmp/sockfix-repro-1000/song.mmp as the control socket: a regular file already exists there, it is not a socket, and starting the server would destroy it; remove it yourself or pass a different path"},"id":-1,"ok":false}
```

**A directory is `invalid_args`, not `refused`** — the question the brief asked to decide explicitly. The
two kinds answer different questions:

* `refused` means *the request is well-formed and I will not do it, because doing it destroys something*.
  The caller's next move is to choose a different path or delete the thing deliberately. Only the caller can
  decide.
* `invalid_args` means *this argument can never work, whatever the state of the world is*. A directory is
  never a socket path: `bind()` cannot create an entry where a directory exists, so the argument is
  permanently wrong, and an agent should fix the argument rather than be told no.

Nothing is deleted in either case, so the split is not about danger; it is about what the caller should do
next, which is the only thing a typed error is for. The classifier does not ask "would the old code have
destroyed it" (that would make the kind depend on a bug's shape), it asks what the caller can resolve: a
*state* is `refused`, a permanently wrong *argument* is `invalid_args`. Everything the caller cannot know
about — an `lstat()` that failed — is `refused`, because refusing to start is recoverable and guessing is
not.

**`lstat()`, not `stat()`,** is part of the same decision. `unlink()` on a symlink deletes the *link*, not
its target, so a symlink is never "a socket, somewhere": treating it as one would delete the user's link
and leave their layout broken. `lstat()` sees the link itself, so a symlink is `refused` and both the link
and its target survive — case 5 of the test.

**A socket file is not a socket somebody is listening on.** This is the fuzz audit's F1/F2 (§8), and the
first cut of this fix got it wrong by calling every socket file a stale leftover. `connect()` on a
non-blocking probe socket is what separates them: `ECONNREFUSED` (or the socket having vanished) means
nothing is there; **everything else** — a successful connect, `EACCES` on a socket owned by another user,
`EAGAIN` on a listener whose backlog is full, or a probe we could not even create — means a listener, and
the start is refused. The asymmetry is the point: being wrong towards "live" costs an exit code and a
message, being wrong towards "stale" costs a running program its control channel. The probe is
non-blocking because a blocking `connect()` to a listener with a full backlog would hang start-up; this
repo's own `tests/control_socket_harness.py` probes a socket the same way.

**The free-path case no longer unlinks at all**, which also closes the start-up race: two instances
starting on the same unused path cannot both bind, and the loser gets `EADDRINUSE` (reported, typed)
instead of silently replacing the winner's socket file.

## 3. How the refusal reaches an agent, when there is no socket to ask

An instance that refused to start has no socket, so it cannot answer `{"id":N,"cmd":...}` — and the whole
point of this surface is that its failures are readable. The refusal therefore goes to **stderr**, in the
exact shape the protocol uses on the wire, before the process returns `EXIT_FAILURE`:

```
control socket: {"error":{"kind":"refused","message":"..."},"id":-1,"ok":false}
control socket: refusing to use /tmp/sockfix-repro-1000/song.mmp as the control socket: ...
```

The first line is machine-readable and is *the same object a client would have received from the socket*
(`id:-1` is the id this server already uses for a reply that belongs to no request — `errorLine(-1, …)` in
`ControlServer.cpp`), so an MCP bridge or a launcher script parses it with the code it already has.
`ControlServer::lastErrorKind()` exposes the kind programmatically. The second line is the existing human
line `src/core/main.cpp` prints, unchanged: **main.cpp is not modified by this fix** (so no ledger entry
and no divergence in inherited code).

## 4. The two sibling defects from the same report

**(a) A partial write could leave half a JSON line.** `writeAll()` returned a bool that the caller
**ignored**. The socket is non-blocking, so a write can accept part of a reply and then `EAGAIN`; the old
code wrote the first half, then wrote the *next* reply's bytes after it on the same connection, where they
read as the tail of the broken line. Now a failed `writeAll()` **retires the connection immediately**, so
the peer reads a partial line followed by EOF — a bounded, diagnosable failure instead of a spliced line.
This is the "atomic per line or refuse" rule from the brief, implemented as *refuse*: the bytes already
written cannot be unsent, and queueing the remainder would mean buffering an unbounded reply.

**(b) The request buffer was unbounded.** `onClientReadable()` appended every readable chunk to a per-client
`QByteArray` and searched for `'\n'` afterwards, so one client that never sent a newline could make the
instance allocate without limit. The cap is `ControlServer::MaxRequestLineBytes` (**1 MiB**), checked
*inside* the read loop so the buffer cannot be grown past it; over the cap the client gets a typed
`invalid_args` naming the cap. Resynchronising (skip to the next `'\n'` and carry on) was rejected: finding
the end of a line of unknown length is precisely what the cap refuses to do.

**The RST trap this uncovered, which is worth writing down.** Writing the refusal and closing immediately
delivers *nothing*: the server has stopped reading with bytes still queued on its socket, and `close()` on a
socket with unread data sends **RST**, which makes the *peer's* kernel discard the refusal it had already
received. The client would see a bare connection error — the same silence this whole change exists to
remove. So an over-cap client is marked `draining`: the refusal goes out first, then everything it sends is
read in 8 KiB chunks and **discarded** until EOF, and only then is the socket closed (a clean FIN, nothing
queued); the capped line's memory is released at that moment. Case 8 of the test is the delivery proof.

## 5. Who owns the path at exit

`close()` used to `unlink(m_path)` unconditionally. With F1's order of events that is the second half of a
zombie: A's socket file is replaced while A runs, A exits, and A deletes the *replacement's* file — after
which the replacement is listening somewhere nothing can reach, with `isListening()` still true. The
server now records the `(device, inode)` it bound and unlinks only if the path still holds it; otherwise it
leaves the file alone and says so:

```
control socket: not unlinking /tmp/…/zene.sock: the path now holds a different file (it was replaced while this instance was listening)
```

Case 7 of the test performs F1's mechanism in the open (unlink our socket, bind a replacement while the
instance is live, then quit it) and asserts the replacement survives.

## 6. The regression test

`tests/control-socket-path-safety.py`, registered as the ctest **`ControlSocketPathSafety`** and in
`tests/fork-sources.txt`. It drives the **real binary** through this repo's own harness — `Instance`,
`Client`, the readiness poll and the assertion helpers from `tests/control_socket_harness.py`; there is no
second client and no invented framing (the protocol is `{"id":N,"cmd":"<id>","args":{},"proto":1}` +
newline, and the harness speaks it). Eight cases, each starting its own instance in its own temp world:

1. **a regular file at the socket path** — exits non-zero; the file is **byte-identical** afterwards
   (sha256 compared, size compared, no socket inode); the typed `refused` refusal names the path; and
   nothing accepts a connection there, i.e. the server was never started. This is the reported defect.
2. **a stale socket** — the fixture `bind()`s and `close()`s a socket, leaving the file exactly as a
   `SIGKILL`ed run leaves it; the instance still starts, `control.ping` answers through the recovered
   socket, and the log carries the unlink line naming the path.
3. **a free path** — binds normally, `control.ping` answers, **no** unlink line and **no** refusal (the
   ordinary path must not regress).
4. **a directory** — exits non-zero, the directory and a file inside it survive, kind `invalid_args`.
5. **a symlink** — `refused`; the link is still a link and its target is byte-identical.
6. **a live listener** — `refused`, the socket at the path is the **same inode** afterwards, and the
   listener still `accept()`s (audit F1/F2).
7. **a socket that replaced ours** — survives our clean exit, and the log says it was left alone
   (audit F1's deletion half).
8. **an over-cap request line** — 2 MiB with no newline: the typed `invalid_args` naming the 1048576-byte
   cap arrives with `id:-1` (the junk was never dispatched as a request), and a well-formed request sent
   afterwards gets **no reply** inside the bound, i.e. the connection is retired rather than having the junk
   quietly buffered.

**It cannot pass by accident.** Run against the same binary built from the pre-fix source
(`ba24a9578`'s `ControlServer.{h,cpp}`, `tests/integration-logs-sockfix/test-prefix-negative-control.log`)
it exits 1 with **7 of the 8 cases failing**, each naming the defect:

```
=== FAIL ===
  a regular file at --control-socket is refused and left byte-identical FAILED (6)
      the file was replaced by a socket: THE DEFECT (0-byte socket)
      the file is now 0 bytes, was 92
      the file is NOT byte-identical: sha256 81e9e20a… -> <unreadable: [Errno 6] No such device or address>
      something ACCEPTED a connection at /tmp/zctl-run-…/zene.sock: the server started on a path it should have refused
  a stale socket is still cleaned up, is reported, and the instance listens FAILED (1)
  a free path still binds normally (no refusal, no unlink line) ok
  a directory at --control-socket is invalid_args, and survives with its contents FAILED (1)
  a symlink at --control-socket is refused, and link and target both survive FAILED (3)
      the symlink at the socket path was removed or replaced
  a live listener at --control-socket is refused, and the listener survives FAILED (4)
      the socket at the path is a different inode: the live socket was replaced (inode 1310797 -> 1310843)
      the foreign listener can no longer accept connections: timed out
  a socket that replaced ours survives our clean exit FAILED (2)
      the instance's exit UNLINKED the replacement socket at the path: a live listener there is now
      unreachable with isListening() still true (F1)
  an over-cap request line is refused typed and the connection is retired FAILED (1)
      no refusal arrived for an over-cap request line inside 10s … (the line was buffered instead of capped)
```

The one case that passes pre-fix is the one that *should*: a free path always bound fine. On the fixed
binary the same file reports all eight `PASS` and exits 0.

## 7. Verification on the release configuration

```
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=RelWithDebInfo -DUSE_WERROR=ON -DWANT_QT6=ON -DWANT_VST3=ON \
      -DWANT_CLAP=ON -DLMMS_VST3_SDK_PATH="$PWD/build-rel/vst3sdk" -DLMMS_CLAP_PATH="$PWD/build-rel/clap"
make -C build-rel -j2
```

(`-j2` on purpose: other lanes were building on this box. The VST3 SDK and CLAP checkouts were copied into
this worktree's own `build-rel/` so no run here reads another lane's build.) Logs and unpiped exit codes:
`tests/integration-logs-sockfix/`. **Configure 0, full build 0** (`build-baseline.log`).

**The exact reproduction, before and after** — `bash tests/integration-logs-sockfix/repro-socket-path.sh
<zene> <label>`, both runs against `build-rel/zene`:

```
== pre-fix (ba24a9578 ControlServer.{h,cpp}) ==
socket path : /tmp/sockfix-repro-1000/song.mmp (32 bytes)
before      : mode=-rw-rw-r-- size=23 sha256=32f6b10007968b9c22e7fa826b916eb853ce2465ead977f3940c8070726e0ff9
exit        : 124 (timeout: the process was still running)
after       : mode=srw------- size=0 sha256=
kind        : socket
-- stdout --
control socket listening on /tmp/sockfix-repro-1000/song.mmp
```

```
== post-fix (fix/control-socket-path-safety) ==
socket path : /tmp/sockfix-repro-1000/song.mmp (32 bytes)
before      : mode=-rw-rw-r-- size=23 sha256=32f6b10007968b9c22e7fa826b916eb853ce2465ead977f3940c8070726e0ff9
exit        : 1
after       : mode=-rw-rw-r-- size=23 sha256=32f6b10007968b9c22e7fa826b916eb853ce2465ead977f3940c8070726e0ff9
kind        : regular file
-- stderr --
control socket: {"error":{"kind":"refused","message":"refusing to use /tmp/sockfix-repro-1000/song.mmp as the control socket: a regular file already exists there, it is not a socket, and starting the server would destroy it; remove it yourself or pass a different path"},"id":-1,"ok":false}
control socket: refusing to use /tmp/sockfix-repro-1000/song.mmp as the control socket: a regular file already exists there, it is not a socket, and starting the server would destroy it; remove it yourself or pass a different path
```

Bytes in, bytes out, same sha256 — and the process says which path it refused and why. Note the pre-fix
run needed the 30-second `timeout`: it *kept running* with the socket bound over the project, which is why
the report's reproduction looked like a normal start. (`sha256sum` on the pre-fix result fails with ENXIO —
"No such device or address" — because the path is a socket by then; that is recorded verbatim.)

**ctest** (`build-rel/tests`, `ctest --output-on-failure`, `ctest.log`):
**87/87 passed, 0 failed, 134.18 s, exit 0.** The release tip's list is 86 tests; the only addition is
`ControlSocketPathSafety` (tests/CMakeLists.txt in this change), so the pre-existing 86 are all still
green.

**Both gates**, exit codes unpiped:

| Gate | Command | Exit | Result |
|---|---|---|---|
| 9 (fork sources) | `bash tests/fork-sources-gate.sh` | **0** | 243 fork-NEW, 1036 inherited, 34 tooling, 0 stale |
| 6 (no upstream regression) | `bash tests/no-upstream-regression-gate.sh` | **0** | 443 changed paths declared; no undeclared divergence |

No upstream-inherited file is touched, so `tests/upstream-modifications.txt` is unchanged: the two changed
product files (`include/ControlServer.h`, `src/core/ControlServer.cpp`) are fork-NEW and already in
`tests/fork-sources.txt`.

**`python3 tests/scripted/check-namespace`: 4 errors, exit 1 — the same 4 as before this change**
(`include/ScriptLuaQtTypes.h`, `src/core/ControlDeviceHosted.cpp:139`,
`src/core/ControlDeviceHosted.cpp:245`, `tools/ncpu-shim.c`), i.e. the known unfixed ones. Nothing in this
change appears in the list.

## 8. The fuzz audit's findings, and what happened to each

`docs/CONTROL-SURFACE-FUZZ.md` (worktree `zene-ctrl-fuzz`, branch `audit/control-surface-fuzz`, read-only
here — never merged). Its headline was that the in-flight path fix, as first written, left the same family
of faults open; that was correct, and the fix now covers two of the five:

| # | Finding | Status after this change |
|---|---|---|
| 1 | **Two instances on one path** (F1): B takes A's path, both print "listening", A's exit unlinks B's socket and leaves B unreachable with `isListening()` true | **CLOSED.** B now probes, finds A live, and refuses, typed (test case 6); and A's exit no longer unlinks a path that is not the inode A bound (test case 7). The start-up race is closed too: the free path is never unlinked, so the loser of a simultaneous bind gets `EADDRINUSE` |
| 2 | **A live third-party socket is destroyed** (F2): `--control-socket` at `S.gpg-agent`, an ssh-agent's socket, `docker.sock`, `/tmp/.X11-unix/X0` replaces it | **CLOSED** by the same probe. Not separately measured: the `EACCES` arm of the probe (a socket owned by another user) is *reasoned* — every errno other than `ECONNREFUSED`/`ENOENT` counts as live — and the audit's foreign-listener case ran as the same user, which the probe covers by a successful connect |
| 3 | **The 0600 mode is pinned once and never re-verified** (F4): `chmod 0666` by a third party sticks forever; the audit measured `mode at listen=0o600; after chmod 0666=0o666; 0.5 s later 0o666; after a normal command 0o666; a new client still connected` | **OPEN, unchanged.** Re-verification needs a periodic check or an `fstat()` on every accept, plus a decision about what to do when it fails; out of scope here. Also still true, from the code rather than a measurement: between `bind()` and `chmod()` the socket briefly has `0777 & ~umask` |
| 4 | **The client COUNT is unbounded** (F5): `64 clients x 1 MiB unterminated: rss 90336 -> 156104 KB (+65768 KB), fds=121` | **HALF CLOSED.** The per-client line cap is this change (1 MiB); the number of clients is not capped, so N clients still buy N × 1 MiB, and 1,024 clients (inside a default `RLIMIT_NOFILE`) would buy 1 GiB. A connection cap belongs beside the line cap, in a follow-up |
| 5 | **`control.undo`'s claim shape** (F6): on an empty journal it answers `ok:true` for a no-op, and the *second* undo answers `ok:true, undone:false` while still carrying `undone_command:"mixer.add_channel"` — the command already undone | **OPEN, untouched.** It is the undo surface's contract, not the socket's. The audit's own correction is worth keeping: `undone:true` with an empty command did **not** reproduce; what reproduces is the shape above |

The same audit also lists `id` truncation (fractional and ≥2³¹ ids collapse to the same reply id as a
framing error), a meaningless message for a valid non-object line, and a blank line that draws no reply and
no disconnect. None is touched here; all three are in that document with reproductions.

## 9. What this deliberately does not fix

* **A `--control-socket` path longer than 107 bytes.** `sockaddr_un::sun_path` is the limit and the check
  was already there; it is now a typed `invalid_args` naming both numbers instead of a bare message. (This
  is not hypothetical: `tests/integration-logs-sockfix/` plus this worktree's path is 126 bytes, which is
  why the reproduction script keeps its fixture in `/tmp` and the log here.)
* **The `EACCES` probe arm is reasoned, not measured** — see finding 2 above.
* **The write path is still "refuse" rather than "queue".** A short write retires the connection instead of
  buffering the remainder and arming a write notifier. Queueing is friendlier and a real change to the
  client lifecycle; the atomicity invariant (never a truncated line followed by other bytes) is what the
  defect needed.
* **The refusal is reported twice on stderr** — the typed line from `ControlServer`, then `main.cpp`'s
  existing human line. Removing the duplicate would mean editing upstream-inherited `src/core/main.cpp`
  (and ledgering it) for a cosmetic gain; the duplication is deliberate.
* **The `lstat()`-to-`unlink()` window is narrowed, not closed.** If a stale socket is replaced by a regular
  file between our `lstat()` and our `unlink()`, that file is still removed. The window exists only on the
  stale-socket path (a free path is never unlinked, and a live socket is never unlinked), and closing it
  needs directory-level locking.
* **1 MiB is a chosen cap, not a derived one.** No command in the surface was measured to need more; a
  request line over it is refused rather than truncated, so the change cannot silently mangle a request.
* Findings 3, 4 (the client count) and 5 of §8, the audit's `id`/message/blank-line findings, and the four
  `check-namespace` errors listed in §7.
