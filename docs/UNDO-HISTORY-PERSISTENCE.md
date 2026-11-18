# Undo history saved with the project (WF-033)

**Status:** implemented on `050/feat-wf033-2026-10-06` (base `6d1a1143d`). This is the decision record
for the feature, in the shape `docs/UNDO-BOUNDS.md` uses: what was decided, where each part lives, what
was measured, and what it does not do. The design note is
`plans/DESIGN-WF-033-UNDO-PERSISTENCE.md` (program root); the owner chose its recommendation **C, full
project snapshots**, with the defaults below.

## 1. The decisions (the design note's open questions, answered)

| question | decision | where it is a setting |
|---|---|---|
| default on or off? | **OFF.** A project opts in; a global default can opt every project in. | per project: `project.set_undo_history {setting: on\|off\|default}` (saved in the project), the Undo History panel's *Save with project* box; global: config key `undohistory/persist` (`0`/`1`), the same panel's *All projects* box, or `settings.set` |
| how many steps? | **20**, the newest. | `undohistory/steps` (1..1000) |
| compressed-size cap, and what if one snapshot exceeds it? | **64 MB** for the stored history (compressed). Over it the **OLDEST** steps are dropped first. If a single snapshot alone exceeds the cap, the history is **not saved from that step back**, and the user is told: `project.save`'s `undo_history.oversize`/`warning`, a status message after an interface save, and a warning on stderr. Never silently. | `undohistory/capmb` (1..1024) |
| plug-in chunk state? | **Included**, exactly as a normal save includes it: a snapshot IS the document a save builds (`Song::buildProjectDocument`), so there is no second plug-in format and no plug-in is special-cased. | - |

The in-memory session bounds of `docs/UNDO-BOUNDS.md` (100 steps / 16 MiB by default, FIFO, coalescing)
are **unchanged and separate**: a snapshot is never counted against the session byte budget, and the
persisted history is bounded by its own two caps above.

## 2. How it works

**Capture.** When persistence is on for the project, every undo step carries the whole project as it was
*before* that step - the state its undo returns to - as a compressed document
(`ProjectJournal::CheckPoint::snapshot`, `src/core/ProjectJournalSnapshots.cpp`). Two capture points,
because the journal has two kinds of step:

* an **object checkpoint** is pushed *before* its change, so the project is captured right there;
* an **action / structural step** (a closure - track delete, device move) is pushed *after* its change,
  so the state before it is captured when the command *starts*: `ControlRegistry::runHandler` brackets
  every mutating true-inverse command with `ProjectJournal::beginEdit()/endEdit()`, and every step the
  command pushes gets that pre-command capture.

Merging keeps the earliest snapshot (the same "earliest capture wins" rule the journal applies to object
captures), so a coalesced drag or a multi-object command persists as ONE step whose snapshot is the state
before the gesture. Undo and redo carry a step's snapshot with it (its pre-state does not change by being
traversed). Only the newest `steps` snapshots that fit the cap are kept in memory; older ones are dropped
as the stack grows (their sizes are remembered so a save can say why they are gone).

A snapshot is `Song::captureUndoSnapshot()` (`src/core/SongUndoSnapshot.cpp`): the save document with two
differences that are the point:

* **every object keeps its journal id.** The `<journallingObject id>` metadata a save prunes is kept, and
  while a snapshot is built (`ProjectJournal::setSnapshotIds`) every `AutomatableModel` writes its id
  *exactly*: an automated one in its usual node (`id`, without the top bit a save clears), a plain one in a
  side attribute `<name>-jid` beside its value - the value keeps its attribute form because some loaders
  read a model's presence from that attribute (measured: MidiPort's `basevelocity` compat reset the value
  to 127 when the model was moved into node form). Automation links (`AutomationClip`'s `<object id>`)
  write the exact id too. So a restore gives every object back the id the steps above it name - an edit
  made above a loaded history still redoes after the history was undone and redone (the test's id-remapping
  sequence; it fails when snapshots do not carry the ids). None of this is ever written into a project file.
* the append-only `<z:provenance>` log is **left out** (an undo never rewinds it).

It runs under `ProjectIds::PureSaveScope`, so a snapshot never counts as a revision of any object
(`rev`/`writer` stay what a real save would make them).

**Save.** Only a *real* save writes the history: `project.save` and the interface's Save / Save As hold an
`UndoHistory::SaveScope`. Templates, autosave/recovery files, render and stem temporaries, versioned copies
and project bundles do not carry it (a bundle rewrites sample paths after the history is sealed, so it is
kept out and the save says so).

**Load.** The history is pushed back onto the journal as steps whose undo replaces the whole project with
the saved snapshot and whose redo puts back the project as it was at that undo
(`Song::restoreUndoSnapshot`). The restore is a project load of the snapshot, **in place**: it holds and
puts back what a load would otherwise reset - the journal around the step, the provenance log, the
project's own undo-history setting, the file name - and never rewinds the stable-id counter (an id is never
handed out twice). The interface's Ctrl+Z and `control.undo` both reach the loaded steps: `project.open`'s
own record is irreversible, so `control.undo` unwinds the steps the open loaded and refuses (typed, as
before) once they are exhausted.

## 3. The format, and how it is versioned

The project format is versioned **per section** in this tree (SPEC-ARCH-4's compatibility rule, the
precedent of `<z:lanes>`, `<z:scenes>`, `<z:provenance>`): new state goes only in a top-level namespaced
section of `<song>` carrying its own `v`, the reader claims only the exact shape it knows, and a section
no reader claims is preserved verbatim (1.6.1). The root `version` attribute is unchanged. WF-033 adds:

```xml
<z:undohistory xmlns:z="urn:zene:core:1" v="1" setting="on" steps="2" fingerprint="sha256:...">
  <z:undostep bytes="5321" sha256="sha256:...">eJzt...base64 of the zlib-compressed snapshot...</z:undostep>
  <z:undostep bytes="5410" sha256="sha256:...">...</z:undostep>   <!-- oldest first -->
</z:undohistory>
```

* `setting` is the project's own choice (`on`/`off`; absent = follow the global default).
* Written **only** when the project opted in or out explicitly, or when the global default is on and there
  are steps to save - a project that did neither writes the bytes it always wrote (the additive rule).
* `fingerprint` digests `<head>` and every other `<song>` section (minus `z:provenance` and `z:index`),
  each with `DocumentIndex`'s canonical, process-independent digest. It is taken at load over the document
  exactly as the file has it, before any reader runs (`UndoHistory::beginLoad`).
* **Claimed always, never re-emitted.** A history this build cannot use (unknown `v`, damaged step, or a
  fingerprint that no longer matches) is dropped and reported (`project.undo_history`'s `last_load`), not
  preserved as unclaimed: a stale history kept verbatim would undo to a project that no longer exists.

**Compatibility, both ways (what was verified, §5):**

* an old project (no section) loads unchanged and re-saves with no section;
* a project WITH a history loads in a build that does not know the section (the base build `6d1a1143d`):
  the section is preserved verbatim as unclaimed, as SPEC-ARCH-4 1.6.1 requires. If that build saves an
  edit, the content no longer matches the fingerprint and this build discards the history on the next open
  and says why; if it re-saves without an edit, the history still matches and loads.

## 4. The surface

| | |
|---|---|
| `project.undo_history` | the project's setting, the global default and caps, what a save would keep now (`saveable_steps`, `saveable_bytes`, `oversize`), and what the last save and the last load did |
| `project.set_undo_history {setting}` | `on` / `off` / `default`; A16 class `snapshot`, the inverse is the command with the previous value; turning it off drops the snapshots the session's steps carried |
| `project.save` | gains `undo_history`: `enabled`, `written`, `saved_steps`, `saved_bytes`, `max_steps`, `cap_bytes`, `dropped_oldest`, `oversize`, `oversize_bytes`, `unavailable` (steps whose pre-state was never captured - made before persistence was turned on), `warning` |
| settings | `undohistory/persist`, `undohistory/steps`, `undohistory/capmb` (ConfigManager; `settings.get`/`settings.set`) |
| interface | Edit ▸ Undo History: *Save with project* (the project's setting) and *All projects* (the default), steps and MB for the saved history; a status message after Save when the history was not fully saved |

## 5. Proof

`tests/src/core/UndoHistoryPersistenceTest.cpp` (ctest `UndoHistoryPersistenceTest`), through real files
and the control surface:

1. `offByDefaultAndAnOldProjectRoundTripsUnchanged` - off by default; no section; an old project loads
   and re-saves as the same document.
2. `aSavedHistoryUndoesToEveryPreEditState` - five edits (an instrument parameter of tripleoscillator, an
   automation point on a linked parameter, a sample clip on a real WAV moved, a clip created, a rename),
   `project.save`, `project.open`; an edit on an unautomated model above the loaded history, undone, the
   newest loaded step undone and redone (two whole-project restores) and the edit redone - it must find its
   model by journal id (**id remapping**; a mutant that drops the snapshot ids fails here); then five
   `control.undo`s: after each, the whole document equals the one before that edit; then the track id, the
   automation link (resolved to the live model), the plug-in value and the sample reference are checked on
   the live objects, the provenance log is checked not rewound, and a loaded step is redone and undone.
3. `aHistoryTheProjectNoLongerMatchesIsDiscarded` - a re-serialised file (another attribute order) keeps
   its history; a file whose content changed under the history loads without it, says why, and the next
   save carries no stale steps.
4. `theCapsDropTheOldestAndAnOversizeStepIsReported` - the step limit drops the oldest; a ~1.5 MB
   compressed snapshot over a 1 MB cap saves nothing and reports `oversize` with a warning.

The red/green runs, the regression families, the mutation check and the cross-build check are in
`docs/wf033-logs/README.md`. Also `tests/src/gui/UndoHistoryPanelTest.cpp`
(`theSaveWithProjectBoxSetsTheProjectsChoice`): the interface box sets the project's own setting.

## 6. Honest limits

1. **Cost.** With persistence on, every edit serialises and compresses the whole project once (a command
   at its start, a direct editor edit at its checkpoint). That is the price of option C; it is why the
   feature is opt-in. Off, nothing is captured.
2. **Steps made before persistence was turned on cannot be saved** (`unavailable`); the history is
   contiguous from the newest step, so such a step also hides every older one.
3. **The redo stack is not saved**, only the undo side.
4. **Undoing a loaded step reloads the whole project** in place: playback stops, editors are rebuilt, as on
   an open. Undoing an in-session step is unchanged (object restore / closure).
5. **Only real saves carry it** - not autosave/recovery, templates, bundles, render temporaries or
   `project.save_version` copies.
6. **The persisted steps are also bounded by the session count cap** (`control.set_undo_depth`): a file
   with more steps than that cap loads only the newest ones.
7. **A history from a project whose setting is `default`** loads (for undo) on a machine whose default is
   off, but is not saved again there.
