# Import detection: transient, BPM and key from an audio file

**Feature row 34 of `docs/FEATURE-LIST-0.3.0.md`** ("Transient / BPM / key detection on import",
section 6), whose two named dependencies — the tempo map (OWNER-31 item 16/26) and the scale
machinery — are both in this line. The owner's own scope note is `BACKLOG.md` **item 10**, and this
feature follows it rather than exceeding it:

> "on import, one offline pass filling *(a)* tempo (BPM) and *(b)* the first transient, shown as
> **suggestions** the user accepts — never applied silently. Key and pitch detection are a
> **separate, harder** item and should not ride in with (a)/(b)."

and, on the dependency question:

> "aubio is GPL-2.0-or-later (compatible), Essentia is AGPL-3.0 (**a blocker**), and a hand-rolled
> spectral-flux detector needs no dependency at all. **Choose explicitly.**"

This document is the record: what was built, by which method, where it writes, what was **measured**
on this box, what was **not**, and what the feature deliberately does not do.

---

## 1. The three command ids, and what each is for

| id | class (SPEC A16) | what it does |
|---|---|---|
| `detect.analyze` | `not_mutating` | Reads ONE audio file and returns the tempo (BPM), the first transient (seconds and frames) and the key (tonic + scale name). **Writes nothing** — this is the suggestion. |
| `detect.apply` | `true_inverse` (recorded ACTION) | Analyses a file and writes the result into the **project's own fields**: the tempo into the **tempo map**, the key into the project's `detected-key` field. A half the file gave nothing for is **refused**, and a refusal writes neither half. |
| `detect.get_state` | `not_mutating` | Reads back what the project holds (the key field, the map's state and its tempo at tick 0), plus the two method names, the declared bounds, the published scale vocabulary — and the accuracy sentence (`method.accuracy_note`). |

`detect.apply`'s arguments are `path` (required), `tempo` (bool, default true), `key` (bool, default
true) and `max_seconds` (number, default 60, maximum 300). `detect.analyze` takes `path` and
`max_seconds`. Argument and result schemas are registered in `src/core/ControlCommandsDetect.cpp`;
`control.commands_list` / `zene_control_commands_list` publish them, and the MCP tool surface picks
them up like every other group (`tools/mcp-zene-control/zene_control/commands_snapshot.json` is
DERIVED from a live instance — regenerate, never hand-edit).

**The suggestion is never applied silently.** Nothing in the product runs a detection on its own:
there is no import hook that fires it, and no UI that calls it (§8). The only way a detection
reaches the project is a caller naming `detect.apply`.

## 2. Where the result is written (the project's own fields)

* **Tempo → the tempo map.** One tempo-only event at **tick 0**, with the map switched on. Tick 0 is
  the map's own "total override" (`include/TempoMap.h`, decision 2): every tick from the start of the
  timeline answers the detected tempo, and nothing before tick 0 exists to retime. The map's events
  are **integers** (`TempoMapEvent::tempo`), so an applied tempo is the estimate **rounded to the
  nearest whole BPM**; the reply reports `bpm` (what was written), `detected_bpm` (the exact
  estimate) and `rounded` (whether the two differ), and the deviation is therefore visible rather
  than hidden. A detected tempo outside the map's own `10..999` range is **refused**, not clamped.
* **Key → the project's `detected-key` field.** One `<detected-key>` element inside `<song>`
  (`include/ProjectKey.h`), carrying `tonic`, `pitch-class`, `scale`, `confidence`, `margin`,
  `method` and `source`. It is written **only when it holds something**, so a project that never ran
  a detection re-saves byte for byte as before, and it is cleared on project load when the element is
  absent and by `Song::clearProject` — the groove pool's own rule
  (`docs/GROOVE-POOL.md` §4), which is what makes the pre-first-detection state reachable again.

