# Control batch-08 — Luna findings (2026-10-05)

Branch `bugs/bunny-control-2026-10-05`. Six findings, all verified against current code.

### Cluster 4980 — `project.merge` overwrites a project file but is classified as non-mutating

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlReversibilityTableMmpzGit.cpp:52-60` classifies `project.merge`
`NotMutating` with `reversible=false`, and the row's own reason states the merge result is
written to the `ours` path. That is the documented position, not an oversight: the same file's
sibling `project.diff` / `project.conflicts` / `project.audible_diff` rows share it, and
`src/core/ControlReversibilityTableDawProject.cpp:56-61` writes the general rule — "the three
readers/writers of the FILESYSTEM are not_mutating on the same ground project.save is: what
they change is a file outside the session, and the session's own serialized state is untouched,
so there is nothing an inverse could restore." `src/core/ControlCommandsProjectMmpzGit.cpp:284`
sets `cmd.mutating = false` with the matching comment "does not change the running session".

The finding's premise — "Other rows classify changes to external files as mutations" — is true
but does not carry over. `chain.save` (`ControlReversibilityTableChain.cpp:74-83`) is
`TrueInverse` because it captures `before.previous_sha256` and can rewrite the exact revision it
replaced: the inverse is available because the store keeps the prior bytes addressable. A merge
driver (`prepareRun` → a child-process `mmpz-git merge`) holds no such prior-revision handle, so
reclassifying this row would move `not_mutating` → `irreversible` in the published A16 figure
(`docs/RELEASE-NOTES-v0.3.0-alpha.md:955`, `not_mutating=353 irreversible=21`) for a row whose
documented semantic is a file-level tool the agent drives deliberately. That is a spec-level
reclassification, not a defect fix, and the `mutating` flag also feeds the registry's
transaction-recording decision — so it is out of this lane's scope rather than a wrong line.

Commit: none

Test: none

Red/green: none

### Cluster 4074 — `midi.retro_capture_arm` persists a setting that cannot be undone

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlReversibilityTablePassive.cpp:177-189` classifies
`midi.retro_capture_arm` `NotMutating` and the row already states in its own text that "The
command DOES write the config file's midi/retrocapture key, and only when the mode actually
moved" — the finding restates what the row documents. The justification is the config-vs-project
distinction: `ConfigManager` is not a `JournallingObject`, so that scalar is the persistent
default of a mode, not project state, and the row cites the `settings.set` precedent for the same
ground. `src/core/ControlCommandsRecordingRetro.cpp:238` sets `cmd.mutating = false`, and its
description repeats it ("Mode/engine state, not project state, so no transaction is recorded").

This is a deliberate, precedented classification of engine/mode state, in the same family as the
`transport.play` / `transport.stop` / `midi.learn_toggle` rows directly above it. Adding a config
checkpoint would also mean classifying ConfigManager-backed settings as project state — a design
change the table deliberately declines.

Commit: none

Test: none

Red/green: none

### Cluster 3063 — `wasm.get_state` consumes logs while classified as a read

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlReversibilityTablePassive.cpp:385-392` classifies `wasm.get_state`
`NotMutating` and the row itself discloses the drain: "It DOES consume the sandbox's log buffer -
the text belongs to the most recent call and takeLog() clears it - which the command's own
description states on the wire rather than leaving the caller to discover it". The handler
`src/core/ControlCommandsWasm.cpp:205-213` implements exactly that, and the registered description
(`src/core/ControlCommandsWasm.cpp:348-349`) tells the agent "'last_log' is the text the most
recent call passed to host_log() - reading it clears it."

So the behaviour is real but intentional, disclosed in both the table and the wire schema, and the
"consumed text" is a single-slot log buffer rather than project state — nothing an inverse would
restore. The proposed remedy (a non-consuming read, or exposing the drain as a separate mutating
command) means ADDING a control command or changing a command's result semantics, which rule 8
puts outside this lane: that moves the `ControlCommandsSnapshot` canary, the parent's job.

Commit: none

Test: none

Red/green: none

### Cluster 4075 — `midi.reconnect_arm` persists a setting that cannot be undone

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlReversibilityTablePassive.cpp:479-486` classifies `midi.reconnect_arm`
`NotMutating`, and the row's own mechanism column states "the flag is persisted to the config
file's midi/reconnect key (the midi.retro_capture_arm precedent)". The config-not-project
argument is the same one cluster 4074 turns on, and the group has a deliberate split: the
BINDING writer `midi.reconnect_set` is `TrueInverse` through a recorded action step (see the
`reversibilityMidiReconnectRowTable` contract in `include/ControlReversibility.h:141-152`, and
why: `MidiPort::loadSettings` only ever SUBSCRIBES ports, never detaches one, so a Track
checkpoint could not take a binding off), while this command only flips the MODE the client's
port-list poll reads. `src/core/ControlCommandsMidiReconnect.cpp:168` sets `cmd.mutating = false`
and the description at `:150` names the persisted key.

