# LANE STATE — `030/import-detection` (transient / BPM / key detection on import, board task 659, feature-list row 34)

Worktree: `zene-030/wdetect` · branch `030/import-detection` · base `release/0.3.0` @ `f611c888b` ·
no merge, no rebase, no push (this pass). GPLv2-clean; no new dependency (see the method below).

## What landed

| commit | what |
|---|---|
| `120c09675` | `feat(detect)`: the detection arithmetic — `include/ImportDetectionDsp.h`, `src/core/ImportDetectionDsp.cpp` (Qt-free: no file I/O, no dependency) and the box-local proof `tools/import-detection-proof.cpp`. |
| `69123e2f1` | `feat(detect)`: the engine layer (`include/ImportDetection.h`, `src/core/ImportDetection.cpp`), the project field (`include/ProjectKey.h`, `src/core/ProjectKey.cpp`), the command group, its SPEC A16 rows, its registration, the `Song` hooks, and both registered proofs. |
| `c1a367983` | `docs(detect)`: `docs/IMPORT-DETECTION.md` (the record), the `docs/KNOWN-LIMITATIONS.md` entry, the release-notes section — plus the file split the length ratchet asked for. |

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
| `tests/src/core/ImportDetectionTest.cpp` | ctest `ImportDetectionTest` (`tests/CMakeLists.txt`, LMMS_TESTS list) | registered; NOT RUN on this box (it needs the whole product linked — see below) |
| `tests/control-detect-commands.py` | ctest `ControlDetectCommands` (`tests/CMakeLists.txt`) | registered; NOT RUN on this box (needs the `zene` binary) |
| `tools/import-detection-proof.cpp` | fork tooling, `tests/tools-sources.txt` | **BUILT AND RUN on this box** — the measured numbers below |

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

## What is NOT verified on this box (stated, not implied)

* **The two registered proofs have not been RUN.** `ImportDetectionTest` and `ControlDetectCommands`
  need the product built and linked; the whole-product build was started (`make -j2
  ImportDetectionTest`, `-DWANT_QT6=ON`) and its outcome is recorded in the lane report — the
  registered proofs are in the tree but their green/red state on this box is whatever that build
  produced, and nothing here claims they pass.
* **Real-world detection accuracy is unverified.** Everything measured is synthesised input with a
  known answer; a click track is the easy case for an onset/autocorrelation estimate. No real-music
  corpus was analysed, the confidence numbers are the detector's own scores (periodicity at the
  chosen lag; a rank margin for the key) and not probabilities, and **no accuracy figure for real
  music is quoted anywhere**. The half/double-time ambiguity inside the 40–240 BPM band is resolved
  only by the declared 120 BPM-centred prior, which biases toward the centre by construction.
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
| Gate 7 (whole tree) | `bash tests/file-length-gate.sh --check --scope all` | **EXIT=1** — RED, and red BEFORE this lane for other files (see below) |

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

Re-run `make -j2 ImportDetectionTest` (and the `zene` binary) to completion, then run
`ctest -R 'ImportDetectionTest|ControlDetectCommands' --output-on-failure` **from `<build>/tests`** —
the two registered proofs have never been executed by this lane.