**The scale vocabulary is the pre-existing one, and it is not re-invented here.**
`InstrumentFunctionNoteStacking::ChordTable` (`include/InstrumentFunctions.h`) — the same table the
piano roll's scale combo is filled from — is the candidate set: entries filtered by the table's OWN
definition of a scale (`Chord::isScale()`, i.e. more than six semitones), deduplicated by pitch-class
mask in table order (two entries can share a mask — the table holds both `Aeolian` and `Minor` for
`{0,2,3,5,7,8,10}`; the **first** name wins, so the same input always reports the same name), and
filtered by one declared information rule: a template covering more than nine of the twelve pitch
classes carries no key information and is skipped (the table's 12-note `Chromatic` entry would
otherwise win on any noisy recording). A winning template whose mask has no name in that table is
reported as `scale_known: false` and **refused** by `detect.apply` rather than written.

The piano roll's OWN key and scale are window state (`src/gui/editors/PianoRoll.cpp` writes them as
attributes of the piano roll's element), which a headless build has no object for; **this feature
does not move them** (§8).

## 3. The method: tempo

**Named: `spectral-flux-autocorrelation`.** Implemented in `include/ImportDetectionDsp.h` /
`src/core/ImportDetectionDsp.cpp` (Qt-free, no file I/O, no dependency):

1. 1024-frame Hann frames, 512-frame hop; spectral flux = the half-wave-rectified sum of positive
   magnitude differences between consecutive frames (1024-point transform, `TempoFrameSize`).
2. The onset envelope: the running mean over ±0.35 s subtracted from the flux and the negative half
   discarded, so a crescendo is not a beat.
3. The transients: local maxima above `max(peak × 0.10, 1e-3 × the signal's RMS)`. The **first** one
   is reported (`first_onset_seconds`, `first_onset_frame`) — it is where a clip would have to start
   for its first hit to land on the grid, and it is the "(b) the first transient" half of item 10.
4. The autocorrelation of that envelope over lags inside the declared **40–240 BPM** band.
5. The lag is chosen by the **prior-weighted** autocorrelation: a log-Gaussian weight centred at
   120 BPM with a 0.9-octave width. This step is not decoration — see §5.1, where the measurement
   that put it there is recorded.
6. The period is refined against the **longest in-band harmonic** of the winning lag: the true period
   of a 128 BPM click track is 40.37 hops, which an integer lag can only read as 40 (a 0.9 % error),
   while the same period at three times the lag is 121.1 hops (0.1 %), and dividing by three carries
   that precision back.
7. `found` is false — nothing is reported — below **four transients** or below a **0.2**
   normalised autocorrelation. `confidence` is that autocorrelation: **the envelope's periodicity at
   the chosen lag, not a probability**, and not an accuracy figure.

## 4. The method: key

**Named: `chroma-tonic-weighted-template-correlation`.** Same file:

1. 8192-frame Hann frames, 4096-frame hop; every bin is mapped to its nearest pitch class with a
   triangular weight over the half semitone either side, and each frame's chroma is normalised to
   unit sum before accumulation (so the loudest section of a track does not decide the key on its
   own). The band is **80 Hz – 4 kHz with RAMPED edges** (full weight 180 Hz – 2.5 kHz): a raised
   cosine across each edge, not a step.
2. The final chroma is normalised so its largest bin is 1.
3. Every **(template, tonic)** pair in the caller's vocabulary is scored by `templateScore()`: the
   **Pearson correlation between the chroma and the template's tonic-weighted degree pattern**
   (the tonic counts `TonicWeight` = 2, its other degrees 1, everything else 0), so the score is in
   −1..1 and a template is rewarded for the notes it explains **and penalised for the ones it
   expects and the recording does not have**. Ties are broken by the first candidate in the caller's
   order and then the lowest tonic, so the same input always reports the same key; a winner whose
   correlation is not positive is **not reported at all** (`found: false`).
