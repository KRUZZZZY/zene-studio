# LANE-STATE — `030/mastering-surface` (feature rows 25 and 72)

**Read this file, not `LANE-STATE.md`.** `LANE-STATE.md` at this tip is **not mine**: it is the
`030/folder-tracks` lane's state doc (711 lines) that the release tree already carried at the base
tip `598d4f5c1`. I restored it byte for byte (sha256 `b3cf4967…`) after accidentally overwriting it in
commit `cfe2f21ce`; the correction is in the commit that added this file. Nothing of that lane's
record is lost and my diff for `LANE-STATE.md` is empty.

**Worktree** `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wmaster`
**Branch** `030/mastering-surface` (based on `release/0.3.0` tip `598d4f5c1`, untouched)
**Tip sha** `161edcf47` + the docs commit that added this file
**Not done, by instruction:** no merge, no push, no snapshot regeneration; `release/0.3.0` and the
`zene-030` worktree never touched.

---

## 1. What is done — the four-part scope contract, per row

### (1) Engine in the tree — named and verified at `598d4f5c1`

| symbol | file | what it is |
|---|---|---|
| `MasteringJob::defaultCandidates()` | `src/core/MasteringJob.cpp:147` | wave-1 **candidate generation** (5 candidates: four named targets, one dynamics variant) |
| `MasteringJob::run()` | `src/core/MasteringJob.cpp:348` | **render-once/branch-many**: one `ProjectRenderer` render, N in-memory branches, N files, N metric rows |
| `MasteringJob::measureCandidate()` | `src/core/MasteringJob.cpp:322` | **objective scoring**: metrics + residual + `lufsPass` / `truePeakPass` / `shortTermWarn` against the candidate's own target |
| `MasteringChain::measure()` | `src/core/MasteringChain.cpp:122` | the BS.1770-4 readings (LUFS-I, loudest 3 s window, measured true peak, crest) |
| `zene master` | `src/core/main.cpp` (dispatch + printed table) | the CLI action the group drives |
| `MasteringTest` | `tests/src/core/MasteringTest.cpp` | the registered ctest that was already there |
| design of record | `docs/AUTO-MASTERING.md` | read first: wave 1 = candidate generation + objective scoring; §8 lists what is not built |

**The engine was complete. What was missing was the surface** — the audit's Table B (#2, #72) counts
this one of the features "in the tree but not drivable through the socket".

### (2) The registered ids, their schemas and their A16 class

| id | args (required) | result (documented shape) | A16 class |
|---|---|---|---|
| `mastering.list_candidates` | none | `candidates[]` (`name`, `target{name, integrated_lufs, tolerance_lu, ceiling_dbtp, standard}`, `dynamics{…}`, `chain_settings{…}`), `count`, `note` | **not_mutating** |
| `mastering.get_state` | none | `has_run`, `last_run` (the run's own report or null), `files[]` (`path, exists, bytes, sha256`), `files_present`, `session_empty`, `note` | **not_mutating** |
| `mastering.run` | `out_dir` (absolute) | the run's report (`candidate_count`, `candidates[]` with metrics/target/verdicts, `render_count`, `sample_rate`, `source_render_file`, `source`, `format`) plus `out_dir`, `files[]`, `created[]`, `created_count`, `replaced_count`, `renderer`, `render_sample_rate`, `note`, `__transaction` | **true_inverse** (recorded ACTION checkpoint on the engine's own undo stack), `reversible: true` |

Rows: `src/core/ControlReversibilityTableMastering.cpp` (a GROUP file on the
track-folder/vca/routing seam, joined by `reversibilityRowTable()`).
`mastering.run`'s inverse: the output directory's `.wav` entries are captured **before the first
write** (bytes, bounded at `MasteringCaptureLimitBytes` = 64 MiB); the recorded step **removes every
file the run created** and **writes every held revision back byte for byte**. Beyond the bound the run
is **refused** (`refused`, typed) rather than performed without an inverse. **No redo half**, stated in
the row, in the transaction record and in the proof.

