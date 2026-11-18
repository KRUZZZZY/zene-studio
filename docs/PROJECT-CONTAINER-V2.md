# The v2 project container (opt-in)

Q050-007. A `.mmpz` project can be saved in one of two shapes. Which one a file is gets decided by its
bytes, never by its name.

| | legacy `.mmpz` (default) | v2 container |
| --- | --- | --- |
| bytes | one `qCompress()` blob of the document's XML | a STORE ZIP: a skeleton entry plus one entry per section (`ProjectContainer.h`) |
| size | smallest | larger: the sections are stored, not compressed |
| older builds | open it | **cannot open it** |
| every build since this one | opens it | opens it (`project.open`, File > Open, recent projects) |

**The default has not changed.** Ordinary Save, Save As, `project.save` without `format`, autosave,
templates, renders and every other writer still produce the legacy blob. Nothing is converted unless
someone opts in.

## Opting in

- **Save As dialog**: the "Save as v2 container (larger file; older builds cannot open it)" checkbox, and
  File > "Save as v2 Container..." which opens the same dialog with the box ticked. The box starts at what a
  plain Save of this project would write, so unticking it is the way a v2 project goes back to legacy.
- **`project.save {path, format: "v2"}`** (agents). `format: "legacy"` forces the old blob. The reply carries
  `format`, `bytes` and, for a container, `format_note` (the larger file / older builds sentence).
- **Global setting** `app/savecontainerv2` = `"1"` (Settings > General > Projects, or `settings.set`): every
  save of a `.mmpz` becomes a container. A `.mmp` or `.mpt` name is never affected.
- **A project that already is a container** stays one on ordinary Save (opened from, or last saved as, a
  container). Silently turning it back into a blob would be the surprise.

A container is only ever written to a name ending in exactly `.mmpz` (lowercase: the writer treats any
other spelling as no suffix and would save a different file than the one named). Asking for one with another
name, `X.MMPZ` included, is refused (typed `invalid_args` from `project.save`; "not saved" in the GUI) and
writes nothing. A Save As that ends on the v2 side (box ticked, or a project that already is a container) runs
through the registered `project.save` command, so the GUI and an agent share one implementation, its revision
rotation and its transaction record; a plain legacy Save As keeps the dialog's own path (bundle and MIDI-connection options).

## Opening

`project.open`, File > Open and recovery recognise a container by its ZIP signature and reassemble the one
document it was cut from, byte for byte (`readDocumentContainer`), then load it exactly as a legacy file.
An interrupted v2 save (`<file>.new`) is adopted or discarded by the same rule as a legacy one; the 256 MiB
recovery limit is measured on the reassembled document (the ZIP file gets 1 MiB of framing allowance). A truncated or
foreign ZIP is refused by name and the session is left as it was.

## Known limits

- A partial `project.open {sections}` of a container still reads every entry (the reader copies the whole
  file first), so it saves parsing, not I/O.
- `project.missing_assets`, `project.hash_assets` and `project.relink` read only the legacy `.mmpz` and `.mmp`;
  they refuse a container by name. `zene --dump` / `--compress` also refuse one by name.
- The revision timeline's compare treats a container as unreadable (it cannot decompress one).
- Older builds, and any tool that expects `qCompress` bytes, cannot read a v2 file.