4. The tonic is weighted for a reason that is measurable: a template's degree SET cannot tell
   relative keys apart — C major and A aeolian are the same seven pitch classes — so a plain set
   match ties on exactly the question a key estimate exists to answer. The chroma mass on the tonic
   breaks that tie. The weights are this project's own choice and are declared here rather than
   borrowed: **no published key-profile constant (Krumhansl–Kessler or otherwise) is copied into this
   feature**, because a borrowed profile would carry a claim about real music this lane cannot
   measure (§5).
5. `margin` is `best − runner-up` in the score's own units: a **rank margin**, not a probability.

### 4.1 Two things the measurements changed (kept here, because they are the point)

Both were found by **running** the registered test rather than by reading the code, and both are
recorded because a reader deserves to know what the numbers were before:

* **A mean of the degrees is the wrong score.** The first version scored
  `chroma[tonic] + 0.5 × mean(chroma over the template's other degrees)`. The mean is diluted by
  every degree the recording does not play, so a *smaller* template wins on material it does not
  describe: the A major fixture came back as **“Neopolitan”, margin 0.003** over the right answer
  (the wrong name, all but tied). The correlation above replaced it the same day, and the same
  fixture now reports **Major, score 0.839, margin 0.077** against the whole vocabulary.
* **A step edge on the chroma band votes for the wrong pitch class.** With a hard 110 Hz floor, a
  **110 Hz sine produced a chroma of A# = 1.000, A = 0.712, B = 0.325** — the lower half of the
  note's main lobe was cut off, and the surviving energy was all on the upper side, so a tuning fork
  read a semitone sharp (the same tone an octave up, where no edge is near it, mapped correctly).
  The ramped edges above fixed it: the same 110 Hz sine now reports **A = 1.000** with A# = 0.383.

## 5. Accuracy: what is MEASURED here, and what is NOT

### 5.1 What is measured

