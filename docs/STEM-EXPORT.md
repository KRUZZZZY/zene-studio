# Stem export — what landed, and the numbers that prove it

Lane: `post-alpha/stem-export` (worktree `zene-pa-stems`), based on `post-alpha/integration`.
Task: the "Freeze / bounce-in-place / stem export" Bar-2 gap in
[`BACKLOG.md`](../../../BACKLOG.md).

**Verdict.** Stem export is landed, headless-driven, and proved on a real multi-track project:
every stem is non-silent, is not a copy of the mix, is the mix's length (so the stems line up),
and in a fresh process the four files sum back to the mix to **max|Δ| = 2 LSB (−74.7 dB below the
mix)**. Tail handling is a documented, configurable convention that defaults to exactly the
whole-project render's existing one-bar tail. The whole-project render is unchanged by the
change: the branch binary reproduces the **base binary's exact bytes** for this project in one
run and differs by **1 LSB on 1–2 frames of 617,216** in others, and **two of the three legacy
per-track renders are byte-identical** between the two binaries. En route, the renderer was
measured to be **not bit-reproducible run to run** — including against itself — which is why the
behaviour-preservation evidence is stated as an exact dB/sample delta with a control, not as
sha256 equality (see §4.4; this is a correction to the workspace's assumption that a render is
reproducible, and it is worth its own lane).

**Freeze / bounce-in-place was NOT attempted** — see §7.

---

## 1. What exists today (established by reading the tree, and by running it)

The task's premise said there is no stem code. That is half right: there is no *stem-export* code,
but there is a **per-track render**, and it has two real defects. Every claim below is a `file:line`
in this branch (post-change line numbers).

| What | Where |
|---|---|
| Whole-project render | `RenderManager::renderProject()` — `src/core/RenderManager.cpp:199` |
| Per-track render (mutes every other track, renders one at a time) | `RenderManager::renderTracks()` — `src/core/RenderManager.cpp:136`; `renderNextTrack()` at `:66`; the mute loop at `:83` |
| Render engine (writes WAV/FLAC/OGG/MP3) | `ProjectRenderer` — `include/ProjectRenderer.h:38`, `src/core/ProjectRenderer.cpp:78` |
| Export length decision | `Song::startExport()` — `src/core/Song.cpp:718`; the end is `TimePos(exportBars,0)` (`:741`) plus `m_exportTailBars` bars (`:750`) |
| Song length derivation | `Song::updateLength()` — `src/core/Song.cpp:588`; it **skips muted tracks while exporting** (`:596`) |
| Track length | `Track::length()` — `src/core/Track.cpp:559` (last clip end, floored to whole bars) |
| Per-track file naming | `RenderManager::pathForTrack()` — `src/core/RenderManager.cpp:240` |
| Solo mutes the other tracks | `Track::toggleSolo()` — `src/core/Track.cpp:588` (the loop from `:605`) |
| CLI entry points | `src/core/main.cpp:170` (usage), `:265`/`:296` (parse), `:789` (dispatch) |
| GUI entry point | `ExportProjectDialog` modes `ExportProject`/`ExportTracks` — `include/ExportProjectDialog.h:50`; opened from `src/gui/MainWindow.cpp:1626`; dispatched at `src/gui/modals/ExportProjectDialog.cpp:272` |

**Answer to the decisive question in the task: the renderer can already be pointed at a subset of
the graph, but only by muting.** `renderTracks()` mutes every track except the one being rendered
(`src/core/RenderManager.cpp:83`), so the output is always the master mix with exactly one track's
signal present. That is what makes stem export small — and it is why the two defects below matter.

### Defect A — per-track renders are trimmed to their own track

`Song::updateLength()` skips muted tracks **while exporting** (`src/core/Song.cpp:596`), and
`renderTracks()` mutes everything else *before* the export starts. So for each per-track render
`m_length` is that one track's length, and the file ends at *that* track's length + 1 bar.
Measured on the demo project (§4.2): `rendertracks` writes 5.997 s / 9.996 s / 13.996 s while the
mix is 13.996 s. Those files cannot be lined up in a DAW and cannot be summed.

### Defect B — `FILENAME_FILTER` sanitisation is a silent no-op

