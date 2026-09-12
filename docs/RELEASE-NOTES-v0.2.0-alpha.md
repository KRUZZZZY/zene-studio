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
> **Provenance of this file.** Applied from `drafts/RELEASE-NOTES-v0.2.0-alpha-DRAFT.md` (reviewed) on
> `post-alpha/release-prep`, based on `post-alpha/integration` at `34c1f4f86`; the applying lane extended it
> while resolving markers. Those drafts were edited after that application, so on 2026-09-12 the corrected
> passages were **ported** into this file at the frozen tip `2239f3cb6` — an edit, never a replacement, because
> the applied copy carries marker resolutions the drafts lack. Every `[VERIFY AT FREEZE]` marker that remained
> was then re-checked against that frozen tree and its built binary: each one is either resolved inline to the
> verified fact, or the claim it guarded is deleted. Which markers were resolved, which were deleted, and why —
> `docs/RELEASE-PREP-0.2.0.md`, §3.
>
> **Digests.** The publish step appends the SHA-256 block generated from the published release
> (`docs/RELEASING.md`, "The publish sequence", steps 4–5). Digests are never typed into this file.

## The two headlines

**1. It is called Zene Studio now.** The application, the packages, the desktop entry, the manual page and
the file paths carry the product's own name instead of the upstream project's. What deliberately does *not*
change: the licence notices and the "derived from LMMS" attribution, which stay because this is a derivative
work under GPL-2.0-or-later and keeping them is a condition of the licence, not a naming choice.
**The residue is narrower than earlier drafts of this text claimed, and several things once listed here are
renamed rather than kept** — verified against this tree:
- **Kept deliberately, because renaming it is an ABI break**: the `lmms_plugin_main` entry symbol
  (`src/core/Plugin.cpp:228`). That symbol is *why* existing native plugins still load unchanged; renaming it
  would be a deliberate break and would be announced as one. Alongside it, the internal identifiers no user
  sees — the `lmms::` namespace, the `LMMS_*` macros and file names such as `lmmsconfig.h` — which were assessed
  with their size and cost and deliberately not attempted.
- **Backwards compatibility, not residue**: `DataFile.cpp` **writes** `<zene-project>`
  (`src/core/DataFile.cpp:128/136/313`) and its **reader still accepts** the old `<lmms-project>` root
  (`:1741-1742`), so old files open and new files are unambiguous.
- **No longer true, and corrected here**: the plugin-logo resource key is **`zene-plugin-logo`**, not
  `lmms-plugin-logo`; and the **JACK and PulseAudio client identity is `Zene Studio`**
  (`AudioPulseAudio.cpp` names the stream `Zene Studio`), not `lmms`.
- **Migrated rather than kept**: user state (`~/.lmmsrc.xml`, `~/Documents/lmms/`). Earlier text here said
  renaming it "would orphan an existing install's settings and projects"; that is no longer the case — the
  settings are **adopted**, which is why the paths in quotes above are the *old* ones being read from, not the
  ones being written to.
`docs/WAVE-R-RENAME.md` §6 still describes the pre-migration version of this list, and the rename lane's own
record is left as it was written; **the limitations page's "The name, honestly" is the accurate half.**
**Files we write identify us now, not upstream.** The WAV files this build renders carry
`Zene Studio (libsndfile-…)` in their software tag where they used to credit LMMS. It is a metadata string, not
audio — the rendered samples are provably identical, and we checked that specifically — but it is the kind of
trace that should not survive a rename. Verified against this tree: the tag is set at
`src/core/audio/AudioFileWave.cpp:91` and `:85` of the FLAC writer (`sf_set_string(m_sf, SF_STR_SOFTWARE,
"Zene Studio")`), and the chunk was read back out of a real render — `tests/integration-logs-3d/final/chunk-parse.log`
records `LIST chunk: INFOISFT Zene Studio (libsndfile-1.2.2)` where the earlier merge train's render held
`LMMS (libsndfile-1.2.2)`, the two files differing in that chunk and nothing else (`docs/INTEGRATION-MERGES-3B.md`,
the `INFOISFT` comparison).
**The artwork is ours now, and it is a placeholder.** An audit found 41 shipped identity images were still
upstream LMMS artwork — and replaced all of them with hand-drawn stand-ins marked as placeholders in their own
metadata. Nothing in this release traces or vendors anyone else's mark, so nothing here carries a licence or
attribution obligation. **The final mark is not in this release**; the placeholder exists so the product stops
shipping someone else's identity while the real one is chosen. Verified against this tree:
`docs/BRAND-PLACEHOLDERS.md` §0 and §1 are the audit — 41 files, 39 of them byte-identical to `origin/master`
and the other 2 identical in drawing data and renamed only in metadata, with **0** already replaced; §3c proves
0 of the 41 remain byte-identical to upstream, and §3a runs the plugin-logo resource test red and green (the
placeholder resolves to a real 48×48 pixmap; the assertion fails when the file is moved away).

