IMPLEMENTED

# Cluster Q050-356: MIDI 2.0 / UMP support position

Verdict: **IMPLEMENTED** for the documentation and checker scope. The complexity gate is RED on this branch (a regression from the new checker, see Gates); that is not a baseline failure.

Scope: `docs/MIDI-INPUT-SUPPORT.md` (new, the UMP position), one paragraph in `data/manual/index.html`, `tests/midi-input-support-doc-check.py` (new checker), `tests/CMakeLists.txt` (`MidiInputSupportDoc` registration), `tests/fork-sources.txt` (one line). No code target is touched. Feature record: `docs/050-features/Q050-356.md`.

Open questions resolved: the owner has not defined the ALSA / UMP scope. The doc now states it as **not defined**; no backend, version or date is promised.

## Acceptance

| Criterion | Command | Exit | Evidence |
| --- | --- | --- | --- |
| Doc states "not yet supported" for UMP, scope "not defined", and names the platform backends. | `python3 tests/midi-input-support-doc-check.py docs/MIDI-LEARN.md` (RED, wrong page) | 1 | 11 failures; `logs/red-2.log` (same run as `logs/red-1.log` from p2) |
| Same checker on the real doc passes (checks a–g). | `ctest -R MidiInputSupportDoc --output-on-failure` in `build/tests` (GREEN) | 0 | `MidiInputSupportDoc` Passed; `logs/doc-green-direct.log` prints OK |
| Manual states the same position. | Manual read-through of `data/manual/index.html` (the checker does not parse the manual) | n/a | The new paragraph says UMP input "is not yet supported, and no date is set for it", and points at `docs/MIDI-INPUT-SUPPORT.md`. |
| Live control surface: `midi.device_list` returns a client and ports. | Headless `build/zene` via `tests/control_socket_harness.py` (offscreen), request `midi.device_list` | 0 | `ok=true`; client `ALSA-Sequencer (Advanced Linux Sound Architecture)`; 3 readable ports: `0:0 System:Timer`, `0:1 System:Announce`, `14:0 Midi Through:Midi Through Port-0`. Transcript: `logs/live-1.log`. Probe script is in `/tmp`, not in the tree. |

Live result detail: the first attempt used a string request id; the server answered `invalid_args` (id must be an integer) and the probe hung. The retry with integer id `9356001` succeeded. That failure is a probe mistake, not a product fault.

GUI and command items: N/A. No user action, command id, menu or format was added. `midi.device_list` is pre-existing and was only read.

Real-time: no audio or MIDI thread code is touched; the change is documentation, a manual paragraph and a Python checker.

## Baseline

Command: `ctest -R 'MidiInputSupportDoc|DocFiguresGate|MidiLearn' --output-on-failure` in `build/tests`.

- First run: 2/5 passed; `MidiLearnTest`, `MidiLearnThreadTest`, `MidiLearnGuiTest` were "Not Run" because the binaries had not been built in this worktree (`logs/004-baseline-ctest-356.log`).
- The three targets failed to build once with `ccache: Permission denied` on the shared ccache temp dir (`logs/005-build-midilearn-356.log`). Retry with `CCACHE_DISABLE=1` built them (`logs/006-build-midilearn-356b.log`, exit 0).
- Re-run: 5/5 passed, exit 0 (`logs/007-baseline-ctest-356b.log`). Count is non-zero.

Diff check: `git diff --name-only $(git merge-base HEAD 050/m0-train-35)..HEAD` lists `data/manual/index.html`, `docs/MIDI-INPUT-SUPPORT.md`, `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/midi-input-support-doc-check.py`. No code target is in the list.

## Gates

| Gate | Command | Exit | Log |
| --- | --- | --- | --- |
| Complexity | `bash tests/complexity-gate.sh --check` | **1** | `logs/gate-1.log` |
| File length | `bash tests/file-length-gate.sh --check` | 0 | `logs/gate-2.log` |
| Doc figures | `python3 tests/doc-figures-gate.py` | 0 | `logs/gate-3.log` |

Complexity gate detail: `REGRESSION: new function over target: main@63-117@tests/midi-input-support-doc-check.py (CCN 19)`, then `FAIL: complexity ratchet regressed`. This is a regression from this branch: the checker's `main` (added in `cb92829bfd`) is over the CCN threshold. The gate does not read docs or the manual. Fix belongs in the checker (split `main` into per-check functions); not done in this packet because the packet says no code changes.

## Self-check

- Feature record exists and matches the Q050-036 shape: yes (`docs/050-features/Q050-356.md`).
- Complexity gate: FAIL (exit 1), regression from the checker's `main`. Not fixed in this packet.
- Baseline ctest: 5/5 green after building the three MidiLearn targets.
- Live `midi.device_list`: run, ok, 3 ports.
- submit_check: see below.
