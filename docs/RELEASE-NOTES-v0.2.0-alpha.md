# Zene Studio 0.2.0-alpha

**The first release under the product's own name.** This is an early alpha — expect crashes, missing features
and rough edges. It is for testing and feedback, not for music you cannot afford to lose. Read the known
limitations before you install.

> **Verification convention.** `[VERIFY AT FREEZE]` marks a claim that must be re-checked against the built
> artefact before this text ships; nothing carrying that marker may be published as-is, and no unverified
> claim may be published *without* one. The convention exists because three of the last four documentation
> passes in this project caught claims that were true when drafted and false when shipped. The rule for this
> release: **every capability claim must be traceable to a build option or a test.**
>
> **Provenance of this file.** Applied verbatim from `drafts/RELEASE-NOTES-v0.2.0-alpha-DRAFT.md` (reviewed)
> on `post-alpha/release-prep`, based on `post-alpha/integration` at `34c1f4f86`, except for the marker
> resolutions and lane-status markers noted inline. Which markers were resolved against the tree, which still
> owe the freeze, and why — `docs/RELEASE-PREP-0.2.0.md`, §3.
>
> **Digests.** The publish step appends the SHA-256 block generated from the published release
> (`docs/RELEASING.md`, "The publish sequence", steps 4–5). Digests are never typed into this file.

## The two headlines

**1. It is called Zene Studio now.** The application, the packages, the desktop entry, the manual page and
the file paths carry the product's own name instead of the upstream project's. What deliberately does *not*
change: the licence notices and the "derived from LMMS" attribution, which stay because this is a derivative
work under GPL-2.0-or-later and keeping them is a condition of the licence, not a naming choice. Verified
against the tree: the residue list is enumerated in `docs/WAVE-R-RENAME.md` §6 — the `lmms::` namespace and
the `LMMS_*` macros; the `<lmms-project>` file format, `creator="LMMS"` and the 175 shipped presets; the
licence headers and the attribution; the `lmms_plugin_main` entry symbol and the `lmms-plugin-logo` resource
key (54 call sites), which is why existing native plugins still load; user state (`~/.lmmsrc.xml`,
`~/Documents/lmms/`, the `lmms-workspace` portable marker), because renaming it would orphan an existing
install's settings and projects; the JACK/PulseAudio client name `lmms`, for the same reason; the registered
MIME types; upstream URLs and identities; and the historical documents.

**2. You can load an instrument.** The 0.1.0-alpha rehearsal build advertised VST3 and CLAP hosting but
contained neither — the CI never provisioned the SDKs and the build degraded silently. Both are now
provisioned and compiled in, and **instrument** hosting works: load a VST3 instrument on a track, play MIDI
into it, hear audio, save the project, reload it and find the instrument and its state intact.
The witness is verified in this tree: the only instrument this release is proven against is the
purpose-built MIT VST3 fixture that ships in the source, `tests/data/vst3-test-instrument/`, with the
standalone probe `Vst3InstrumentFixtureProbe` (`tests/CMakeLists.txt:737-752`) and the SDK's own validator
result recorded in `docs/VST3-INSTRUMENT-FIXTURE.md` §0 (47 tests passed, 0 failed, including *"No bypass
parameter found. This is an instrument."*) and §5 (the fixture consumes MIDI from `ProcessData::inputEvents`
and renders audio in response: 16/16 checks).
`[VERIFY AT FREEZE: the host-side load → MIDI in → audio out → save → reload run — the implementing lane is
`post-alpha/instrument-hosting-impl` (7566607f2) and it is NOT an ancestor of the release-prep base
34c1f4f86, so this half of the claim cannot be verified from this tree]`

**The instrument's own editor does not open yet** — but the instrument window does, and we ran it rather than
assuming: load a VST3 instrument, and a window opens listing that plugin's controls in the host's generated
grid. That is the honest state and it is why this is an alpha. Verified against this tree: the plug-in's own
editor does not exist in the host — `grep -rn IPlugView src/ include/ plugins/Vst3Effect/ plugins/ClapEffect/`
returns **0** hits (the only `IPlugView` occurrences in the repository are inside the vendored Carla copy of
the VST3 SDK headers, `plugins/CarlaBase/carla/source/includes/vst3sdk/...`), so what a user gets is the
generated `Knob` grid, exactly as `docs/INSTRUMENT-HOSTING-SPEC.md` §0 records.
`[VERIFY AT FREEZE: the shipped-binary run of that window (the "Bass" instrument window opening, showing
"Controls for Zene VST3 Test Instrument" and the `Level` knob) — that evidence is on
`post-alpha/instrument-view-safety`, which is not an ancestor of the release-prep base]`
Three more things belong next to that
claim rather than in a footnote: **no third-party VST3 instrument has been tested by us** — the only instrument
this release is proven against is a purpose-built test instrument that ships in the source tree — there is **no
multi-out**, no preset management, and **no instrument latency compensation in PDC**.