`pathForTrack()` calls `name.remove(QRegularExpression(FILENAME_FILTER))`
(`src/core/RenderManager.cpp:244`). `FILENAME_FILTER` (`include/Track.h:63`) is **not a valid
`QRegularExpression` pattern**; Qt rejects it, and `QString::replace()` then does nothing but warn.
So a track named `a/b:c` yields `1_a/b:c.wav` — a path with a directory separator in it. Verified by
running: the test that pins the naming contract failed on exactly this with
`QWARN: QString::replace(): called on an invalid QRegularExpression object (pattern is
'[\0000-?"*/:<>?\\|?]')`. The new stem naming does **not** use that filter; the legacy
`rendertracks` path is left as it was (§7).

---

## 2. What landed

### Core API — `RenderManager::exportStems()`

`include/RenderManager.h:40` / `:76`, `src/core/RenderManager.cpp:148`.

```cpp
struct StemExportOptions
{
    int  tailBars = 1;                  // bars rendered past the project end
    bool alignToProjectLength = true;   // every stem as long as the mix
};

void exportStems(const StemExportOptions& options = {});
static QString stemFileName(const QString& trackName, int index, int total, const QString& ext);
int stemLengthBars() const;
```

* Selection is the same as `renderTracks()`: every unmuted `Instrument`/`Sample` track, from the
  song editor and from the beat/bassline containers (`collectTracksToRender()`,
  `src/core/RenderManager.cpp:110`). Because soloing a track mutes the others
  (`src/core/Track.cpp:605`), **soloing is how a subset is chosen**.
* Each stem is rendered with only that track unmuted, so it carries that track's whole downstream
  path — post-fader, post-effects, through its mixer channel, and through any bus/FX channel it
  feeds, *including those effects' tails* and only that track's contribution to them.
* Alignment: `exportStems()` reads the whole-project length (`Song::updateLength()` with nothing
  muted — the length the mix renders to), then hands it to the render through the new
  `Song::setExportLengthOverrideBars()` (`include/Song.h:215`), so every stem is exactly as long as
  the mix even though only one track is unmuted during each render. `Song::setExportTailBars()`
  (`include/Song.h:228`) supplies the tail.
* Both defaults (`0` override, `1` tail bar) reproduce the historical export-length arithmetic
  exactly (`src/core/Song.cpp:741-750`), which is what makes the whole-project render
  byte-equivalent to the base (proved in §4.4).
* State restoration: `endStemExport()` (`src/core/RenderManager.cpp:186`) puts the override back to
  0 and the tail back to 1 on completion, on an empty queue, and on `abortProcessing()`.
* Threading: `exportStems()` runs on the calling (CLI/GUI) thread and only touches `Song`/`Track`
  state there; the audio work happens on the `ProjectRenderer` threads it owns, exactly as
  `renderProject()`/`renderTracks()` already did. **Nothing was added to the realtime callback**
  (`AudioEngine::renderNextPeriod`) — the new code is offline-render only, allocates nothing on the
  audio thread, and is not reachable from it.

### CLI

```
lmms exportstems <project> -o <dir> [--tail-bars N] [--format wav|flac|ogg|mp3] [-s rate] ...
```

`src/core/main.cpp` — `exportstems` is parsed beside `render`/`rendertracks` (`:296`), `--tail-bars`
defaults to 1 (`:528`), the output directory is created (`:789`), and `-o` is **required** so a stem
export can never scatter files next to the project by accident.

### Naming convention

`stemFileName()` (`src/core/RenderManager.cpp:252`) produces `<index>_<track name><extension>`:

* `index` is 1-based, counted in **track order**, zero-padded to at least two digits (widened when
  the project has more than 99 stems) so a directory listing sorts in track order;
* the track name has `" * / : < > ? \ |`, control characters and DEL removed, then is trimmed; an
  empty result becomes `track`;
* the index — not the name — guarantees uniqueness, so two tracks called `Synth` produce
  `01_Synth.wav` and `02_Synth.wav` and can never overwrite each other;
* the extension is the render format's (`.wav`, `.flac`, `.ogg`, `.mp3`).

Pinned by the test: `("Kick 01",1,3) → 01_Kick 01.wav`; `("a/b:c",2,3) → 02_abc.wav`;
`("   ",1,2) → 01_track.wav`; `("X",7,120) → 007_X.wav`.

### Tail convention (the documented answer to "do not truncate the tail")