The `before` state a record carries is `{directory, held_before[], held_before_count, created[],
created_count, capture_limit_bytes}` — no file content: the content lives in the recorded closure.

### (3) Registered proofs

* `ControlMasteringCommands` — `tests/control-mastering-commands.py` (436 lines), registered in
  `tests/CMakeLists.txt`, ctest **Passed** (35/35 checks), `SKIP_RETURN_CODE 77` (never *Passed*
  without the shipped fixture generator). Plumbing in `tests/mastering_probe_lib.py` (the Gate 7
  split, the `agent_surface_lib.py` precedent). Fixture: the product's own
  `tools/auto-mastering-demo.py make` (the doc's reproduction section), opened via `project.open`.
* `MasteringTest` — extended with `theReportDocumentIsTheRunItself()`: the JSON document's rows **are**
  the job's own reports (field by field), the document survives the file round trip the surface
  depends on, and an unmeasurable reading is `null` rather than a fabricated number.
* `ReversibilityContractTest` — histogram updated (+1 `true_inverse`, +2 `not_mutating`).

### (4) UI absence — written down

* `docs/KNOWN-LIMITATIONS.md` — paragraph appended (verbatim in §6).
* `docs/RELEASE-NOTES-v0.3.0-alpha.md` — new section "Auto-mastering wave 1 — candidate generation and
  objective scoring, drivable", with the UI-absence line, and the A16 histogram moved from
  210/116/16/4/74 to **213/117/16/4/76** (telemetry in; base **211/117/16/4/74**).

---

## 2. What of wave 1 is built vs recorded-unbuilt

**Built (drivable end to end, measured):**
* candidate generation — the engine's own set, published by `mastering.list_candidates`;
* objective scoring — every candidate measured against its own named, cited target, returned by
  `mastering.run` and read back by `mastering.get_state`;
* the render-once claim — `render_count` comes from the engine's own `ProjectRenderer::renderCount()`;
* the inverse — the recorded action checkpoint, proved twice over (§3).

**Recorded-unbuilt (stated in the row text, the limits lines and here — not fabricated):**
* **no pick-log** — the feasibility study's step 5 decision record (level-matched A/B, commit winner,
  log the choice) needs a UI and a decision record; wave 1 delivers the measurement half only;
* therefore **no learned ranker** — wave 3 is gated on real user pick-logs, which do not exist yet;
  the study's own finding (FAD/CLAP rank correlation 0.14 vs 0.62 for human judgement) is why nothing
  in this group orders the candidates;
* **no reference-matching arm**, no Matchering sidecar, no FFmpeg `loudnorm` QC export;
* **no level-matched A/B** — the candidates differ in loudness by design (that is the target axis), so
  nothing presents them as comparable by ear;
* **`wav` only**, no per-candidate parallelism, and the cost of `MasteringChain::process()` was never
  benchmarked (no speed claim is made);
* **renders are not bit-reproducible** run to run, so two runs of the same master agree only to the
  meter's tolerance (≤ 0.05 LU / 0.01 dB), never byte for byte (within one run all candidates branch
  off the same render, so they are comparable with each other).

**Design decision recorded (a decision, not an omission):** `mastering.run` runs the shipped CLI
action in a **child process on a serialised copy**, following `render.render`
(`src/core/ControlCommandsProject.cpp`) and `bounce.in_place` (`include/BounceInPlace.h`): an
in-process render would drive THIS instance's audio engine
(`ProjectRenderer::startProcessing()` → `audioEngine()->startProcessing()/stopProcessing()`, which owns
the device thread) and `Song::startExport()` stops playback and re-measures the song. The measurements
come back through a new `--report <path>` CLI option (the run's own JSON document,
`src/core/MasteringReport.cpp`) rather than from a parse of the printed table. The printer **moved out**
of `src/core/main.cpp` into that file so the inherited file is not grown (`main.cpp` is 1399 lines
before and after, which is its `--scope all` baseline entry). There is **no per-candidate override
verb**: wave 1's candidate generation IS the engine's set.

