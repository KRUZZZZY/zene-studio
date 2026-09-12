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
> passages were **ported** into this file at the tip `2239f3cb6` — an edit, never a replacement, because
> the applied copy carries marker resolutions the drafts lack. **That tip is not this file's state: the file
> was edited twice more that day** — the residue-list correction, then the control-surface section — **and the
> last of those describes code the frozen tip does not contain at all.**
> `include/ControlVocabulary.h`, `include/ControlRegistry.h` and `src/core/ControlCommands*.cpp` arrive with
> the control surface, after `2239f3cb6`: `git show 2239f3cb6:include/ControlVocabulary.h` answers `fatal:
> path 'include/ControlVocabulary.h' does not exist`. `git log --oneline -- docs/RELEASE-NOTES-v0.2.0-alpha.md`
> is the edit list. The marker resolutions are the applied copy's, taken at `post-alpha/release-prep`; which
> markers, and why — `docs/RELEASE-PREP-0.2.0.md`, §3.
> **This file was then re-checked claim by claim against the tree at `0834e40f1`** (the tip this pass ran on)
> and the two build directories item 2 names, and **every line number this file still carries was re-derived at
> that commit**; where a line number was incidental to the claim, it was replaced by a symbol — a line number is
> only true of the commit it was taken at.
>
> **Digests.** The publish step appends the SHA-256 block generated from the published release
> (`docs/RELEASING.md`, "The publish sequence", steps 4–5). Digests are never typed into this file.

## The three headlines

**1. It is called Zene Studio now.** The application, the packages, the desktop entry, the manual page and
the file paths carry the product's own name instead of the upstream project's. What deliberately does *not*
change: the licence notices and the "derived from LMMS" attribution, which stay because this is a derivative
work under GPL-2.0-or-later and keeping them is a condition of the licence, not a naming choice.
**The residue is narrower than earlier drafts of this text claimed, and several things once listed here are
renamed rather than kept** — verified against this tree:
- **Kept deliberately, because renaming it is an ABI break**: the `lmms_plugin_main` entry symbol
  (`src/core/Plugin.cpp`, the `pi.library->resolve("lmms_plugin_main")` lookup). That symbol is *why* existing native plugins still load unchanged; renaming it
  would be a deliberate break and would be announced as one. Alongside it, the internal identifiers no user
  sees — the `lmms::` namespace, the `LMMS_*` macros and file names such as `lmmsconfig.h` — which were assessed
  with their size and cost and deliberately not attempted.
- **Backwards compatibility, not residue**: `DataFile.cpp` **writes** `<zene-project>` (the string occurs
  three times in `src/core/DataFile.cpp`: the document/root construction, the root element creation and
  `documentElement().setTagName`) and its **reader still accepts** the old `<lmms-project>` root (the
  `firstChildElement("lmms-project")` fallback beside `firstChildElement("zene-project")`), so old files open
  and new files are unambiguous.
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
trace that should not survive a rename. Verified against this tree: the tag is the
`sf_set_string(m_sf, SF_STR_SOFTWARE, "Zene Studio")` call in the WAV writer
(`src/core/audio/AudioFileWave.cpp`) and its twin in the FLAC writer (`src/core/audio/AudioFileFlac.cpp`), and
the chunk was read back out of a real render — `tests/integration-logs-3d/final/chunk-parse.log`
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
standalone probe `Vst3InstrumentFixtureProbe` (its `add_executable` target in `tests/CMakeLists.txt`) and the SDK's own validator
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
the `WANT_VST3_TEST_INSTRUMENT` option (`tests/CMakeLists.txt`, default `OFF`), as the paragraph below states.