Everything below was produced by this lane on this box. The arithmetic is exercised twice: by the
registered `ImportDetectionTest` (in-tree, through the engine's own decoder) and by
`tools/import-detection-proof.cpp`, a standalone driver over the same Qt-free unit that a developer
can compile with one `g++` command on a machine where the product does not build.

```
g++ -std=c++20 -O2 -Wall -Wextra -Iinclude tools/import-detection-proof.cpp \
    src/core/ImportDetectionDsp.cpp -o /tmp/import-detection-proof   # EXIT=0
/tmp/import-detection-proof                                          # EXIT=0
```

| synthesised input (answer known by construction) | measured | tolerance |
|---|---|---|
| click track at **128 BPM**, first click at 0.500 s | **128.131 BPM**, 21 transients, first transient **0.499 s** | ±0.5 BPM, ±30 ms |
| click track at **90 BPM**, first click at 0.500 s | **89.878 BPM**, 15 transients, first transient **0.499 s** | ±0.5 BPM, ±30 ms |
| A major scale (A B C# D E F# G# A) over an A bass, rooted at 220 Hz | tonic **A** (pitch class 9), template **major**, score **0.839**, margin **0.381** against the driver's two-template vocabulary / **0.077** against the full ChordTable vocabulary (the registered test) | tonic + template exact |
| a single 110 Hz sine (a tuning fork) | tonic **A** at pitch class 9, score 0.561 — and **not** the A# the pre-fix band edge voted for | tonic exact |
| silence | tempo **not found** | must refuse |
| steady 440 Hz tone, no transients | tempo **not found** (0 transients) | must refuse |
| an empty vocabulary | key **not found** | must refuse |

The confidence values that ride with those numbers are the detector's own scores (0.667 and 0.565 for
the two click tracks; 0.839 for the key fixture's correlation); they are reported because they are
what the algorithm has, not because they were calibrated against anything.
**Nothing above was tuned on real music** — the constants that changed (§4.1) changed because a
synthesised fixture with a KNOWN answer disagreed with them, which is the only tuning signal this box
has.

### 5.1b What the two REGISTERED proofs measured, running for real

The whole product was built on this box (`cmake -DWANT_QT6=ON …`, `make -j2 ImportDetectionTest
zene` → both EXIT=0) and both registered proofs were then run **from `<build>/tests`**:

```
ctest -R ImportDetectionTest   --output-on-failure   -> Passed   (7/7 checks)
ctest -R ControlDetectCommands --output-on-failure   -> Passed   (22/22 checks)
```

The QTest reported, through the engine's own decoder: **128.131 BPM** and **89.878 BPM** for the two
click tracks (tolerance 0.5), the first transient at **0.499 s**, and the A major fixture as **tonic A,
scale Major, correlation 0.839, margin 0.077** against the whole ChordTable vocabulary. The socket
transcript reported, over the real binary: `detect.apply` writing **bpm 128 (from a detected 128.131,
rounded true) as ONE event at tick 0 with the map switched on**, `transport.tempo_map_get` reading that
event back independently (`tempo_at_position` 128), `control.undo` reporting
`undone_command: "detect.apply"` and taking **both** halves off, `project.save` writing a file whose
XML carries `<detected-key tonic="A" … method="…template-correlation">` and `<tempo-map … bpm="128">`,
and four typed refusals (a missing `path`, a path that is not a file, `max_seconds: 0`, and a file with
no transients) each leaving the project untouched.

### 5.1c The honest side of the same run: a click track gets a NEAR-TIE key

The transcript's click-track fixture has no pitched content, and the estimator still reports a
best-scoring key for it — **tonic B, scale "Enigmatic", correlation 0.646, margin 0.018** — because
"which of these templates fits this chroma best" always has an answer. That is the feature's real
behaviour, and the number that matters is the **margin**: 0.018 is a near-tie and says "do not read
this name as a key". The registered transcript asserts exactly that property (a margin below 0.05 for
the percussion-only fixture) rather than a scale name the fixture cannot carry. **No margin threshold
is applied to suppress the report**, because no real-music corpus was measured to calibrate one: the
suggestion is reported with the evidence against it, and `detect.apply` writes it only when a caller
asks for that half by name.

### 5.2 What is NOT measured — stated plainly

**Real-world detection accuracy is unverified on this box.** No real-music corpus was analysed here:
every number above comes from a signal synthesised to have a known answer, and a click track is the
**easy case** for an onset/autocorrelation estimate. In particular, none of the following is
measured, and no accuracy figure for real music is quoted anywhere in this release:

* how often the tempo estimate is right on a full mix (percussion, rubato, a slow intro, a tempo
  change inside the analysed span);
* the **half/double-time ambiguity** inside the declared band. The 120 BPM-centred prior resolves
  the metrical relatives of a *click track*; a 60 BPM ballad with busy subdivisions, or a 200 BPM
  track, can still be reported at a relative, and the prior itself biases toward the centre by
  construction;
* how often the key estimate names the key a musician would name — the literature on this problem is
  large, the honest band for it in this register was `[RESEARCH NEEDED]` (`BACKLOG.md` item 10), and
  the answer this feature gives is a **suggestion with a rank margin**, not a verified key;
* anything about tuning, microtonality or material outside 12-tone equal temperament (the chroma is
  built on 12 equal-tempered pitch classes by definition).

* how a **real** track's key would be reported: the only key fixtures measured are a synthesised scale
  over its own tonic and a single sine, and the near-tie behaviour in §5.1c shows the estimator will
  name something for material that has no key at all. The `margin` is the only warning it offers, and
  that margin is an uncalibrated rank difference;
The same sentence is on the wire: `detect.get_state` returns it as `method.accuracy_note`, so an
agent reading the surface cannot mistake a suggestion for a measurement, and every `detect.analyze`
reply carries the same two method names beside the numbers.

### 5.3 What the estimates cost

The analysis is **offline and import-time** (an allocation-heavy call — the realtime rule of
`AGENTS.md` §4 forbids it on an audio-thread path, and no audio-thread path calls it). It reads at
most `max_seconds` (default 60, hard maximum 300) from the **start** of the file; that window is
stated in the reply (`analysed_seconds`) rather than implied. The transform is a hand-rolled
iterative radix-2 FFT: the feature adds **no dependency at all**, and the licence question item 10
asks about is answered by not having one to verify.

## 6. The A16 answer, honestly

The rows live in `src/core/ControlReversibilityTableDetect.cpp` and are joined into the one contract
table (`reversibilityRowTable()`), so `control.transactions` and the anti-drift test see them like
every other row. `detect.analyze` and `detect.get_state` write nothing: `not_mutating`.

`detect.apply` is `true_inverse` through a **recorded ACTION checkpoint**, because neither half is
reachable by a live object checkpoint: the tempo map is not a `JournallingObject` and is not inside
the Song's own checkpoint (which captures `TrackContainer::saveSettings` — the finding
`ControlCommandsTransportMap.cpp` records), and the key is a plain value on the `Song`. Both states
are captured **before the first write** — the map by value, the key as its own XML text, carried
whole in the transaction's before-state — and the recorded step writes both back, so **one
`control.undo` takes a whole detection off**. The pre-first-detection state is expressible and not a
special case: an empty key is a value `ProjectKey::clear()` writes, and an empty, inactive map is
what `TempoMap::operator=` restores. There is **no redo half beyond the recorded pair** — re-issuing
`detect.apply` is the forward path — and a refusal never leaves half a detection behind, because
every refusal condition is evaluated before the first write.

