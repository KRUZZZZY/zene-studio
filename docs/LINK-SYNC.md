# Session sync (`link.*`) — real Ableton Link, and the fallback

Zene Studio syncs with **Ableton Link**: tempo, beat phase and start/stop are
shared with Ableton Live, Bitwig, Reason, iOS apps and every other Link
application on the network, through the **reference library** vendored in
`src/3rdparty/ableton-link` (WF-051, owner decision 2026-10-07). It replaced the
"Link semantics without the Link library" model of 0.3.0 (D11) as the default;
that model is kept as a fallback for builds without the library and for syncing
with Zene 0.3/0.4.

| Part | Where |
|---|---|
| The library (Link 4.1 + standalone asio 1.38.2) | `src/3rdparty/ableton-link/` (`README.lmms`, `MANIFEST.tsv`, licences) |
| The one translation unit that includes it | `src/core/AbletonLinkSession.cpp` (static library `zene_ableton_link`); `AbletonLinkSessionStub.cpp` without it |
| UI-thread backend: tempo both ways, start/stop | `include/AbletonLinkBridge.h`, `src/core/AbletonLinkBridge.cpp` |
| Audio-thread follower: rate and beat grid | `include/LinkAudioFollower.h`, `include/LinkPhaseLock.h`, called from `Song::processNextBuffer()` |
| Facade and the zene-link-style fallback | `include/LinkSync.h`, `src/core/LinkSync.cpp`, `LinkSyncWire.cpp`, `LinkUdpTransport.cpp` |
| Surface | `link.get_state`, `link.set_enabled`, `link.set_quantum`, `link.set_start_stop_sync`, `link.set_session_tempo` (`src/core/ControlCommandsLink.cpp`) |
| Interface | the arranger toolbar's **Ableton Link** toggle (`SessionSyncAction`): peer count and session tempo |

---

## 1. The licence finding, and what is vendored

**Verdict: Ableton Link is GPL-2.0-or-later, compatible with this
GPL-2.0-or-later product, and its one bundled dependency (standalone asio) is
BSL-1.0, which is GPL-compatible. Vendoring both is permitted; it is done.**

**Ableton Link.** `https://github.com/Ableton/link`, tag **`Link-4.1`**, commit
**`9c9091275e707ab09d09a5a608fcdb84bf0dec85`** (2026-08-14; the tag and `master`
named the same commit when it was taken, 2026-10-07). Its `LICENSE.md`, kept
verbatim at `src/3rdparty/ableton-link/link/LICENSE.md` (with the GPL text it
refers to, `GNU-GPL-v2.0.md`), reads in full:

> ```
> # License
>
> Copyright 2016, Ableton AG, Berlin. All rights reserved.
>
> This program is free software: you can redistribute it and/or modify
> it under the terms of the GNU General Public License as published by
> the Free Software Foundation, either version 2 of the License, or
> (at your option) any later version.
>
> This program is distributed in the hope that it will be useful,
> but WITHOUT ANY WARRANTY; without even the implied warranty of
> MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
> GNU General Public License for more details.
>
> You should have received a copy of the GNU General Public License
> along with this program.  If not, see <http://www.gnu.org/licenses/>.
>
> If you would like to incorporate Link into a proprietary software application,
> please contact <link-devs@ableton.com>.
> ```