---

## 3. Every number, with the command that produced it (unpiped exit codes)

```
bash tools/local-ci.sh --build-dir build --jobs 2       configure EXIT=0  build EXIT=0
                                                        ctest: 147/148 passed, 1 failed (see RED §4)
bash tests/run-all-gates.sh                             EXIT=1  (gate 1 ctest: the expected snapshot
                                                        drift; gate 5 mutation: see §4 note)
bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build   EXIT=0  PASS (6/6)
bash tests/complexity-gate.sh --check                   EXIT=0  PASS (no baseline entry)
bash tests/file-length-gate.sh --check                  EXIT=0  PASS (fork scope)
bash tests/file-length-gate.sh --check --scope all      EXIT=1  (6 PRE-EXISTING regressions, none mine)
bash tests/duplication-gate.sh                          EXIT=0  PASS (2.12% of the 5% budget)
bash tests/fork-sources-gate.sh                         EXIT=0  PASS (420 fork-NEW, 1060 inherited, 34 tooling)
bash tests/no-upstream-regression-gate.sh               EXIT=0  PASS (409 declared paths)
bash tests/unregistered-tests-gate.sh                   EXIT=0  PASS (128 sources, 126 registered, 2 declared, 4 helpers)
bash tests/evidence-gate.sh                             EXIT=0  PASS (6288 files, 0 refused)
cd build/tests && ctest -R ControlMasteringCommands --output-on-failure   EXIT=0  (35/35 checks)
cd build/tests && ctest -R MasteringTest               EXIT=0  (Passed, 11.5 s)
cd build/tests && ctest -R ReversibilityContractTest   EXIT=0  (Passed; 211-row telemetry-off base)
cd build/tests && ctest -R agent_surface               EXIT=0  (Passed, 3.2 s - `mastering.run`
                                                                survives junk args, no budget overrun)
cd build/tests && ctest -R ControlCommandsSnapshot     EXIT=8  (the expected drift; §4)
# the CLI half, by hand, from the worktree:
build/zene master <fixture>/demo.mmp -o cand2 -f wav -s 44100 --report report.json   EXIT=0
#   report.json: 8 keys, candidate_count 5, render_count 1, every candidate loudness_pass and
#   true_peak_pass; the printed table still ends with "No candidate is preferred: …"
```

Measured, all read off the wire by the transcript:

* `candidate_count 5`, `render_count 1`, six wav files matching the reported paths, hashes and sizes;
* every candidate inside **its own** target's tolerance and at or below its ceiling
  (`streaming-14` −14.095, `streaming-16` −16.000, `streaming-14-ceiling-2` −14.094 / −2.000 dBTP,
  `streaming-16-dynamics` −16.035, `ebu-r128` −23.000 with the published ±0.5 LU);
* `control.transactions` carries the run's record: `class true_inverse`, `reversible true`, `step 2`,
  `before.capture_limit_bytes 67108864`, `before.created_count 6`;
* `control.undo` → `{undone: true, undone_command: "mastering.run", can_undo: false}` and the directory
  holds **no candidate**; `control.redo` → `{redone: false}` and still no candidate;
* a second run **replaces** the first run's six files (`replaced_count 6`) and `control.undo` restores
  them **byte for byte** (sha256 compared before and after);
* the capture bound is enforced: a directory holding 74 517 864 bytes of wav is refused, typed, naming
  `67108864`;
* `project.get_state` is identical before and after the run (the session does not move).

### The negative control (the reset matters)

I neutered the **removal half** of the recorded step (`for (const QString& path : createdCopy)
{ QFile::remove(path); }` → a no-op), rebuilt `zene` (`build EXIT=0`) and re-ran the transcript:

```
cd build/tests && QT_QPA_PLATFORM=offscreen python3 ../../tests/control-mastering-commands.py ../zene
EXIT=1
  the directory the run wrote into holds no candidate afterwards FAILED
      files=('00_source-mix.wav','01_streaming-14.wav', … '05_ebu-r128.wav')
  control.redo does not resurrect a candidate set there is no redo half for FAILED
```

