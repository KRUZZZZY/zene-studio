# cmds2 batch-08 — 2026-10-05

Six findings, six distinct claims. One fixed, one real but untestable in this
build, one out of scope, three false positives. The one that was genuinely
wrong in the tree beyond the fixed defect — a stale "zones do not route" note in
a source header — is corrected in a comment-only commit.

### Cluster 5747 — Removing a layer zone publishes an unusable inverse

Verdict: FIXED

Evidence: `src/core/ControlCommandsRackLayers.cpp:345-346` built the transaction
payload for `rack.layer_zone_remove` as `transactionPayload(layerZoneJson(zone,
index), "rack.layer_zone_add", QJsonObject{{"target", trackIdText}}, ...)`, so
the advertised inverse carried only the track. `rack.layer_zone_add`'s args
schema (`:310-317`) requires `target` and `layer` plus the four range values and
is closed (`additionalProperties: false`), so replaying the advertised inverse
failed argument validation and could not restore the dropped zone. The recorded
undo step already re-inserts the captured `RackZone` at its own index
(`:338-342`), so the args only had to describe the zone: they now carry `layer`
and `low_key`/`high_key`/`low_velocity`/`high_velocity`, the same shape
`plugin.midi_part_remove` already used (`src/core/ControlCommandsPluginParts.cpp:232-239`).

Commit: a84e5d2fe

Test: InstrumentLayersCommandsTest::layerZoneRemoveAdvertisesAReplayableInverse

Red/green:
- Red: `cmake --build build --target zene InstrumentLayersCommandsTest` EXIT=0;
  `cd build/tests && ctest -R '^InstrumentLayersCommandsTest$'` EXIT=8 —
  `FAIL! ... Compared values are not the same: inverseArgs.value("layer") was
  "", expected "layer-0"`
- Green: same build EXIT=0, same ctest EXIT=8 with
  `layerZoneRemoveAdvertisesAReplayableInverse PASS` (6 passed, 1 failed,
  1 skipped)
- Gates: `complexity-gate.sh --check` EXIT=0; `file-length-gate.sh --check` EXIT=0
- Pre-existing, untouched: `outputBusesAnswerTyped` fails identically on
  unmodified HEAD (verified by rebuilding the HEAD revision of the test file and
  re-running: EXIT=8, `5 passed, 1 failed`), so it is not caused by this change
  and was left alone.

### Cluster 5743 — `rack.wrap` can reorder selected effects

Verdict: REAL-UNTESTED

Evidence: the defect is real. `src/core/ControlCommandsRackWrap.cpp:63-78`
(`namedDevices`) pushes devices in exactly the order the caller listed them in
`plugins`, and `wrap` then captures state XML in that order (`:158-159`) and
recreates the devices in it (`:165-169`). Wrapping a chain that holds A then B
with `plugins: [B, A]` therefore yields B then A, contradicting the in-order
promise in the file header (`:24-28`) and in the command description (`:181-186`).

No product code was changed: this build ships no effect modules at all
(`build/plugins/` contains only `libtripleoscillator.so`,
`libclapinstrument.so`, `libzynaddsubfx.so` — all instruments), so no effect can
be loaded onto a chain and `rack.wrap` can never be exercised. Every effect test
in `DeviceCopyTest` skips with "this build loads no effect", including
`wrappingMovesTheDevicesIntoARackChain` and the drum-rack paths (a drum-rack pad
channel carries exactly one device, so it cannot express a two-device order).
A regression test was written and built but could only ever skip, so per the
lane procedure it was reverted rather than shipped unverified.

Commit: none

Test: none (a candidate case, `DeviceCopyTest::rackWrapKeepsTheSourceOrderOfTheSelectedDevices`,
was written, built, and reverted because it cannot fail in this build)

Red/green: none

### Cluster 2062 — `rack.wrap` leaves macro targets pointing at old effect positions

Verdict: SKIPPED-SCOPE

Evidence: the observation is accurate — `include/RackMacros.h:58-66`
(`RackMacroTarget`) addresses a device by chain index plus effect index, and
nothing in the move path updates it: `include/ControlDeviceMoves.h:45-70` and
`src/core/ControlDeviceMoves.cpp` (186 lines) expose only move, recreate,
record and journal helpers, with no macro handling. `RackMacros` itself
(`include/RackMacros.h:146-150`) offers only `addTarget`, `insertTarget` and
`removeTarget` — no way to rewrite an existing target's chain/effect indices, so
there is nothing to call from the move path, and dead targets are silently
skipped at `macro_set` time by design (`docs/RACK-MACROS.md:78-88`).