**2. You can load an instrument.** The 0.1.0-alpha rehearsal build advertised VST3 and CLAP hosting but
contained neither — the CI never provisioned the SDKs and the build degraded silently. Both are now
provisioned and compiled in, and **instrument** hosting works: load a VST3 instrument on a track, play MIDI
into it, hear audio, save the project, reload it and find the instrument and its state intact.
The witness is verified in this tree: the only instrument this release is proven against is the
purpose-built MIT VST3 fixture that ships in the source, `tests/data/vst3-test-instrument/`, with the
standalone probe `Vst3InstrumentFixtureProbe` (`tests/CMakeLists.txt:806-821`) and the SDK's own validator
result recorded in `docs/VST3-INSTRUMENT-FIXTURE.md` §0 (47 tests passed, 0 failed, including *"No bypass
parameter found. This is an instrument."*) and §5 (the fixture consumes MIDI from `ProcessData::inputEvents`
and renders audio in response: 16/16 checks).
Verified against this tree: the host half is in the tree too — `docs/VST3-INSTRUMENT-HOSTING.md` §0 is the
implementing lane's own verdict ("one VST3 instrument loads onto one track, receives MIDI from a real
`MidiClip` through the track's existing MIDI path with sample-accurate timing, renders audio into the track,
and its state survives save/reload of the project"), and the lane `post-alpha/instrument-hosting-impl` that
wrote it is an ancestor of this tip (`git merge-base --is-ancestor post-alpha/instrument-hosting-impl
2239f3cb6` → 0). The end-to-end claim is therefore settled here rather than owed to the freeze. **The
in-tree regression test for that load path is not run by CI** — it lives behind
`WANT_VST3_TEST_INSTRUMENT` (default `OFF`), as the paragraph below states.

**The instrument's own editor does not open yet** — but the instrument window does, and we ran it rather than
assuming: load a VST3 instrument, and a window opens listing that plugin's controls in the host's generated
grid. That is the honest state and it is why this is an alpha. Verified against this tree: the plug-in's own
editor does not exist in the host — `grep -rn IPlugView src/ include/ plugins/Vst3Effect/ plugins/ClapEffect/`
returns **0** hits (the only `IPlugView` occurrences in the repository are inside the vendored Carla copy of
the VST3 SDK headers, `plugins/CarlaBase/carla/source/includes/vst3sdk/...`), so what a user gets is the
generated `Knob` grid, exactly as `docs/INSTRUMENT-HOSTING-SPEC.md` §0 records.
Verified against this tree: the shipped-binary run is in the tree —
`docs/INSTRUMENT-VIEW-SAFETY.md` §3 records the product run under Xvfb against a project carrying a VST3
instrument track on the "Bass" track, on the pre-fix and the post-fix binary alike: the window opens, the
process stays alive, and the window contains *"Controls for Zene VST3 Test Instrument"*, a `Level` knob and the
disclosure line. Two qualifications belong with it, and both were checked. **That run is out of band, not
CI**: the two VST3 instrument suites sit behind `WANT_VST3_TEST_INSTRUMENT` (`tests/CMakeLists.txt:796`,
default `OFF`), so the default CI configuration neither builds nor runs them — the guard was proven by running
the suite out of band, green with the guard and `SIGSEGV` exit 139 at address `0x8` without it
(`docs/INSTRUMENT-VIEW-SAFETY.md` §4). And **no physical display was used** — offscreen and Xvfb only (§6.1
names what a desktop session would still add).
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
  Verified against this tree: `docs/WARP.md` §0 is the implementing lane's report — `WarpMarkers` is a child of
  `<sampleclip>`, markers are pinned to source frames so a trim moves `sourceIn`/`sourceOut` and leaves the
  markers on the audio, the map is monotonic and exact at every marker, and with no markers the arithmetic is
  the pre-warp path bit for bit; a headless render puts a source whose transients sit at 0/1/2/3 s at
  0/0.5/1.0/1.5 s under a marker pair declaring 2×. Lane `post-alpha/warp` is an ancestor of this tip.