The grant is GPL-2.0-or-later. The last paragraph offers a separate commercial
licence (Ableton's dual-licensing); it does not restrict the GPL grant, and this
product uses Link under that grant, with the same obligations it already carries
(copyleft, notices kept). Every vendored Link header carries the same grant and
"Copyright <year>, Ableton AG, Berlin" (`ableton/util/Log.hpp` carries the
copyright line only; `LICENSE.md` covers it).

**Its dependency: standalone asio.** Link pins `modules/asio-standalone` as a git
submodule: `https://github.com/chriskohlhoff/asio`, commit
**`8806a6803cde7054c3049d3666d3ec36786568c5`** ("asio version 1.38.2 released",
2026-07-19) - the commit Link-4.1 names, not a newer one. Licence: **Boost
Software License 1.0** (`asio/COPYING`, `asio/LICENSE_1_0.txt`, verbatim). All
548 vendored asio headers carry the BSL-1.0 notice ("Copyright (c) 2003-2026
Christopher M. Kohlhoff"; a few also name a co-author under the same licence -
Rep Invariant Systems, Oliver Kowalke, Boris Schaeling, Stefan Arentz, Roelof
Naude, Klemens D. Morgenstern). BSL-1.0 is permissive and on the FSF's list of
GPL-compatible licences; its one condition - keep the notice and licence with
source copies - is met by keeping both files and every header unmodified. Link's
other bundled code (a Catch copy for its own tests) is not vendored. No other
dependency: on Linux and macOS Link uses the C++ standard library and the OS
socket API; on Windows the system `ws2_32`, `iphlpapi` and `avrt` libraries.

**What is vendored.** Only what a build can reach: the include closure of
`ableton/Link.hpp` and `ableton/link/HostTimeFilter.hpp` (70 headers, every
platform branch kept) and of `asio.hpp` + `asio/system_timer.hpp` (548 headers),
plus the four licence files. Not taken: LinkAudio, Link's tests and examples,
`asio/ssl`, `asio/experimental`, `asio/ts`. **Every file is byte-identical to
upstream**: `MANIFEST.tsv` records each file's git blob sha, which equals the
blob `git ls-tree` reports at the pinned commit (the verify loop is in
`README.lmms`). Local modifications: none.

**The name.** In the interface the feature is called **Ableton Link**, in full,
with its Enabled/Disabled state, as Ableton's Link guidelines (the PDF in the
upstream repository) ask. "Ableton" and "Ableton Link" are Ableton AG's names;
Zene Studio is not affiliated with or endorsed by Ableton.

**Build.** `WANT_ABLETON_LINK` (top-level `CMakeLists.txt`, default ON). A MinGW
cross build from a case-sensitive host is refused with a stated reason (Link's
Windows headers include `<Windows.h>`, which that toolchain spells
`<windows.h>`) and gets the fallback.

## 2. The model: what a session agrees on

### 2.1 Real Ableton Link (the default)

* **A session is every Link application on the network** that has Link on.
  Discovery, the peer set, the shared timeline and the clock synchronisation are
  the library's. Link reports a peer **count** (`numPeers`), not who they are, so
  `link.get_state` reports `peer_count` and an empty `peers` list, and
  `session_revision` / `session_revision_owner` (zene-link-style notions) stay
  empty.
* **Tempo, both ways, through the path a user's change takes.**
  * A **local** change - the tempo dial, `transport.set_tempo`, a script - is seen
    as "the Song's tempo is not what the bridge last wrote or accepted" on the
    25 ms service tick and committed to the session. `link.set_session_tempo`
    declares an exact (fractional) tempo directly.
  * A **peer's** tempo is written with `Song::setTempo` - the tempo dial's own
    call, so frames-per-tick, sounding notes, VST sync and `tempoChanged` follow
    - with the tempo model's journalling suspended.
  * **Undo decision: a peer's tempo is NOT an undo step** and does not by itself
    mark the project modified - like a MIDI clock it is an external source's
    trajectory, and an undo that "restored" a tempo the session has moved on from
    would only be overwritten by the next peer update. A LOCAL change keeps its
    own semantics: `transport.set_tempo` and the dial are journalled as before,
    and `link.set_session_tempo` records its own undo step (which re-declares the
    previous tempo to the session). `AbletonLinkInteropTest` measures both halves
    (`ProjectJournal::canUndo()` false after a peer's tempo, true after a local
    one).
  * **A session tempo is a double, the Song's tempo an integer** (`bpm_t`). The
    Song shows the session tempo snapped to 0.01 BPM (Link's wire carries
    microseconds per beat, so 97.5 arrives as 97.49994) and rounded; the audio
    thread runs the play head at the session's exact rate (§7). `link.get_state`
    reports both (`session_tempo`, `engine_tempo`).