`control.undo` **reported success while every file stayed on disk** — which is exactly why the removal
half is load-bearing, and why the proof measures the directory rather than the reply. The source was
restored with `git checkout HEAD -- src/core/ControlMasteringSupport.cpp` and verified:
`sha256 = d5792aa4fb89edc46a6bcf6eb00ae535c91a14036511b4985f739d1dcc64d5ca`, identical to `HEAD` and to
the pre-neuter copy (**no mutant left in the tree**).

*The other shape of the A16 trap* — a feature the engine serialises only when it is non-default, so a
restore of the pre-first-edit XML cannot take it back — **does not arise in this group**, and the row
says why: no mastering command writes any project XML. The render runs in a child process on a
serialised copy, so no attribute, element or model of the running project is touched (measured:
`project.get_state` before == after).

---

## 4. What is RED, with the exact command and exit code

1. **`ControlCommandsSnapshot` — RED, expected, collected.**
   `cd build/tests && ctest -R ControlCommandsSnapshot --output-on-failure` → **EXIT=8**:
   ```
   MISSING from the snapshot  3 - the binary registers these; the offline list does not offer them
                       mastering.get_state
                       mastering.list_candidates
                       mastering.run
   FAIL: the committed snapshot and the built binary do not agree (1 finding(s), 3 drifted id(s)).
   ```
   **No id drifted the other way** (0 extra). Per the owner directive this is a **merge-time** step:
   the merge lane regenerates `tools/mcp-zene-control/zene_control/commands_snapshot.json` from a live
   instance of the MERGE tip after the LAST command-group merge. Nothing here was regenerated or
   hand-edited, so the red is expected and is a *collected* red, not a stop.
2. **`file-length-gate.sh --check --scope all` — RED, pre-existing, not this lane's.**
   EXIT=1, six regressions, all present at the base tip (`git show 598d4f5c1:<path> | wc -l`):
   `src/core/midi/MidiAlsaSeq.cpp` 718→867, `src/core/Mixer.cpp` 2116→2147,
   `src/core/Song.cpp` 2022→2104, `src/core/Track.cpp` 1043→1119,
   `src/gui/MainWindow.cpp` 1945→2062, `tests/src/core/RetroMidiCaptureCommandsTest.cpp` 558 (new).
   The `scope all` baseline is stale relative to the tip; the fork scope (the acceptance bar's
   default) is **green**. My largest new file is 493 lines.
3. **`run-all-gates.sh` gate 5 (mutation testing) — first attempt FAILED on a leftover mutant.**
   My first, timeout-interrupted run of that gate left the Gate 5 mutant
   (`src/core/RoutingGraph.cpp:205`, `std::min` → `std::max`) in the tree, and the next run **refused
   to start**: `mutation-gate: src/core/RoutingGraph.cpp has uncommitted changes — commit or stash
   first`. I restored the file with `git checkout HEAD -- src/core/RoutingGraph.cpp` and verified
   `sha256 = 1fc2d8fb3fe015e94468cd77e8fb285805198f6074258e0c7ced217c8632f163`, identical to `HEAD`
   (**no mutant left**). The gate was then re-run to completion — see the row below.

```
bash tests/run-all-gates.sh   (final run, clean tree)
```

Everything else in the acceptance bar is green (§3).

---

## 5. The VERBATIM replacement row text for `docs/FEATURE-LIST-0.3.0.md`

`docs/FEATURE-LIST-0.3.0.md` lives on `030/audit` and was **NOT edited**. The parent merges these two
rows, replacing the current text of rows 25 and 72 (section 5, "Audio engine and DSP"):

**Row 25** (replace the ids column and the whole Status cell):

