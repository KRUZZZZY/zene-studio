# Luna undo fixes — 2026-10-05

Baseline: `bcac7c05ce4f1646b82d17cb696e64ae7311f1d4` (0.5.0 catch-up wave 3).
Lane: `bugs/luna-r7-2026-10-05`, independent `zene-luna-r7` worktree and build.

## Verified defects

1. **Device undo retains a destroyed chain.** Luna pass 3, revision
   `e5a603132`, slice 20, “Undo callbacks retain a deleted effect chain”.
   The pointer overloads of `recreateDevices` and `removeDevicesRecorded`
   retained a raw chain in journal closures. Wrapping a mixer channel, deleting
   it, and undoing the wrap crashed. Removing a device, removing its track, and
   undoing the track deletion could instead restore to an unrelated chain that
   occupied the retired allocation. Both overloads now resolve the recorded
   owner and chain address on every journal replay.
2. **Copied effects survive undo after their track is rebuilt.** The prior
   catch-up review left the `Made` holder's missing slot fallback open.
   `plugin.copy` and `chain.copy`, followed by track removal, undo removal, and
   undo copy, left the copied effect installed. Guarding an old object does not
   identify its replacement. Device moves now reuse the structural helpers'
   guarded instance / recorded slot-and-plugin lookup, with reverse-order
   removal. The regression also checks an unrelated effect survives and the
   sequence works again through redo and another track rebuild.
3. **Bounce undo leaves a rebuilt source muted.** Luna pass 6, revision
   `57980beef`, slice 2, “Undoing a bounce may leave a restored source track
   muted”, repeated in pass 7, revision `9a9f339dc`, slice 2. The guarded source
   object disappeared when the track was removed. Undoing that removal rebuilt
   the track, but undoing the bounce only removed the landed track. Bounce
   replay now resolves the source's recorded track ID before restoring mute.

These are three defects, not one defect per failing test or per repeated Luna
finding. The raw patrol remains a queue of claims, not a verified bug count.

## Implementation and contracts

`recordedChainFinder` and `removeRecordedEffect` expose the existing structural
undo helpers for use by `ControlDeviceMoves`. No command IDs, argument schemas,
result schemas, coalescing rules, or audio-thread code change. The slot fallback
retains the existing helper's plugin-descriptor comparison; this batch does
not introduce a new persistent effect identity or a hosted-subplugin identity.

`DeviceMovesUndoTest` requires the amplifier and bassbooster fixtures and fails
if they are absent. Its pointer-overload case deliberately occupies retired
allocations before undo: an earlier version accidentally passed the baseline
because the allocator reused the old chain's address. The strengthened case
was separately run against the baseline and failed.

The socket success-path test adds bounce/remove/undo/redo assertions and a
successful `window.sample_editor` call on the bounced clip, covering the
checked-command gap identified by the 2026-10-04 handover. Replies are checked
against the advertised result schema.

## Evidence and validation

- [Device failures and crash](luna-r7-2026-10-05-logs/red-device-moves.md).
  The initial combined run's socket leg had a harness invocation error; it is
  explicitly excluded from the bounce proof.
- [Strengthened pointer-overload failure](luna-r7-2026-10-05-logs/red-pointer.md).
- [Corrected bounce failure](luna-r7-2026-10-05-logs/red-bounce.md): only the
  two source-unmute assertions fail after the MIDI fixtures are built.
- [Targeted green run](luna-r7-2026-10-05-logs/targeted-green.md): all five
  suites pass (`DeviceMovesUndoTest`, `UndoHonestyTest`, `DeviceCopyTest`,
  `DeviceToolsTest`, `ControlSuccessPaths`), exit 0.
- Fork complexity and file-length checks pass without baseline changes.
- Full lane build, complete suite, and gate results are pending below.

Evidence uses Markdown because the repository's evidence gate refuses committed
`.log` files. The original command logs remain in `/tmp`; no gate exemption is
added. The independent build uses RelWithDebInfo, warnings as errors, Qt6,
stem splitting, VST3 and CLAP, pinned local SDK copies, and stripped executable
debug sections to keep the full test build within the disk budget.
