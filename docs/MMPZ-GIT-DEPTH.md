# mmpz-git depth — task #612

What the mmpz-git merge tooling did before this change, what it does now, and
the evidence for both. Everything below was measured on this worktree
(`post-alpha/mmpz-git-depth`, base `0c23587d2`) with the binary built once via
`JOBS=4 tools/local-ci.sh --build-dir build --jobs 4` (configure OK, build OK,
`ctest` 25/25 from `build/tests`, overall exit 0).

Task #612's shape, from the product's own review pass (`docs/STATUS.md:227`):
"three-way merge of concurrent track edits, musical conflict presentation, large
assets, audible-diff CLI, CI render recipes".

## 1. What existed, with file:line

`tools/mmpz-git/mmpz_git.py` (818 lines at the base commit):

| capability | where |
| --- | --- |
| qCompress container codec | `is_container` :39, `decompress` :53, `compress` :63 |
| Qt-shaped XML serialisation | `serialize` :113, `to_bytes` :185 |
| canonical form | `canonicalize_dom` :210, `canonical_xml` :264 |
| semantic diff → operation list | `diff_dom` :332, `cmd_diff` :397 |
| merge driver | `Conflict` :430, `_conflict_comment` :444, `merge_elem` :467, `cmd_merge` :577 |
| git filter/diff/merge config | `cmd_install` :718 |
| CLI | `main` :754 |

Supporting files: `demo_edits.py` (deterministic DOM edits), `run-demo.sh`
(the existing end-to-end demo; its merge sections are 6a "different tracks" and
6b "conflicting BPM"), `gitattributes.sample`, `scratch/` probes.

Its status of record: `docs/STATUS.md:24` — "`mmpz-git` (filters, semantic diff,
merge driver)". Its test suite is `tools/mmpz-git/tests/test_mmpz_git.py`, 15
tests over five classes: `Container` :64, `Verbatim` :90, `Canonical` :110,
`Diff` :183, `Merge` :212 (including `test_disjoint_track_edits_merge_clean`
:231 and `test_conflicting_bpm_is_a_resolvable_conflict` :241). Baseline run:
**Ran 15 tests, OK (skipped=1), EXIT=0** — the byte-verbatim round-trip contract
(`test_verbatim_roundtrip`: every shipped fixture except the legacy
`tests/emptyproject.mmp` reproduces its input bytes exactly) is the contract
this change must not break.

## 2. Where the merge gave up (measured, before changing anything)

I built base/ours/theirs variants of the real fixture
`data/projects/shorties/sv-DnB-Startup.mmpz` (368 notes, 9 tracks, 51 patterns)
and ran the driver directly. Note counts are the observable:

| case | exit | conflicts | result |
| --- | --- | --- | --- |
| A: ours adds a note to `Bass`, theirs to `Kick` (different tracks) | 0 | 0 | merged 370 = 368 + both edits — **works** |
| B: same track, different patterns | 0 | 0 | both edits kept — works |
| C: same pattern, different notes | 0 | 0 | both edits kept — works |
| D: same note, different velocity | 1 | 1 | conflict reported — works |
| **E: ours renames track `Kick`, theirs adds a note inside it** | **0** | **0** | merged 368: **theirs' note silently gone** |
| **F: ours deletes track `Kick`, theirs adds a note inside it** | **0** | **0** | merged 344: **theirs' note silently gone** |

The cause is one shallow test, in two places:

```python
# base 0c23587d2, merge_elem, lines 477 and 482 (and 533, 539)
if _attrs(a) == _attrs(o) and len(_children(a)) == len(_children(o)):
    return None      # "clean delete"
```

`len(_children(track))` counts the track's **direct** element children — the
instrument and the patterns. A note added inside a pattern changes neither the
track's attributes nor that count, so the driver concluded "ours did not touch
it", took the delete/rename as clean, and dropped the other side's notes while
returning exit 0 and writing **zero** conflict markers. That is exactly the
failure mode the task calls the worst one: a merge that silently drops a side.
It also silently reproduced through a **rename**, because `_identity` for a
track is `(type, name)` (:286), so a rename is indistinguishable from
delete+add under the old test.

Two smaller findings from the same probe:

* a conflict over an embedded sample (`sampledata`, written by
  `plugins/AudioFileProcessor/AudioFileProcessor.cpp:199`, and `data` by
  `src/core/SampleClip.cpp:299`) produced a **22,435-character** XML comment —
  both sides' full base64 blobs inlined — growing the project from 53,501 to
  86,901 bytes for one field;