## What else is new

- **A crash reporter**, offline and local: a crash writes a small report you can attach to a bug report. No
  network, no telemetry — sending anything is a separate, opt-in choice.
- **Autosave that does not strand you.** Previously a stale recovery file left the application waiting at a
  prompt before autosave had even started; recovery is now gated on the file belonging to *this* project and
  being newer than it.
- **Plugin scanning with a cache and a quarantine list**, so a large plugin folder is not re-scanned on every
  start and a plugin known to be bad can be hidden. Verified against the tree: the user-facing surface is a
  data-layer one, not a UI — the cache is JSON on disk and the documented way to quarantine a plugin is a
  `{"path": …, "reason": …}` entry in it, and `docs/PLUGIN-SCAN-CACHE.md` §5 records that there is no GUI for
  it yet.
- **A loudness meter and a loudness report on export** — integrated LUFS, short-term maximum and true peak,
  measured against the EBU R128 target, and independently cross-checked against a reference implementation.
- **MIDI learn**: focus a control, move a knob, and the binding is saved in the project.
- **Stem export**: render a project's tracks as separate files that line up and sum back to the mix.
- **Automation modes** — Read / Touch / Latch / Write — so riding a fader in Read cannot destroy automation
  you already wrote. Verified against the tree, and stated as a limitation deliberately: **sample-accurate
  automation playback is NOT in this release** — automation is evaluated once per tick rather than per frame,
  so a value lands on a tick boundary rather than on a sample (`docs/AUTOMATION-MODES.md`, which names the
  line that blocks a per-frame producer).
- **A clip model that survives a playback pass.** Trimming a clip used to be reverted by the next playback;
  the authored window is now authoritative and is saved with the project. Editing gestures are not in yet.
- **A warp engine**: markers that pin positions in the audio and let a clip follow (or lead) the project
  tempo. Time-stretching is done by resampling, so it **changes pitch** — a 2× stretch is an octave up.
  `[VERIFY AT FREEZE: post-alpha/warp is not in the release-prep base 34c1f4f86, so no part of this bullet is
  verifiable from this tree]`
- **Note probability and velocity jitter**, seeded and saved per note so a take is repeatable, plus a note
  search-and-transform operation.
- **Racks**: parallel chains on a channel with a chain selector, saved with the project. **There is no UI and
  no scripting binding yet** — a rack can only be created by loading a project that already contains one — and
  macros, key/velocity zones, per-chain latency compensation and undo are not in. Switching chains is not
  crossfaded, so it can click.
  `[VERIFY AT FREEZE: post-alpha/racks is not in the release-prep base 34c1f4f86]`
- **VCA / mix-and-edit groups**, saved with the project and tested for bit-exact reversibility. Same caveat as
  racks: **you cannot create one from the interface yet.**
  `[VERIFY AT FREEZE: post-alpha/vca is not in the release-prep base 34c1f4f86 — no `<vcagroup>` handling
  exists anywhere under src/ or include/ in this tree]`
- **Per-note MPE expression**, captured from MPE input and stored on the note. **Pitch is applied on playback;
  pressure and timbre are captured and stored but not applied**, and there is no expression editor.
  `[VERIFY AT FREEZE: post-alpha/mpe is not in the release-prep base 34c1f4f86]`
- **Opt-in telemetry, off by default**, with a preview of the exact payload and a closed 24-field allowlist
  that cannot carry a project name, path, plugin name, email, IP or installation ID. **There is no server to
  send to yet, so nothing leaves your machine even if you switch it on.**
  `[VERIFY AT FREEZE: post-alpha/telemetry is not in the release-prep base 34c1f4f86]`
- **Auto-mastering on the command line** (`zene master`): one render, several measured candidates scored
  against a named loudness target with integrated LUFS, short-term maximum and true peak. It **generates and
  measures; it does not pick a "best"** — there is no preference scorer.
  `[VERIFY AT FREEZE: the exact subcommand and the candidate list — post-alpha/auto-mastering is not in the
  release-prep base 34c1f4f86, and no `master` action exists in `src/core/main.cpp` there, so neither the
  subcommand nor a candidate list can be confirmed]`