The fix needs a new public target-update API on a persisted model plus an undo
step recording every macro target it rewrote, on top of the existing move steps.
That is a design change to `RackMacros`, not a minimal patch.

Commit: none

Test: none

Red/green: none

### Cluster 2064 — Removing a rack chain has no undo action

Verdict: FALSE-POSITIVE

Evidence: the undo step exists. `src/core/ControlCommandsRack.cpp:285` calls
`journalRemoval(...)` BEFORE `rack.removeChain`, and `journalRemoval`
(`:213-219`) captures each device's state XML and hands it to
`journalChainRemoval` (`src/core/ControlDeviceMoves.cpp:156`), which is the
structural undo step `include/ControlDeviceMoves.h:65-70` documents. An inner
chain — one that other chains still feed — cannot be restored, and
`removalTransaction` (`:223-237`) honestly reports `reversible: false` for
exactly that case, with the command description saying so at `:249`. The
handler returns bounded state XML AND registers a restoring undo step; the
finding's "only returns bounded state XML" is wrong.

Commit: none

Test: none

Red/green: none

### Cluster 2916 — `rack.wrap` ignores device recreation errors

Verdict: FALSE-POSITIVE

Evidence: the `error` output IS checked. `src/core/ControlCommandsRackWrap.cpp:169`
passes `&error` into `recreateDevices`, and `wrapResult` (`:131-145`) compares
`ids.size()` against the number named and, when fewer came back, inserts
`stopped` with the reason and `not_wrapped` with the shortfall (`:138-143`)
instead of reporting plain success. The mechanism text at `:135-137` and the
description at `:185` both tell the caller `control.undo` is the way back. The
command does not "return success regardless"; it returns a result that names the
failure and the remedy, which is the documented shape for this move family.

Commit: none

Test: none

Red/green: none

### Cluster 4898 — Rack zone command descriptions claim note routing that is not implemented

Verdict: FALSE-POSITIVE

Evidence: note routing IS implemented, so the description is accurate.
`src/core/AudioBusHandle.cpp:175` constructs `RackZoneFeed zoneFeed(...)` on the
instrument-track path, routing notes through the zone model;
`tests/src/core/RackZoneRoutingTest.cpp` covers it and
`docs/KNOWN-LIMITATIONS.md:265-269` records that "Since 0.5.0 (E20/E38) zones
route: a note of an instrument track playing into the channel that a zone claims
plays on that zone's chain only". The `rack.zone_add` description
(`src/core/ControlCommandsRackZones.cpp:105-112`) describes exactly that. The
description and its schema are part of the `ControlCommandsSnapshot` canary and
were not touched.

What WAS stale is the file's own header, which still claimed "nothing in this
build calls RackZones::resolve to pick a chain while a note plays" (`:10-14`).
That comment is now rewritten in `9d6ac5124` to state what is true — the zone
model is socket-only, and routing lives in `RackZoneFeed`, not in this file.

Commit: 9d6ac5124 (comment-only correction of the stale header; the command
description was deliberately left as-is)

Test: none (no behaviour changed)

Red/green:
- Green: `cmake --build build --target zene RackZonesTest RackZoneRoutingTest`
  EXIT=0; `cd build/tests && ctest -R '^RackZonesTest$|^RackZoneRoutingTest$'`
  EXIT=0
- Gates: `complexity-gate.sh --check` EXIT=0; `file-length-gate.sh --check` EXIT=0

Leftovers: Cluster 5743 (rack.wrap ordering) is still unfixed — the code defect
is real and the one-line ordering fix in `namedDevices` is straightforward, but
this build has no effect modules so it cannot be given a failing test, and it was
left alone rather than shipped unverified; it needs a build with a loadable
effect plugin. Cluster 2062 (macro targets not remapped by a move) is still open
and needs a `RackMacros` target-update API before it can be fixed. Separately,
`InstrumentLayersCommandsTest::outputBusesAnswerTyped` fails on unmodified HEAD
in this environment (`plugin.output_buses` reports `bus_count` 0) — pre-existing,
unrelated to this batch, and untouched.