- **Note probability and velocity jitter**, seeded and saved per note so a take is repeatable, plus a note
  search-and-transform operation.
- **Racks**: parallel chains on a channel with a chain selector, saved with the project. **There is no UI and
  no scripting binding yet** — a rack can only be created by loading a project that already contains one — and
  macros, key/velocity zones, per-chain latency compensation and undo are not in. Switching chains is not
  crossfaded, so it can click.
  Verified against this tree: `docs/RACKS.md` §0 is the implementing lane's report — a mixer channel can hold
  a rack, two or more chains process the same input block and sum into the channel's output, and both the
  chains and the selector are persisted in the project; macros and key/velocity zones are named as out of the
  slice. The saved element is `<rack>` (`src/core/Rack.cpp:48`, `RACK_ELEMENT`), and the lane
  `post-alpha/racks` is an ancestor of this tip.
- **VCA / mix-and-edit groups**, saved with the project and tested for bit-exact reversibility. Same caveat as
  racks: **you cannot create one from the interface yet.**
  Verified against this tree: the group is a real entity — `src/core/VcaGroup.cpp` / `include/VcaGroup.h`, with
  the mixer owning them (`Mixer::createVcaGroup`, `src/core/Mixer.cpp:720`, and the gain applied on the audio
  path at `:528-545`) — and it persists: the save/load element is `vcagroup` with a `vca` child
  (`src/core/Mixer.cpp:1893`, `:1898`, `:2022`, `:2037`), so the `<vcagroup>` this bullet's limitations-page
  twin refers to is present in the tree and not merely planned. `docs/VCA-GROUPS.md` is the lane's report; the
  lane `post-alpha/vca` is an ancestor of this tip. No interface creates one, which is why the caveat stays.
- **Per-note MPE expression**, captured from MPE input and stored on the note. **Pitch is applied on playback;
  pressure and timbre are captured and stored but not applied**, and there is no expression editor.
  Verified against this tree: `docs/MPE.md` §0 is the lane's verdict — expression is captured from MPE-style
  input, stored backwards-compatibly on the note as `mpepitch` / `mpepressure` / `mpetimbre`, readable and
  editable through a headless API, with the pitch axis applied by the playback path
  (`src/core/NotePlayHandle.cpp`) and the pressure/timbre axes captured and stored but not applied; the doc
  names the exact lines that block a pressure/timbre path instead of inventing one. `src/core/midi/MpeExpression.cpp`
  is in the tree. Lane `post-alpha/mpe` is an ancestor of this tip.
- **Opt-in telemetry, off by default**, with a preview of the exact payload and a closed 24-field allowlist
  that cannot carry a project name, path, plugin name, email, IP or installation ID. **There is no server to
  send to yet, so nothing leaves your machine even if you switch it on.**
  Verified against this tree: `docs/TELEMETRY-V1.md` is the implementing lane's report — the payload is a
  closed **24-key** allowlist that refuses anything else (§3), the consent state defaults to all-false, the
  "what we send" dialog renders the exact bytes produced, and the kill switch is the configure option
  `ZENE_TELEMETRY` (`CMakeLists.txt:140`, default `ON`), whose `OFF` compiles the client and its networking
  code out — proved in that document's §5. `src/core/Telemetry.cpp`,
  `src/core/TelemetryNetworkTransport.cpp` and `src/gui/TelemetryConsentDialog.cpp` are in the tree. Lane
  `post-alpha/telemetry` is an ancestor of this tip. **The build's own `--version` line does not report the
  telemetry option** — `ZENE_TELEMETRY` matches neither `^WANT` nor `LMMS_(HAVE|DEBUG)`, so it never reaches
  the build-options dump (`src/CMakeLists.txt:6-14`); that is why `tests/advertised-features.tsv` carries no
  telemetry row, and its header says so.