- **Scripting and git tooling improvements**: a versioned Lua API surface with a compatibility policy and
  console, and a deeper `.mmpz` merge driver that refuses to silently lose an edit.

## Fixed since the rehearsal build

- **It no longer aborts on exit.** The published rehearsal build could die with
  `QFATAL: QThread: Destroyed while thread is still running` — an audio worker thread outliving the engine that
  owns it, then being deleted as a child during teardown. It is load-dependent, which is why it never showed up
  in a quiet run: reproduced **31 times in 184 suite runs** under load, and the teardown that races it takes
  ~9.5 s on a 20-core box because the engine waited **500 ms per worker** and gave up on 19 of them. The cause
  is now pinned down rather than described: one worker is parked in a condition wait while `quit()` and the
  single wake are non-atomic, so a preempted worker **misses the wake**, and the shutdown timeout expires while
  it is still a live child of the engine. Fixed in the product (a bounded re-check in the worker's wait, and an
  unbounded join when the engine is destroyed), proven red/green by reverting only the re-check — the new
  teardown test is then deterministically red, `stranded: 8` of 8 workers, and green 4/4 with the fix. Verified
  under real interference: **30 parallel suite runs, 46/46 passing each, 0 aborts, 1,380 test executions.**
  `[VERIFY AT FREEZE: post-alpha/test-hygiene is not in the release-prep base 34c1f4f86 — `git log --all
  -S"stranded"` attributes this fix to it, and neither the re-check nor the teardown test is in this tree]`
- **A use-after-free in the MIDI-learn notification tool.** `MidiLearnGui` held a raw `QAction*` and
  dereferenced it after the object that owned it had gone (a segfault at `0x188`). It was found by turning the
  coverage gate on for the first time: the tool's own test **crashed**, which is why the file had no coverage
  record at all. Fixed with a guarded pointer; the test passes and the file now measures 86 %.
- **Four test sources that could never run now do, and a new gate stops the fifth.** They were in the tree and
  invisible: one could not even compile (its compressor-library definition was undefined), and three others were
  never registered, so the suite reported fewer tests than it appeared to contain. The suite went **41 → 46**,
  and a new gate now fails the build when a test source is not registered anywhere — with a deliberately red
  negative control proving the gate itself can fail.
  `[VERIFY AT FREEZE: this item shares the post-alpha/test-hygiene owner with the teardown fix above; the
  registration gate it names is not in the release-prep base 34c1f4f86]`

- **Renders are now reproducible — which they were not.** Rendering the same project twice used to produce
  different audio: measured on the nine projects this repository bundles, **six differed on every run and a
  seventh intermittently**, the worst on 98.3 % of its frames. Loudness never moved, so no level, RMS or LUFS
  reading could see it — only a sample-level comparison could. The cause was the offline renderer spreading each
  period's work across a worker pool, so which thread ran which job was a scheduling decision. Exports now render
  on one thread: **7 of the 9 bundled projects are bit-reproducible**, five subsequent renders of the test project
  being byte-identical to the single-threaded answer. Two remain non-deterministic and are **not** the fix's
  fault — for both, pinning to one CPU, disabling ASLR, a fixed `rand()` and a frozen clock *each* still produce
  different files, so their cause is in the instruments rather than the renderer, and it is named in our notes.
  Live playback is unaffected (the pool still runs it). Stock LMMS has this defect too — this is a fix we made
  that upstream has not, and we made it because it corrupted our own testing evidence.
  The two are named in the tree: `demos/StrictProduction-DearJonDoe.mmp` and
  `shorties/Root84-TrancyLoop.mmpz` (`docs/RENDER-DETERMINISM.md` §9, "2 of 9 bundled projects are still not
  reproducible"; §10 is the six falsification experiments — CPU pinned, ASLR disabled, `rand()` fixed, clock
  frozen, a mute bisection and a per-track isolation pass — that place the cause inside those projects'
  instruments rather than in the renderer). The post-fix sweep is that lane's measured result, recorded in
  §7 of the same document; it was not re-run for this file.

- **The recorder no longer writes wraparound garbage for out-of-range samples.** Correction to an earlier
  assumption of ours: the **WAV and FLAC exporters already clipped** (`SFC_SET_CLIPPING` at
  `AudioFileWave.cpp:89` / `AudioFileFlac.cpp:83`) — both verified in this tree — so exported files were never
  corrupt. The wrap was on the **recorder's** writer, and it is **latent rather than always-on** — it cannot be
  reached until a track can be armed. It is fixed by clamping in C++ on the writer thread rather than by
  enabling the library flag, because the flag was measured to shift *in-range* negative samples by 1 LSB; the
  fix keeps in-range audio bit-identical.
  `[VERIFY AT FREEZE: the fix itself — post-alpha/recording-realtime is not in the release-prep base
  34c1f4f86, and the writer thread in this tree (`src/core/audio/TrackRecorder.cpp:149-180`) clamps nothing:
  its only `std::clamp` is the input-channel index at `:57`]`