Commit: none

Test: none

Red/green: none

### Cluster 4994 — WASM snapshot reversibility rows disappear in WASM builds

Verdict: FALSE-POSITIVE

Evidence: the finding's mechanism is wrong: it assumes `LMMS_HAVE_WASM` reaches a translation unit
through the generated `lmmsconfig.h`. It does not. `src/CMakeLists.txt:163-167` defines it ONLY as
a target compile definition on `lmmsobjs` (`TARGET_COMPILE_DEFINITIONS(lmmsobjs PRIVATE
LMMS_HAVE_WASM=1)`), and `src/CMakeLists.txt:298-302` says so explicitly — "Every LMMS_HAVE_*
macro the closure uses arrives through the generated lmmsconfig.h - except LMMS_HAVE_WASM, which
exists ONLY as this compile definition". `ControlReversibilityTableSnapshot.cpp` is compiled as
part of `lmmsobjs` (the `LMMS_SRCS` glob at `src/CMakeLists.txt:137-141`), so its guard at `:183`
sees the macro in any `WANT_WASM` build with no include required; the sibling guards in
`ControlReversibilityTablePassive.cpp:370` and `ControlReversibilityTableWasmRender.cpp:45` work the
same way and also include only `ControlReversibility.h`. Adding `#include "lmmsconfig.h"` would be
a no-op.

Confirmed live rather than by inference: this tree configures `WANT_WASM:BOOL=ON`
(`build/CMakeCache.txt:740`) while the generated `build/lmmsconfig.h` contains no `WASM` token at
all, and `ControlRegistryRegistrations.cpp:266` gates the wasm registrations on the same macro as
the table rows — so registry and table stay in step, which is what
`ReversibilityContractTest::everyRegisteredCommandHasAContractRow()` asserts in both directions.

Commit: none

Test: none

Red/green: none

### Cluster 4070 — Folder routing undo can lose mixer-channel settings

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlReversibilityTableTrackFolder.cpp:92-108` classifies `track.set_routing`
`TrueInverse` and the row already carries the finding's exact scenario as a written LIMIT:
"a channel released by the switch is deleted and switching back creates a fresh one, so a child
can come back on a different channel INDEX (Mixer::deleteChannel renumbers)". The mechanism is an
action checkpoint that switches the mode back through the same call, restoring the mode AND every
child's channel as ONE step from the folder's `prevch` attribute.

The implementation confirms the deletion is deliberate and ordered, not incidental:
`src/tracks/TrackFolder.cpp:382-393` (`releaseRouting()`) unroutes every child first, clears
`m_savedChildChannels`, deletes the folder channel, then shrinks each child's channel range — with
a comment naming the silent-wrong-route failure the ordering designs out. `prevch` is written and
read at `src/tracks/TrackFolder.cpp:562,579-580`, and the same limit is disclosed on the wire in
`src/core/ControlCommandsTrackFolder.cpp:281`. Preserving a deleted channel's fader, effects and
sends would mean retaining the channel rather than deleting it — a design change to the routing
model, larger than this lane's fix budget.

Commit: none

Test: none

Red/green: none

Leftovers: nothing half-done — no product code was changed in this batch, so no build or test
target was rebuilt and no regression was run. All six verdicts rest on reading the current
classification rows against their handlers; the two config-persistence findings (4074, 4075) and
the log-drain finding (3063) are deliberate, precedented classifications whose suggested remedies
would require adding or reclassifying control commands (rule 8, parent's lane), and 4980's
remedy would move the published A16 histogram in
`docs/RELEASE-NOTES-v0.3.0-alpha.md:955`.