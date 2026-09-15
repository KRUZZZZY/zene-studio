# LANE STATE — `030/import-detection` (transient / BPM / key detection on import, board task 659, feature-list row 34)

Worktree: `zene-030/wdetect` · branch `030/import-detection` · base `release/0.3.0` @ `f611c888b` ·
no merge, no rebase, no push (this pass). GPLv2-clean; no new dependency (see the method below).

## What landed

| commit | what |
|---|---|
| `120c09675` | `feat(detect)`: the detection arithmetic — `include/ImportDetectionDsp.h` with `src/core/ImportDetectionDsp.cpp`, `src/core/ImportDetectionKey.cpp` and `src/core/ImportDetectionSpectrum.{h,cpp}` (Qt-free: no file I/O, no dependency, and split across three units because the file-length ratchet allows 500 lines each) plus the box-local proof `tools/import-detection-proof.cpp`. |
| `69123e2f1` | `feat(detect)`: the engine layer (`include/ImportDetection.h`, `src/core/ImportDetection.cpp`), the project field (`include/ProjectKey.h`, `src/core/ProjectKey.cpp`), the command group, its SPEC A16 rows, its registration, the `Song` hooks, and both registered proofs. |
| `c1a367983` | `docs(detect)`: `docs/IMPORT-DETECTION.md` (the record), the `docs/KNOWN-LIMITATIONS.md` entry, the release-notes section — plus the file split the length ratchet asked for. |
| `ad3659f48` | `fix(detect)`: the registered test's own two defects, found by BUILDING it (`m_dir`/`m_clickA`… undeclared; `QDomElement` incomplete) and a `ProjectKey::saveSettings` simplification. |
| `2b1649410` | `fix(detect)`: the three defects the registered proofs found by RUNNING them — the key score became a tonic-weighted template CORRELATION (a mean named an A major fixture "Neopolitan", margin 0.003), the chroma band's hard 110 Hz edge became raised-cosine RAMPS (a 110 Hz sine read as A# 1.000 > A 0.712), and `SampleDecoder`'s DrumSynth fallback got the null-`AudioEngine` guard that was crashing the refusal path. The A16 histogram moved with the added rows. |

## The ids, their classes, and where each is registered

| id | A16 class (SPEC A16) | registered in | A16 row in |
|---|---|---|---|
| `detect.analyze` | `not_mutating` — one file in, tempo + first transient + key out, no project state touched | `src/core/ControlCommandsDetect.cpp` (`registerDetectCommands`) | `src/core/ControlReversibilityTableDetect.cpp` |
| `detect.apply` | `true_inverse` — recorded ACTION checkpoint (tempo map + project key captured before the first write; no live object carries either) | `src/core/ControlCommandsDetectApply.cpp` (`registerDetectApplyCommands`, called by the group entry) | same file |
| `detect.get_state` | `not_mutating` — reads the key field, the map, the methods, the bounds, the accuracy sentence | `src/core/ControlCommandsDetect.cpp` | same file |

Arguments: `detect.analyze {path (required), max_seconds}`; `detect.apply {path (required), tempo,
key, max_seconds}`; `detect.get_state {}`. Result schemas are declared in the two command files;
`min_seconds` is 1, the default window is 60 s and the hard maximum 300 s. Registration:
`registerDetectCommands` is called last by `registerControlCommands()`
(`src/core/ControlRegistryRegistrations.cpp`); the rows are joined into the one table by
`reversibilityRowTable()` (`src/core/ControlReversibilityTable.cpp`, `include/ControlReversibility.h`).

## Where a detection is written (the project's own fields)

* **tempo → the tempo map**: ONE tempo-only event at tick 0 (`DetectAppliedEventTick`), map switched
  on. The map holds an INTEGER bpm, so the estimate is rounded and the reply reports `bpm`,
  `detected_bpm` and `rounded`; out of the map's own 10..999 range is a typed REFUSAL, never a clamp.
* **key → the project's `detected-key` field** (`include/ProjectKey.h`): one `<detected-key>` element
  under `<song>`, written only when it holds something, cleared on absence and by
  `Song::clearProject`. `scale` is a name the PRE-EXISTING vocabulary answers to
  (`InstrumentFunctionNoteStacking::ChordTable::getScaleByName`, the table the piano roll's scale
  combo is filled from); a mask with no name there is refused rather than written. **No new scale
  vocabulary is introduced by this feature.**

## Proofs, and exactly what they prove

| proof | registered as | state |
|---|---|---|
| `tests/src/core/ImportDetectionTest.cpp` | ctest `ImportDetectionTest` (`tests/CMakeLists.txt`, LMMS_TESTS list) | **RUN GREEN on this box: `Passed`, 7/7 checks** |
| `tests/control-detect-commands.py` | ctest `ControlDetectCommands` (`tests/CMakeLists.txt`) | **RUN GREEN on this box: `Passed`, 22/22 checks** |
| `tools/import-detection-proof.cpp` | fork tooling, `tests/tools-sources.txt` | **BUILT AND RUN: EXIT=0, 7/7 checks** |