- **Auto-mastering on the command line** (`zene master`): one render, several measured candidates scored
  against a named loudness target with integrated LUFS, short-term maximum and true peak. It **generates and
  measures; it does not pick a "best"** — there is no preference scorer.
  Verified against this tree: the subcommand is `master` (alias `--master`) in `src/core/main.cpp:371`, with its
  usage line at `:177` and its options at `:230-237`, and the run prints one line per candidate (`:281-286`).
  The candidate list too: `docs/AUTO-MASTERING.md` §0 is the lane's measured wave 1 — **one** project render
  feeds **five** mastering candidates, each scored with the merged BS.1770-4 meter against a named target, and
  explicitly not ranked. Lane `post-alpha/auto-mastering` is an ancestor of this tip.
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
  Verified against this tree: `docs/TEST-HYGIENE.md` §0 is the lane's own verdict — the abort is one
  `AudioEngineWorkerThread` that misses the single wake-up issued during teardown, and a `wait(500)` in
  `~AudioEngine` that gives up on it while the QThread is still a child; 31 of 184 engine-test runs aborted at
  load ~27 before the fix, 0 of 30 full-suite runs after. Both halves are in the tree: the bounded re-check in
  the worker's wait (`src/core/AudioEngineWorkerThread.cpp:204`) and the teardown assertion
  (`tests/src/core/AudioEngineTeardownTest.cpp:135-148`, `:170-179`, asserting `stranded == 0`). Lane
  `post-alpha/test-hygiene` is an ancestor of this tip.
- **A use-after-free in the MIDI-learn notification tool.** `MidiLearnGui` held a raw `QAction*` and
  dereferenced it after the object that owned it had gone (a segfault at `0x188`). It was found by turning the
  coverage gate on for the first time: the tool's own test **crashed**, which is why the file had no coverage
  record at all. Fixed with a guarded pointer; the test passes and the file now measures 86 %.
- **Four test sources that could never run now do, and a new gate stops the fifth.** They were in the tree and
  invisible: one could not even compile (its compressor-library definition was undefined), and three others were
  never registered, so the suite reported fewer tests than it appeared to contain. The suite went **41 → 46**,
  and a new gate now fails the build when a test source is not registered anywhere — with a deliberately red
  negative control proving the gate itself can fail.
  Verified against this tree: the gate is `tests/unregistered-tests-gate.sh` (Gate 10), wired into
  `tests/run-all-gates.sh:186`, with its own DECLARED table for the two sources left unbuilt on purpose and a
  negative control that exits 1 (`docs/TEST-HYGIENE.md` §7, §8 — the four recovered sources are
  `PhaseDSidechainTest`, `PhaseFChannelScaleTest`, `MixerRoutingBackwardCompatTest`, `MixerAbRegressionTest`;
  the suite count is 41 → 42 → 46, not the single jump the sentence above reads). Lane
  `post-alpha/test-hygiene` is an ancestor of this tip. **This is the same lane as the teardown fix above** —
  both items share one owner, which is why they are adjacent here.

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
  Verified against this tree: the clamp is real — the writer thread clamps before the write
  (`src/core/audio/TrackRecorder.cpp:190`, `std::clamp(m_writeScratch[i], sample_t{-1}, sample_t{1})`), and the
  code says why the library flag was refused instead (`:99-101`), which is the 1-LSB in-range shift the sentence
  above describes. `docs/RECORDING-REALTIME-FIXES.md` is the lane's report; the lane
  `post-alpha/recording-realtime` is an ancestor of this tip. The `SFC_SET_CLIPPING` lines in the two exporters
  were re-checked here and are unchanged.
