# LANE-STATE — 030/midi-reconnect (0.3.0 feature-list row 18, OWNER-31 item 7)

**Lane:** MIDI controller auto-reconnection.
**Worktree:** `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms/zene-030/wmidi`
**Branch:** `030/midi-reconnect`. Base `6d077931c` (`release/0.3.0`); tip after this pass `2a815b704`.
**Build dir:** `<worktree>/build` — this lane's own; DELETED at the end of this pass (see "Next command").

## The numbers below are from a REBUILT binary

The lane was parked with a binary (`build/zene`, 06:30:22) that predated its own fix batch
(`4592ef133`, 06:33:34), so none of its earlier acceptance numbers counted. This pass rebuilt
from the tip first and re-ran everything:

```bash
cmake --build build -j2                     # BUILD EXIT=0  (twice: the tip, then the inverse fix)
```

## Verified on the rebuilt binary (2026-09-15)

| What | Command | Result |
|---|---|---|
| Full suite (Gate 1) | `ctest` from `build/tests` on this final tree | **138 of 139 pass, SUITE EXIT=8**; the one failure is `ControlCommandsSnapshot` (see below) |
| All gates | `bash tests/run-all-gates.sh` | **GATES EXIT=1**, and the ONLY failing gate is Gate 1, whose single failure is that same derived snapshot; gates 3, 5, 6, 7, 8, 9, 10, 11 PASS, gate 2 SKIP (needs `--with-coverage`). **A lane tree cannot reach the brief's expected exit 3**: the derived snapshot is red by construction until the merge regenerates it, so `3` is a merge-tip number, not a lane number |
| Complexity, fork scope | `bash tests/complexity-gate.sh --check` | **exit 0** — this lane's `MidiReconnect::reconcile` was CCN 11 in the parked tree and is NO LONGER over the target (see "the fourth finding" below); no other regression is this lane's |
| Complexity, whole tree | `bash tests/complexity-gate.sh --check --scope all` | exit 1 — 6 regressions, none of them this lane's (`Song.cpp` loadProject/processNextBuffer, `MidiAlsaSeq::run`/`processOutEvent`, `Track::loadTrack`/`saveTrack`, `MidiClientRaw::parseData`): this lane's `MidiAlsaSeq.cpp` change is 4 lines inside `updatePortList()`, not in those methods |
| File length, fork scope | `bash tests/file-length-gate.sh --check` | **exit 0** |
| File length, whole tree | `bash tests/file-length-gate.sh --check --scope all` | exit 1 — 5 regressions, none of them this lane's (`src/core/midi/MidiAlsaSeq.cpp` 718→871, `Song.cpp` 2022→2104, `Track.cpp` 1043→1119, `src/gui/MainWindow.cpp` 1945→2062, new `tests/src/core/RetroMidiCaptureCommandsTest.cpp` at 558) |
| Engine proof | `ctest -R MidiReconnectTest` (from `build/tests`) | PASSED |
| A16 contract + histogram | `ctest -R ReversibilityContractTest` | PASSED — the histogram constant is this tree's own measurement (187 / 100 / 14 / 4 / 69 telemetry-off base, 189 / 100 / 14 / 4 / 71 in this configuration) |
| **Socket + real-device proof** | `ctest -R ControlMidiReconnect` | **PASSED** (11.7 s) |
| The same, verbosely | `QT_QPA_PLATFORM=offscreen python3 tests/control-midi-reconnect.py build/zene` | **exit 0, 8/8 steps** — see the four measured claims below |
| Release honesty | `bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build` | **exit 0, 6/6 PASS** |

The transcript's four measured claims (its own output, unpiped):

```
engine     : 'ALSA-Sequencer (Advanced Linux Sound Architecture)'
  attached: engine port trk-2 <- 129:0 Zene Reconnect Probe:controller
  +4 event(s) through the binding, 0 from the unbound client
  loss: 129:0 gone, assignment kept, lost=1
  reattached: 129:0 -> 131:0 Zene Reconnect Probe:controller (reconnects=1)
  +4 event(s) through the restored subscription
  mode: disarmed left it dead (0 events), armed brought it back (reconnects=2, +4 events)
  inverse: bound 130:0 Zene Unbound Client:controller, ONE control.undo removed it
=== PASS ===   (TRANSCRIPT EXIT=0)
```