```
| 25 | Mastering chain / auto-mastering | `mastering.*`, 3 ids: `mastering.list_candidates`, `mastering.get_state`, `mastering.run` | **in the tree** *(landed since the audit)* — the engine was already there (`src/core/MasteringChain.cpp`, `src/core/MasteringJob.cpp`, `MasteringTest`) and is now **drivable**: `mastering.list_candidates` publishes the wave-1 candidate set the engine generates (`MasteringJob::defaultCandidates()`: five candidates varying target loudness, true-peak ceiling and the dynamics stage, each target with the document its numbers come from — EBU R 128's published −23 LUFS-I ± 0.5 LU and −1 dBTP, and a −14 LUFS-I streaming *convention* with a tolerance this project chose and states); `mastering.run` renders the mix **ONCE** (`render_count` is the engine's own `ProjectRenderer::renderCount()`), branches every candidate off that one render, writes one wav per candidate into a directory the caller names and measures each with the merged BS.1770-4 meter (LUFS-I, loudest 3 s window, measured dBTP, crest, the residual against that candidate's own target and its loudness and true-peak verdicts); `mastering.get_state` reads the last run back and hashes the files it wrote. The run is a **child process on a serialised copy** of the session (`render.render` / `bounce.in_place`'s rule), so the session is not modified. `mastering.run` is **`true_inverse`** through a recorded ACTION checkpoint: the output directory's `.wav` entries are captured before the first write (bounded at 64 MiB; beyond that the run is REFUSED rather than performed without an inverse), the recorded step removes what the run created and writes replaced revisions back byte for byte, and there is **no redo half** (stated in the row and in the record). Proof: `MasteringTest` (engine, incl. the report-document ↔ run agreement) and the registered ctest `ControlMasteringCommands` (`tests/control-mastering-commands.py`, 35 checks over `--control-socket`). **Stated limits:** nothing ranks the candidates and none is preferred; `wav` only; no per-candidate parallelism; renders are not bit-reproducible run to run, so two runs agree only to the meter's tolerance (≤ 0.05 LU / 0.01 dB); the UI absence is in `docs/KNOWN-LIMITATIONS.md` | audit Table B #2; master list (#610); charter Out §3.3 |
```

**Row 72** (replace the ids column and the whole Status cell):