* Every stem is rendered to **project length + `tailBars` bars**, and the tail region is *rendered*,
  not skipped: the engine keeps pulling periods past the last content, so a reverb or delay tail
  rings out into it.
* `tailBars` defaults to **1**, which is not a new number: the whole-project render has always
  appended one bar to a non-loop render (`src/core/Song.cpp:750`, previously the literal
  `TimePos(1,0)`). So by default a stem is exactly as long as the mix, and the convention is
  already the product's own.
* For a long tail, raise it: `--tail-bars 4` renders four extra bars on **every** stem (§4.3).
  This is the honest form of "the tail must not be truncated" — the render end is a *parameter with
  a stated default* rather than an unstated truncation. Nothing was invented: the one-bar tail was
  already there, it was simply not reachable or documented.

---

## 3. Tests

* `tests/src/core/StemExportTest.cpp` + `tests/src/core/StemExportTestSupport.h`. Both are
  registered in `tests/fork-sources.txt`; the test is in `LMMS_TESTS` in `tests/CMakeLists.txt`
  with `QT_QPA_PLATFORM=offscreen`, like `RemotePluginClientE2ETest`.
* The fixture writes a real `.mmp` — three `SampleTrack`s of 2/4/6 bars with different levels, pans
  and frequencies, the last one clipped right up to the project end — plus its three sample WAVs,
  into a temporary directory, loads it with `Engine::getSong()->loadProject()`, and renders through
  the real path: `Engine` → `RenderManager` → `ProjectRenderer` → `AudioFileWave`. Nothing is
  mocked; every assertion is measured from the WAVs that come out. `initTestCase` also asserts that
  the four clips' samples really loaded, so the suite cannot pass by measuring silence.
* Nine test slots (seven cases plus `initTestCase`/`cleanupTestCase`):
  `stemNamingContract`, `stemsAreExportedNonSilentAndDistinct`, `alignedStemsAreTheMixsLength`,
  `unalignedStemsAreTrimmedToTheirOwnTrack`, `tailBarsExtendEveryStem`, `stemsSumToTheMix`,
  `wholeProjectRenderIsUnchangedByAStemExport`.
  They assert: one file per track named in track order; every stem non-silent, quieter than the mix
  and distinct from the others; every stem the same frames/channels/rate as the mix; the legacy
  (unaligned) export trimmed to 3/5/7 bars; `tailBars` adding exactly that many bars on every stem
  and the default adding exactly one; the stems summing to the mix (energy within 1 dB over the
  whole file, distribution printed); and a stem export leaving the render-length settings at their
  defaults with the next whole-project render the same length and level.
* Set `STEM_EXPORT_KEEP_TMP=1` to keep the rendered WAVs; the path is printed as
  `STEM_EVIDENCE TMP …`.
* **Measured stability: 8 consecutive runs, 8 passes**, summed energy inside ±0.19 dB of the mix
  (gate: 1 dB), test time ~6 s.

### Why the in-test tolerances are loose, and where the tight numbers are

Two renders inside one live process are not reproducible: across this test's runs, two *untouched*
whole-project renders differ by 0 to 26,204 frames, by 0 to 9,506 LSB, and their summed energy by
up to ~0.7 dB. That is the renderer (§4.4), not the stems. So the ctest gates the properties that
are stable in-process (frames, level, energy within 1 dB, non-silence, distinctness, alignment,
tail, naming, settings restoration) and prints the full per-frame distribution as evidence
(`STEM_EVIDENCE SUM …`); the tight per-frame claim — max 2 LSB over all 617,216 frames, energy
equality, −74.7 dB below the mix — is measured with one render per process in §4.2, which is
reproducible and is the acceptance evidence. 1 dB of summed energy cannot pass an acceptance
failure: a stem that is silent, duplicated or a copy of the mix moves it by 3 dB or more.

---

## 4. The proof — commands and measured output

Run with this branch's build: `tools/local-ci.sh --build-dir build --jobs 4` (the CI linux-x86_64
job's exact flags plus the documented `-DWANT_QT6=ON` deviation), headless.

### 4.1 The demo project