## 7. What the feature deliberately does not do

* **No automatic application.** No import path calls it; the socket is the surface (§8).
* **No pitch detection.** Item 10 splits it out explicitly ("a separate, harder item"), and it is not
  here.
* **No chord detection, no key-aware quantise, no transposition.** The detected key is a stored
  label; nothing else in the engine reads it yet.
* **No tempo curves.** The applied tempo is ONE map event (`docs/TEMPO-MAP.md`: events are steps), and
  a detected tempo that changes inside the analysed span is reported as one number.
* **No sub-BPM tempo.** The map's tempo is an integer; fractional tempi are rounded (§2) and the
  rounding is reported.
* **No moving of a clip to the first transient.** The transient is reported
  (`first_onset_seconds`, `first_onset_frame`); placing material against it is an edit the caller
  performs.
* **No chord/scale vocabulary beyond the table above**, and no per-key spelling choice (a detected
  key is a tonic pitch class plus a scale name).

## 8. UI absence — one line

**Import detection is drivable through the control socket (`detect.analyze`, `detect.apply`,
`detect.get_state`) and has no interface: nothing in the product runs a detection, shows a suggestion
or accepts one, and `grep -rniI 'detect\.' src/gui/` returns no call site of these commands.**
`docs/KNOWN-LIMITATIONS.md` carries that sentence with the bounds above.

## 9. Reproducing the proofs

```bash
# the engine arithmetic, with no Qt, no FFTW and no libsndfile (this box can run it):
g++ -std=c++20 -O2 -Wall -Wextra -Iinclude tools/import-detection-proof.cpp \
    src/core/ImportDetectionDsp.cpp -o /tmp/import-detection-proof
/tmp/import-detection-proof; echo "EXIT=$?"

# the registered engine proof (needs the product built):
ctest -R ImportDetectionTest --output-on-failure        # from <build>/tests

# the registered socket transcript (needs the product built):
ctest -R ControlDetectCommands --output-on-failure      # from <build>/tests
```

**Both registered proofs were RUN on this box** (§5.1b); `ImportDetectionTest` writes its fixtures byte by byte (its own RIFF/WAVE writer — not libsndfile,
so the decoder is not validated by its own library) and reads them back through the engine's own
`SampleDecoder`, which is also the path an import takes. `ControlDetectCommands` synthesises the same
fixtures with the Python standard library's `wave` module, drives the **real binary** over
`--control-socket`, and checks the project's own fields — including that `project.save` carries both
into the file and that one `control.undo` takes both off.