* `mmpz-git --help` **crashed** (`argparse` expands `%` in help strings and the
  merge help contained `(%O %A %B)`) — confirmed pre-existing against the base
  commit, `ValueError: unsupported format character 'O'`.

## 3. What was added

All of this is in `tools/mmpz-git/`.

### 3.1 Deep modification test — never lose an edit silently

* `sig_index` :522 — an order-insensitive whole-subtree fingerprint for every
  element, built in one bottom-up pass (O(n log n); a recompute-per-node
  signature would be O(n²) on a 3 MB project).
* `merge_elem` :902 now takes pre-mutation fingerprint snapshots and uses
  `ia.get(id(a)) != io.get(id(o))` instead of the attributes-plus-child-count
  test at all four sites. Any change anywhere below the element now counts.
* `describe_change` :561 turns the difference into "+1 note" so the conflict
  says what the other side actually did.
* `audit_no_lost_edits` :854 + `symbolic_index` :828 — an independent post-merge
  check over the bytes about to be written: every element that either side
  changed or added, keyed by the merge's own sibling keys, must still be in the
  result. A violation is reported **and marked in the file**, never returned as
  success. It catches keying bugs the merge's own bookkeeping cannot see.

### 3.2 Musical conflict presentation

* Vocabulary grounded in this tree: `note_name` :458 uses
  `FirstOctave = -1`, `KeysPerOctave = 12` (`include/Note.h:79-80`, the rule
  behind `PianoRoll::getNoteString`, `src/gui/editors/PianoRoll.cpp:134`);
  `project_meter` :466 reads `<head timesig_*>` and applies
  `DefaultTicksPerBar = 192` (`include/TimePos.h:38`); `bar_beat` :488.
  `<note pos>` is pattern-relative and each `<pattern>` carries its own `pos`,
  so both are reported — conflating them would put every conflict in bars 1–2.
* `conflict_record` :613, `format_location` :639, `format_conflict` :670,
  `render_report` :690. Primary output is now, on stderr and from
  `mmpz-git conflicts <file>`:

```
mmpz-git: 1 conflict(s) to resolve in project.mmpz

  [1] track "Bass" > pattern "I" > note F#1 (key=30) at bar 1 beat 1 in the pattern
      note velocity: base 100 -> ours 40, theirs 90
      location: /song/trackcontainer type="song"/track name="Bass"/pattern name="I"/note pos="0"
```

* Each conflict is also marked **in the project file** by a comment that carries
  the readable lines plus a base64 JSON payload (`CONFLICT_DATA_TAG` :43), so
  `mmpz-git conflicts` can re-present it later without re-running the merge, and
  the payload survives XML's no-`--`-in-comments rule by construction. New
  subcommand `cmd_conflicts` :1228 / `marker_records` :1201.

### 3.3 A corruption bug found and fixed on the way

While validating "the binary must be able to read anything we write", a
conflicted output **hung the renderer**: `render` never returned (killed at
120 s and again at 400 s), where a healthy render of the same project takes
~1.5 s. Bisected with a 60 s timeout per placement:

| comment placed as a child of | `render` |
| --- | --- |
| `<trackcontainer>` | **exit 124 — hangs** |
| root `<lmms-project>`, `<head>`, `<song>`, `<bbtrack>`, `<instrumenttrack>`, `<fxchannel>`, `<track>`, `<pattern>`, `<note>`, `<instrument>`, `<automationpattern>` | exit 0 |

A `<trackcontainer>`'s element children are walked by the loader
(`src/core/TrackContainer.cpp:108-144`), and a comment among them is fatal.
The fix is not "never put markers near the conflict" but "never put one where
the loader walks": `host_of` :740 routes a comment aimed at a `<trackcontainer>`
to the host of its parent, `merge_elem` threads that host down the recursion
(parents detach their children before recursing, so `parentNode` is not
available at the conflict site), and `unsafe_comment_hosts` :752 is checked on
the finished bytes — if a marker ever lands in a container the driver **refuses
to write** (exit 2) rather than hand the DAW a project it cannot open.

### 3.4 Audible-diff CLI

`cmd_audible_diff` :1489, backed by `render_tracks` :1297 (the renderer's native
`rendertracks <project> -o <dir>`, one WAV per track) and a full-mix `render_mix`
:1290. `wav_mono` :1318 reads PCM WAVs by hand (8/16/32-bit, any channel count,
either byte order); `bar_windows` :1359, `bar_rms_db` :1381,
`bar_peak_delta_db` :1397, `compare_bars` :1427.