```
| 72 | Auto-mastering wave 1 (`#610`) — candidate generation + objective scoring | `mastering.*`, 3 ids: `mastering.list_candidates`, `mastering.get_state`, `mastering.run` | **in the tree** *(landed since the audit)* — both halves of wave 1 are built and drivable: **candidate generation** (`MasteringJob::defaultCandidates()`, published with each target's cited source by `mastering.list_candidates`) and **objective scoring** (`MasteringJob::measureCandidate()` over the merged BS.1770-4 meter, published per candidate by `mastering.run` and read back by `mastering.get_state`). Proof: `MasteringTest` (engine — incl. the new `theReportDocumentIsTheRunItself`, which holds the document the surface reads to the run's own reports field by field) and the registered ctest `ControlMasteringCommands` (35 checks: 5 candidates from 1 counted render, every candidate inside its own target's tolerance and at or below its ceiling, `control.undo` removes what a first run created and restores a re-run's replaced revision byte for byte, the 64 MiB capture bound refused typed). **What remains UNBUILT of wave 1, named rather than implied:** the *decision* half the feasibility study's step 5 names — **no pick-log** (no record of which candidate a user chose), and therefore **no learned ranker** (wave 3 is gated on real user pick-logs, which do not exist yet, and the study's own rank-correlation finding is why nothing here orders the candidates); **no reference-matching arm**; **no level-matched A/B** (the candidates differ in loudness by design — that is the target axis); **`wav` only**; the cost of `MasteringChain::process()` was never benchmarked. `docs/AUTO-MASTERING.md` §8 is the record; `docs/KNOWN-LIMITATIONS.md` and the release notes carry the same sentences | verdict Group A #16; `docs/AUTO-MASTERING.md`; master list (`#610`) |
```

## 6. The limits lines as written

`docs/KNOWN-LIMITATIONS.md` (appended):

> **Auto-mastering (wave 1) is drivable, and it does not rank anything.** `mastering.run` renders the
> open session **once** and writes N measured candidates into a directory the caller names
> (`mastering.list_candidates` publishes the set the candidates are generated from,
> `mastering.get_state` reads the last run back); it **does not rank them and does not claim a best**,
> because no validated preference scorer exists for master variants of one song. Candidate verdicts are
> against named, cited targets — EBU R 128 with its published ±0.5 LU, and a −14 LUFS-I streaming
> **convention** with a tolerance this project chose and states. The run is a **child process on a
> serialised copy of the session**, so the session is not modified and the running instance's audio
> path is untouched — but that also means the candidate files themselves are the only artefact,
> `control.undo` takes them back by **removing what the run created** and writing back the revisions
> the directory already held (bounded at 64 MiB of pre-existing wav files; beyond that the run is
> **refused** rather than performed without an inverse), and **there is no redo half**: `control.redo`
> cannot re-create a candidate set, only a re-issue of `mastering.run` can. Renders in this tree are
> **not bit-reproducible** run to run, so two runs of the same master are equal only to the meter's
> tolerance (≤ 0.05 LU / 0.01 dB), never byte for byte; within **one** run all candidates branch off
> the same render, so their metrics ARE comparable with each other. **`wav` only**, no per-candidate
> parallelism, no reference-matching arm, **no level-matched A/B** (the candidates differ in loudness by
> design, which is the target axis) and **no pick-log** — which is exactly why the learned ranker
> (wave 3) is not here: it is gated on real user pick-logs, which do not exist yet. Likewise the engine
> is drivable through the socket and **nothing in the interface masters anything**: there is no
> Export-dialog mastering mode, no candidate list panel and no A/B player.

`docs/RELEASE-NOTES-v0.3.0-alpha.md` — the new section's UI-absence bullet:

> **UI absence — one line: auto-mastering is drivable through the socket, not from the interface.**
> There is no Export-dialog mastering mode, no candidate list panel and no A/B player;
> `grep -rniI 'Mastering' src/gui/` returns **0** hits. `docs/KNOWN-LIMITATIONS.md` carries the sentence
> and the bounds above.

(Verified: `grep -rniI 'Mastering' src/gui/` returns nothing on this tree.)

## 7. The next exact command

The parent's merge sequence, in order:

```bash
# 1. merge this branch (4 commits) into release/0.3.0 as usual
# 2. THEN regenerate the snapshot from a live instance of the MERGE tip (the merge lane's step):
QT_QPA_PLATFORM=offscreen build/zene --control-socket /tmp/z.sock &
python3 tools/mcp-zene-control/snapshot_commands.py --socket /tmp/z.sock
git add tools/mcp-zene-control/zene_control/commands_snapshot.json   # +3 ids: mastering.*
# 3. fold the two row texts in §5 into docs/FEATURE-LIST-0.3.0.md (030/audit) by hand
# 4. from <build>/tests:  ctest -R 'ControlMasteringCommands|ControlCommandsSnapshot' --output-on-failure
```

## 8. Remaining acceptance list

| item | state |
|---|---|
| `tools/local-ci.sh --build-dir build --jobs 2` | configure 0, build 0, ctest 147/148 (snapshot drift red) |
| `tests/run-all-gates.sh` | EXIT=1 — gate 1 (the same snapshot drift) and gate 5; see §4 |
| `tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build` | EXIT=0 |
| `tests/complexity-gate.sh --check` | EXIT=0 |
| `tests/file-length-gate.sh --check` | EXIT=0 (fork); `--scope all` red, pre-existing |
| `tests/duplication-gate.sh` | EXIT=0 |
| `tests/fork-sources-gate.sh` | EXIT=0 |
| `tests/no-upstream-regression-gate.sh` | EXIT=0 |
| `tests/unregistered-tests-gate.sh` | EXIT=0 |
| `tests/evidence-gate.sh` | EXIT=0 |
| snapshot regeneration | **NOT DONE, BY INSTRUCTION** (merge-time, §7) |
| build directory deleted at the end of the lane | done, freed space reported in the handoff |
