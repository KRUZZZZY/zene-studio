# Zene Studio 0.3.0-alpha — release notes (in progress)

**Status: DRAFT, not a release record.** There is no `v0.3.0-alpha` tag yet, and this file is opened by the
warp-marker lane rather than by the release lane: the 0.3.0 scope contract
(`NEXT-0.3.0-AGENT-PROMPT.md` §3.1, `V0.3-ALPHA-PLAN.md` §1) requires every feature that ships in 0.3.0 to
write its **UI absence** down in one line in the release notes *and* in `docs/KNOWN-LIMITATIONS.md`. The
user-first rewrite of this file, the version bump and the rest of the notes are wave **W12**'s deliverable;
this draft carries the entries the earlier lanes are obliged to add as they land.

Released before this one: **v0.1.0-alpha** (`docs/RELEASE-NOTES-v0.1.0-alpha.md`) and **v0.2.1-alpha**
(`docs/RELEASE-NOTES-v0.2.1-alpha.md`, the patch line that made the seven-platform matrix build and the
release claims true).

## The shape of 0.3.0

0.3.0 is an **engine-and-agent release, not a UI release**. Everything that can exist in the backend and be
driven through the control surface is folded in; the interface stays deliberately minimal; and the taste work
(stock devices, factory content, the design system) plus every UX/UI item is deferred. Stated as one line:

> **Everything is operable through `--control-socket` and the MCP bridge; almost nothing is operable from the
> interface.**

## Warp marker editing (`warp.*`) — added 2026-09-13

- **New: the warp map has an editing surface.** `warp.list`, `warp.add`, `warp.move`, `warp.remove` and
  `warp.set` are registered control-surface commands, so an agent can pin a frame of a sample clip's audio to a
  timeline position, move and delete markers, replace the whole marker list, and choose the clip's warp tempo
  mode (`follow` the project, or `source` — lead with the tempo the clip was recorded at) and its declared
  source tempo in BPM. Each mutating call records its SPEC A16 reversibility class (`true_inverse`), how it is
  reversed (the clip's own ProjectJournal checkpoint) and its before-state, so one `control.undo` takes the
  edit back.
- **The engine half already shipped in 0.2.1.** The markers, the monotonic map, the `<warp>` element and the
  measured render are #597's and are unchanged by this addition; see `docs/WARP.md` for the implementing lane's
  report and its numbers. Nothing about the stretching was touched here.
- **UI absence — one line: warp marker editing is drivable through the socket, not from the interface.**
  There is no marker handle, no drag, no grid snap and no groove template; nothing in the interface can place
  the first marker. `docs/KNOWN-LIMITATIONS.md` carries the same sentence.
- **Still true, and still not fixed:** the stretch is resampling, so a warp changes pitch (2× is an octave
  up); there is no pitch-preserving time-stretch.

## Not in this draft yet

The Session View, racks, comping, MPE modulation, Link sync, browser search and the engine-gap items of the
0.3.0 scope, plus the release-bar statements, are the responsibility of their own lanes and wave W12. This
file grows as those land; it is not a summary of 0.3.0 and must not be read as one.