Two metrics per bar, because one is not enough: a bar counts as different when
its loudness changed by ≥ `--threshold-db` (default 1.0 dB) **or** its
sample-level difference reached `--peak-threshold-db` (default −60 dBFS).
Measured sensitivity, adding one note to the Bass at song bar 3:

```
track "Bass"     bar 3 differs (max 0.15 dB RMS, -18.7 dBFS peak)
full mix         bar 3 differs (max 0.06 dB RMS, -18.7 dBFS peak)
```

0.15 dB RMS alone would be missed by any sane loudness threshold; the peak
metric is what localises it. Turning a Bass note down moved bar 1 by 13.53 dB
RMS and was reported as `bars 1-2 differ`.

### 3.5 Large assets

What actually happens: LMMS does **not** inline samples referenced by path
(`<audiofileprocessor src="drums/bassdrum04.ogg">`); a sample is inlined only as
a **base64 attribute** — `sampledata` (AudioFileProcessor.cpp:199, SlicerT.cpp:333),
`data` (SampleClip.cpp:299), `chunk`/`state` (VST/CLAP plugin state,
VstPlugin.cpp:304, ClapEffectControls.cpp:111) — plus project bundles
(`writeFile(..., withResources=true)`, DataFile.cpp:315) which copy resources to
a directory instead. So for a project holding a large sample:

* the merge **copies the bytes verbatim** and never re-hashes them (a clean
  merge over an 8 KB embedded sample reproduces the base64 exactly — test),
* the diff and the conflict comment used to print both blobs in full. Now
  `abbrev_value` :500 summarises anything over `MAX_INLINE_VALUE = 256` as
  `"<10924 chars, sha256=9938212aa8539cd7, prefix='AAECAw…'>"` in the report,
  in the diff (`diff_dom` :343) and in the marker; the **full value is written
  next to the file** in `<project>.mmpz-git-conflicts.json`
  (`_conflict_artifacts` :1156), so nothing is lost. One sample-only conflict
  went from a 22,435-character comment to a small one (project 86,901 → 66,075
  bytes, of which 10,924 is the asset itself).
* Not fixed, documented: a merge never rewrites asset bytes, so it cannot
  corrupt a sample; but two people editing the *same* sample still conflict at
  the value level (base64), which is not a waveform merge. `gitattributes.sample`
  already says this about LFS.

### 3.6 CI render recipes

`tools/mmpz-git/render-recipe.sh` — finds or builds the binary once, renders
with `QT_QPA_PLATFORM=offscreen`, prints each exit code unpiped, reports the
WAV size and sha256, and refuses to pass on an empty output. Two modes:
`-o FILE` (`render`, whole mix) and `--tracks DIR` (`rendertracks`, one file per
track). Verified end to end:

```
$ bash tools/mmpz-git/render-recipe.sh data/projects/shorties/sv-DnB-Startup.mmpz \
      -o /tmp/recipe-test/song.wav --samplerate 44100
[exit 0] render
rendered: /tmp/recipe-test/song.wav (2177112 bytes)
sha256  : 6b51f70fc32e993d7aaad3592e356954622ce842a0cbf1d6043962f185f24cb1
```

Determinism, which the whole audible-diff rests on: two renders of the same
project are **byte-identical** (`cmp` clean, same sha256), and per-bar RMS over
two renders agreed to 0.000000 dB on every bar of every track.

## 4. The two proofs

`bash tools/mmpz-git/depth-demo.sh` runs all three as a transcript in a
throwaway git repo with the real drivers installed, and exits non-zero if any
behaves differently. All exit codes below are printed unpiped.

### Proof 1 — a real three-way merge of edits to different tracks

Side A edits `Bass`, side B edits `Kick`, then `git merge`:

```
$ git merge --no-edit feat/drums
[EXIT=0]
--> git merge exit=0 (0 = merged clean, both edits kept)

$ demo_check.py project.mmpz --has-note 60:48 --has-note 36:96 --notes 370
  OK   --has-note     60:48            present
  OK   --has-note     36:96            present
  OK   --notes        370              370 notes
  PASS
```

368 notes in the base, 370 in the merge: both edits present, checked rather than
asserted.

### Proof 2 — the negative control

Both sides change the velocity of the *same* Bass note, differently:

```
$ git merge --no-edit feat/vol-theirs
CONFLICT (content): Merge conflict in project.mmpz
Automatic merge failed; fix conflicts and then commit the result.
[EXIT=1]

$ mmpz-git conflicts project.mmpz
mmpz-git: 1 conflict(s) to resolve in project.mmpz

  [1] track "Bass" > pattern "I" > note F#1 (key=30) at bar 1 beat 1 in the pattern
      note velocity: base 100 -> ours 40, theirs 90
```

The tool reports the conflict, names the track, pattern, note and bar, and says
what each side did. It does not pick a side silently.

### Proof 3 — the silent-loss class that used to return exit 0

Ours deletes the `Kick` track; theirs adds a note inside it:

```
$ git merge --no-edit feat/edit-kick
$ mmpz-git conflicts project.mmpz
  [1] track "Kick"
      ours removed this, theirs edited it (ours deleted, theirs changed it (+1 note))
$ demo_check.py project.mmpz --has-note 36:96     # theirs' note survived
  OK   --has-note     36:96            present
```

Before: exit 0, no markers, theirs' note gone. Now: exit 1, marked in the file,
theirs' note present, and a human decides. The equivalent rename case (E) is a
conflict too.

### And the file is still a project the DAW can open

Every file the tool writes is checked by the built binary through
`DataFile::loadData` (`render`, `src/core/DataFile.cpp:2128` — it parses XML
first and falls back to `qUncompress`), not through `dump`: `dump`
(`src/core/main.cpp:461`) calls `qUncompress` directly with **no** XML fallback,
so on the uncompressed form the merge driver writes it prints
`qUncompress: Input data is corrupted` and exits 0 with empty output — a vacuous
check. Measured: both a clean and a conflicted merge output render to 12.34 s of
stereo audio, exit 0.

## 5. Audible diff — exact coverage and limits

What it does: renders the two projects, matches the renderer's per-track WAVs by
track name, and reports per track and for the full mix which bars differ, with
the loudness delta (dB RMS) and the sample-level delta (dBFS peak) for each run
of consecutive bars. Exit 0 = no difference, 1 = differences, 2 = error.

Covered:

* which bars, and which track, differ — backed by a measurement, deterministic
  on this build (renders byte-identical across runs);
* a track present in one render and not the other (removed, added, or muted);
* a differing render format or length, reported as an error rather than
  compared.

Not covered, explicitly:

* it names the bar and the track, **not the note**; a change that leaves a bar's
  per-bar energy and peak identical is invisible;
* the bar grid assumes a straight `numerator/4` bar computed from `<head bpm>`
  and the project's own `DefaultTicksPerBar`; a project with tempo changes or
  odd meters is not handled and the tool says so in its own output;
* it needs a built binary (`--renderer`, `$MMPZ_GIT_RENDERER`, `build/lmms`,
  then `$PATH`), so it is not part of the build-free test path; the pure
  measurement functions (`bar_windows`, `bar_rms_db`, `bar_peak_delta_db`,
  `compare_bars`) are unit-tested with synthetic PCM WAVs and need no binary;
* `--track NAME` compares one track; it does not attribute a *mix* difference to
  an instrument.

## 6. Tests

Run: `python3 tools/mmpz-git/tests/test_mmpz_git.py -v` (no build needed for the
bulk; the binary-dependent classes skip when `build/lmms` is absent, exactly as
the pre-existing Qt6 test skips without `scratch/qtsave2`).

Added classes: `MergeDepth` :270 (8 tests — the deep test, the audit, the
marker-placement guard), `BinarySafety` :448 (clean and conflicted merge outputs
must load in the binary), `MusicalPresentation` :498, `LargeAssets` :586,
`PureAudioMaths` :679, `AudibleDiffBinary` :766, `Cli` :817.

The pre-existing 15 tests are unchanged in intent and still pass, including the
byte-verbatim round-trip and the canonical-form contracts.

## 7. Reproducing

```bash
# tooling tests (no build required for the bulk)
python3 tools/mmpz-git/tests/test_mmpz_git.py -v

# the two proofs, as a transcript with unpiped exit codes
bash tools/mmpz-git/depth-demo.sh

# build once, then reuse the same directory
JOBS=4 tools/local-ci.sh --build-dir build --jobs 4

# render recipes
bash tools/mmpz-git/render-recipe.sh song.mmpz -o /tmp/a.wav --samplerate 44100
bash tools/mmpz-git/render-recipe.sh song.mmpz --tracks /tmp/stems

# audible diff (per track + mix, or one track, or the mix only)
python3 tools/mmpz-git/mmpz_git.py audible-diff A.mmpz B.mmpz
python3 tools/mmpz-git/mmpz_git.py audible-diff A.mmpz B.mmpz --track Bass

# read a conflicted project's conflicts back
python3 tools/mmpz-git/mmpz_git.py conflicts project.mmpz
```