- the engine attached to a REAL external ALSA-sequencer client, and the KERNEL's subscription table
  (`aconnect -l`, matched by the engine's PID) shows it;
- the device was killed by PID, the loss was recorded ONCE with the assignment kept, and the device
  came back at a NEW address (131:0) where the engine re-attached it **without user action** — the
  project's own saved `<midiport inports>` moved too;
- MIDI played by the re-connected client arrived through the restored subscription (+4 events) and a
  client nothing is bound to delivered 0 — the count is gated on the binding, not on "a client exists";
- the mode switch is measured in BOTH directions at the kernel: disarmed, the re-created client's burst
  arrives **nowhere** (0 events); re-armed, it arrives again (+4 events) and the re-attachment is counted.

## Three defects the rebuilt-binary run found, and a fourth red it cleared — all in `2a815b704` and `f0e0b1e`

1. **The recorded inverse of `midi.reconnect_set` was swapped.** `recordBindingInverse` took `detach`
   and forwarded it into `applyBinding`'s `subscribe` position, so the undo of a bind subscribed again
   (leaving the binding on the port) and the redo detached it. Measured before the fix: after ONE
   `control.undo`, the live port still reported `bound: true` and the assignment was still there.
   The engine's `MidiReconnectTest` cannot see this — it drives `MidiReconnect`, not the command.
2. **The proof bound a track the surface cannot address.** The fixture's first `<midiport>` belongs to a
   track inside a Beat/Bassline container, and `trk-<n>` ids resolve over the song container alone
   (`resolveTrack`, `src/core/ControlEditSupport.cpp`): measured, the engine refused with
   `'Jupiter' is not a track id of the form trk-<n>`. The fixture binds the song-container instrument
   track now, and the status reports `trk-2`.
3. **The mode-switch step measured nothing.** A client that returns with the number it just freed inside
   one poll interval is never SEEN to leave, so the engine kept the binding and the "re-attached while
   DISARMED" reading was noise. The step now waits (bounded) for the RECORDED loss first and then
   measures both directions by delivery (0 events disarmed, +4 armed). `check_attached` also reads
   `notice` back from all three commands, so the per-backend claim is measured, not asserted.
4. **`MidiReconnect::reconcile` was over the complexity target** (CCN 11, target 10) and was therefore a
   red line in BOTH complexity scopes at the parked tip. The two non-decisional steps — an assignment
   whose port is gone, and the direction to subscribe — are now `subscribeAssignment()` in the same
   translation unit: CCN 9, the fork-scope gate is **exit 0**, and the whole-tree listing no longer
   names this file. Behaviour is unchanged and the transcript above was re-run on the binary rebuilt
   after the extraction (its output above is from THAT binary).

## Which backends expose hotplug notice HERE — measured, not read

One instance per configured MIDI client (the config file's `audioengine/mididev`), then
`midi.reconnect_status` read back (`/tmp/wmidi-logs/backend_notice.log`, exit 0):

| configured `mididev` | running client | `notice` | ports |
|---|---|---|---|
| ALSA-Sequencer | ALSA-Sequencer | **polled** | 4 |
| ALSA Raw-MIDI | Dummy (fallback) | none | 0 |
| Jack-MIDI | Dummy (fallback) | none | 0 |
| OSS Raw-MIDI | Dummy (fallback) | none | 0 |
| sndio MIDI | Dummy (fallback) | none | 0 |
| Dummy | Dummy | none | 0 |

Also measured on this host: `jackd` absent, `sndiod` absent, `/dev/midi*` absent, `/dev/sequencer`
absent; `LMMS_HAVE_WINMM` undefined in `build/lmmsconfig.h`. **The JACK, ALSA-raw, OSS and sndio
backends cannot be reached here at all, and their own APIs' hotplug behaviour is
unverified-on-hardware** — not measured, not claimed; the same sentence names WinMM and CoreMIDI, which
this platform does not compile. `docs/KNOWN-LIMITATIONS.md` and `docs/RELEASE-NOTES-v0.3.0-alpha.md`
carry exactly that.

## Red / open (state it, do not route around it)

- **`ControlCommandsSnapshot` is RED and that is the ratchet working.**
  `tools/mcp-zene-control/zene_control/commands_snapshot.json` is DERIVED: the binary registers 4 ids
  (`midi.clients_list`, `midi.reconnect_arm`, `midi.reconnect_set`, `midi.reconnect_status`) that the
  committed snapshot does not offer. It is regenerated ONCE, at a merge, from a live instance —
  `python3 tools/mcp-zene-control/snapshot_commands.py --socket <sock>` — never by hand.
- **`include/ControlRegistry.h` is a hotspot at 495 of the 500-line limit.** This lane's two group
  declarations (7 lines) sit there because the lane predates the brief's rule to declare groups in
  `include/ControlRegistryGroups.h`. The file-length gate is GREEN at 495 ≤ 500, but any other lane that
  appends to that header puts the merged tip over the cap. Moving the two `LMMS_EXPORT void
  registerMidiReconnect*` declarations to `include/ControlRegistryGroups.h` (which `ControlRegistry.h`
  already includes, line 38) costs 105 compiles + 105 relinks — measured with
  `make -C build -n -j2 | grep -c ' -c '` — which is why it was left to the merge rather than done here
  after the verification pass. `hotspot: include/ControlRegistry.h — 495/500 with this lane's 7 lines`.
- Any pre-existing red on a file this lane did not touch is not this lane's; the gate run below names
  them.

## Next command for whoever picks this branch up

```bash
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms/zene-030/wmidi
# 1. the branch is verified; the merge is the parent's:
#    merge 030/midi-reconnect into release/0.3.0, then, ON THE MERGE TIP:
bash tests/run-all-gates.sh                                  # expect exit 3 (or 0), never 1
bash tests/complexity-gate.sh --check --scope all ; bash tests/file-length-gate.sh --check --scope all
# 2. regenerate the DERIVED snapshot against a live instance of the merged build:
QT_QPA_PLATFORM=offscreen build/zene --control-socket /tmp/snap.sock &
python3 tools/mcp-zene-control/snapshot_commands.py --socket /tmp/snap.sock
# 3. re-measure the A16 histogram constant (ReversibilityContractTest) on the merged tip:
#    this lane's 187 / 100 / 14 / 4 / 69 is ITS tree's measurement, not a sum.
# 4. move the group's two declarations to include/ControlRegistryGroups.h if any other lane
#    appended to include/ControlRegistry.h.
```