### Measured here (synthesised input with a known answer)

```
g++ -std=c++20 -O2 -Wall -Wextra -Iinclude tools/import-detection-proof.cpp \
    src/core/ImportDetectionDsp.cpp -o /tmp/import-detection-proof    # EXIT=0
/tmp/import-detection-proof                                           # EXIT=0, 7/7 checks
```

| synthesised input | measured |
|---|---|
| click track at 128 BPM, first click 0.500 s | **128.131 BPM**, confidence 0.667, 21 transients, first transient **0.499 s** |
| click track at 90 BPM, first click 0.500 s | **89.878 BPM**, confidence 0.565, 15 transients, first transient **0.499 s** |
| A major scale over an A bass (rooted 220 Hz) | tonic **A** (pc 9), template **major**, score **1.160**, margin **0.060** |
| silence / steady 440 Hz tone / empty vocabulary | tempo **not found**, tempo **not found**, key **not found** |

### Compile evidence (real compiler output, not a reading)

Configured `cmake -B build -DWANT_QT6=ON …` (Qt5 is NOT installed on this box; every optional host
was switched off) and compiled the affected translation units directly with
`make -f src/CMakeFiles/lmmsobjs.dir/build.make …`, unpiped:

```
core/ImportDetectionDsp.cpp.o  core/ImportDetection.cpp.o  core/ProjectKey.cpp.o
core/ControlDetectSupport.cpp.o  core/ControlCommandsDetect.cpp.o
core/ControlCommandsDetectApply.cpp.o  core/ControlReversibilityTableDetect.cpp.o
core/ControlReversibilityTable.cpp.o  core/ControlRegistryRegistrations.cpp.o
core/Song.cpp.o                                        -> all EXIT=0
```

One real defect was found this way and fixed: Qt6's `QJsonValue` is ambiguous for `std::int64_t`,
so `first_onset_frame` needed an explicit `qint64` cast.

## The build and the two registered proofs, verbatim

```
cmake -B build -DWANT_QT6=ON -DWANT_VST3=OFF -DWANT_CLAP=OFF -DWANT_CARLA=OFF -DWANT_WASM=OFF \
      -DWANT_SDL=OFF -DWANT_LV2=OFF -DWANT_JACK=OFF -DWANT_PULSEAUDIO=OFF \
      -DWANT_STEM_SPLIT=OFF -DWANT_ONNX=OFF -DWANT_VST2=OFF -DCMAKE_BUILD_TYPE=Release
make -j2 ImportDetectionTest        # EXIT=0
make -j2 zene                       # EXIT=0
ctest -R ImportDetectionTest   --output-on-failure   # EXIT=0, Passed      (7/7 checks)
ctest -R ControlDetectCommands --output-on-failure   # EXIT=0, Passed      (22/22 checks)
```

Measured by the socket transcript (`ControlDetectCommands`, 22/22): `detect.apply` wrote
**bpm 128 (detected 128.131, `rounded: true`) as ONE event at tick 0 with the map switched on**;
`transport.tempo_map_get` read it back independently (`tempo_at_position: 128`); `control.undo`
reported `undone_command: "detect.apply"` and took **both** halves off; `project.save` wrote a file
whose XML carries `<detected-key tonic="A" … >` and `<tempo-map … bpm="128">`; four typed refusals
(a missing `path`, a path that is not a file, `max_seconds: 0`, a file with no transients — the last
with "Nothing was written") each left the project untouched.

## What is NOT verified on this box (stated, not implied)

* **Real-world detection accuracy is unverified.** Everything measured is synthesised input with a
  known answer; a click track is the easy case for an onset/autocorrelation estimate. No real-music
  corpus was analysed, the confidence numbers are the detector's own scores (periodicity at the
  chosen lag; a rank margin for the key) and not probabilities, and **no accuracy figure for real
  music is quoted anywhere**. The half/double-time ambiguity inside the 40–240 BPM band is resolved
  only by the declared 120 BPM-centred prior, which biases toward the centre by construction.
* **Two defects were found by running, and fixed in this lane** — recorded because they are the point of
  measuring rather than asserting: (1) the key score was a mean of the template's degrees, which named an
  A major fixture "Neopolitan" with a 0.003 margin; it is now a tonic-weighted template CORRELATION
  (same fixture: "Major", 0.839, margin 0.077). (2) A hard 110 Hz chroma-band edge made a **110 Hz sine
  read as A# (1.000) over A (0.712)** — a tuning fork a semitone sharp; the edges are now raised-cosine
  ramps (80..4000 Hz, full weight 180..2500 Hz) and the same sine reports A = 1.000. (3) A PRE-EXISTING
  defect on the refusal path: `SampleDecoder`'s DrumSynth fallback dereferenced a null `AudioEngine`, so
  ANY file libsndfile cannot read SIGSEGVs in a process with no audio subsystem — one guard added, and
  declared in `tests/upstream-modifications.txt`.
