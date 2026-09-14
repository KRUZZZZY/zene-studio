# Session sync (`link.*`) — the model, the licence record, and what is not here

Zene Studio 0.3.0-alpha, wave W8 (D11 "Ableton Link sync"). Engine half:
`include/LinkSync.h` (the model), `include/LinkPeerTransport.h` (the seam),
`src/core/LinkSync.cpp` (the state machine and the peer table),
`src/core/LinkSyncWire.cpp` (the announcement codec and the two projections of
that state: the packet it publishes and the report `link.get_state` answers
with), `src/core/LinkUdpTransport.cpp` (how an announcement travels); surface:
`src/core/ControlCommandsLink.cpp` (`link.get_state`, `link.set_enabled`,
`link.set_quantum`, `link.set_start_stop_sync`, `link.set_session_tempo`).

**One-line summary.** Two Zene instances on one box join one session over UDP
multicast and one drives the other's tempo and beat phase, through the socket,
with no relay between them — implementing Ableton Link's *semantics* without the
Ableton Link *library*, for the reason in §1.

---

## 1. The licence finding, first, because it decides the shape

**What was read.** `https://github.com/Ableton/link` — the reference
implementation of Ableton Link — default branch `master`, file
**`LICENSE.md`** (the repository's licence file; there is no `LICENSE`). Verbatim,
in full:

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

**Which arm applies to a GPL-2.0-or-later product: the GPL arm, and it is the
only arm in the file.** The grant is GPL-2.0-or-later. The final paragraph is an
**offer of a separately negotiated commercial licence** — Ableton's dual-licensing
model — and it does not alter, qualify or restrict the GPL grant of the source in
that repository. A GPL-2.0-or-later product may therefore use Link under the GPL
arm, with no exception, no clean-room requirement, and the same obligations it
already carries (copyleft, notices retained).

**The consequence, stated plainly, because the brief that commissioned this lane
assumed the opposite:** **vendoring Ableton Link is NOT licence-blocked for Zene
Studio.** It is GPL-2.0-or-later code entering a GPL-2.0-or-later product. The
program's rule (`AGENTS.md` rule 8) forbids vendoring Steinberg VST2 headers and
requires AI dependencies to be MIT/BSD; Link is neither — it is neither a VST2
header nor an AI dependency, and it is copyleft-compatible in its own right.
`LICENCE-AUDIT.md`'s rule for a new vendored tree (licence text beside the code
plus a `README.lmms` recording upstream URL, version, SHA-256, licence and the
files taken) is what a Link vendoring lane would owe, and it is satisfiable.

**And the licence is not why this lane did not vendor it.** The reasons are
scope, not permission:

1. A vendored third-party tree is its own lane: a pinned fetch (the way the VST3
   SDK is handled in `cmake/modules/Vst3Sdk.cmake`), the licence + `README.lmms`
   notices, the build integration on all seven CI platforms, and the review that
   any such tree gets. Folding it into the lane that also builds the model, the
   surface and the two-instance proof would have made the proof the *last* thing
   to land instead of the first thing verifiable.
2. Link's own protocol needs its clock estimation, its own thread and a
   link-time dependency (`asio`) that this tree does not carry; the seam in §3 is
   what makes that a follow-up rather than a rewrite.

**What the follow-up must do, so the record is actionable**: add a second
implementation of `LinkPeerTransport` that speaks Link's protocol (it will
announce a different `model` name, and `LinkSyncEngine::decodePacket` already
refuses any packet whose `model`/`proto` is not this one — so the two can even
run side by side), vendor the library per `LICENCE-AUDIT.md`, and change nothing
in `include/LinkSync.h`. The engine-side contract — session tempo, shared beat,
quantum, peer set — is the same contract; only the wire changes.

---

## 2. The model: what a session agrees on

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
* **`start_stop_sync` is a declaration, not an action.** The flag is announced
  and reported; nothing in this release starts or stops another instance's
  transport, because that would write the audio thread's play state from a
  network announcement. See §5.

## 3. The seam: `LinkPeerTransport`

`include/LinkPeerTransport.h` is the whole interface: `start/stop`,
`available/reason/endpoint`, `send`, `setReceiver`. `createLinkUdpTransport()`
returns this build's implementation:

* **UDP multicast on `224.76.78.75:20808`** — the group and port Ableton Link
  itself uses for discovery, so a future real-Link transport joins the same place
  instead of inventing one.
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
* **Windows gets the stub**: it reports itself unavailable, with the reason, in
  `link.get_state.transport.reason`. A second socket implementation that this
  lane could not run a single test for is worse than a stated gap; winsock2 is
  the follow-up, and the model above it is unchanged.

## 4. The clock assumption, stated because the phase arithmetic rests on it

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

1. **No Ableton Link interoperability.** The library is not vendored (§1 — it is
   *permitted*, not done). A Link-enabled third-party application will not join
   this session and this session will not join it.
2. **No acting on `start_stop_sync`.** Announced and reported, never enforced.
3. **The play head is not repositioned.** `link.get_state` reports the shared
   phase and this engine's own phase with the error between them
   (`phase_error_beats`), and the *tempo* is applied to the engine, but nothing
   moves the audio thread's play position onto the session grid. An aligned
   launch needs the session-launch path (`session.launch_scene`, which already
   schedules to a bar line) to be given a session-derived phase; that is a
   follow-up on top of this model, not a hidden behaviour of it.
4. **Windows cannot announce** (§3).
5. **A session is whoever is on the segment.** Two instances on one box is the
   use case exercised here; a third instance — or a colleague on the same
   network, including a real Link session if one is running — is a peer. The
   tests assert on *the* peer they started, never on a total peer count, so a
   shared network cannot make them lie.
6. **No UI.** See the one-line note in `docs/KNOWN-LIMITATIONS.md` and the
   release notes.

## 6. Proof, and how to re-run it

Two registered ctests, both from `<build>/tests`:

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

## 7. Real-time safety

The engine-side rule (`AGENTS.md` rule 4 — no allocation, no locking, no
unbounded growth on audio-thread paths) holds **by construction** here: nothing
in this feature runs on, or is called from, the audio thread. The model is
serviced by a UI-thread `QTimer` (100 ms), the transport reads its socket through
a `QSocketNotifier` on the UI thread, and the single engine interaction is
`Song::setTempo` — the same call the tempo dial makes. There is no new thread to
synchronise with, and the peer table is a fixed-size, pre-reserved vector.