* **Beat and phase.** While the song plays, its play head is held on the
  session's beat grid: position modulo the quantum IS the session's phase (§7).
  A Zene beat is a quarter note (`DefaultTicksPerBar / 4` ticks), which is what a
  Link beat is.
* **The quantum is local** (4 beats by default, 1..64), as it is in Link.
* **Start/stop** (`link.set_start_stop_sync`, Link's start/stop sync). With it on,
  a CHANGE of the session's playing state starts or stops this transport with
  `Song::playSong()` / `Song::stop()` (the play and stop buttons' own calls), and
  this transport's own start or stop is committed to the session. The state a
  session is in when the flag is turned on is left alone (Link: the local
  start/stop state persists when joining). With it off, a peer's start stays the
  peer's.

### 2.2 The zene-link-style fallback model (unchanged)

* **A session is a set of peers.** Every instance has an id
  (`zene-<pid>-<random>`), announces itself every 100 ms, and treats a peer that
  has not been heard for 1.5 s as gone. The peer table is **fixed-size**
  (`MaxPeers = 16`): a full table replaces its stalest entry, so the model's
  memory does not grow with the number of instances on the network.
* **One timeline, in beats.** `beat = anchorBeat + (t - anchorUs) * bpm / 60`,
  with `t` on a monotone clock in microseconds. `anchorUs` is re-anchored at
  every tempo change *before* the change takes effect, so beats are continuous
  across it and no peer's phase jumps. The engine's own beat rate is
  `DefaultTicksPerBar / 4` ticks per beat, which is exactly what this engine's
  tempo means: at 120 BPM a 192-tick bar lasts `192 * 1250 / 120 = 2000 ms`
  (`TimePos::ticksToMilliseconds`), i.e. four quarter notes.
* **Declarations are ordered by `(revision, revisionOwner)`.** A peer that
  declares a tempo takes `highest seen + 1`, so a session converges instead of
  oscillating. Two instances that declare simultaneously with the same revision
  — the only genuine collision, since neither has heard the other — are separated
  by the owner id, which is a **total** order: both sides pick the same winner.
* **The engine's own tempo is the authority on "something changed locally".**
  The model compares the Song's tempo with the value it last applied; if they
  differ, that is a local edit and it is declared to the session. An adopted
  tempo never looks like a local edit, and an agent needs no sync-specific call:
  `transport.set_tempo` reaches every peer on the next announcement.
* **The quantum is local.** 4 beats by default (one 4/4 bar), 1..64 accepted. It
  is this instance's own launch preference, as it is in Link; a peer's quantum is
  reported and not adopted.
* **A session tempo is a double on the wire and an integer in the engine.**
  `Song`'s tempo model is `bpm_t` (`uint16_t`, 10..999 in `include/Song.h`), so
  what is applied is the rounded value clamped to the engine's own bounds, and
  that applied integer is what the model remembers. `link.get_state` reports both
  (`session_tempo`, `engine_tempo`).
* **`start_stop_sync` is a declaration, not an action** in this fallback. The
  flag is announced and reported; nothing starts or stops another instance's
  transport. (The Ableton Link backend acts on it - §2.1.)


## 3. The backends, and how one is chosen

`LinkSyncEngine` (include/LinkSync.h) is the facade every `link.*` command and
the toggle talk to. It picks its backend once, at construction:

* **`ableton-link`** whenever the build carries the library
  (`AbletonLinkSession::compiledIn()`), unless the environment says
  `ZENE_LINK_BACKEND=zene-link-style`;
* **`zene-link-style`** otherwise. It is what Zene 0.3/0.4 speak, so the variable
  is also how to sync with an older Zene build.

`link.get_state.model` and `interop.backend` name the one in use, and
`interop.reason` says why the fallback answers when it does.

### 3.1 Why a backend and not a `LinkPeerTransport`

The 0.3.0 record planned real Link as a second implementation of the
`LinkPeerTransport` seam. It went in one level up instead: Link owns the
timeline, the peer set and the clock synchronisation, not only the wire, so it
replaces the model (revisions, the peer table, the shared-clock arithmetic)
rather than the transport under it. The seam is unchanged and still carries the
fallback.

### 3.2 The zene-link-style transport: `LinkPeerTransport`

`include/LinkPeerTransport.h` is the whole interface: `start/stop`,
`available/reason/endpoint`, `send`, `setReceiver`. `createLinkUdpTransport()`
returns this build's implementation:

* **UDP multicast on `224.76.78.75:20808`** — the group and port Ableton Link
  itself uses for discovery. Link ignores these JSON datagrams (its own messages
  carry the `_asdp_v` header) and this transport ignores Link's, so the two can
  share the group.
* Two instances on one host **both bind the group port** (`SO_REUSEADDR`, plus
  `SO_REUSEPORT` where it exists, its failure ignored) and the kernel loops each
  send back to all of them (`IP_MULTICAST_LOOP`) — a unicast datagram to a port
  two sockets are bound to is delivered to *one* of them, which is why multicast
  is the shape and not a choice. The membership is joined on every interface
  (`INADDR_ANY`) and the group send leaves by the system's default multicast
  interface, so the same code reaches a LAN with no configuration.
* Non-blocking socket read through a **`QSocketNotifier` on the UI thread** —
  the same pattern `ControlServerSocket.cpp` uses. There is no receive thread,
  so there is no lock.
* **`available` is a MEASUREMENT, not a configured socket** (defect fix,
  `030/platform-defects`). Binding the port and joining the group says nothing
  about whether a datagram sent to the group is ever RECEIVED: a host can accept
  both and deliver nothing, and then two instances on one box can never see each
  other while `link.get_state.transport` reports `available: true`. `start()`
  measures it - after the join it opens a **second** socket, configured
  identically (one definition, `LinkUdpTransport::joinGroup`, for both), sends a
  probe from the real socket to the group, and requires that second socket to
  read it inside **250 ms** (`LoopbackProbeBoundMs`). A failed probe makes
  `start()` false, so the transport reports itself unavailable and `reason()`
  names what was measured.
  * **Why a second socket and not a send-to-self.** A socket reading its OWN
    looped datagram proves the kernel loops a packet back to its sender. The
    session needs it delivered to ANOTHER socket - which is what two instances on
    one box are, and what a platform that cannot receive multicast fails to do.
    Measuring the weak property would let exactly the host this check exists for
    pass it.
  * **The answer travels with the claim.** `transport.loopback_probe`
    (`{attempted, delivered, elapsed_ms, bound_ms}`) is the receipt for
    `transport.available`, so a client can CHECK it; `attempted: false` is "this
    transport cannot answer", never "the probe passed".
  * **The cost is bounded and paid once.** A host that can deliver does so in
    well under a millisecond (measured on loopback: 0.1 ms for the raw
    two-socket probe, 0 ms inside the transport, §6), so a healthy host pays
    essentially nothing at `link.set_enabled`; only a host that is about to be
    reported as unable to carry a session pays the whole bound.
* **Windows gets the stub** (for the fallback only; the Ableton backend has its
  own Windows implementation in the library): it reports itself unavailable, with the reason, in
  `link.get_state.transport.reason`. A second socket implementation that this
  lane could not run a single test for is worse than a stated gap; winsock2 is
  the follow-up, and the model above it is unchanged.


## 4. The clock

### 4.1 Real Ableton Link

Link synchronises clocks itself: each peer measures the others' clock offset
(its ping/pong measurement), so peers on different machines share a timeline
without a shared clock. `link.get_state.interop.clock` says so. Zene converts
its sample clock to Link's clock with Link's own `HostTimeFilter` (a linear
regression over the last 512 periods); a reading further than two periods plus
5 ms from Link's clock restarts the filter, because a gap in rendering (an xrun,
a device restart) would otherwise leave it lagging, and a start or stop stamped
in the past loses to a peer's later one.

### 4.2 The zene-link-style fallback: a shared-clock assumption

A packet carries the sender's beat at its send time **and that send time**, both
on `std::chrono::steady_clock` (CLOCK_MONOTONIC on Linux, QueryPerformanceCounter
on Windows — both **system-wide** within a host). The receiver advances the
peer's beat by the datagram's **actual transit time** rather than estimating it,
so after adoption the two timelines agree to within the transit time (microseconds
on loopback) instead of to within a round-trip estimate. This is the one
assumption: **the instances' monotone clocks agree**. That is true for two
instances on one host, and true on hosts whose clocks are synchronised; it is not
true in general across machines, which is precisely the class of thing Link's own
protocol spends its handshake on. `link.get_state` carries the assumption in
`interop.clock` so no reader has to find it here.


## 5. What is NOT here (each is a limitation, not an omission)

1. **No proof against a third-party application.** There is no Ableton Live on a
   CI or development box. The peer in every test is either Zene or the reference
   library itself (`ableton::Link`, what Live and Bitwig embed) - §6 says exactly
   what that proves.
2. **Peers are counted, not named** - Link exposes a count only.
3. **Output latency is approximated.** The host time of a period is its sample
   time plus one engine period plus the device's reported output latency
   (`AudioDevice::outputLatencyFrames()`, 0 for devices that do not report it).
   There is no user latency offset yet, so on a device that under-reports its
   latency Zene sounds that much late against other Link apps.
4. **The tempo map is not followed while a Link session plays**: the session
   tempo wins the frame/tick rate; the map takes over again when the song stops
   or sync is switched off.
5. **The Song's tempo is a whole number**; a fractional session tempo is shown
   rounded (the play head still runs at the exact rate).
6. **A seek while synced lands on the session grid**, at most half a quantum from
   where it was asked to (§7); a start while synced counts in to the session's
   phase (up to one quantum of silence) unless the session is empty or already
   playing with start/stop sync.
7. **Not built for a MinGW cross build** (§1) - that build gets the fallback.
   The Linux build is the one verified here; the Windows (MSVC, MSYS2 clang) and
   macOS builds compile the same vendored sources but were not built in this lane.
8. **No Ableton LinkAudio** (Link 4's audio sharing) - not vendored. Zene-to-Zene audio is a separate stream of our own, `link.audio_*`, which is not transport sync: docs/LINK-AUDIO.md.
9. **Start/stop is not shared during an export render** (an offline render is
   not a transport start).

## 6. Proof, and how to re-run it

### 6.1 Real Ableton Link

All from `<build>/tests`; each is `RUN_SERIAL` (the multicast group is shared
state by design) and skips only on a MEASURED multicast failure (the fallback
transport's two-socket probe on Link's group).

```bash
# the library and its wrapper: two Link participants in one process
ctest -R '^AbletonLinkSessionTest$' --output-on-failure
# Zene's engine against a peer built from Link's own API, period by period
ctest -R '^AbletonLinkInteropTest$' --output-on-failure
# two real Zene binaries in one Link session, through their sockets
ctest -R '^ControlLinkInterop$' --output-on-failure
# the phase-lock arithmetic, and the audio-thread follower's allocation count
ctest -R '^LinkPhaseLockTest$' --output-on-failure
```

What they proved on this host (linux-x86_64, 2026-10-07):

* **The library**: two `ableton::Link` participants in one process form one
  session; a tempo set on either side reaches the other (application-thread and
  audio-thread commits); both read the same phase at the same host time (error
  2e-6 beats); start and stop travel both ways; 64 audio periods of
  capture/commit/host-time filtering allocate nothing.
* **Zene against Link** (`AbletonLinkInteropTest`): Zene joins the peer's
  session and reports `model: ableton-link`; the peer's 133 BPM becomes the
  engine's tempo and is not an undo step (a local `transport.set_tempo` is);
  Zene's `transport.set_tempo` 97 and `link.set_session_tempo` 101.5 reach the
  peer (the engine shows 102); at a **fractional** session tempo of 120.5 BPM -
  which an integer-tempo engine cannot match by running at the same rate - the
  play head was compared period by period with the PEER's own phase at the same
  host time: **worst error 0.0003-0.0004 beats over 200 periods** (stated
  tolerance 0.02 beats = 10 ms at 120 BPM), and the head advanced at the
  session's rate (2.3314 beats against 2.33 by the wall clock); with start/stop
  sync on both sides the peer's start and stop start and stop Zene and Zene's
  reach the peer; with it off on Zene's side a peer's start leaves Zene stopped;
  leaving keeps the tempo. Before the engine integration all seven behaviour
  tests FAILED (no session formed).
* **Two Zene binaries** (`ControlLinkInterop`, transcript with
  `tests/control-link-interop.py ../zene --transcript FILE`): both report the
  pinned library; each counts the other; A's `transport.set_tempo` 133 reaches
  B's engine; B's `link.set_session_tempo` 97.5 reaches A's session (97.49994
  over the wire) and A's engine (98); with start/stop sync A's play starts B, both
  heads stay locked (`interop.audio.locked`) with **worst |phase_error_beats|
  0.015-0.026** in ten samples each (tolerance 0.06: `phase_error_beats` compares
  the session NOW with a play head one period plus the render-ahead in front of
  it), the session tempo is not moved by the start, and B's stop stops A; B leaves
  and A's tempo stays; both quit with 0.
* **What none of it proves**: behaviour against Ableton Live/Bitwig themselves,
  across two machines (Link's clock offset estimation runs, but both peers share
  one clock here), or under a real sound card's latency.

### 6.2 The zene-link-style fallback

Two registered ctests, both from `<build>/tests`, both pinned to the fallback
with `ZENE_LINK_BACKEND=zene-link-style` (tests/CMakeLists.txt):

```bash
# (1) the model + the surface (5 commands, typed refusals, A16 inverses, the
#     revision/phase arithmetic computed independently)
cd build/tests && ctest -R ControlLinkCommandsTest --output-on-failure > log 2>&1; echo EXIT=$?

# (2) THE TWO-INSTANCE PROOF: two real binaries, one session, A drives B
cd build/tests && ctest -R ControlLinkSync --output-on-failure > log 2>&1; echo EXIT=$?

# the transcript itself, with the evidence written to a file:
cd build/tests && QT_QPA_PLATFORM=offscreen python3 ../../tests/control-link-sync.py ../zene \
    --transcript /tmp/link-sync-transcript.txt > log 2>&1; echo EXIT=$?
```

`ControlLinkSync` is registered `RUN_SERIAL`: ctest must not run it beside another
test, because the multicast group is shared state by design and a concurrently
running instance is a legitimate peer. It exits **77** (ctest "Skipped", never
"Passed") when the host cannot carry announcements at all, and names the reason.

**A skip is earned by a measurement, never chosen because an assertion was
inconvenient** (defect fix, `030/platform-defects`). The skip used to cover only
"the socket could not be configured" - the *carry* half - so a host that could
never RECEIVE a multicast datagram did not skip; it failed, with `peer_count: 0`
and nothing naming the cause. Step 2b now requires the other half:
`transport.available` must be the verdict of a loopback probe that was MEASURED
and DELIVERED, and the step makes its own independent two-socket probe on the same
host (`link_sync_evidence.host_multicast_loopback`) so the transport's claim and
the test's measurement can be compared. On this host (`linux-x86_64`,
`enp130s0`) the transport reports
`loopback_probe: {attempted: true, delivered: true, elapsed_ms: 0, bound_ms: 250}`
and the test's own probe `delivered=True elapsed_ms=0`; on a host where nothing
arrives the transport reports `available: false` with the measurement in
`reason`, and this step reports **Skipped**.