- **The capture path no longer takes the model lock.** Pushing input frames could block behind the GUI's model
  lock (measured: a push blocked for the full 600 ms another thread held it), which on the JACK/SDL callback
  thread is a realtime hazard. A fixed-capacity lock-free staging ring now carries the frames instead, with
  zero allocations measured on the append path and exact conservation under overload (1,024,000 frames pushed
  → 16,384 staged, 1,024,000 dropped, nothing silently lost).
  `[VERIFY AT FREEZE: post-alpha/recording-realtime is not in the release-prep base 34c1f4f86. The ring
  itself is older than that lane — `include/TrackRecorder.h:3,50,66` and
  `src/core/audio/TrackRecorder.cpp:137-141` describe one input channel -> a lock-free SPSC ring buffer, from
  the two-track recording prototype (`1dd9840d0`, `3f12dbf58`) — but the 600 ms measurement and the overload
  conservation figures are not recorded anywhere in this tree]`
- **Saving no longer reports success when it failed.** A failed rename during save could lose a project while
  telling you it saved.
  `[VERIFY AT FREEZE: post-alpha/saveload-integrity is not in the release-prep base 34c1f4f86, and the defect
  is still observable there: the two renames that publish a saved project, `src/core/DataFile.cpp:435` and
  `:438`, have their return values discarded and `DataFile::save` returns `true` regardless of whether they
  succeeded]`
- **A project saved by a build without the Session View feature no longer silently loses that data.**
- **A set of long-standing mixer concurrency defects** — a one-click mute dropout, a crash on plugin-handler
  reallocation, a use-after-free when adding a channel, an unguarded effect reorder, and a sidechain-cleanup
  omission — found by an external concurrency audit, confirmed to be inherited from upstream, and fixed with
  a thread-sanitiser before-and-after.
  `[VERIFY AT FREEZE: the TSAN evidence, and the fix itself — post-alpha/mixer-concurrency is not in the
  release-prep base 34c1f4f86]`
- **Save failures are no longer reported as successes.** A failed move-aside during save — including the case
  Qt refuses by design, renaming over an existing file — left the project unwritten while the application
  reported success. The save now refuses loudly, keeps the destination byte-identical, and leaves the written
  project recoverable beside it. A project that cannot be saved also no longer leaves the document in a
  degraded state that disabled autosave and undo until restart.
  `[VERIFY AT FREEZE: post-alpha/saveload-integrity is not in the release-prep base 34c1f4f86. This bullet
  and the "Saving no longer reports success when it failed" bullet above make the same claim twice; which one
  survives is a decision for the freeze, not a silent deletion here]`

## Known limitations

This is an alpha and the list is long; a separate **known limitations** page covers it in detail. The short
version: no instrument editor; instrument hosting is one-per-track and proven only against our own test
instrument; no clip editing gestures, take lanes or comping; no plugin-scanning interface worth the name; no
stable project format; no measured crash-free rate (the reporter is new); VCA groups and racks exist in the
project format but **cannot be created from the interface**; and **renders are reproducible for 7 of the 9
bundled projects, with two still not** — those two are named in the limitations page, along with the reason
the cause is inside their instruments rather than in the renderer.
A **reported defect that is not fixed in this release**: a sample whose rate differs from the project's plays
at the wrong pitch (a 48 kHz sample in a 44.1 kHz project is about 8.8 % sharp). It is recorded here rather
than quietly dropped.

## Your projects

Keep backups. Files saved by this build may not open in a later build, an older build, or in LMMS. An older
1.3-alpha build **will open** a new file and *silently drop* parts of it — which is worse than refusing, so do
not open a 0.2 project in an older build. Each save also writes a `.bak` file next to your project; its
location is documented below. The version of the application that wrote a project is stamped into the file,
and that is what drives the behaviour above.
Verified against this tree: the `.bak` is `<project file>.bak`, beside the project —
`src/core/DataFile.cpp:347` composes `fullName + ".bak"` and `:435` moves the current file there before the
new one is renamed into place at `:438` (skipped when `app/disablebackup` is set, `:427`) —
and `creatorversion` is the project-root attribute recording the version of the build that wrote the file
(written at `:140` and `:2105`, read back at `:2179`), which drives both the "Version difference" notice and
the selection of the upgrade routine to run (`legacyFileVersion()`, `:2226-2238`).

