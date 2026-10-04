# The note editor (0.5.0)

The reworked piano roll, and the content of the shell's **Note Editor** panel (`detail`): below
the arranger in the Arrange view, in the centre in the Edit view (`docs/UI-SHELL.md`). The
owner asked for it on 2026-10-02 ("rework the piano roll, I want it based kind of on Bitwig").
It borrows the interaction patterns and none of the assets.

## 1. Layout

```
[clip name] [draw select slice erase audition] [grid v] [Q] [fold scale] [fold used] [scale v] [ghosts] [fit]
          +-----------------------------------------------------------------------------+
          | 1        2        3   <- bars; the accent bar is the clip's loop region      |
  +-------+-----------------------------------------------------------------------------+-+
  | keys  | notes: rows (scale rows tinted), grid, ghost notes, notes coloured by       | |
  |       | velocity, the clip end marked in the accent, the playhead                   | |
  +-------+-----------------------------------------------------------------------------+-+
  | lane v| expression lane: one stem per note (velocity / pan / chance / timbre /       |
  |       | pressure / micro-pitch)                                                       |
  +-------+-----------------------------------------------------------------------------+
```

## 2. One implementation for the user and the agent

Every edit the editor makes is a registered command. The editor and an agent on the socket
therefore make the same call, leave the same transaction record and share one undo history
(SPEC A11):