## 8. What is NOT done

* **Rename-aware merging.** Track identity is `(type, name)`, so a rename on one
  side plus an edit on the other is a *conflict* (both versions are kept, nothing
  is lost) rather than a clean merge that carries the edit into the renamed
  track. Changing the identity to drop `name` would make rename+edit merge
  cleanly, but it makes sibling pairing sensitive to insertion order, which is a
  worse trade for the failure mode this work exists to remove. Left as a
  conflict, deliberately.
* **Waveform/asset diffing.** Large assets are copied byte-for-byte and
  summarised by hash; there is no per-sample diff and no LFS integration beyond
  what `gitattributes.sample` documents.
* **Note-level audible attribution.** The audible diff stops at track+bar. There
  is no per-note or spectral attribution, and no tempo-map support.
* **Sequential reuse of the per-track stems for long projects.** Each
  `audible-diff` renders both projects from scratch; nothing is cached.
* `run-demo.sh`, the pre-existing end-to-end demo, was re-run against this
  change: **exit 0**, with `ROUND-TRIP: byte-identical` and `IDENTICAL: real
  lmms and our helper agree byte-for-byte` still reported, and its merge
  sections (6a different tracks, 6b conflicting BPM, 6c later merges) all
  exiting 0. Its `demo_edits.py resolve` still finds the new markers, because
  they contain the string `mmpz-git`. `depth-demo.sh` is the addition that
  *checks* the merge results rather than printing them.

## 9. Source registration, and the Gate 6 finding

`tests/fork-sources.txt` lists fork-NEW sources and feeds the C/C++ analysers
(`tests/complexity-gate.sh` via `lizard`, `tests/file-length-gate.sh`,
`tests/duplication-gate.sh`, `tests/run-coverage.sh`); its own regeneration rule
is `git diff --diff-filter=A … | grep -E '\.(cpp|c|h|hpp|cc|cxx)$'`, and it does
not sweep `tools/`.

Gate 6 turned red the moment this tooling was touched for the first time, and
the reason is worth writing down: `tests/no-upstream-regression-gate.sh`
compares `gate-base..HEAD` and classifies every changed path. `tests/*`, build
config, `.github/*` and `*.md` are allowed; everything else is a violation
unless it is listed in `tests/fork-sources.txt` (fork-NEW) or declared in
`tests/upstream-modifications.txt`. `git diff --name-only 01148947ea..0c23587d2
-- tools/mmpz-git` is **empty**, so the tooling had simply never been touched
since the gate base — Gate 6 was green by inattention, not by scope. Upstream
master has **no `tools/` directory at all**, so these files are fork-new.

I measured both ways out rather than picking one on taste:

* registering the six paths in `tests/fork-sources.txt` makes Gate 6 pass
  (`fork-NEW (allowed)`) but turns **two other gates red** —
  `file-length-gate.sh --check`: `REGRESSION: new file over 500 lines:
  tools/mmpz-git/mmpz_git.py (1892)` and
  `tools/mmpz-git/tests/test_mmpz_git.py (843)`; `complexity-gate.sh --check`:
  9 functions over CCN 10, including pre-existing ones (`diff_dom` CCN 34,
  `serialize` CCN 28, `canonicalize_dom.walk` 20, `cmd_diff` 12). Those two
  ratchets target C/C++ sources and grandfather by editing baselines that other
  lanes depend on, which is not a feature lane's call;
* declaring the six paths in `tests/upstream-modifications.txt` with truthful
  reasons leaves every other gate exactly as it was.

I took the second, and re-verified the neighbours are unchanged:
`file-length-gate.sh --check` PASS, `complexity-gate.sh --check` PASS,
`duplication-gate.sh` PASS (1.08% of a 5% budget), `no-upstream-regression-gate.sh`
PASS. The ledger entry's reason states plainly that these are *not*
upstream-inherited files, why they are not in `fork-sources.txt`, and what was
measured — so a future gate-config change (`tools/` given its own category,
which is what this really needs) can find it and act. This is a gate-scope
question surfaced in a feature lane, not solved in one.