- **The capture path no longer takes the model lock.** Pushing input frames could block behind the GUI's model
  lock (measured: a push blocked for the full 600 ms another thread held it), which on the JACK/SDL callback
  thread is a realtime hazard. A fixed-capacity lock-free staging ring now carries the frames instead, with
  zero allocations measured on the append path and exact conservation under overload (1,024,000 frames pushed
  → 16,384 staged, 1,024,000 dropped, nothing silently lost).
  Verified against this tree: the measurement is recorded, by the lane that took it —
  `docs/RECORDING-REALTIME-FIXES.md` reports the blocked push at 600 ms (`:183`, `:196`, `:201`, on the same
  test that reports it) and the overload conservation exactly (`:303`, `:313`, `:317`: `pushed 1024000 frames:
  staged 16384 -> 16384, dropped 1024000`, against `AudioEngine::InputStageCapacityFrames`), with the ring
  allocation and the realtime-safety argument at `:155`. The ring the older prototype left is now
  `RecordRingBuffer` (`include/TrackRecorder.h:41`, `:66`, `:110`), consumed off the audio thread (`:81-85`).
  Lane `post-alpha/recording-realtime` is an ancestor of this tip, so both halves are in this tree.
- **A project saved by a build without the Session View feature no longer silently loses that data.**
- **A set of long-standing mixer concurrency defects** — a one-click mute dropout, a crash on plugin-handler
  reallocation, a use-after-free when adding a channel, an unguarded effect reorder, and a sidechain-cleanup
  omission — found by an external concurrency audit, confirmed to be inherited from upstream, and fixed with
  a thread-sanitiser before-and-after.
  Verified against this tree: `docs/MIXER-CONCURRENCY-FIXES.md` names the audit's defects D1, D2(ii), D2(iii),
  D3, D4, D5 and D6 as confirmed live and now fixed, states that every one is inherited from upstream master
  (the same bare swaps and the same `bool m_muted` are in `origin/master`), and records a **ThreadSanitizer**
  build before and after using the tree's own switch (`-DCMAKE_BUILD_TYPE=Debug -DWANT_DEBUG_TSAN=ON`), run by
  `tests/run-mixer-concurrency-tsan.sh` with three pre-existing out-of-scope suppressions in
  `tests/mixer-concurrency-tsan.supp` and each slot run separately so one crash cannot mask another. Lane
  `post-alpha/mixer-concurrency` is an ancestor of this tip.
- **A failed save is refused and reported, not reported as success.** A failed move-aside during save —
  including the case Qt refuses by design, renaming over an existing file — left the project unwritten while the
  application reported success. The save now **refuses and tells you**: the destination is left
  **byte-identical**, and the project it had already written stays **recoverable** beside it. A project that
  cannot be saved also no longer leaves the document in a **degraded state that disabled autosave and undo
  until restart**.
  Verified against this tree: the two renames that publish a saved project are now checked and their failures
  propagated. `src/core/DataFile.cpp:461-490` carries the checked sequence the pre-fix code discarded (the
  comment there records that the old code "returned true unconditionally"), the move to `.bak` at `:494`, the
  rename into place at `:510` and its rollback at `:516` are each failure-checked, and `writeFile()` returns
  `false` on every failure path (`:485`, `:494`, `:510`, `:523`). `docs/SAVELOAD-INTEGRITY.md` §1 lists this as
  D3 ("rename failures discarded, `writeFile` returned `true` regardless") with the tests that cover it; the
  lane `post-alpha/saveload-integrity` is an ancestor of this tip. This bullet and the "Saving no longer reports
  success when it failed" bullet that used to sit above it made the same claim twice — the two are merged here,
  and the second copy is gone.

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