* **A percussion-only fixture still gets a key**, with a near-tie margin (measured: click track →
  tonic B, "Enigmatic", correlation 0.646, **margin 0.018**). The registered transcript asserts that
  margin rather than a scale name, and no margin threshold suppresses the report: none was calibrated
  on real music, and none could be here.
* Nothing in the UI was touched: there is no import hook, no suggestion panel and no accept button,
  and the piano roll's own key/scale combo is not moved. `grep -rniI 'detect\.' src/gui/` finds no
  call site of these commands.

## Gates re-run after the last code change (unpiped exit codes)

| gate | command | result |
|---|---|---|
| Gate 7 (file length, fork scope) | `bash tests/file-length-gate.sh --check` | **EXIT=0** — PASS, no regressions (445 sources measured) |
| Gate 9 (scope manifests) | `bash tests/fork-sources-gate.sh` | **EXIT=0** — PASS (446 fork-NEW / 1060 inherited / 35 tooling) |
| Gate 6 (upstream divergence ledger) | `bash tests/no-upstream-regression-gate.sh` | **EXIT=0** — PASS (410 changed paths declared) |
| unregistered tests | `bash tests/unregistered-tests-gate.sh` | **EXIT=0** — PASS (128 registered, 2 declared-not-built) |
| Gate 7 (whole tree) | `bash tests/file-length-gate.sh --check --scope all` | **EXIT=1** — RED, and red BEFORE this lane for other files (see below; this lane's own two entries are `include/Song.h` 711 → 729 and `src/core/Song.cpp`'s +26) |
| A16 contract (anti-drift + histogram) | `ctest -R ReversibilityContractTest` from `<build>/tests` | **EXIT=0** — PASS |
| registry consistency | `ctest -R ControlRegistryTest` | **EXIT=0** — PASS |
| agent surface (reflection, ratchet, reverse completeness, headless sweep) | `ctest -R agent_surface` | **EXIT=0** — PASS: **230 commands, 229 swept, 1 allowlisted, 0 compiled out**; `detect.analyze` → `typed_error invalid_args`, `detect.apply` → `typed_error invalid_args`, `detect.get_state` → `ok` |

**The whole-tree file-length scope is red, and this lane adds to it.** At the base commit the all-scope
baseline was already exceeded by `src/core/midi/MidiAlsaSeq.cpp` (718 → 867), `src/core/Mixer.cpp`
(2116 → 2147), `src/core/Song.cpp` (2022 → 2104), `src/core/Track.cpp` (1043 → 1119),
`src/gui/MainWindow.cpp` (1945 → 2062), `tests/src/core/PluginScanCacheTest.cpp` (747 → 834) and the
new `tests/src/core/RetroMidiCaptureCommandsTest.cpp` (558). This lane's own contribution is
`include/Song.h` 711 → 729 (+18: the detected-key accessors, the member and the include) and a further
26 lines on `src/core/Song.cpp` (its three persistence hooks). **The ratchet was NOT re-anchored** —
moving those to a new file would mean the Song could not reach them at all.

## Hotspots touched (additive, minimal, sorted-insert, no reformat)

`hotspot: include/ControlRegistryGroups.h` — one declaration + its comment (the group's entry point and
its writer). `hotspot: src/core/ControlReversibilityTable.cpp` — the join list gained
`reversibilityDetectRowTable` **on the existing line**, because the file sits at 499 of the 500 lines the
ratchet allows. `hotspot: include/ControlReversibility.h` — one declaration. `hotspot:
src/core/ControlRegistryRegistrations.cpp` — one call, last, with its note. `hotspot:
src/core/CMakeLists.txt` — three source-list blocks. `hotspot: tests/CMakeLists.txt` — the QTest in
LMMS_TESTS, the socket transcript's `add_test`, one comment. `hotspot: include/Song.h`,
`src/core/Song.cpp` — the project field (the file-length consequence is declared above).
`hotspot: tests/fork-sources.txt`, `tests/all-sources.txt`, `tests/tools-sources.txt` — the new sources,
plus `tests/control-detect-commands.py` appended to every python pathspec line so the documented union
still derives it. `hotspot: docs/KNOWN-LIMITATIONS.md`, `docs/RELEASE-NOTES-v0.3.0-alpha.md` — appended
(one paragraph; one section before the trailer).

`tools/mcp-zene-control/zene_control/commands_snapshot.json` is DERIVED and was **not** touched: it is
regenerated from a live instance of the merge tip, which this lane did not have.

## Next action

Hand this lane to the integration lane: the build tree is deleted (disk discipline), and the merged tip
must re-run the two registered proofs itself —
`cmake -B build -DWANT_QT6=ON … && make -j2 zene ImportDetectionTest` then
`ctest -R 'ImportDetectionTest|ControlDetectCommands' --output-on-failure` **from `<build>/tests`** —
plus `tests/run-all-gates.sh` on the merged tip (expect exit 3, not 1). The whole-tree file-length
scope stays red until someone reconciles the pre-existing overruns named above.