## 7. Real-time safety, and the audio-thread follower

`Song::processNextBuffer()` calls `followLinkSession()` once per period: on the
stopped path (so the follower sees a stop) and, while playing, after the tempo
map (`include/LinkAudioFollower.h`). With sync off it is one relaxed atomic load
and the block is byte-identical. Engaged, a period does exactly this:

1. **Host time**: `HostTimeFilter` over the running sample count, plus the output
   latency (§5 item 3), restarted after a rendering gap (§4.1).
2. **One capture -> change -> commit** of the session, as `Link.hpp` prescribes:
   `captureAudioSessionState()` in a local scope on the audio thread, the change
   applied, `commitAudioSessionState()` only if something changed. The change is a
   tempo the UI thread posted through a lock-free mailbox (Link's rule: while the
   audio thread runs it is the one that modifies the session; the
   application-thread commit is used only when it does not), and - with
   start/stop sync - this transport's own start or stop edge (counted from
   `playbackStateChanged`, so a stop and a start inside one period are still
   seen). Among peers a start/stop commits only the start/stop state, never a
   timeline: inside Link's one-second local-modification grace period
   (`link/Controller.hpp`) the audio thread's captured timeline can be older than
   the session's, and committing it put a tempo a peer had since replaced back
   into the session (measured in the two-binary test before the fix: A's 133 BPM
   overwrote B's 97.5 the moment A pressed play). Alone in a session a start maps
   the play head's beat to now (Link's `requestBeatAtTime`), so it is immediate.
