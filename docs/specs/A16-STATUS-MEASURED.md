<!--
  In-repo copy of the specification this repository's code and tests cite (DOC-5, 2026-09-15).
  This is a PINNED SNAPSHOT, not a live document:
    source   : the program workspace, ableton-gap/A16-STATUS-MEASURED.md
    sha256   : 4631af8220ebb137449ace7952cad8b75bd83979fab585ad8246042abc222241
    bytes    : 7516
    why this file: the measured A16 reversibility status; cited by docs/A16-REVERSIBILITY.md as the measurement its rows disagree with, in writing
  The workspace copy remains the working copy and is where the document is edited; refreshing
  this snapshot is a deliberate act, recorded in the commit that does it. If a citation in this
  repository and this file ever disagree, reconcile them in one commit - the citations name the
  file by bare name (e.g. `SPEC-zene-studio.md`), and this directory is where that name resolves.
  See docs/specs/README.md for the convention and for the cited documents NOT yet committed.
-->
# A16 reversibility — MEASURED status of the whole agent surface (2026-09-12)

> **PARTLY SUPERSEDED 2026-09-12 by the implemented contract (task #623).** This file remains what it
> was: the **measured baseline of what the engine did before the contract existed** — 36 mutating
> commands driven, 17 reversible, 19 not, seven classes. The contract that followed moved seven of
> those rows (and added a used-by-nobody-yet `project.restore_revision`), so the **current** truth is:
> - the classification table, one row per command (71), shipped as data in
>   `src/core/ControlReversibilityTable.cpp` and stamped into the registry so a handler cannot claim an
>   inverse the contract denies;
> - the lane's reconciliation with this file — every disagreement argued, row by row — in
>   `zene-pa-reversible/docs/A16-REVERSIBILITY.md` §2 (it lives in the lane, not here, because
>   `ableton-gap/` is the parent's tree);
> - the seven rows where the implementation disagrees with this measurement: `track.add`, `track.remove`,
>   `mixer.add_channel`, `automation.add_point` (the creating call) and `track.set_solo` move
>   false → **true_inverse**; `project.save` moves false → **snapshot + reversible** (it now keeps a
>   bounded previous revision); `clip.select`/`note.select` move to **not_mutating** (they now record no
>   transaction at all).
>
> Read the rest of this file as history with a method attached (it also records *how* to measure this),
> not as the current contract.

**What this is.** The owner's decision on A16 was: *no confirmation gates on destructive commands;
find a way to make everything reversible.* The first thing that decision needs is the truth about what
is reversible **today** — so this is measured, not read from the code. Every row below comes from
driving the running binary and reading what `control.transactions` reports afterwards.

**Method.** Merged binary `post-alpha/agent-surface-integration` @ `059bf6bad` (70 commands, 18 groups),
started headless with the documented recipe (offscreen + a `--config` whose
`<audioengine audiodev="Dummy (no sound output)"/>` matches `AudioDummy::name()`, `HOME`/`XDG_*` in a
temp dir), talking raw JSON-RPC over the socket. A **scratch copy** of `tests/data/automation-audio-fixture.mmp`
was opened first, so the repository's fixture is untouched. Objects were created in dependency order
(track → clip → note, instrument → effect) so every later command addressed a real id rather than
tripping an id lookup, and the transaction list was read once at the end.

**Result: 36 mutating commands exercised; all 36 recorded a transaction; 17 are `reversible: true`.**

| reversible | commands |
|---|---|
| **true — a real `ProjectJournal` checkpoint backs the inverse** | `transport.set_tempo`, `mixer.set_volume`, `track.rename`, `track.set_mute`, `clip.add`, `clip.delete`, `clip.split`, `clip.duplicate`, `clip.move`, `clip.resize`, `note.add`, `note.remove`, `note.move`, `note.resize`, `note.velocity_set`, `plugin.param_set`, `plugin.bypass` |
| **false — and the reason the engine gives** | `track.add`, `track.remove` (no checkpoint exists; the product's own add/remove path has it commented out), `mixer.add_channel` (same), `automation.add_point` (only on the call that creates the `AutomationTrack`), `track.set_solo` (**partial**: one checkpoint covers one object, but the action also writes every other track's mute), `clip.select`, `note.select` (selection is view state, not project state), `transport.seek` (the play position is not a `JournallingObject`), `project.open`, `project.save` (no previous file revision), `plugin.load`, `plugin.unload`, `plugin.state_save`, `plugin.state_load`, `plugin.preset_save`, `plugin.preset_load`, `settings.set`, `audio.device_set`, `script.run` |

**Two typed refusals encountered, both honest and both naming the reason:** `track.set_arm` (arm state
lives on the prototype `MultiTrackRecorder`, not the song model) and `automation.mode_set` (this build
has no automation modes — the message cites `docs/KNOWN-LIMITATIONS.md:84`). A probe of mine called
`mode_set` with valid-looking args and got `invalid_args: missing required property 'parameter'` — the
schema was right and my probe was wrong; the command answers `refused` once `parameter` is supplied.
Worth recording because it shows the schemas are strict enough to catch a caller's mistake before the
handler sees it.

## What it would take to make each class reversible (the #623 design input)

| class | examples | what an inverse needs |
|---|---|---|
| **no checkpoint exists in the engine** | `track.add`, `track.remove`, `mixer.add_channel`, the first `automation.add_point` | uncomment/implement the checkpoints upstream left commented out, **or** accept a bounded snapshot of the container (track list / channel list / the automation track) as the inverse — the transaction already records the before-state, so this is mostly a decision about where to store and how to bound it |
| **partial: one checkpoint, many objects** | `track.set_solo` | the transaction must record the whole *action* (every track's mute+solo), not one object's checkpoint — this is exactly the "one agent command = one Ctrl+Z" grouping rule #623 asks for |
| **view state, not project state** | `clip.select`, `note.select` | nothing to inverse in the file; the honest answer is a documented no-op class, or the surface records the previous selection so `control.undo` restores the *view* |
| **not a `JournallingObject`** | `transport.seek`, `settings.set`, `audio.device_set` | small scalar inverses (`seek` to the previous tick, `set` to the previous value) recorded in the transaction; the engine does not journal them, so the registry must carry them |
| **file-level** | `project.save`, `plugin.state_save`, `plugin.state_load`, `plugin.preset_save`, `plugin.preset_load` | a previous-revision policy: keep the prior bytes (bounded) — the transactions already record `before.previous_content`/`previous_sha256` for the state/preset pair, so the gap is `project.save`, which keeps nothing |
| **replacement / removal** | `plugin.load`, `plugin.unload` | the removed device's state XML is already captured in `before` (bounded); the missing half is *recreating the instance and restoring it*, which needs the state-load path to be reachable from an inverse |
| **out-of-band mutation** | `script.run` | a script mutates through its own bindings; the registry cannot invert it in general. The honest options are (a) require the script to use `addCheckPoint()` itself, or (b) run scripts inside a transaction boundary and snapshot the project |

## Honest limits of this measurement

- **36 of the mutating commands** were exercised. Not exercised here: `mixer.set_pan` and
  `automation.mode_set` (both refusals by design), `mixer.remove_channel`, `automation.clear`,
  `automation.remove_point`, `plugin.preset_list`-adjacent read paths, and `track.remove` with
  `dry_run:false` (the matrix row shown is the dry-run preview, which correctly changes nothing — the
  real call is covered by the integration test).
- The matrix is a **snapshot of one binary**; `#628` changes track ids, which will change the mechanism
  strings for `track.*`.
- It measures what the registry *records*, not whether the inverse actually restores the state — for the
  17 `reversible:true` rows that was proven separately by each lane's own `control.undo` assertions, and
  for the rest it is #623's job to prove.