| Gesture | Command |
|---|---|
| Draw a note (click, or drag for its length) | `note.add` |
| Move, transpose or resize a selection (either edge); nudge with the arrow keys; Alt+wheel velocity or nudge; mute stroke; the properties box | `note.edit`: one batch, one undo step |
| Delete (a right-drag, a Delete-tool stroke, the Delete key) | `note.remove_many`: one undo step |
| Slice (the Slice tool's line, or a right-drag from an empty row) | `note.slice`: one undo step |
| Copy, cut, paste, duplicate (Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+B) | `note.copy` (no undo step: the clipboard is not project state), `note.cut`, `note.paste`, `note.duplicate` |
| Paint stroke | `note.add_many` |
| Select (click a note, Ctrl-click, Ctrl-drag, Ctrl+Shift to add, Ctrl+A, Escape) | `note.select` |
| Velocity, pan or chance lane | `note.edit` (one batch per stroke) |
| Timbre, pressure or micro-pitch lane | `note.expression_set` per note |
| Quantise | `groove.quantize` |
| Scale and root | `scale.set`, `scale.root_set` (read back with `scale.get_state`) |
| Drag the end of the loop region | `clip.resize` |
| Tool, grid, fold, lane, ghosts, zoom, fit, open a clip | `window.note_editor_set` (view state, not journalled); `window.get_state` reports the same object as `note_editor` |

`note.edit` is new in 0.5.0. Several notes' `position`, `key`, `length`, `velocity`, `pan`
and `probability` change as **one** edit, so a drag of a forty-note selection is one undo step,
not forty. Before 0.5.0 no command could set a note's key; `note.transpose` only moves a whole
scope by a fixed amount. The whole batch is validated first, and one bad entry refuses it and
changes nothing. The inverse is `note.edit` with each note's previous values, and one MidiClip
journal checkpoint covers the batch.

**Selection** is the notes' own flags (`Note::selected()`), which `note.select` now sets as
well as its index store (BUGS_FOUND 11.33): before 0.5.0 a selection made over the socket
never reached an editor, so an agent could not select what the user would then drag.

The editor repaints from the clip's own `dataChanged` signal. Every note command emits it, so
an agent's edit shows at once.

## 3. Tools and gestures: FL Studio's piano roll

The owner decided on 2026-10-03 that the editor follows FL Studio's piano roll
(`research/reviews/bughunt-2026-10-03/ux-research/FL-PIANO-ROLL-SPEC.md`). The tools take FL's
letters; **1** to **7** still pick them, in the order draw, select, slice, delete, play, paint, mute.

| Tool | Key | What it does |
|---|---|---|
| Draw (the default) | P | Add, move, resize and delete notes |
| Paint | B | Lay a note on every grid cell the drag crosses |
| Delete | D | Click or drag to delete notes (one undo step per stroke) |
| Mute | T | Click or drag to mute notes (the first note crossed decides mute or unmute) |
| Slice | C | Drag a line; every note it crosses is cut where it stands |
| Select | E | Click or drag a rectangle to select; drag a note to move it |
| Zoom to selection | Z | Fit the selected notes, or the whole clip when none is selected |
| Play | Y | Press a note or a row to hear it; drag to scrub |

With the Draw (and Select) tool:

- **Left click** on an empty row draws a note of the last note's length: the last one drawn,
  touched or resized. **Left drag** from an empty row draws it and sets its length.
- **Left drag** on a note moves it (the selection with it); on its right edge it resizes it;
  **Shift-drag on its left edge** resizes it from the start.
- **Right click or drag** deletes every note the pointer crosses, whatever the tool; a right
  drag that starts on an empty row is the slice tool for that stroke.
- **Double-click** a note to open its properties: key, start, length, velocity, pan, release
  velocity, Mod X and Mod Y (the note's MPE timbre and pressure), chance, slide and mute. OK
  writes the changes as one `note.edit`. A double-click that draws a note does not open it.
- **Ctrl**-click selects one note, Ctrl-drag a rectangle; **Ctrl+Shift** adds to the selection.
- While dragging, **Shift** locks the pitch and **Ctrl** the timing; **Alt** turns the snap off.
- **Alt+wheel** over a note changes its velocity by 5 (Ctrl+Alt by 1); **Shift+Alt+wheel**
  nudges it a grid step. The selection follows when the note is part of it. The wheel alone
  scrolls; Ctrl zooms, Shift scrolls sideways, Alt over an empty row changes the row height.
- The **middle button** drags the view both ways; a middle-button vertical drag on the key
  column zooms the rows.
- **Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+B** copy, cut, paste and duplicate the selection (the whole clip
  when nothing is selected); a paste lands where the notes were copied from, and a duplicate
  just after them; both leave the new notes selected. **Delete** removes the selection; the
  arrow keys move it by a grid cell or a semitone (Shift: an octave). The Notes menu's Edit
  submenu has the same verbs. On macOS Ctrl reads as Command and Alt as Option.
- **New Note Length** (the Notes menu, or a right-click on the grid box) fixes the length a new
  note takes instead of the last note's.

## 4. Rows, scale and folding

- **Scale highlight:** rows in the scale context's scale (`scale.get_state`) are tinted, and the
  key column marks them with an accent bar.
- **Fold to scale:** shows only those keys. **Fold to used keys:** shows only the keys the clip
  plays. Folding hides rows, never notes. A fold with nothing to show, such as no scale set or an
  empty clip, shows every key instead. A drag in a folded view moves by visible rows, so a move
  stays in the scale.
- **Ghost notes:** the other MIDI clips on the same track are drawn as outlines at their
  arrangement offset.

## 5. Expression lane

The lane shows one stem per visible note for the chosen value:

| Lane | Range | Notes |
|---|---|---|
| Velocity | 0 to 200 | the engine's note volume |
| Pan | -100 to 100 | |
| Chance | 0 to 100 % | play probability |
| Timbre | 0 to 127 | MPE CC74 |
| Pressure | 0 to 127 | MPE |
| Micro-pitch | ±4800 cents | drawn over ±200 |

Drag across stems to draw values. With a selection, only selected notes take the value, so a
ramp can be drawn over a chord.

The brief also names a **gain** lane, but this engine has no per-note gain separate from
velocity, which is the note's volume. Adding one would mean a new note attribute, a new
project-format field and a change to the render path. That was not done; it is listed in §8.

## 6. Performance

A frame draws only what is on screen:

- **Notes:** the visible ones are found by binary search over the position-sorted note list,
  starting one longest-note length to the left of the view.
- **Grid:** thins out as you zoom out.
- **Ghost clips:** those outside the view are skipped whole.

Zoom is continuous (Ctrl+wheel, anchored under the pointer) rather than stepped.
`PianoRollBenchmarkTest` measures this editor beside the classic piano roll at 1k, 5k, 10k
and 50k notes, at the default zoom and fully zoomed out.

Measured on the development box (2026-10-02, RelWithDebInfo, offscreen, 1600 × 900 window,
median of five frames, two runs):

| Notes in the clip | Note editor, default zoom (ms) | Note editor, widest zoom (ms) | Notes on screen at widest | Classic roll, default (ms) | Classic roll, 12.5 % (ms) |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 000 | 2.9-3.3 | 4.0-4.3 | 1 000 | 4.4 | 13.1 |
| 5 000 | 2.8-3.2 | 8.4-9.0 | 5 000 | 4.2 | 13.4 |
| 10 000 | 2.8 | 9.2-9.4 | 6 259 | 4.3 | 13.6 |
| 50 000 | 2.7-2.8 | 9.2-9.3 | 6 259 | 4.5 | 13.5 |

- **At default zoom** the new editor's frame is about two thirds of the classic roll's, and
  it is flat in the clip's size. Notes off screen cost nothing.
- **With the same ~1 000 notes on screen**, it draws them in about 4 ms, where the classic roll
  takes 13 ms. The test asserts this comparison, which holds on any machine (new ≤ classic).
- **Zoomed out it reaches further:** 6 259 notes on screen, which the classic roll's 12.5 %
  floor cannot show, still fit a 60 fps frame (9.3 ms against 16.7 ms).
- **Two changes bought most of that** (measured, from 24 ms at 6 259 notes):
  - The velocity colour ramp is a 201-entry table instead of a mix per note.
  - The lane draws its dots only when the stems are far enough apart to tell them apart, and
    notes only a few pixels wide skip their edge band and label.

The +20 % regression gate stays opt-in per machine (`ZENE_BENCH_BASELINE`). The keys are
`ne:<n>` and `ne:<n>@widest` (`docs/PIANO-ROLL-BENCHMARK.md`).

## 7. The classic piano roll

The classic editor (`PianoRollWindow`) still exists, for what the new editor does not do yet:
recording from a MIDI keyboard, step input, chord stamping and detuning automation. It opens
as a floating window from View ▸ Classic Piano Roll, and `window.toggle` accepts the name
`classic_piano_roll`. Opening a clip sets it in both editors.

## 8. Not done, stated

- **Not in the new editor yet:** MIDI recording, step input, chord stamping and detuning
  automation. Use the classic piano roll (§7).
- **Gain lane:** there is no separate per-note gain lane (§5).
- **Not measured:** clips of several thousand notes with the expression lane open; the lane
  uses the same visible range as the note area.