**The instrument's own editor does not open yet** — but the instrument window does, and we ran it rather than
assuming: load a VST3 instrument, and a window opens listing that plugin's controls in the host's generated
grid. That is the honest state and it is why this is an alpha. Verified against this tree: the plug-in's own
editor does not exist in the host — `grep -rn IPlugView src/ include/ plugins/Vst3Effect/ plugins/ClapEffect/`
returns **0** hits — so what a user gets is the generated `Knob` grid, exactly as
`docs/INSTRUMENT-HOSTING-SPEC.md` §0 records. **That 0 is over the four paths in the command, not over the
repository**, and the difference is stated here rather than implied: the string also appears in one product
file, `plugins/Vst3Instrument/Vst3InstrumentView.h`, in a comment recording that the interface is not
implemented, and in the vendored Carla copy of the VST3 SDK headers
(`plugins/CarlaBase/carla/source/includes/vst3sdk/...`).
Verified against this tree: the shipped-binary run is in the tree —
`docs/INSTRUMENT-VIEW-SAFETY.md` §3 records the product run under Xvfb against a project carrying a VST3
instrument track on the "Bass" track, on the pre-fix and the post-fix binary alike: the window opens, the
process stays alive, and the window contains *"Controls for Zene VST3 Test Instrument"*, a `Level` knob and the
disclosure line. Two qualifications belong with it, and both were checked. **That run is out of band, not
CI**: the two VST3 instrument suites sit behind the `WANT_VST3_TEST_INSTRUMENT` option
(`tests/CMakeLists.txt`, default `OFF`), so the default CI configuration neither builds nor runs them — the guard was proven by running
the suite out of band, green with the guard and `SIGSEGV` exit 139 at address `0x8` without it
(`docs/INSTRUMENT-VIEW-SAFETY.md` §4). And **no physical display was used** — offscreen and Xvfb only (§6.1
names what a desktop session would still add).
Three more things belong next to that
claim rather than in a footnote: **no third-party VST3 instrument has been tested by us** — the only instrument
this release is proven against is a purpose-built test instrument that ships in the source tree — there is **no
multi-out**, no preset management, and **no instrument latency compensation in PDC**.

**3. Another program can drive a running Zene Studio.** You can start Zene Studio so that a program
instead of a person drives the open session. Launch it with `--control-socket <path>` and it listens on
that local socket — a file, mode `0600`, in the working directory, with nothing listening on the network
— and answers named commands: set the tempo, start and stop, add a track, write notes into a clip, load a
plugin, move a fader, save the project, render it. It is opt-in and off by default, and the opt-in is
invisible: an instance that was not started that way has no socket at all, and nothing in the interface
reports one that is open (owner decision, `ableton-gap/AGENT-TOOLING.md` §9.1 — the only reader of
`isAgentInstance()` is `UnattendedRun` itself). **Where a registered command also has a menu or toolbar
entry, that entry and the registry handler are one implementation** — `telemetry.consent` is the case on
record, declared on the Help-menu action and handled by the registry. The gate measures that direction only
one way: it proves every *registered* command is reachable and swept, and it reflects **46** menu/toolbar
actions of which **4** resolve to a registered command and **42** are baselined as unregistered
(`tests/integration-logs-3f-fix/08-gate-committed.log`). Those 42 are not commands, and this text does not
claim they are. Every command the surface exposes is a normal edit the application already knows how to do,
and it lands on the same undo history as the GUI's Ctrl+Z, which is what makes the rest of this section
possible.

**Technically**, it is three things. A **registry of named commands** (`include/ControlRegistry.h`,
`src/core/ControlRegistry.cpp`): each command is a `group.verb` id with a JSON schema for its arguments,
a schema for its result, a `requires` declaration naming any of display, audio device or human it needs,
and a handler that runs on the UI thread. A **server** (`ControlServer`) that accepts line-delimited
JSON-RPC over a local UNIX socket; every message carries an integer `proto` and a failure carries
`error.kind` from a closed set — `not_found | requires | invalid_args | busy | refused | irreversible`
(`include/ControlRegistry.h`). And **unattended operation** (`include/UnattendedRun.h`):
`isAgentInstance()` is true when the process was started with `--control-socket`, and `isUnattendedRun()`
is true when it was started that way *or* when Qt is running on a platform with no display (offscreen,
minimal, VNC). In that state the application opens no modal dialog — a dialog nobody can answer is a
hang, and that was measured rather than reasoned about: in the recorded run a modal "audio device setup
failed" box blocked the application before it was usable, leaving `engine_ready` false and every command on
the typed `busy` error for 92 s and counting (`ableton-gap/AGENT-TOOLING.md` §4 — program workspace; the
reproducer, `tests/control-no-audio-device.py`, is in this tree). The requirement this section implements is
task #625, whose base commit `6b01b98eb` is a real commit (`git cat-file -t 6b01b98eb` → `commit`); the
per-run figures above are that lane's record, not a measurement taken for this file. The gate that
suppresses the dialog in the unattended case is the `isUnattendedRun()` check in `src/core/main.cpp`.

### What you can do with it

The registered commands fall into these families; the module list is `src/core/CMakeLists.txt:61-93` and
each family's ids are in its own `Control*.cpp` file.

- **Transport** — play, stop, seek, read the play head, read and set the tempo.
- **Tracks and arrangement** — list tracks, read one track, read the whole arrangement with its clips,
  add, remove, rename, mute, solo. Record-arm is registered but **refuses** (below).
- **Clips and notes** — add, move, resize, split, delete and duplicate a clip; add, remove, move,
  resize and set the velocity of a note; read a clip's note list; select clips and notes.