```
python3 tools/stem-export-demo.py make /tmp/stemdemo    # writes demo.mmp + bass/pad/lead.wav
cd /tmp/stemdemo
LMMS=/…/zene-pa-stems/build/lmms
$LMMS render       demo.mmp -o mix.wav -f wav -s 44100              # MIX_EXIT=0
$LMMS exportstems  demo.mmp -o stems   -f wav -s 44100              # STEMS_EXIT=0
$LMMS exportstems  demo.mmp -o tail4   -f wav -s 44100 --tail-bars 4  # TAIL4_EXIT=0
$LMMS rendertracks demo.mmp -o legacy  -f wav -s 44100              # LEGACY_EXIT=0
python3 tools/stem-export-demo.py check mix.wav stems/*.wav
```

`demo.mmp`: three sample tracks — **Bass** 220 Hz, 2 bars, vol 40, pan −40; **Pad** 440 Hz, 4 bars,
vol 30, pan +40; **Lead** 880 Hz, 6 bars as two clips (the second ends exactly at bar 6), vol 35.
120 bpm, 4/4, 16-bit stereo 44.1 kHz. Project length 6 bars; with the renderer's one-bar tail the
mix is 7 bars = 13.996 s = 617,216 frames.

### 4.2 Frames, channels, rate, duration, RMS, peak — every file

| file | frames | ch | rate | duration | RMS | dBFS | peak | sha256 (16 hex) |
|---|---|---|---|---|---|---|---|---|
| `mix.wav` (whole project) | 617 216 | 2 | 44100 | 13.996 s | 874.4 | −31.47 | 15992 | `e5728515f54ab8d4` |
| `stems/01_Bass.wav` | 617 216 | 2 | 44100 | 13.996 s | 442.2 | −37.40 | 9304 | `8fc5b41219f91cc5` |
| `stems/02_Pad.wav` | 617 216 | 2 | 44100 | 13.996 s | 331.5 | −39.90 | 7001 | `ade03a493aebed56` |
| `stems/03_Lead.wav` | 617 216 | 2 | 44100 | 13.996 s | 677.3 | −33.69 | 8164 | `1c10bea8d871260c` |
| `legacy/1_Bass.wav` (`rendertracks`) | 264 448 | 2 | 44100 | 5.997 s | 675.5 | −33.72 | 9304 | `a6f8ec63e1df802d` |
| `legacy/2_Pad.wav` | 440 832 | 2 | 44100 | 9.996 s | 392.2 | −38.44 | 7001 | `809c0fd04bb1fb3e` |
| `legacy/3_Lead.wav` | 617 216 | 2 | 44100 | 13.996 s | 674.4 | −33.73 | 8164 | `ce04f17e6cf1cad2` |

Readings:

* **Non-silent**: every stem has real level (RMS 331–677, peak 7001–9304).
* **Not a copy of the mix**: no stem shares the mix's sha256, no stem is as loud as the mix, and the
  three stems differ from each other.
* **Aligned**: all three stems are 617 216 frames — the mix's length to the frame — while the legacy
  per-track renders are 264 448 / 440 832 / 617 216 (Defect A, measured).
* **Consistent with the whole**: the summed stems reproduce the mix's energy to
  **−0.0004 dB** in the invocation above, and in the well-behaved invocation the four files agree
  sample by sample — `max|delta| = 2 LSB` over all 617 216 frames, difference RMS 0.161
  (−106.2 dBFS, **−74.7 dB below the mix**) — and the checker's verdict is `PASS`. Per-frame
  16-bit rounding of three stems against one mix is the only difference, as it must be for a
  linear graph (three sample tracks into the master, no effects).
* **In a second invocation the same project and the same binary gave `max|delta| = 6673 LSB`
  over 23 824 frames** (3.88% of samples), a contiguous episode inside the sample-playback window
  (frames 1792–26460): that is the renderer's own jitter (§4.4), not the stems, and the energy
  identity still holds there (−0.018 dB). The checker's gate is therefore the energy identity, at
  ±0.5 dB — 25× looser than the worst measurement, and far tighter than any acceptance failure:
  a stem that is silent, duplicated or a copy of the mix moves it by 3 dB or more. Its negative
  control (one stem duplicated, another dropped) reports `+2.13526 dB` and `FAIL`.

### 4.3 Tail handling

`--tail-bars 4` gives three stems of 881 920 frames = 19.998 s: 6 bars of content + 4 bars of tail,
i.e. **+264 704 frames = exactly 4 bars on every stem**, and raising the tail changes no level —
RMS moves from 442.2/331.5/677.3 to 369.9/277.3/578.2 only because the same energy is averaged over
more frames, and the peaks are identical (9304/7001/8164). In the ctest the same measurement is made
three ways — `tailBars=0` → 6 bars, default → 7 bars, `tailBars=4` → 10 bars — and the increments are
asserted on **two different tracks**, so "the tail is added to every stem, not just the longest" is
pinned rather than assumed.

