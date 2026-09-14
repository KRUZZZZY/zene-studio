# LANE-STATE — 030/midi-reconnect (0.3.0 feature-list row 18, OWNER-31 item 7)

**Lane:** MIDI controller auto-reconnection.
**Worktree:** `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms/zene-030/wmidi`
**Branch:** `030/midi-reconnect` (base tip `6d077931c` = `release/0.3.0`).
**Build dir:** `<worktree>/build` (this lane's own; ~18 GB, deleted or declared at the end).

## Done (committed)

- `26b337186` `feat(midi): controller auto-reconnection …` — the engine, the
  `midi.*` control group (4 ids), the two proofs, the A16 rows, the histogram
  figures, the UI-absence lines, the upstream-modification declarations.
- `409c2005f` `chore(manifests): fork-sources.txt from its own recipe` — the
  entry I wrote by hand for `tests/src/core/MidiReconnectTest.cpp` is NOT
  derived there (the recipe's awk keep-list is an INCLUDE list); the test is
  whole-tree material and lives in `tests/all-sources.txt`. Both manifests now
  reproduce: `diff <(grep -vE '^\s*(#|$)' <file>) <(recipe)` prints nothing.

### What was built

| Piece | Where |
|---|---|
| Identity helpers + the assignment memory (`MidiReconnect`) | `include/MidiReconnect.h`, `src/core/midi/MidiReconnect.cpp` |
| The memory on the client + `noticesPortChanges()` | `include/MidiClient.h` |
| The subscription records (or forgets) the identity | `src/core/midi/MidiPort.cpp` |
| The poll that re-attaches | `src/core/midi/MidiAlsaSeq.cpp` (`updatePortList()` → `reconcile()`), `include/MidiAlsaSeq.h` |
| The command group | `src/core/ControlCommandsMidiReconnect.cpp` (status, clients_list), `...Edit.cpp` (arm, set), `...Shared.h` |
| A16 row (true_inverse, recorded action) | `src/core/ControlReversibilityTableMidiReconnect.cpp` + the join in `...Action.cpp` |
| A16 rows (3 × not_mutating) | `src/core/ControlReversibilityTablePassive.cpp` |
| Engine proof | `tests/src/core/MidiReconnectTest.cpp` (registered QTest, no device) |
| Socket + real-device proof | `tests/control-midi-reconnect.py` (registered ctest `ControlMidiReconnect`), `tests/midi_reconnect_flows.py`, `tests/midi_reconnect_probe.py` (the external ALSA client) |

## Red / not yet verified

- **The build had not finished when this file was written.** Everything below
  that needs the binary is UNVERIFIED until it does.
- Every acceptance command in the brief is still to be run (see the list below).
- The MCP command snapshot (`tools/mcp-zene-control/zene_control/commands_snapshot.json`)
  has NOT been regenerated yet: it needs a live instance of THIS build
  (`python3 tools/mcp-zene-control/snapshot_commands.py --socket <sock>`).
  Until it is, `ControlCommandsSnapshot` is expected to be RED with the four new
  ids missing, which is the ratchet working, not a defect to route around.

## The exact next command

```bash
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms/zene-030/wmidi
tail -3 /tmp/wmidi-logs/wmidi-build2.log; grep -n "error:" /tmp/wmidi-logs/wmidi-build2.log | head
```

(If it says `BUILD EXIT=0` and prints no `error:` lines, run the acceptance
block below in this order.)

## Remaining acceptance list

```bash
bash tools/local-ci.sh --build-dir build --jobs 2                  # ctest MUST be 100%; 0 tests is an ERROR
cd build/tests && ctest -R 'MidiReconnectTest|ControlMidiReconnect|ControlCommandsSnapshot|ReversibilityContractTest' --output-on-failure
bash tests/run-all-gates.sh                                        # exit 3 or 0, NEVER 1
bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build   # 6/6
bash tests/complexity-gate.sh --check ; bash tests/file-length-gate.sh --check ; bash tests/duplication-gate.sh
bash tests/fork-sources-gate.sh ; bash tests/no-upstream-regression-gate.sh
bash tests/unregistered-tests-gate.sh ; bash tests/evidence-gate.sh ; bash tests/no-tautology-gate.sh
bash tests/complexity-gate.sh --check --scope all ; bash tests/file-length-gate.sh --check --scope all
# snapshot (needs the built binary):
QT_QPA_PLATFORM=offscreen build/zene --control-socket /tmp/wmidi-logs/snap.sock &   # then:
python3 tools/mcp-zene-control/snapshot_commands.py --socket /tmp/wmidi-logs/snap.sock
git add tools/mcp-zene-control/zene_control/commands_snapshot.json
```

## Bounds this lane could not measure (stated, not hidden)

- Only the ALSA-sequencer client publishes port-list changes in this build
  (`MidiAlsaSeq::noticesPortChanges()`); JACK/WinMM/CoreMIDI behaviour was NOT
  measured and is NOT claimed — the engine reports `notice: "none"` for them and
  `docs/KNOWN-LIMITATIONS.md` says so.
- `aplaymidi` cannot supply the proof's external client (it requires `--port`,
  i.e. direct addressing, which bypasses subscriptions — measured: exit 1,
  "Please specify at least one port with --port"), so the lane's own probe
  process is the external client.