- **Mixer** — read the channels, add and remove a channel, read and set a fader. Channel pan is
  registered but **refuses** (below).
- **Plugins** — list the device catalogue, load, unload, bypass; read and set a parameter; list, load
  and save a preset; save and load a device's whole state.
- **Project and files** — read project state, open, save, restore a previous file revision, and render.
  The render path is callable headlessly and returns a hash, so a caller can prove the audio changed
  rather than assert it (`AGENT-TOOLING.md` §8).
- **Automation and scripting** — read automation state; add, remove and clear points; `script.run`
  executes Lua inside the running instance.
- **Settings, audio, MIDI, DSP** — read and set a config value, list audio devices (and set one, which
  applies on next start), list MIDI devices, toggle MIDI learn, read the build identity, read the DSP
  device chains.
- **Control, telemetry and reflection** — ping, version, list every command, read the transaction
  record, undo, redo, quit, report the menu/toolbar surface; and the two `telemetry.*` commands —
  `telemetry.status` reports whether telemetry is compiled in, the consent record, and the exact payload
  `submit()` would send, while `telemetry.consent` opens the consent screen and refuses every automated
  caller, because it declares `requires: display, human`.

Commands address objects by string ids — `trk-<n>`, `clip-<n>`, `note-<n>`, `ch-<n>`, `dev-<n>`.
**`trk-<n>` is assigned at creation and written into the project file**, so a track keeps its id across a
save and reload (`src/core/Track.cpp:232`, `:323`; `tests/src/core/StableTrackIdsTest.cpp`). **The others
are index-derived and are not written into the project file** (`include/ControlVocabulary.h:89-95`), so an
id is good for the session that returned it and not for the next one (`AGENT-TOOLING.md` §4).

### The reversibility contract