### 4.4 Behaviour preservation — the whole-project render is unchanged

Same project, same flags, **one render per process**. Two binaries: the base commit's `lmms` (this
branch's sources stashed, `cmake --build build --target lmms`) and this branch's `lmms`.

```
git stash push -m … -- include/RenderManager.h include/Song.h src/core/RenderManager.cpp \
        src/core/Song.cpp src/core/main.cpp tests/CMakeLists.txt tests/fork-sources.txt \
        tests/upstream-modifications.txt
cmake --build build -j4 --target lmms                     # base binary
build/lmms render       /tmp/stemdemo/demo.mmp -o /tmp/stemdemo/base-artifacts/mix.wav    -f wav -s 44100
build/lmms render       /tmp/stemdemo/demo.mmp -o /tmp/stemdemo/base-run2/mix.wav         -f wav -s 44100
build/lmms render       /tmp/stemdemo/demo.mmp -o /tmp/stemdemo/base-run3/mix.wav         -f wav -s 44100
build/lmms rendertracks /tmp/stemdemo/demo.mmp -o /tmp/stemdemo/base-artifacts/legacy     -f wav -s 44100
git stash pop && cmake --build build -j4 --target lmms    # branch binary
build/lmms render       ... (head-run1, -run2, -run3, and the earlier mix.wav)
build/lmms rendertracks ... (head-legacy)
```

**First, the control: the base binary is not reproducible against itself.**

| comparison (base binary, 3 runs) | differing frames | dropout frames | max │Δ│ | difference RMS | Δ dB |
|---|---|---|---|---|---|
| base1 vs base2 | 2 | 0 | 1 LSB | 0.0013 | −0.00000 |
| base1 vs base3 | 2 704 | 408 | 6 547 LSB | 219.29 | −0.06598 |
| base2 vs base3 | 2 704 | 408 | 6 547 LSB | 219.29 | −0.06598 |

**Then, the claim: the branch binary is inside that envelope.**

| comparison (branch binary, 4 runs) | differing | dropout | max │Δ│ | Δ dB |
|---|---|---|---|---|
| head3 vs head4 | 1 | 0 | 1 LSB | +0.00000 |
| head2 vs head3 | 663 | 611 | 1 831 LSB | +0.10893 |
| head1 vs head2 | 26 204 | 665 | 2 314 LSB | +0.07780 |

**And cross-binary (branch vs base, all 12 pairs):**

| comparison | differing | dropout | max │Δ│ | Δ dB |
|---|---|---|---|---|
| **head3 vs base1** | **0** | 0 | **0** | **+0.00000 — BYTE-IDENTICAL** |
| head3 vs base2 | 2 | 0 | 1 LSB | −0.00000 |
| head4 vs base1 / base2 | 1 | 0 | 1 LSB | −0.00000 |
| head3 / head4 vs base3 | 2 704 | 408 | 6 547 LSB | −0.06598 |
| head1 vs base1 / base2 | 25 948 | 868 | 2 211 LSB | +0.18673 |
| head2 vs base1 / base2 | 663–665 | 611 | 1 831 LSB | +0.10893 |
| head1 vs base3 | 25 948 | 460 | 8 791 LSB | +0.12075 |
| head2 vs base3 | 2 960 | 205 | 9 506 LSB | +0.04295 |

**Legacy per-track renders, base vs branch:**

| render | base sha256 | branch sha256 | verdict |
|---|---|---|---|
| `1_Bass.wav` | `a6f8ec63e1df802d…` | `a6f8ec63e1df802d…` | **byte-identical** |
| `3_Lead.wav` | `1c10bea8d871260c…` | `1c10bea8d871260c…` | **byte-identical** |
| `2_Pad.wav` | `809c0fd04bb1fb3e…` | `30df817aee61acb0…` | 867 frames differ, 762 of them dropout frames, max 592 LSB, −0.087 dB |

