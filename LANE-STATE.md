# LANE-STATE — 030/clip-note-stem-verbs (the 0.3.0 verb wave)

**Worktree** `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wverbs`
**Branch** `030/clip-note-stem-verbs`
**Base** `3956ef589` on `release/0.3.0` (the assigned base; NOT rebased onto the moving merge tip)
**Tip** `b2f09b8e4` (`1c699a593` the feature commit, `b2f09b8e4` the gate-7 length fix)
**Not merged. Not pushed. `tools/mcp-zene-control/zene_control/commands_snapshot.json` NOT regenerated**
(merge-time step, done by the merge lane after the last command-group merge — see the skill's touch-point 9).
**Build dir** `zene-030/wverbs/build` (to be DELETED at hand-off; space freed reported at the end).
**Logs** `/home/kruzzzzy/zene-030-wverbs-3956ef589/logs/` (private per-run dir, never `/tmp/<name>.log`).

---

## 1. What is verifiably done (command + exit code)

| What | Command | Exit |
|---|---|---|
| configure (smallest config that still exercises the code) | `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=OFF -DWANT_VST3=OFF -DWANT_CLAP=OFF -DWANT_CARLA=OFF -DWANT_LV2=OFF -DWANT_SUIL=OFF -DWANT_SOUNDIO=OFF -DWANT_STEM_SPLIT=OFF -DWANT_QT6=ON -DWANT_SESSION_VIEW=ON -DZENE_TELEMETRY=OFF` | **0** |
| gate 9 fork-sources | `bash tests/fork-sources-gate.sh` | **0** (397 entries, 0 stale) |
| gate 4 complexity | `bash tests/complexity-gate.sh --check` | **0** |
| gate 6 no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | **0** |
| gate 7 file-length | `bash tests/file-length-gate.sh --check` | **0** (was **1** — see §3) |
| gate 8 duplication | `bash tests/duplication-gate.sh --check` | **0** (2.07% vs 5% budget) |
| unregistered tests | `bash tests/unregistered-tests-gate.sh` | **0** (127 scanned, 125 registered, 2 declared) |
| evidence gate | `bash tests/evidence-gate.sh` | **0** (6262 files, 0 refused) |
| fork-sources recipe reproduces | the recipe under "Verify it" in `tests/fork-sources.txt` | **0** → prints `REPRODUCES` |
| all-sources recipe reproduces | the recipe under "Verify it" in `tests/all-sources.txt` | **0** (empty diff) |
| build | `cmake --build build -j 2` | see §3 |

## 2. The ids registered, and their A16 class

| id | args | result | class | mechanism |
|---|---|---|---|---|
| `clip.trim` | `clip` (req), `start` (req, 0..MaxSongLength), `end` (opt) | `clip, position, length, end, offset` | **`true_inverse`**, reversible | live **Clip** journal checkpoint |
| `clip.slip` | `clip` (req), `offset` (req, signed) | `clip, position, length, offset` | **`true_inverse`**, reversible | live **Clip** journal checkpoint |
| `note.probability_set` | `clip`, `note`, `probability` (0..1, all req) | `clip, note, probability, position, length, key` | **`true_inverse`**, reversible | live **MidiClip** journal checkpoint |
| `render.stems` | `out` (req, absolute dir), `format` (enum wav/flac/ogg/mp3), `tail_bars` (0..64) | `directory, format, sample_rate, tail_bars, count, stems[]` | **`not_mutating`**, not reversible | writes output artefacts; no project state |

`render.stems` is `cmd.mutating = false` (the `render.render` shape): it must not record a project
transaction, and its A16 row documents the declared render bound in the `mechanism` field.

## 3. What is RED, and why

- **Research note: upstream HEAD has moved.** `git ls-remote origin HEAD` returns
  `518a7e8ef525a276ba9702df87c613ea0ff25c47`, **not** the `4e677cb6c` the brief names. The fork's
  baseline commit in both manifests is still `4e677cb6c6ab`, which is correct (that is the fork
  point) and is what I verified against. Recorded, not a defect.
- **gate 7 was RED and is fixed.** `tests/src/core/ControlRegistryTest.cpp` went to **501** lines
  because the 84→88 comment I added was two lines longer than the text it replaced.
  `bash tests/file-length-gate.sh --check` → **EXIT=1**, message
  `REGRESSION: new file over 500 lines: tests/src/core/ControlRegistryTest.cpp (501)`.
  Fixed by stating the same facts in the original line count (now **499**); re-run → **EXIT=0**.
  No baseline was moved (`--reanchor` was not used).
- **`ControlCommandsSnapshot` is EXPECTED RED** while this branch is unmerged: the bridge's
  committed offline snapshot carries the pre-wave id set and the four new ids are unknown to it.
  **This is a collected red, not a stop** — regenerating is a merge-time step this lane is
  explicitly forbidden to perform. Run it and record the result:
  `cd build/tests && ctest -R ControlCommandsSnapshot --output-on-failure`. Its failure text prints
  the exact `python3 tools/mcp-zene-control/snapshot_commands.py --socket <sock>` command.
- **Build/ctest results are appended below** once the build finished (§4). Until then the two new
  registered proofs (`ControlVerbInverseTest`, `ControlStemExportVerb`) are **written but not yet
  run**, and that is stated rather than claimed.

## 4. Build + test results (appended)

```
(pending — see "next exact command")
```

## 5. Next exact command

```bash
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wverbs
cmake --build build -j 2 > /home/kruzzzzy/zene-030-wverbs-3956ef589/logs/build.log 2>&1; echo EXIT=$?
cd build/tests && ctest -R 'ControlVerbInverseTest|ReversibilityContractTest|ControlRegistryTest' \
  --output-on-failure > /home/kruzzzzy/zene-030-wverbs-3956ef589/logs/ctest-verbs.log 2>&1; echo EXIT=$?
```

Then, in order:

```bash
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wverbs
bash tools/local-ci.sh --build-dir build --jobs 2            # ctest must be 100%; 0 tests is an ERROR
bash tests/run-all-gates.sh                                   # exit 0 or 3
bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build
cd build/tests && ctest -R ControlCommandsSnapshot --output-on-failure   # expect RED while unmerged
```

## 6. Remaining acceptance list (the bar, item by item)

- [x] `clip.trim` registered + schemas + A16 row + UI-absence lines
- [x] `clip.slip` registered + schemas + A16 row + UI-absence lines
- [x] `note.probability_set` registered + schemas + A16 row + UI-absence lines
- [x] `render.stems` registered + schemas + A16 row + UI-absence lines + declared bound
- [x] engine named by file/symbol for all four (see the commit message and the section headers)
- [x] a registered proof authored for each row (`ControlVerbInverseTest`, `ControlStemExportVerb`)
- [ ] `cmake --build build -j 2` → EXIT=0 (in flight)
- [ ] `ctest` from `build/tests` → the two new proofs GREEN
- [ ] `bash tools/local-ci.sh --build-dir build --jobs 2`
- [ ] `bash tests/run-all-gates.sh` (exit 0 or 3)
- [ ] `bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build`
- [ ] the NEGATIVE CONTROL, recorded (see §8)
- [ ] `rm -rf build` and report the space freed
- [ ] commit LANE-STATE.md; STOP (do not merge, do not push)

## 7. Verbatim replacement row text for `docs/FEATURE-LIST-0.3.0.md`

The feature list is owned by the audit lane (`zene-030-audit`) — **I did not edit it.** The parent
replaces rows 60, 61, 67 and 68 with these lines verbatim. The table's column shape is
`| # | feature | id | status | sources |`.

**Row 60** (replaces the `**to build**` line):

```
| 60 | Clip trim | `clip.trim` | **done (0.3.0)** — the id is registered with an argument schema (`clip`, `start`, optional `end`), a result schema (`clip`, `position`, `length`, `end`, `offset`) and a `true_inverse` A16 row; the handler follows the song editor's own left-edge drag (`src/gui/clips/ClipView.cpp`) so the start, the length and the source offset move together and the audio stays at the same song position, which is what neither `clip.move` nor `clip.resize` can do alone. Proof: ctest `ControlVerbInverseTest` (invoke → read back → `control.undo` → read back, plus the A16 contract row). UI absence stated in `docs/KNOWN-LIMITATIONS.md` and the release notes: socket-only, no menu item/action/keybinding reaches the command. Dependency OWNER-31 item 12 / `#611` was a dependency, not a gate: the engine needed is `Clip`'s own serialized `pos`/`len`/`off`/`autoresize`, all present at the base. Declared limit: the verb does not author `SampleClip`'s `srcin`/`srcout` window (no reset-on-absence exists for it, so a pre-first-edit checkpoint could not undo it) — a frame-domain trim is a later feature | verdict Group A #6; `PLANNED-WORK-MASTER-LIST` :100, :387-390; branch `030/clip-note-stem-verbs` |
```

**Row 61** (replaces the `**to build**` line):

```
| 61 | Clip slip | `clip.slip` | **done (0.3.0)** — `clip.*` is now 12 ids and carries both clip-editing verbs. `clip.slip` moves the audio INSIDE a fixed clip rectangle: position and length do not move and the part of the source that plays at the clip's start becomes `offset` ticks into it. It is the FIRST implementation of the verb in the product — a case-insensitive grep for `slip` over `src/` + `include/` returns seven hits, every one a comment, and the nearest concept (the comp take's `srcpos`) is recorded but deliberately not applied (`docs/COMPING.md`) — so the design decision is recorded in `src/core/ControlCommandsClipTrim.cpp` and in the A16 row: the offset domain (`off`, serialised unconditionally) rather than the frame-domain window. `true_inverse` through the clip's own checkpoint. Proof: ctest `ControlVerbInverseTest`. UI absence in `docs/KNOWN-LIMITATIONS.md` + release notes | verdict Group A #6; `PLANNED-WORK-MASTER-LIST` :387-390; branch `030/clip-note-stem-verbs` |
```

**Row 67** (replaces the `**to build**` line):

```
| 67 | `note.probability_set` — the probability verb the boarded-gaps list names | **done (0.3.0)** — the engine is `Note::probability()` / `setProbability(float)` (`include/Note.h:140-141`, clamped to [0, 1] at `src/core/Note.cpp:158-161`), persisted per note as the OPTIONAL `prob` attribute (`src/core/Note.cpp:282-285`), read back with a default of 1 (`:325-329`) and consumed at trigger time by `NoteRandom::passesProbability` (`src/tracks/InstrumentTrack.cpp:906-911`). The registered id adds an argument schema (`clip`, `note`, `probability`), a result schema and a `true_inverse` A16 row through the owning `MidiClip`'s checkpoint — the `note.velocity_set` shape, because a `Note` is a SerializingObject and not a JournallingObject. A probability outside [0, 1] is REFUSED typed before the checkpoint, never silently clamped (SPEC A11). Proof: ctest `ControlVerbInverseTest` — invoke → read back → `control.undo` → read back from the state the trap lives in (a note at the DEFAULT, which writes no `prob` attribute at all). UI absence is already stated by `docs/MIDI-DEPTH.md` and is repeated in `docs/KNOWN-LIMITATIONS.md` and the release notes | verdict Group A #14; `PLANNED-WORK-MASTER-LIST` :387-390; audit Table B #11; branch `030/clip-note-stem-verbs` |
```

**Row 68** (replaces the `**partial**` line):

```
| 68 | Stem export — per-track / per-bus, post-fader, tail convention, headless CLI (distinct from stem *separation*, row 26) | `render.stems` | **done (0.3.0), with three declared limits** — the engine is `RenderManager::exportStems()` (`include/RenderManager.h:40-52` `StemExportOptions{tailBars=1, alignToProjectLength=true}`, `:76`) driven through the shipped `lmms exportstems` CLI (`src/core/main.cpp:381`, `:1002-1008`, `:1033-1036`) in a child process, exactly as `render.render` drives `lmms render` and for the same reason (`src/core/ControlCommandsProject.cpp:296-309`, `:327-333`). The registered id adds an argument schema (`out` absolute directory, `format` enum, `tail_bars`), a result schema (`directory`, `format`, `sample_rate`, `tail_bars`, `count`, `stems[]`) and a `not_mutating` A16 row. Proof: ctest `ControlStemExportVerb` — a REAL export over a live socket (two tracks, the documented `<index>_<name>.wav` naming, non-empty RIFF/WAVE files, and a second export into the same directory that must report the files it REWROTE rather than refuse an empty set). **Declared limits, all in `docs/KNOWN-LIMITATIONS.md`:** (a) **per track, not per bus** — a "bus" is a `MixerChannel`, not a `Track`, and the render path isolates tracks by muting (`docs/STEM-EXPORT.md`, "No bus-level stems"); (b) **per-fader and post-effects is what the engine does**, and the tail is one bar past the project end by default (`tail_bars` settable), which is the whole-project render's own convention; (c) **the render bound is declared, not fixed** — the child blocks the dispatch thread on `waitForFinished(600000)` so the control surface does not answer, `control.ping` included, until the export finishes (`docs/RENDER-CHILD-WAIT.md:120-126` records the defect and designs the deferred-reply fix; this lane does NOT build it), and the ctest gives the call its own declared per-command budget (`RENDER_TIMEOUT = 180.0`, the `tests/freeze_bounce_evidence.py:57` number) instead of raising a socket timeout. UI absence in `docs/KNOWN-LIMITATIONS.md` + release notes: the File menu's "Export Tracks..." action is the different, pre-existing `renderTracks()` path and is neither changed by nor wired to this id | verdict Group A #5 and #14; `docs/STEM-EXPORT.md`; `PLANNED-WORK-MASTER-LIST` :387-390; branch `030/clip-note-stem-verbs` |
```

**The limits lines written to the two docs** are reproduced verbatim in §9 so the parent can check
`docs/KNOWN-LIMITATIONS.md` and `docs/RELEASE-NOTES-v0.3.0-alpha.md` against them.

## 8. The negative control (the trap)

The trap, tested against this tree:

> A checkpoint captures the object's XML **before** the write, and the engine serialises some
> features **only when non-default**, so restoring a pre-first-edit state may not take the feature
> back.

Two findings, and they are different — this is the useful part:

1. **For the two clip-edge verbs the trap is REACHABLE and is the reason for the design.** The
   authored source window is `SampleClip::m_window` (`srcin`/`srcout`): written only when it is not
   the whole buffer (`src/core/SampleClip.cpp:468-473`) and applied on load **only**
   `if (_this.hasAttribute("srcin") || _this.hasAttribute("srcout"))` (`:549-556`) — **there is no
   reset-on-absence for it**, unlike `Note`'s `prob` (`attribute("prob","1")`). A Clip checkpoint
   taken before a *first* window edit therefore cannot take that edit back. `clip.trim` and
   `clip.slip` are consequently authored **outside** the window, against the four attributes both
   `SampleClip::saveSettings` and `MidiClip::exportToXML` write **unconditionally** (`pos`, `len`,
   `off`, `autoresize` — `src/core/SampleClip.cpp:459-469`, `src/tracks/MidiClip.cpp:435-436`) and
   whose loaders read unconditionally (`SampleClip.cpp:551-565`, `MidiClip.cpp:531-532`).
2. **For `note.probability_set` the trap is present as a RULE but is not reachable**, because
   `MidiClip::loadSettings` calls `clearNotes()` and re-creates every note
   (`src/tracks/MidiClip.cpp:497-507`), so each restored note starts from `m_probability = 1.f`
   (`include/Note.h:339`) before `Note::loadSettings` reads the attribute. Stated plainly rather
   than claimed as a save.

**The negative control to run (needs a rebuilt engine, so it is a recorded procedure):** neuter the
reset-on-absence and expect the proof to FAIL. For finding 1, patch `SampleClip::saveSettings` to
the "only when non-default" pattern and the matching `loadSettings` to guard its read:

```cpp
// src/core/SampleClip.cpp — NEGATIVE CONTROL, revert immediately
if (startTimeOffset().getTicks() != 0) { _this.setAttribute("off", startTimeOffset()); }
// ...
if (_this.hasAttribute("off")) { setStartTimeOffset(_this.attribute("off").toInt()); }
```

Rebuild, then `cd build/tests && ctest -R ControlVerbInverseTest --output-on-failure`. Expected:
**FAIL** on `clipSlipIsOneUndoableStep` — the checkpoint's XML has no `off` (the clip started at 0),
the guarded read skips, and the offset stays at the slipped value while the shipped code restores it.
Revert and rebuild: expected **PASS**. That is the proof that the unconditional read, and not luck,
is what carries the class.

## 9. Files created / modified

**Created (fork-NEW, declared in BOTH manifests):**
- `src/core/ControlCommandsClipTrim.cpp` — `clip.trim`, `clip.slip`
- `src/core/ControlCommandsNoteProbability.cpp` — `note.probability_set`
- `src/core/ControlCommandsRenderStems.cpp` — `render.stems`
- `src/core/ControlReversibilityTableVerbs.cpp` — the three live-checkpoint A16 rows
- `tests/src/core/ControlVerbInverseTest.cpp` — ctest `ControlVerbInverseTest`
- `tests/control-stem-export-verb.py` — ctest `ControlStemExportVerb`

**Modified:** `include/ControlRegistry.h`, `include/ControlReversibility.h`,
`src/core/CMakeLists.txt`, `src/core/ControlRegistry.cpp`,
`src/core/ControlReversibilityTable.cpp` (3-line join), `src/core/ControlReversibilityTablePassive.cpp`
(the `render.stems` row), `tests/CMakeLists.txt` (two ctests + one `offscreen` property),
`tests/all-sources.txt`, `tests/fork-sources.txt`, `tests/src/core/ReversibilityContractTest.cpp`
(histogram `183/99/14/4/66` → `187/102/14/4/67`), `tests/src/core/ControlRegistryTest.cpp`
(surface count `84` → `88`, original line count preserved), `docs/KNOWN-LIMITATIONS.md`,
`docs/RELEASE-NOTES-v0.3.0-alpha.md`.

**Not touched, by instruction:** `docs/FEATURE-LIST-0.3.0.md`, `zene-030`, the v0.2.1-alpha tag,
every other lane's worktree, `tools/mcp-zene-control/zene_control/commands_snapshot.json`.