The owner's decision was *no confirmation prompts on destructive commands; make them reversible
instead* (`AGENT-TOOLING.md` §9.3, boarded as task #623). The first thing that decision needs is the
truth about what was reversible **then**, so it was measured rather than read from the code: a scratch
copy of the test fixture was opened in a headless instance of the merged surface
(`post-alpha/agent-surface-integration` @ `059bf6bad`, 70 commands in 18 groups), driven over raw
JSON-RPC, with the transaction list read at the end. **36 mutating commands were exercised; all 36
recorded a transaction; 17 are `reversible: true`** — the other 19 record a before-state snapshot and a
stated reason but no automatic inverse (`ableton-gap/A16-STATUS-MEASURED.md` line 35, the measured
baseline this release's contract reconciles against; carried in `docs/A16-REVERSIBILITY.md` §2).

**Reversibility here is a declared classification plus a per-call record, not a promise.** Every
registered command has one row in a table shipped as data in `src/core/ControlReversibilityTable.cpp`,
in one of four classes, and an anti-drift test (`tests/src/core/ReversibilityContractTest.cpp`) checks
the table and the registry against each other in both directions — every registered command has a row,
and every row names a registered command. The four classes, in the report's own terms:

- **`true_inverse`** — a live checkpoint on the engine's own undo stack. The call that unwinds it is
  `ProjectJournal::undo()`, the same call the GUI's Edit→Undo makes, so an agent's edit and a person's
  edit share one history at one granularity. Three flavours of the same checkpoint: one object's
  serialized XML; a **composite** checkpoint that restores several objects in one pop, so a command that
  writes N objects still costs one Ctrl+Z; and an **action** checkpoint that runs a recorded inverse
  operation, for a change with no live state to put back (a created or deleted object, a scalar in a
  subsystem the engine does not journal, a file revision).
- **`snapshot`** — the recorded state is a manual fallback rather than a live object: an undo attempt is
  **refused, typed, and the refusal names the fallback** instead of pretending.
- **`irreversible`** — no inverse exists for this command by its nature. Undo fails with the typed
  `irreversible` error naming the command, its class, the engine's own reason and the documented
  fallback, and **the journal is not touched**, so an irreversible command can never make `control.undo`
  silently unwind an older one.
- **`not_mutating`** — the command writes nothing: read-only inspectors, the three handlers that refuse
  every call, the two selection commands, undo and redo themselves, the transport run state, and
  `render.render`, which writes an output artefact and leaves the session alone.

**How many commands there are depends on what you count, so here is each number with its method.** The
live registry in this build holds **74** commands — that is what `control.commands_list` returns and what
the agent-surface gate sweeps (**73 swept + 1 allowlisted**, the allowlisted one being `telemetry.consent`,
which needs a display and a human) — and the same **74** ids are registered in
`src/core/ControlCommands*.cpp`. It held **72** before the telemetry fix that added the two `telemetry.*`
commands. The table that ships as data in `src/core/ControlReversibilityTable.cpp` has **74 rows** today,
one per registered command: 30 `true_inverse`, 5 `snapshot`, 3 `irreversible`, 36 `not_mutating`. The
A16 contract's **classified table has 72 rows** (30 + 5 + 3 + 34) — a different thing that coincides with
the pre-fix registry size, which is exactly why a command count has to name what it was counted over. The
bridge's committed snapshot holds **70** (it is deliberately stale — a command missing from it is not
missing from the DAW). And a wider surface line counts **87** by its own method (71 + 16,
the sixteen being tail groups such as `record.*`, `import.*` and `export.*` that this release line does
not carry). **That figure is that line's own tally, carried in the program workspace
(`POST-ALPHA-PLAN.md`: "`registered ids in ControlCommands*.cpp | 87 | 71 + 16`") and is not measurable from
this tree**, which is why it is named as a record rather than quoted as a count of the same kind as the ones
above. **Quote a count with the thing it was counted over.**

Every successful mutating command records one transaction naming the command, the class (stamped from
the table, never from the handler), the before-state, the inverse descriptor, the call's own `reversible`
verdict, the mechanism and a byte count. The bounds are stated and enforced: `MaxTransactionRecords`
(100) — deliberately the same depth as the undo stack (`ProjectJournal::MAX_UNDO_STATES`), so a record never
outlives the step it describes — `MaxTransactionBytes` (256 KiB) in total, a 64 KiB per-record ceiling on a
captured device state or track XML (`ControlSnapshotLimit`, `MaxTrackSnapshotChars`), and FIFO eviction whose
evictions are **counted and reported** rather than hidden: `ControlRegistry`'s retained-record cap counts them
and `control.transactions` returns the `evicted` count. (An earlier version of this sentence cited
`include/ControlReversibility.h:143,149` for the bounds and `ControlCommandsArrangement.cpp:53` for the
eviction — that line is `MaxTrackSnapshotChars`, a per-record cap, and the eviction accounting lives in
`ControlRegistry`.) The record does not survive a restart, and neither does
the undo history.

**One agent command is one undoable step**, and the mechanism for that was found by measurement, not
assumed: `AutomatableModel::setValue()` pushes a checkpoint of its own on every non-automated write
(`src/core/AutomatableModel.cpp:305`), so a command that writes N models leaves N checkpoints and the
handler's own checkpoint is buried under them. Before the registry merged a command's window into one
step, one `control.undo` after `track.set_solo` restored exactly one track's mute and left the rest of
the action in place — the contract test caught it with all three tracks still muted.

**File-level saves are the one deliberate asymmetry.** `project.save` keeps a bounded previous revision
(policy `keep-3`: `<file>.rev0/.rev1/.rev2`, 8 MiB each, 24 MiB per project; a project over the
per-revision cap is not copied at all, because a truncated revision is worse than none) and its inverse
is a **command**, `project.restore_revision`, which `control.undo` dispatches. File-level commands are
not put on the GUI undo stack on purpose: a file is not project state, and reverting someone's file
behind their back on a Ctrl+Z would be a surprise. So `control.undo` after an agent save restores the
previous revision, while a GUI Ctrl+Z after the same save behaves exactly as it did before.

**Against the measured baseline, the report reconciles the rows the implementation changes, one by one.**
`track.add`, `track.remove`, `mixer.add_channel`, `automation.add_point` (on the call that creates the
automation clip) and `track.set_solo` move `false` → `true_inverse`; `project.save` moves `false` →
`snapshot` **and reversible** (it now keeps the revision above); `clip.select` and `note.select` move to
`not_mutating`, because they now record no transaction at all and so cannot shadow or block the undo of
a real edit. What the remaining non-reversible mutating commands do instead: the `snapshot` ones record
a bounded before-state and refuse automatic undo with a named manual fallback — `plugin.load` of an
**instrument** is destructive (the replaced instrument's parameter values are gone) and is recorded
`reversible: false` even though the command's declared class is `snapshot`, while loading an **effect**
is reversible because the instance it creates carries only defaults; `plugin.state_save` and
`plugin.preset_save` write a file outside the project, so their previous bytes are recorded and the
fallback is to write them back. The three `irreversible` ones — `project.open`, `script.run`,
`plugin.unload` — each name a fallback instead of an inverse: reopen the file the record names, take a
checkpoint inside the script, or reload the device and restore its captured state XML (the chain order
is not restored). `automation.mode_set`, `mixer.set_pan` and `track.set_arm` are registered with full
schemas and **refuse every call**, recording no transaction, rather than inventing a flag the project
format does not have.

### What the surface does not do

**It is opt-in, and the opt-in is invisible.** A build of this release contains the surface but no
socket: the socket exists only when the process was started with `--control-socket`, and nothing in the
interface reports one that is open — the only reader of `isAgentInstance()` is `UnattendedRun` itself
(`AGENT-TOOLING.md` §9.1).

**Headless operation has a floor.** The engine still needs an audio device that opens: the documented
recipe is an offscreen Qt platform plus a config whose audio device string matches
`AudioDummy::name()` exactly, with `HOME` and the `XDG_*` variables pointed at a temp directory, and
then polling `control.ping` until `engine_ready` is true. The socket exists before the engine does, and
until it is ready every engine command answers the typed `busy` error with the reason
(`AGENT-TOOLING.md` §4). `automation.mode_set`'s refusal cites `docs/KNOWN-LIMITATIONS.md` for
"this build has no automation modes" — **but that page, in this tree, says the opposite: its automation
bullet states that the Read/Touch/Latch/Write modes work**, landing on tick boundaries rather than samples,
and the line the refusal names is a save/open-integrity paragraph. The behaviour is honest (register the
command, refuse, do not fake a write); the reason it cites is stale, and it is not repeated here as a fact.
**The line number the refusal quotes is not repeated here either** — a line number is only true of the commit
it was taken at, and the page's automation bullet is what a reader can find.

**The contract's own limits, listed rather than implied** (`docs/A16-REVERSIBILITY.md` §8): undo depth
is 100 steps for both the model stack and the record, and neither survives a restart. `track.remove`'s
inverse is bounded at 64 KiB of track XML and refuses a larger track rather than keeping a truncated
one. `track.add`'s inverse does not rewind the project id counter, so a re-added track gets a fresh id.
`track.set_solo` does not restore one transient C++ field (`Track::mutedBeforeSolo`). A revision restore
rewrites the file but does not reload the session — the in-memory session is untouched until
`project.open`. `plugin.unload` cannot be replayed by the registry even though its state XML is
captured. `mixer.remove_channel`, `automation.mode_set`, `track.set_arm` and `mixer.set_pan` were not
exercised by the measured baseline at all; their rows are this lane's measurement. And a merged step is
only as good as the window it merges: work a command spawns outside its handler would land above the
merged step and cost a second undo — nothing in the current surface does that, but it is a property to
keep, not one the engine can enforce.

One smaller gap worth the same breath. `project.restore_revision` is in the tree and the table and is
described in the baseline's own header as *used by nobody yet* — it is the inverse of `project.save`,
not a command anyone has asked for. The surface's own tests run on the **Dummy** device (the documented
headless recipe), and `tests/control-no-audio-device.py` deliberately makes the device unopenable to
prove the fallback; no test drives the surface against a real audio backend.

### Where the detail lives

- `docs/A16-REVERSIBILITY.md` — the contract: the classification table with a row and a reason per
  command, the reconciliation with the measured baseline row by row, the transaction record's shape and
  bounds, and the honest-limits list. **In this tree.**
- `ableton-gap/A16-STATUS-MEASURED.md` — the measured baseline: 36 mutating commands exercised, 17
  reversible, with the method (how to start the instance headless and read `control.transactions`) and
  the list of what was not exercised. In the program workspace, not in this repository — the contract doc
  cites it as its baseline, and it is the one reference in this section a reader of the product repo
  alone cannot open.
- `CMDN-REPORT.md` and `CMDN-TRANSCRIPT.md` — the notes/clips/tracks lane's report and a verbatim
  request/response transcript over the socket, including the transaction list and the typed errors. In
  the repository root.
- `docs/VERSIONING.md` — the numbering rule, which names this surface as the reason `0.2.0` is a MINOR
  rather than a patch, and separates the product version from the control protocol version.
- `ableton-gap/AGENT-TOOLING.md` — the surface contract (A11–A15), the headless start recipe, the
  readiness order a client must follow, the per-group command tables, the acceptance gates, and the
  owner decisions including the opt-in consent model. In the program workspace.

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
  Verified against this tree: the subcommand is `master` (alias `--master`), dispatched from the argument
  parser in `src/core/main.cpp`; its usage line is in that file's usage text, its options under
  `Options for "master":`, and the run prints one line per candidate (the `Auto-mastering: %d candidates`
  header, then a row each).
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
- **A use-after-free in the MIDI-learn notification tool.** `MidiLearnGui` is a function-local static that
  outlives the window owning its `QAction`, and it held that action as a raw `QAction*`, so `setArmed()` could
  dereference freed memory. It was found by turning the coverage gate on for the first time: the tool's own
  test **crashed** (`SIGSEGV` in `MidiLearnGui::setArmed`, `EXIT=139`), which is why the file had no coverage
  record at all. Fixed with a guarded pointer (`QPointer<QAction>`); the test passes and the file then measures
  **86.00 %** — `docs/COVERAGE-GATE-GREEN.md` §1 carries the crash frame, the backtrace log and the figure.
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
  different audio: measured on **the nine projects the determinism sweep covers** — this repository ships
  **68** `.mmp`/`.mmpz` files (`git ls-files '*.mmp' '*.mmpz' | wc -l` → 68), and nine is the sweep's sample, not
  the repository's project count — **six differed on every run and a
  seventh intermittently**, the worst on 98.3 % of its frames. Loudness never moved, so no level, RMS or LUFS
  reading could see it — only a sample-level comparison could. The cause was the offline renderer spreading each
  period's work across a worker pool, so which thread ran which job was a scheduling decision. Exports now render
  on one thread: **7 of those nine are bit-reproducible**, five subsequent renders of the test project
  being byte-identical to the single-threaded answer. Two remain non-deterministic and are **not** the fix's
  fault — for both, pinning to one CPU, disabling ASLR, a fixed `rand()` and a frozen clock *each* still produce
  different files, so their cause is in the instruments rather than the renderer, and it is named in our notes.
  Live playback is unaffected (the pool still runs it). Stock LMMS has this defect too — this is a fix we made
  that upstream has not, and we made it because it corrupted our own testing evidence.
  The two are named as the tree ships them: `data/projects/demos/StrictProduction-DearJonDoe.mmp` and
  `data/projects/shorties/Root84-TrancyLoop.mmpz` (`docs/RENDER-DETERMINISM.md` §9, "2 of 9 bundled projects are
  still not reproducible"; §10 is the six falsification experiments — CPU pinned, ASLR disabled, `rand()` fixed, clock
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
  propagated. `DataFile::writeFile()` in `src/core/DataFile.cpp` carries the checked sequence the pre-fix code
  discarded (the comment there records that the old code "returned true unconditionally"): the move to `.bak`,
  the rename into place and its rollback are each failure-checked, and `writeFile()` returns `false` on every
  failure path. `docs/SAVELOAD-INTEGRITY.md` §1 lists this as
  D3 ("rename failures discarded, `writeFile` returned `true` regardless") with the tests that cover it; the
  lane `post-alpha/saveload-integrity` is an ancestor of this tip. This bullet and the "Saving no longer reports
  success when it failed" bullet that used to sit above it made the same claim twice — the two are merged here,
  and the second copy is gone.

- **`control.undo` could kill the application — and the GUI's own Ctrl+Z reached the same fault.**
  `control.undo` over the control socket did not merely drop the client's connection: it **SIGSEGV'd the
  DAW** (`returncode -11`), and every client died with the process. The fault was a null dereference in
  `PatternStore::updateComboBox()` (`src/core/PatternStore.cpp:203`) reached from
  `ProjectJournal::undo()` — *below* the call the GUI's Edit ▸ Undo makes, because the GUI's Ctrl+Z **is**
  `Engine::projectJournal()->undo()` (`src/gui/MainWindow.cpp:1417-1420`), the identical call the socket
  path makes. The mechanism in one line: a pattern-track destructor erased its entry from a static
  registry, and a GUI slot then read that registry with `QMap::operator[]`, **which inserts a fabricated
  entry for the dying track**; the allocator reused that address for the replacement track, which derived
  its pattern number from the registry's size, leaving index 0 vacant while the count said 1 — so the
  first loop iteration dereferenced a null. Fixed by every registry read becoming `QMap::value()` rather
  than `operator[]` (five in `src/tracks/PatternTrack.cpp`, one in `include/PatternTrack.h`, two in
  `src/gui/tracks/PatternTrackView.cpp`) plus a null guard in `updateComboBox()`; declared in
  `tests/upstream-modifications.txt`, one entry per file. **The GUI equivalence is by code identity, not
  by a measured Ctrl+Z run** — the diagnosing lane is headless (`QT_QPA_PLATFORM=offscreen`), did not
  click Ctrl+Z, and said so; the two paths share both the entry point and the ghost-producing view, and
  the faulting frame sits below both. Verified against this tree: `docs/CONTROL-UNDO-CONNECTION-DROP.md`
  §2 is the gdb stack (`#0 lmms::PatternStore::updateComboBox ... PatternStore.cpp:203`, `pt = 0x0`,
  `numOfPatterns() == 1`) and §7 is the after-state — the probe's `control.undo` answers `ok` on a live
  connection, restores the recorded inverse tempo (140), the process stays alive, and the test render is
  unchanged (`943e3238…`, the same `data` chunk `b37cefc5…` as the train before it).
- **An optional MCP bridge ships for the control surface.** `tools/mcp-zene-control/` is a stdio MCP
  server that exposes a *running* instance to an MCP client (`tools/mcp-zene-control/README.md`). It is a
  **client**: the control socket exists without it, and the bridge holds no command knowledge of its own —
  its tool list is generated from the instance's live registry, so it cannot advertise a command the DAW
  lacks, and with no instance reachable it serves the last-known list (a cache, then a committed
  snapshot) so an agent can see what exists before launching anything.

## Known limitations

This is an alpha and the list is long; a separate **known limitations** page covers it in detail — that page
is `docs/KNOWN-LIMITATIONS.md`, and **there is no version-suffixed 0.2.0 limitations file** (the 0.1.0 page,
`docs/KNOWN-LIMITATIONS-v0.1.0-alpha.md`, is the one it replaced), so a process looking for the suffixed name
will not find the page this text points at. The short
version: no instrument editor; instrument hosting is one-per-track and proven only against our own test
instrument; no clip editing gestures, take lanes or comping; no plugin-scanning interface worth the name; no
stable project format; no measured crash-free rate (the reporter is new); VCA groups and racks exist in the
project format but **cannot be created from the interface**; and **renders are reproducible for 7 of the nine
projects the determinism sweep covers, with two still not** — those two are named in the limitations page, along with the reason
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
`src/core/DataFile.cpp` composes `fullName + ".bak"` and moves the current file there before the new one is
renamed into place, skipped when `app/disablebackup` is set — and `creatorversion` is the project-root
attribute recording the version of the build that wrote the file (set from `LMMS_VERSION` wherever that file
creates the root, read back on load), which drives both the "Version difference" notice and the selection of
the upgrade routine to run (`DataFile::legacyFileVersion()`).

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
`ConfigMigration::adoptConfigFile` and `adoptWorkingDir` (`src/core/ConfigManager.cpp`, defined in
`ConfigMigration` and called from `ConfigManager`'s constructor) rename the legacy file/directory into the new
name where that is possible, copy where it is not, and as a last resort keep reading the legacy path so the
user's settings are never orphaned; it is exercised by `tests/src/core/ConfigMigrationTest.cpp` and declared in
`include/ConfigManager.h`. Read-both is in the reader (`src/core/DataFile.cpp`, "Read-both: files written
before the rename carry the legacy root name") and write-new in the writer (same file, "files written by this
build say Zene Studio"). This is commit
`6c1ff660c` ("user state is migrated, not orphaned"), on lane `post-alpha/rename-complete`, which is an
ancestor of this tip.

> Corrected here rather than carried: the applied copy of this paragraph said the old configuration location
> was **left alone**, and its marker said the sentence was not true of the release-prep base. Both were right
> at that base and are wrong at this one — the migration commit has since merged, so the paragraph above is the
> draft's corrected version, ported verbatim. **One stale record is left standing, and it is named rather than
> silently harmonised:** `docs/WAVE-R-RENAME.md` §6's "User state" bullet still gives "renaming it would
> orphan an existing install's settings and projects" as the reason `~/.lmmsrc.xml`, `~/Documents/lmms/` and
> the `lmms-workspace` marker were left alone. On this tree that reason no longer holds — the adoption in
> `src/core/ConfigManager.cpp` is exactly what stops the orphaning — so §6 is stale, and the limitations
> page's own "The name, honestly" section (which says the paths are migrated) is the correct half of the
> pair. An earlier version of this note also named the first headline higher up this file for that error; the
> notes' residue list says user state is **migrated**, not kept, so that half of the charge was stale and is
> removed here.

---

## Freeze status (was: "TODO before this text is applied")

Resolved against the tree and the binary at `post-alpha/release-prep` base `34c1f4f86`; the full table is
`docs/RELEASE-PREP-0.2.0.md` §3.

1. **Replace every `[VERIFY AT FREEZE]` with a verified fact — or delete the claim.** *Done.* **The file
   carries no live marker, and that is a command, not an impression:** `grep -n "VERIFY AT FREEZE"
   docs/RELEASE-NOTES-v0.2.0-alpha.md` returns three hits and every one is this convention being described, not
   a marker on a claim (the limitations page has one, of the same kind). **No marker count is stated here.**
   None is derivable: no marker list survives in this file's history that a reader could count against, and the
   per-marker table this item cites — `docs/RELEASE-PREP-0.2.0.md` §3 — is the record of a *different* pass, at
   its own base `34c1f4f86`, where a marker the tree could not settle was left in place with its blocker named
   and nothing was deleted (§3). A number would name neither pass. The dominant blocker was true when it was
   written and is not true now: every lane a marker named as "not an ancestor of the release-prep base" has
   since merged into this tip. One claim was **merged rather than resolved** — the save-failure claim appeared
   twice and the two bullets are now one, the second copy gone (the surviving bullet records this itself). The
   only group this file still owes is item 3's digest/SHA-256 block, which needs the published artefacts; the
   publish step appends that block to the release body rather than writing it here.
2. **Fill the version string.** *Done, with the artefact half removed.* `git describe --tags --match
   'v[0-9]*.[0-9]*.[0-9]*'` at this file's original base `34c1f4f86` returns `v0.1.0-alpha-123-g34c1f4f86`
   (checked here) — **not** a `v0.2.0-alpha` string — so the version the assembly builds from it is
   `Zene Studio 0.1.0-alpha.123+34c1f4f`: the minor version the tag supplies, not the `2` that `CMakeLists.txt`
   declares. **No `lmmsversion.h` is quoted for that string, because no build of that commit exists on this
   box to read one from** — a build directory's header reports whatever commit that directory was last
   configured from, not the base this item is about. For the record of what is on the box: `build/lmmsversion.h`
   reads `0.1.0-alpha.247+f68cf8e`, and `build-coverage/lmmsversion.h` read `0.1.0-alpha.315+6daed30` when this
   pass checked it (it is rewritten by every configure). The string that ships, `Zene Studio 0.2.0-alpha`, needs
   the `v0.2.0-alpha` tag to exist at the release commit — **that tag does not exist yet and nothing is tagged
   by this pass** (`git tag -l 'v0.2.0-alpha'` → empty) — or the `-DFORCE_VERSION=internal` configure. Exact
   output and the two configure lines: `docs/RELEASE-PREP-0.2.0.md` §1.
3. **Add the artefact list and the SHA-256 digests block, generated from the published release rather than
   typed.** *Pending by design — needs the published artefacts.* The digests are appended by the publish step
   (`docs/RELEASING.md`, steps 4–5). The asset **names** follow from the package-name pattern
   `${CMAKE_PROJECT_NAME}-${VERSION}-<platform>`. **The concrete string is quoted from the build directory it
   was read in, not from "this base":** `build/CPackConfig.cmake` — the release-configuration directory on this
   box, configured at `f68cf8e` — sets `CPACK_PACKAGE_FILE_NAME` to
   `zene-0.1.0-alpha.247+f68cf8e-linux-x86_64` (and its source pair to `zene-0.1.0-alpha.247+f68cf8e`), and
   `build/lmmsversion.h` reports the same commit. So the release's assets are
   `zene-0.2.0-alpha-<platform>.<ext>`; the per-platform extension list is the release job's upload glob
   (`.github/workflows/build.yml`, `.AppImage` / `.dmg` / `.exe`).
4. **Cross-check every capability line against `tests/advertised-features.tsv`.** *Done — except the
   enforcement half, which is **not** green on either build on this box and is not claimed to be.*
   The manifest binds **compile-time capability**, and it carries six rows: the three hosts this release
   documents as present (`vst3effect`, `vst3instrument`, `clapeffect`) and the three features it documents as
   absent (`session-view`, `wasm-sandbox`, `stem-separation`). It is not, and by its own header cannot be, a row
   per runtime feature — so "delete any claim not represented in the manifest" taken literally would delete
   these notes. The runtime bullets are guarded by their tests. **The guard that binds the manifest to a binary
   is `tests/release-honesty-gate.sh`**, and its mechanism is what this item can assert: it fails a feature the
   release documents as present when the build under test does not report that option `ON` (`AUTO` is not `ON`
   — the script's own header says so), it exits 1 on any mismatch, and the six build jobs in
   `.github/workflows/build.yml` run it against the binary they have just built. Run against the two binaries
   that exist here it does **not** pass: `3 of 6` rows on `build-coverage/zene` (the three hosts report `AUTO`)
   and `1 of 6` on `build/zene` (a stale `WANT_SESSION_VIEW=ON` left in that directory's cache). **The
   release-configuration build whose options would let the gate read six of six is being run by the release
   engineer (`scripts/release-verify.sh`); until that run has been observed, item 4 is not Done.** Checked line
   by line in `docs/RELEASE-PREP-0.2.0.md` §4.
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