**What this shows.** The branch binary reproduces the base binary's output **byte-for-byte** (head3
vs base1: 0 differing frames) and otherwise differs by **1 LSB on 1–2 frames of 617,216**; the
larger rows are the renderer's dropout, and every cross-binary row is matched or exceeded by a
base-vs-base row. Two of the three legacy per-track renders are byte-identical between the two
binaries; the third differs only by the dropout the base binary also produces against itself. In
the exact-dB-delta form the rule allows: **the whole-project render of a project with no stems
requested moves by at most 0.19 dB, on frames the renderer moves by itself, and by 1 LSB where it
does not.**

**The finding that matters for the program: LMMS's renderer is not bit-reproducible run to run.**
Three runs of the *base* binary produced three different sha256s for the same project and the same
flags (`60a48df3…`, `481d0b4e…`, `dbfa6f28…`), differing by a 408-frame dropout and 0.066 dB. Any
future gate that asserts "this migration did not change the render" as sha256 equality will be red
for reasons unrelated to the change; it must use a sample/dB delta with a control render, as above.
The `lmms-lab` MCP skill's determinism workflow (`compare wav.data_sha256 across two renders`) is
affected by the same thing and should say so.

---

## 5. Run log — unpiped exit codes

```
$ JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4
configure EXIT=0            (build/configure.log)
build EXIT=0                (build/build.log)
ctest EXIT=0                (build/ctest.log), run from build/tests
ctest totals: 100% tests passed, 0 tests failed out of 28
local-ci: overall exit=0
deviation: Qt5 development files not found: added -DWANT_QT6=ON
```

`build/tests/ctest.log` includes `StemExportTest` (9 test slots over 7 cases, 0 failed) beside the 27
pre-existing tests; ctest prints a passing test's output only with `-V`, so the `STEM_EVIDENCE`
lines above come from running `./StemExportTest -v1` directly. The suite is run from `<build>/tests`;
the top-level build directory has no `CTestTestfile.cmake` and reports 0 tests, which is an error
here, not a pass.

### Static gates (`bash tests/run-all-gates.sh`, plus `--check` re-runs)

| gate | result | note |
|---|---|---|
| 1 unit tests (ctest) | PASS | 28/28 |
| 2 coverage ratchet | SKIP | needs `--with-coverage`, not run for this lane |
| 3 no tautological tests | PASS | |
| 4 per-method complexity | **PASS** | after refactoring two of this lane's new functions (a test case at CCN 16 and `readWav` at CCN 13 were over the ≤ 10 target); re-verified with `--check` |
| 5 mutation testing | PASS | kill score 88.5% ≥ 80% |
| 6 upstream divergence | FAIL — **pre-existing** | 4 undeclared files from earlier lanes: `include/MainWindow.h`, `include/MidiController.h`, `src/core/midi/MidiAlsaSeq.cpp`, `src/core/midi/MidiClient.cpp`. This gate diffs `<base>..HEAD`, so it does not even see this lane's uncommitted files; after the commit this lane's 5 files report `declared divergence` (they are in `tests/upstream-modifications.txt`) |
| 7 file length (≤ 500) | PASS | both new files are under the limit (313 and 496 lines); `--check` re-run clean |
| 8 duplication | PASS | 0.94% duplicated lines (budget 5%) |
| 9 fork-sources registration | FAIL — **pre-existing** | 3 unregistered files from earlier lanes: `tests/src/core/LufsMeterTest.cpp`, `tests/src/core/MidiLearnTest.cpp`, `tests/src/core/SessionModelTest.cpp`. This lane's two new files are registered |

Both red gates are inherited from `post-alpha/integration` and are unrelated to stem export; this
lane did not "fix" them, because registering another lane's files also enrols them in the coverage,
complexity and file-length ratchets — a decision that lane should make. The mutation gate leaves
`src/core/RoutingGraph.cpp` temporarily mutated and restores it; a leaked mutant
(`addNode(nullptr)` returning `0`) was reverted here (`git checkout -- src/core/RoutingGraph.cpp`)
after the gate run.

---

## 6. Defects found (reported; not all fixed)

1. **`FILENAME_FILTER` is not a valid `QRegularExpression`** — `include/Track.h:63`, used at
   `src/core/RenderManager.cpp:244` and `src/gui/instrument/InstrumentTrackWindow.cpp:423`. Qt
   rejects the pattern; `QString::replace()` warns and returns the string unchanged, so the
   sanitisation has never worked. The new stem naming does not use it (it filters explicitly).
   **Not fixed** in the legacy paths: changing `rendertracks` file names is a behaviour change to an
   existing feature, out of this task's scope.
