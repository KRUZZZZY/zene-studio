# MIDI batch-07 — 2026-10-05

Three findings, all classified from the current tree. No product code was changed.

### Cluster 5148 — JACK MIDI output sends truncated SysEx messages

Verdict: FALSE-POSITIVE

Evidence: `src/core/midi/MidiJack.cpp:238-242` — `sendByte()` refuses to store past
`sizeof(t_outBytes)` (3), so the theoretical claim is that a longer message would be truncated. The
premise is unreachable: the only caller is `MidiClientRaw::processOutEvent`
(`src/core/midi/MidiClient.cpp:350-405`), whose switch emits at most three bytes for any type
(note/key-pressure/velocity; CC number/value; song-position LSB+MSB; time-code byte). SysEx is not
a case in that switch, so it reaches `default:` and emits **zero** bytes plus a `qWarning`
(`MidiClient.cpp:401-403`) — never a fourth byte. No event longer than the 3-byte buffer is ever
assembled, so nothing is queued truncated and JACK never receives a partial event. The comment at
`MidiJack.cpp:240` already records this ("longer messages (SysEx) are not carried by this port").
The suggested "encode variable-length messages fully" would require SysEx support in the base
client and a variable-size realtime-path buffer (no unbounded growth on a JACK callback), which is
out of this lane's rules 6/7.

Commit: none

Test: none

Red/green: none — no change made. Gates run clean: `bash tests/complexity-gate.sh --check` EXIT=0,
`bash tests/file-length-gate.sh --check` EXIT=0.

### Cluster 4229 — Reconnect memory merges distinct ports with the same identity

Verdict: FALSE-POSITIVE

Evidence: the merge at `src/core/midi/MidiReconnect.cpp:134-143` keys on
(port, readable, identity), and `midiPortIdentity()` deliberately strips a real `<d>:<d> ` address
prefix (`MidiReconnect.cpp:98-109`). That is the feature: a device that comes back at a different
address must still be recognised as the same device, which is exactly what
`tests/src/core/MidiReconnectTest.cpp:198` (`theDeviceComingBackAtANewAddressIsReattached`)
asserts — `128:0` -> `131:0` reattaches and counts one reconnect. Including the address in the key
would break that test and defeat address-independent reconnection. The duplicate-name case is also
deliberately handled rather than ignored: `bestMatch()` (`MidiReconnect.cpp:64-79`) counts every
same-identity candidate (`matches`) and prefers the exact remembered name, else the lowest address,
with a comment naming this two-instances-of-one-controller case. Giving each live endpoint its own
record needs a new matching policy — a design change, not a defect fix.

Commit: none

Test: none

Red/green: none — no change made.

### Cluster 1536 — Maximum positive MPE bend falls one cent short

Verdict: FALSE-POSITIVE

Evidence: `src/core/midi/MpeExpression.cpp:127-132` divides by `halfSpan = (MidiMaxPitchBend + 1) / 2`
= 8192 for both directions, so bend 16383 yields `8191 * 48 * 100 / 8192 = 4799` cents. The value is
intentional and already pinned: `tests/src/core/MpeExpressionTest.cpp:73-75` asserts
`pitchCentsForBend(MidiMaxPitchBend, 48) == 4799` under the comment "range itself is (8192 steps down,
8191 up)". Scaling the positive side by 8191 would make the bend curve asymmetric about centre for no
audible gain (one cent at 48 semitones) and would contradict the standard symmetric `offset/8192`
convention; it also requires rewriting that existing assertion, which rule 5 forbids. Symmetry about
the centre is the correct behaviour.

Commit: none

Test: `MpeExpressionTest` (existing, green)

Red/green: `cmake --build build --target zene MpeExpressionTest` EXIT=0, then
`cd build/tests && ctest -R '^MpeExpressionTest$' --output-on-failure` EXIT=0 (1/1 passed) —
confirms the shipped mapping is the asserted one.

Leftovers: none. No commits for this batch other than the report; no half-done edits, and
`git status --short` is empty.