3. **While the song plays**: the frame/tick rate for the whole part of the
   session tempo (`Engine::updateFramesPerTickForTempo`, so the engine never runs
   fast), and `LinkPhaseLock` on the play head - in beats, position = session
   beat + a whole number of quanta:
   * a **local start** counts in: the play head holds (nothing renders, nothing
     advances) until the session's phase reaches the play head's - Link's
     quantized launch; joining a session someone else started (start/stop sync)
     catches up to where it would be had it started with the session;
   * **locked**, each period nudges the frame offset toward the target by at most
     5 % of a period - never backward past the tick's first frame, never forward
     past half a frame into the next tick, so no tick is processed twice or
     skipped - which absorbs the whole-BPM rate and the sound card's drift;
   * an error above **1/8 beat** (a seek, an xrun) re-locks to the phase-aligned
     position nearest the play head and jumps there (`Timeline::setTicks`, the
     call the loop wrap already makes on this thread). Sync switched on while the
     song plays re-locks the same way, without a count-in.

**Measured**: `LinkPhaseLockTest::theFollowerPeriodDoesNotAllocate` runs 64
periods of `followLinkSession()` - with a posted tempo, a start edge, a lock and a
re-lock jump among them - and counts **0 allocations** on the calling thread;
an allocation injected into the follower reads 64 and fails it.
`AbletonLinkSessionTest::audioPeriodDoesNotAllocate` does the same for the
wrapper's capture/commit (0; 64 with an injected allocation). The lane counted
through `tests/src/core/LinkRealtimeProbe.h`, its own NON-inline `operator new`
replacement, because the shared `tests/src/core/AllocationProbe.h` then defined
its replacement `inline`, which the standard forbids and which GCC never emits, so
it counted only allocations made inside the test's own translation unit (measured:
an allocation injected into `AbletonLinkSession::audioPeriod()` read 0 through it).
That shared probe was fixed the same way (BUGS_FOUND 13.9, `050/feat-alloc-probe-fix`),
and on the 2026-10-07 feature merge `LinkRealtimeProbe.h` was folded into it: it now
only wraps `allocationsDuring()` over the shared counters, and both tests link
`AllocationProbe.cpp` like every other probe test. The
measurement is headless: with a GUI, `Timeline::setTicks` signals (emitted on a
re-lock jump, as on every loop wrap) reach whatever receivers the GUI connected.

The UI-thread half (`AbletonLinkBridge`, the 25 ms service tick, the
zene-link-style model and its socket notifier) never runs on the audio thread.