**Your settings are migrated rather than orphaned**: on first run, if the new configuration location is
absent and the old one exists, the old settings are adopted — so your preferences, audio device and recent
files come across instead of being stranded under the old name. **Your projects are read-both, write-new**:
the reader still accepts the old project and preset root, and the writer emits the new one, so old files open
and new files are unambiguous.
**What does NOT move, and this is deliberate**: the code-identifier layer — the `lmms::` namespace, `LMMS_*`
macros, `lmms_plugin_main`, and file names such as `lmmsconfig.h`. It was **assessed with its size and cost
rather than attempted**, and the consequence is the one that matters to you: **the plugin entry symbol is
unchanged, so existing native plugins still load.** The licence headers, copyright notices and the "derived
from LMMS" attribution also stay, because this is a derivative work under GPL-2.0-or-later and keeping them is
a condition of the licence.
Verified against this tree: the adoption is real code, not a promise —
`ConfigMigration::adoptConfigFile` and `adoptWorkingDir` (`src/core/ConfigManager.cpp:790`, `:823`, called from
`:709-790`) rename the legacy file/directory into the new name where that is possible, copy where it is not,
and as a last resort keep reading the legacy path so the user's settings are never orphaned; it is exercised by
`tests/src/core/ConfigMigrationTest.cpp` and declared in `include/ConfigManager.h:319-336`. Read-both is in the
reader (`src/core/DataFile.cpp:1739`, "Read-both: files written before the rename carry the legacy root name")
and write-new in the writer (`:2277`, "files written by this build say Zene Studio"). This is commit
`6c1ff660c` ("user state is migrated, not orphaned"), on lane `post-alpha/rename-complete`, which is an
ancestor of this tip.

> Corrected here rather than carried: the applied copy of this paragraph said the old configuration location
> was **left alone**, and its marker said the sentence was not true of the release-prep base. Both were right
> at that base and are wrong at this one — the migration commit has since merged, so the paragraph is now the
> draft's corrected version, ported verbatim. **One disagreement this exposes, reported and not silently
> harmonised:** the first headline's residue list higher up this file still lists user state
> (`~/.lmmsrc.xml`, `~/Documents/lmms/`, the `lmms-workspace` marker) as a deliberate residual "because
> renaming it would orphan an existing install's settings and projects", and `docs/WAVE-R-RENAME.md` §6's
> "User state" bullet says the same. On this tree that reason no longer holds — the adoption in
> `src/core/ConfigManager.cpp` is exactly what stops the orphaning — so the bullet and §6 are stale, and the
> limitations page's own "The name, honestly" section (which says the paths are migrated) is the correct half
> of the pair.

---

## Freeze status (was: "TODO before this text is applied")

Resolved against the tree and the binary at `post-alpha/release-prep` base `34c1f4f86`; the full table is
`docs/RELEASE-PREP-0.2.0.md` §3.

1. **Replace every `[VERIFY AT FREEZE]` with a verified fact — or delete the claim.** *Done, at the frozen tip
   `2239f3cb6`.* This file carried **16** markers and all 16 are settled. The dominant blocker was true when it
   was written and is not true now: every lane a marker named as "not an ancestor of the release-prep base" has
   since merged into this tip, so each claim was re-checked against this tree and its built binary and replaced
   with the fact that settles it, naming the path or the command. One was a **deletion rather than a
   resolution** — the save-failure claim appeared twice and the two bullets are now one, with the second copy
   gone. No marker remains, and no unverified claim was left unmarked. The only group this file still owes is
   item 3's digest/SHA-256 block, which needs the published artefacts; the publish step appends that block to
   the release body rather than writing it here.
2. **Fill the version string.** *Done, with one number corrected.* `git describe --tags --match
   'v[0-9]*.[0-9]*.[0-9]*'` at this file's original base `34c1f4f86` returns `v0.1.0-alpha-123-g34c1f4f86` —
   **not** a `v0.2.0-alpha` string — so an untagged build of that base reports
   `Zene Studio 0.1.0-alpha.123+34c1f4f`: the minor version the tag supplies, not the `2` that `CMakeLists.txt`
   declares. (The applied copy of this item printed `0.2.0-alpha.123+34c1f4f`, which contradicted item 3's own
   asset name below *and* the release-prep record this item cites; it is corrected here against the command
   output, not reworded.) On this tip the built header reports `Zene Studio 0.1.0-alpha.241+2239f3c`
   (`build-coverage/lmmsversion.h`; `git describe` → `v0.1.0-alpha-241-g2239f3cb6`). The string that ships,
   `Zene Studio 0.2.0-alpha`, needs the `v0.2.0-alpha` tag to exist at the release commit — **that tag does not
   exist yet and nothing is tagged by this pass** — or the `-DFORCE_VERSION=internal` configure. Exact output
   and the two configure lines: `docs/RELEASE-PREP-0.2.0.md` §1, whose §1.2 states the same mechanism and the
   same `0.1.0-alpha.123+34c1f4f` untagged string.
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
