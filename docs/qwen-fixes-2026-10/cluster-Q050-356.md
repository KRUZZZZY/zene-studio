IMPLEMENTED

# Cluster Q050-356: MIDI 2.0 / UMP support position

Verdict: **IMPLEMENTED** for the documentation and checker scope. The complexity gate was RED on the first commit of this branch (the new checker's `main` at CCN 19, a regression from this work); it was fixed in `e00cd4e984` and is now green (exit 0).

Scope: `docs/MIDI-INPUT-SUPPORT.md` (new, the UMP position), one paragraph in `data/manual/index.html`, `tests/midi-input-support-doc-check.py` (new checker), `tests/CMakeLists.txt` (`MidiInputSupportDoc` registration), `tests/fork-sources.txt` (one line). No code target is touched. Feature record: `docs/050-features/Q050-356.md`.

Commits on `feat/050-Q050-356-oc`: `66b1472a6c` (doc + manual), `c677123693` (wording "not defined"), `cb92829bfd` (checker and test registration), `3bba734922` (feature record and this report), `e00cd4e984` (checker split for the complexity gate), `e546ed8c82` (self-check record), `9083ef8907` (round 2: doc verified against the source), `a13c2fccaf` (round 2: checker enforces the MIDI 1-only text, the backend guard mapping and the manual wording), and the round-2 evidence commit that adds `## Round 2`.

Open questions resolved: the owner has not defined the ALSA / UMP scope. The doc states it as **not defined**; no backend, version or date is promised.

## Acceptance

| Criterion | Command | Exit | Evidence |
| --- | --- | --- | --- |
| Doc states "not yet supported" for UMP, scope "not defined", and names the platform backends. | `python3 tests/midi-input-support-doc-check.py docs/MIDI-LEARN.md` (RED, wrong page) | 1 | 11 failures; `logs/red-1.log` (p2) and `logs/red-3.log` (after the refactor, same 11) |
| Same checker on the real doc passes (checks a–g). | `python3 tests/midi-input-support-doc-check.py` (GREEN) | 0 | `logs/green-2.log` prints OK |
| Same check through CTest. | `ctest -R MidiInputSupportDoc --output-on-failure` in `build/tests` | 0 | `MidiInputSupportDoc` Passed; the ctest run (quiet, 100%) |
| Manual states the same position. | Manual read-through of `data/manual/index.html`, plus round-2 checker check (k) | n/a | The new paragraph says UMP input "is not yet supported, and no date is set for it", and points at `docs/MIDI-INPUT-SUPPORT.md`. |
| Live control surface: `midi.device_list` returns a client and ports. | Headless `build/zene` via `tests/control_socket_harness.py` (offscreen), request `midi.device_list` | 0 | `ok=true`; client `ALSA-Sequencer (Advanced Linux Sound Architecture)`; 3 readable ports: `0:0 System:Timer`, `0:1 System:Announce`, `14:0 Midi Through:Midi Through Port-0`. Transcript: `logs/live-1.log`. The probe script is in `/tmp`, not in the tree. |

Live result detail: the first attempt used a string request id; the server answered `invalid_args` (the id must be an integer) and the probe hung. The retry with integer id `9356001` succeeded. That was a probe mistake, not a product fault.

GUI and command items: N/A. No user action, command id, menu or format was added. `midi.device_list` is pre-existing and was only read.

Real-time: no audio or MIDI thread code is touched; the change is documentation, a manual paragraph and a Python checker.

## Baseline

Command: `ctest -R 'MidiInputSupportDoc|DocFiguresGate|MidiLearn' --output-on-failure` in `build/tests`.

- First run: 2/5 passed; `MidiLearnTest`, `MidiLearnThreadTest`, `MidiLearnGuiTest` were "Not Run" because the binaries had not been built in this worktree (`logs/004-baseline-ctest-356.log`).
- Building those targets once failed with `ccache: Permission denied` on the shared ccache temp dir (`logs/005-build-midilearn-356.log`). Retrying with `CCACHE_DISABLE=1` built them (`logs/006-build-midilearn-356b.log`, exit 0).
- Re-run: 5/5 passed, exit 0 (`logs/007-baseline-ctest-356b.log`). Count is non-zero.

Diff check: `git diff --name-only $(git merge-base HEAD 050/m0-train-35)..HEAD` lists `data/manual/index.html`, `docs/MIDI-INPUT-SUPPORT.md`, `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/midi-input-support-doc-check.py`, and now the two docs in this record. No code target is in the list.

## Gates

| Gate | Command | Exit | Log |
| --- | --- | --- | --- |
| Complexity (first run, before the fix) | `bash tests/complexity-gate.sh --check` | 1 | `logs/gate-1.log`: `REGRESSION: new function over target: main@63-117@tests/midi-input-support-doc-check.py (CCN 19)` |
| Complexity (after `e00cd4e984`) | `bash tests/complexity-gate.sh --check` | 0 | `logs/gate-1b.log`: `PASS (check mode: no regressions; baseline not written)` |
| File length | `bash tests/file-length-gate.sh --check` | 0 | `logs/gate-2.log`; re-run after the fix `logs/gate-2b.log` |
| Doc figures | `python3 tests/doc-figures-gate.py` | 0 | `logs/gate-3.log`; re-run `logs/gate-3b.log` |

The complexity regression came from this branch (the checker's `main`, added in `cb92829bfd`). The gate does not read docs or the manual. The fix split the checks into small functions with the same messages and order.

## Self-check

Submit check: `logs/submit-1.log` (first run, exit 1). Its one FAIL was the complexity regression above, now fixed. The rerun is in `logs/submit-2.log`. Round 2 submit check: `logs/submit-r2.log`.

- Feature record and report exist and are committed: PASS.
- User-manual paragraph added: PASS.
- No allocation, lock, signal or I/O on the audio path: PASS (no audio code touched).
- Tests added or changed: PASS (`tests/midi-input-support-doc-check.py`, `MidiInputSupportDoc`).
- GUI path, GUI test and agent command WARNs: N/A. No user-facing action was added; the only user-visible change is documentation text.
- "Each new test FAILS with the change reverted": the change is a doc, not product code. The doc test's RED is the checker against `docs/MIDI-LEARN.md`: exit 1, 11 failures (`logs/red-3.log`). Reverting the doc itself is the same as running the checker on a page without the position.
- "Every acceptance criterion proven by a test run": criterion 1 is proven by `MidiInputSupportDoc` (ctest exit 0) plus the RED run (exit 1). The manual paragraph is checked by round-2 check (k) and by read-through. The live `midi.device_list` result is in `logs/live-1.log`.
- "Each edit is ONE undo step, reversibility class": N/A. No command was added or changed.
- "Old projects still load and render as before; new data round-trips": N/A. No project, preset or file format was touched.
- Completeness (real user path and agent path): the user path is reading `docs/MIDI-INPUT-SUPPORT.md` or the manual; the agent path is `midi.device_list`, which lists the ALSA client and 3 ports live. Edge cases run: a wrong page (`docs/MIDI-LEARN.md`) fails 11 checks; the real doc passes. Not run: a missing doc path (the `FAIL (a)` branch exists in the code but was not exercised here).
- Qt 5.15 / MSVC / macOS portability: N/A. The change is markdown, a manual paragraph, a Python checker and a CMake test registration; no Qt or C++ API is used.

## Round 2

Reviewer's blocking point (round-1 review, `Q050-356.r1.feature.md`): the test proved nothing. The round-1 checker did not enforce the MIDI 1-only statement, the ALSA/backend platform mapping or the manual wording, so a doc or manual that said the wrong thing could still pass `MidiInputSupportDoc`.

New checks in `tests/midi-input-support-doc-check.py` (docstring items (h)–(k)):

- (h) `## Status` says input is MIDI 1 only; `## Current input path (MIDI 1)` names both paths and "no UMP message types" and does not mention MIDI 2.
- (i) the Current-input section names and cites the raw feeders and the decoded clients; each raw feeder `.cpp` really calls `parseData(`.
- (j) each backend row of `## Supported-platform boundary` names its guard macro; the real `src/core/midi/<Class>.cpp` has `#ifdef <macro>`; the required platform words are present. The backend-to-guard mapping is therefore checked against the source.
- (k) `data/manual/index.html` has a `<p>` about UMP that says "not yet supported", "MIDI 1" and links `docs/MIDI-INPUT-SUPPORT.md`.

Mutant proofs. Each mutant is one edit to a copy of the doc or manual (`logs/make_fixtures.py`); exit 1 means the checker rejects it. Old = round-1 checker, new = round-2 checker.

| Mutant | Edit | Old exit | New exit |
| --- | --- | --- | --- |
| M1 | "Today's input is MIDI 1 only." → "...MIDI 1 and MIDI 2." | 0 | 1 |
| M2 | ALSA sequencer guard `LMMS_HAVE_ALSA` → `LMMS_HAVE_JACK` | 0 | 1 |
| M3 | drop the WinMM row | 1 | 1 |
| M4 | "Win32/Cygwin and Apple" → "Cygwin and Apple" | 0 | 1 |
| M5 | drop the "2. **Decoded-event path.**" line | 0 | 1 |
| M6 | manual: drop "not yet supported" | not run | 1 |
| M7 | manual: drop the doc link | not run | 1 |

Real doc and real doc+manual: old exit 0, new exit 0 (`logs/01-old-checker-exits.txt`, `logs/02-new-checker-exits.txt`).

Other round-2 results:

- ctest `'MidiInputSupportDoc|DocFiguresGate|MidiLearnTest|MidiLearnThreadTest|MidiLearnGuiTest'` in `build/tests`: 5/5 passed, exit 0 (`logs/test-p3.log`).
- Gates, all exit 0: complexity (`logs/gate-complexity-p3.log`), file length (`logs/gate-length-p3.log`), doc figures (`logs/gate-figures-p3.log`), `feature_check.py` (`logs/feature-check-p3.log`).
- Live, default client: `midi.device_list` ok; `ALSA-Sequencer (Advanced Linux Sound Architecture)`, count 4, 3 readable, 1 writable; same as round 1 (`logs/live-p3-default.json`).
- Live, dummy client: isolated HOME; the probe writes `mididev="Dummy (no MIDI support)"` into the harness config. Result: ok; client `Dummy (no MIDI support)`, count 0 (`logs/live-p3-dummy.json`). The probe is in `/tmp`, not in the tree.

Prevention note (narrow): the checker now compares the doc's backend-to-guard mapping with the `#ifdef` lines in the source, and the `parseData` feeders with the code, so drift in this doc fails CI. This is a check on this doc only, not a general rule.
