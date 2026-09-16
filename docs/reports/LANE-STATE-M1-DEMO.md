# LANE-STATE-M1-DEMO — milestone M1 (board task #641), wave 9

**Worktree:** `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms/zene-030/wm1`
(the `lmms/`-prefixed location; the path in the dispatch was missing that prefix).
**Branch:** `030/m1-demo`, based on `a36e8b0d4` (the wave-5 merged tip, = `release/0.3.0`).
**Commits:** `bec4d02a1` (the proof records its source commit), `61cc0543f` (the grid's UI-absence
line in both documents), `272056fad` (the M1 transcript, re-captured on this tree's own build).
No merge, no rebase, no push.

## 1. What the card asked for, and what is where

| Asked for | State | Evidence |
|---|---|---|
| M1 proven end to end through `--control-socket` — a saved project launches 4 clips across 2 scenes in sync at the next bar | **PASS** on this tree's own build of the tip | `tests/control-session-m1-transcript.txt` (committed), verdict PASS |
| a registered ctest | `ControlSessionLaunch`, `tests/CMakeLists.txt:1847`, guarded by `if(LMMS_HAVE_SESSION_VIEW)` | 1/1 passed from `build/tests` |
| the release-configuration flip recorded | `WANT_SESSION_VIEW` **ON** at `CMakeLists.txt:133`; `tests/advertised-features.tsv` `session-view` row **ON** (`required` column, line 114) — both true in the tree, so no second flip commit was needed. Verified against a real binary as well: the gate reports `[PASS] session-view ON matches ON` | see §3 |
| the grid's UI absence in one line in both documents | added to `docs/KNOWN-LIMITATIONS.md` and `docs/RELEASE-NOTES-v0.3.0-alpha.md` | see §4 |

The engine halves this milestone rides on (Follow Actions, Arrangement Record) were already merged
in the tip by `030/session-completion`; this lane added no engine work and no `session.*` ids.

## 2. The M1 proof — commands and results

Build (my own build directory, `build/`, now deleted):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWANT_QT6=ON -DUSE_COMPILE_CACHE=ON
# CONFIGURE EXIT=0   (WANT_SESSION_VIEW is NOT passed: the default must be the thing measured)
cmake --build build -j2   # BUILD EXIT=0
```

The milestone, driven through the socket:

```sh
QT_QPA_PLATFORM=offscreen python3 tests/control-session-m1.py ./build/zene \
    --transcript /tmp/wm1-m1-transcript.txt      # EXIT=0, verdict PASS
```

The transcript's own header, which is the part a verifier needs:

```
  binary          /home/kruzzzzy/.../lmms/zene-030/wm1/build/zene
  binary sha256   a08b7fbdbde110dd682ab4ec620ea76794ea4f3860be637e7d0b7a4fbad51a81
  source commit   61cc0543f2b34601733ae412b7d0cb222ece73fc
  version         "0.2.1-alpha.436+a36e8b0"      <- the build id carries the C++ commit
```

and its assertions (all held): 2 tracks × 2 scenes built through `session.set_grid` /
`session.set_scene` / `session.set_slot` over an empty grid; the project SAVED and the file read
back client-side (`<session>` block, 4 `<clip>` children); REOPENED (grid 2×2, 4 clips); the
transport run and parked inside a bar; both scenes launched (`in_sync: true`, `sync_tick 192` on
both, all four clips on one distinct line); then the engine's own launch state polled to
`completed_launches 4`, `start_line 192`, `start_line_starts 4`, `start_observed 192`,
`dropped_commands 0`.

The registered ctest, from the build tree's `tests/` directory (189 tests registered — not 0):

```sh
cd build/tests && ctest -R "^ControlSessionLaunch$" -V   # EXIT=0
1/1 Test #147: ControlSessionLaunch .............   Passed    4.30 sec
```

Adjacent proofs on the same build, for the stack this milestone sits on:

| ctest | result |
|---|---|
| `SessionFollowTest` | Passed |
| `SessionArrangementRecordTest` | Passed |
| `ReversibilityContractTest` | **FAILED** — see §5 |

Gates run on this tree (no gate, baseline or manifest was weakened, and no test was deleted):

```sh
bash tests/fork-sources-gate.sh      # EXIT=0  (624 fork-NEW, 1103 inherited, 40 tooling, 0 stale)
bash tests/unregistered-tests-gate.sh  # EXIT=0 (167 sources: 165 registered, 2 declared not-built)
bash tests/release-honesty-gate.sh --dump /tmp/wm1-version.txt --artifacts build   # EXIT=1, see §3
```

## 3. The release-configuration flip — what was found

* `CMakeLists.txt:133` — `OPTION(WANT_SESSION_VIEW "..." ON)`: **ON, as the previous lane recorded.**
  The comment above it states the release's own promise ("a session is drivable through
  `--control-socket`, so the release configuration contains it").
* `tests/advertised-features.tsv:114` — `session-view  WANT_SESSION_VIEW  ON  -  …  *`: **ON**, with the
  note at line 29 explaining why the row moved OFF → ON in 0.3.0-alpha, and an explicit statement of
  what it does and does not claim (no clip launcher, no scene launcher, no audio from a launched slot).
* The flip is real in a binary, not only in the file: this lane's configure passed **no**
  `-DWANT_SESSION_VIEW`, and the built binary reports `WANT_SESSION_VIEW='ON'` and
  `LMMS_HAVE_SESSION_VIEW='1'` in `--version`'s build-options line.
* `bash tests/release-honesty-gate.sh --dump … --artifacts build` → `[PASS] session-view  ON matches ON`.
  The gate's overall EXIT=1 is **not** a flip defect: its three FAILs are `vst3-hosting`,
  `vst3-instrument-hosting` and `clap-hosting` — *"WANT_VST3/WANT_CLAP is not reported by this build
  at all"* — because my configure was given no `LMMS_VST3_SDK_PATH` / `LMMS_CLAP_PATH`, so those
  options degraded. The release jobs pass the SDK paths; this build dir did not.

## 4. The grid's UI absence — the one line, both documents

* `docs/KNOWN-LIMITATIONS.md` — new bullet: **"UI absence — one line: the clip-launch grid is drivable
  through the socket, not from the interface."**, with the measurement behind it and the statement
  that the grid UI itself (#598) is out of 0.3.0.
* `docs/RELEASE-NOTES-v0.3.0-alpha.md` — the same line, in the release notes' own register, in the
  Session View section (its M1 bullet also now names the source commit the transcript records).
* The measurement, re-runnable: `grep -rniIE "session\.(set_|launch_|stop_|clear|get_state|follow_|arrangement_|back_to_)" src/gui/`
  returns **zero** matches, as does the same grep for `SessionView|SessionModel|SessionClip|SessionScheduler|SessionFollow`.
  The only `session` spellings in `src/gui/` are `MainWindow`'s crash-recovery `SessionState` and one
  telemetry-consent string.
* Both edits are declared in `tests/upstream-modifications.txt` in the same commit (`61cc0543f`), with
  the file's trailing newline and `#`-prefixed notes intact.

## 5. What is red on this tip (NOT caused by this lane)

`ReversibilityContractTest` fails in the release configuration. Exact output:

```
FAIL!  : ReversibilityContractTest::theTableHistogramIsTheDocumentedOne() 'entries.size() == kRows'
         returned FALSE. (the table has 326 rows, the documented histogram counts 328.
         If a row was added, update the histogram in docs/RELEASE-NOTES-v0.3.0-alpha.md (and here)
         - the point of this assertion is that the two cannot drift.)
```

Measured, with the tree's own probe and this build's own flags
(`DAWPROJECT_PROOF_BUILD=$PWD/build bash tools/dawproject-proof.sh`, `histogram EXIT=0`):

```
joined     rows=262 true_inverse=158 snapshot= 14 irreversible=  7 not_mutating= 83
snapshot   rows= 15 true_inverse=  0 snapshot= 15 irreversible=  0 not_mutating=  0
passive    rows= 73 true_inverse=  0 snapshot=  0 irreversible=  3 not_mutating= 70
stems      rows=  0 (WANT_STEM_SPLIT off)
MEASURED rows=326 true_inverse=158 snapshot=29 irreversible=10 not_mutating=129
```

**Diagnosis.** The constant's base is `{326, 158, 29, 10, 129}` and `#ifdef ZENE_TELEMETRY_ENABLED`
adds `+2 rows / +2 not_mutating` → 328 / 131. But the table *already contains* the two telemetry rows
in this configuration: the running registry answers `control.commands_list` with **326 registered
commands, `telemetry.consent` and `telemetry.status` among them**, the table holds 326 rows, and
`everyRegisteredCommandHasAContractRow` (the other direction) PASSES — one row per command, telemetry
included. So the guard **double-counts** the telemetry rows for this configuration, and the release
configuration's real histogram is **326 / 158 / 29 / 10 / 129**.

**What a fix must do — and must not.** The constant is a measurement, so the fix is to re-anchor it to
what the merged tip measures; it is *not* a gate to weaken and the assertion must stay as it is. Because
the same guard is shared, whoever fixes it must measure **both** telemetry configurations (and the
`LMMS_HAVE_WASM` / `LMMS_HAVE_STEM_SPLIT` ones if those guards are touched). This lane did not touch
the constant: the drift is inherited from the tip, `git log -1 --format=%h -- tests/src/core/ReversibilityContractTest.cpp`
is not this lane's work, and a wrong re-anchor in four configurations is worse than a named red.

## 6. What was NOT verified

* **Not run on a packaged/extracted binary.** There is no AppImage anywhere under the program
  workspace (`find … -iname '*.AppImage'` → nothing), so packaging was not reproducible in this
  window. The proof ran on `build/zene` built from this tree — recorded exactly, not implied. The
  release phase's job re-runs on the AppImage; `ControlSessionLaunch` is registered in the suite, so
  it re-runs there with no changes.
* **No sound is asserted, and none could be.** A launched session slot does not render audio in this
  tree (no session-clip playback path; `src/core/SessionClip.cpp` is serialisation only). The
  transcript says so in its own header; the documents repeat it.
* **The other three A16 configurations** (telemetry off × wasm on/off) were not measured here; only the
  release configuration was (§5).
* **The product version string is `0.2.1-alpha.436+a36e8b0`**, not `0.3.0-alpha`. It is what the tip
  carries (`CMakeLists.txt:65-72`); the release phase bumps it. The transcript quotes the binary, so it
  quotes `0.2.1-alpha…` — that is the measurement, not a mistake in the capture.

## 7. Hotspots

* `hotspot: tests/control-session-m1.py` — the M1 proof is shared by the ctest, the release notes and
  this lane's commits; a later lane editing the claim must re-capture the transcript in the same commit.
* `hotspot: tests/advertised-features.tsv` — the `session-view` row and `CMakeLists.txt:133` must move
  together (the release-honesty gate is what makes that non-optional).
* `hotspot: tests/src/core/ReversibilityContractTest.cpp` — the A16 histogram constant, red on this
  tip (§5); the merged tip must re-measure it.
* `hotspot: tests/upstream-modifications.txt` — both documents edited here are in this ledger.

## 8. The single next action

Re-anchor `ReversibilityContractTest`'s documented histogram to the merged tip's own measurement
(release configuration: **326 / 158 / 29 / 10 / 129**), measuring the telemetry-off configuration in
the same pass so the shared guard is right in both — then re-run
`cd build/tests && ctest -R "^ReversibilityContractTest$"` and expect exit 0.