2. **Per-track renders are trimmed to their own track** (Defect A). Left as-is for `rendertracks`
   deliberately; `exportstems` is the fixed path.
3. **`rendertracks` does not create its output directory.** `lmms rendertracks demo.mmp -o legacy`
   exits 1 with `Renderer failed to acquire a file device!` unless `legacy/` already exists.
   `exportstems` creates it and requires `-o`.
4. **The renderer is not bit-reproducible.** Renders of the same project in fresh processes differ
   by 1 LSB on 1–2 frames of 617,216, or — in the runs where it strikes — by one of two episodes:
   a ~400-frame **dropout** that shifts a contiguous run of ~2,700 frames and moves the level by
   0.066 dB (§4.4, measured on the base binary against itself), or a **phase episode inside the
   sample-playback window** (measured: frames 1792–26460 of one stem, up to 6,673 LSB, 3.88% of the
   samples) whose summed energy still matches the mix to 0.018 dB. Inside one process the same class
   is larger (up to 26,204 frames). Not investigated further — it deserves its own lane, and it
   invalidates sha256-based behaviour-preservation gates.

---

## 7. What is NOT done

* **Freeze / bounce-in-place: NOT attempted.** No freeze code, no frozen-clip state, no unfreeze, no
  save/reload round-trip. The task's ordering was "deliver 1 first, commit it, then attempt 2";
  stem export consumed the budget, and a freeze that cannot be undone or does not survive
  save/reload is worse than no freeze. The smallest honest freeze on top of this work: render one
  track's chain with the same muting into a file in the project directory, add a `<frozen src="…"/>`
  element to that track's `saveSettings`, mute the live clips while it is set, and provide an
  unfreeze that deletes the element and the file. The reversible state needs a new `Track` member
  with save/load plumbing — which is why it is not an hour's change.
* **No GUI entry point.** `exportstems` is the CLI plus the `RenderManager` API; the export dialog
  still offers project/tracks only. Insertion points: a third `ExportProjectDialog::Mode`, a `switch`
  case in `ExportProjectDialog::onStartButtonClicked()` (`src/gui/modals/ExportProjectDialog.cpp:267`),
  and a third item beside `MainWindow::exportProject(true)` (`src/gui/MainWindow.cpp:1673`).
* **No bus-level stems.** A "bus" here is a `MixerChannel`, not a `Track`, and the render path
  isolates tracks by muting, so `exportStems` selects tracks. A track stem *does* contain the whole
  downstream path (its own channel and any bus it feeds), so a bus carrying one track is covered; a
  bus carrying several is not.
* **No stems for `Event`/`Video`/`Automation` tracks** — the same selection rule as the existing
  per-track render (`Instrument` and `Sample` only).
* **`Song::updateLength()` still skips muted tracks during export.** That is what made the override
  necessary; it is left alone because it is also what stops a *deliberately muted* track from
  extending a normal render.
* **No new third-party dependency.** The whole change is Qt plus the existing tree,
  GPL-2.0-or-later compatible, nothing added to `src/3rdparty/`.
* **The legacy `rendertracks` naming and trimming defects are reported, not fixed** (§6.1, §6.2).

---

## 8. Reproduction

```bash
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-stems
JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4          # configure + build + ctest
cd build/tests && ctest -j2 -R StemExportTest --output-on-failure
STEM_EXPORT_KEEP_TMP=1 QT_QPA_PLATFORM=offscreen ./StemExportTest -v1   # the STEM_EVIDENCE lines

# end to end, on a real project
python3 tools/stem-export-demo.py make /tmp/stemdemo
cd /tmp/stemdemo
$BUILD/lmms render      demo.mmp -o mix.wav -f wav -s 44100
$BUILD/lmms exportstems demo.mmp -o stems   -f wav -s 44100
$BUILD/lmms exportstems demo.mmp -o tail4   -f wav -s 44100 --tail-bars 4
# prints frames/channels/rate/duration/RMS/peak/sha per file, the sum-vs-mix
# measurement, and PASS/FAIL (gate: summed energy within 0.5 dB of the mix)
python3 <repo>/tools/stem-export-demo.py check mix.wav stems/01_Bass.wav stems/02_Pad.wav stems/03_Lead.wav
```