## Coming from 0.1.0-alpha

Your settings and your projects are migrated rather than orphaned: on first run, if the new configuration
location is absent and the old one exists, the old settings are adopted.
`[VERIFY AT FREEZE: NOT TRUE OF THIS TREE, and the sentence must be rewritten or removed before it ships.
`post-alpha/integration` @ 34c1f4f86 contains no migration code at all (`grep -rniI "migrat" src/ include/`
matches only comments about the *plugin* migration), and the rename lane deliberately kept the old paths:
`docs/WAVE-R-RENAME.md` §6 ("User state") records that `~/.lmmsrc.xml`, `~/Documents/lmms/` and the
`lmms-workspace` marker were left alone because renaming them would orphan an existing install. There is no
"new configuration location" to move to, so there is nothing to migrate]`

---

## Freeze status (was: "TODO before this text is applied")

Resolved against the tree and the binary at `post-alpha/release-prep` base `34c1f4f86`; the full table is
`docs/RELEASE-PREP-0.2.0.md` §3.

1. **Replace every `[VERIFY AT FREEZE]` with a verified fact — or delete the claim.** *Partly done.* Every
   marker that this tree and its built binary can settle is settled inline above. Every marker that remains
   names its blocker: a lane that is **not an ancestor of the release-prep base** (the dominant case — the
   merge train was still filling), or an artefact that **does not exist until the release is published**.
   **Nothing was deleted**, and no claim was unmarked.
2. **Fill the version string.** *Done.* The built binary reports
   `Zene Studio 0.2.0-alpha.123+34c1f4f` for an untagged build of this base (the version is derived from
   `git describe` by `cmake/modules/VersionInfo.cmake`), and `Zene Studio 0.2.0-alpha` when built from the
   `v0.2.0-alpha` tag or with `-DFORCE_VERSION=internal` — which is the string that ships. Exact output and the
   two configure lines: `docs/RELEASE-PREP-0.2.0.md` §1.
3. **Add the artefact list and the SHA-256 digests block, generated from the published release rather than
   typed.** *Pending by design — needs the published artefacts.* The digests are appended by the publish step
   (`docs/RELEASING.md`, steps 4–5). The asset **names** follow from the verified package-name pattern
   `${CMAKE_PROJECT_NAME}-${VERSION}-<platform>` (`build/CPackConfig.cmake:45` produces
   `zene-0.1.0-alpha.123+34c1f4f-linux-x86_64` on this base), so the release's assets are
   `zene-0.2.0-alpha-<platform>.<ext>`; the per-platform extension list is the release job's upload glob
   (`.github/workflows/build.yml`, `.AppImage` / `.dmg` / `.exe`).
4. **Cross-check every capability line against `tests/advertised-features.tsv`.** *Done, with a correction.*
   The manifest binds **compile-time capability**, and it carries six rows: the three hosts this release
   documents as present (`vst3effect`, `vst3instrument`, `clapeffect`) and the three features it documents as
   absent (`session-view`, `wasm-sandbox`, `stem-separation`). It is not, and by its own header cannot be, a row
   per runtime feature — so "delete any claim not represented in the manifest" taken literally would delete
   these notes. What the manifest does enforce is that no documented-**present** host is compiled out, and that
   no documented-**absent** feature is compiled in. The runtime bullets are guarded by their tests. Checked
   line by line in `docs/RELEASE-PREP-0.2.0.md` §4.
5. **Have an independent reader compare this text against the built binary, not against the plans.**
   **Not done here, and it is not this lane's to do.** This lane is the author of the resolutions above; an
   independent reader is a second party, and the parent should nominate one before the tag.
6. **Delete any claim not represented in `tests/advertised-features.tsv`.** See item 4: the manifest's scope is
   compile-time capability, stated in its own header ("the build options that must hold for that claim to be
   true"), and its five-column schema has no room for a runtime feature. Read as a command to delete every
   runtime bullet, the instruction self-destructs. The two claims removed from this draft on 2026-09-12 —
   out-of-process hosting, and a racks bullet that omitted "cannot be created from the interface" — were
   removed for the real reason: they were **false or unproven**, not because they were absent from the
   manifest. The session view stays deliberately **absent** from these notes: `WANT_SESSION_VIEW` defaults off,
   the release jobs do not pass it, and there is no clip launcher even with it on.
