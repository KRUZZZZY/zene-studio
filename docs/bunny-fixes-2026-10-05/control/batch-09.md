# Control batch-09 — Luna findings (2026-10-05)

Branch `bugs/bunny-control-2026-10-05`. Six findings, all verified against current code.
Three fixed with a regression test each, one duplicate, two false positives.

### Cluster 4996 — `plugin.record` changes saved device state without an undo record

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlReversibilityTableW3Devices.cpp:61-63` already states the position in
the row's own reason column: `not journalled: the Recorder's last-take path (saved with the
project) is not put back; delete the file`. The row is `NotMutating` with `reversible=false`,
so the persistent change is disclosed rather than missed, and `plugin.recorder_land` on the next
line (`TrueInverse`) is the row that carries a real inverse for the take itself.

The finding asks for "an inverse for the device state change; the take file may remain as an
output artifact" — but the device-state write is what the row documents as out of reach, and
adding an inverse means classifying the row differently. That reclassification would move
`not_mutating` → `irreversible` for this row in the published A16 histogram
(`docs/RELEASE-NOTES-v0.3.0-alpha.md`), the same spec-level move this lane declined in batch-08
for clusters 4980 / 4074 / 4075, and the `mutating` flag also feeds the registry's
transaction-recording decision. Reclassification belongs to the parent's lane, not to a fix here.

Commit: none

Test: none

Red/green: none

### Cluster 4995 — W3 WASM reversibility rows disappear in WASM builds

Verdict: DUPLICATE (of batch-08 Cluster 4994, already adjudicated FALSE-POSITIVE)

Evidence: `src/core/ControlReversibilityTableWasmRender.cpp:45` guards the rows with
`#ifdef LMMS_HAVE_WASM`, and the finding's premise is that the macro "has no lmmsconfig.h include
in this translation unit". But `LMMS_HAVE_WASM` does not come from `lmmsconfig.h` at all: it is a
target compile definition set in `src/CMakeLists.txt:163-167`
(`TARGET_COMPILE_DEFINITIONS(lmmsobjs PRIVATE LMMS_HAVE_WASM=1)`), and
`src/CMakeLists.txt:296-302` says so in as many words — "Every LMMS_HAVE_* macro the closure uses
arrives through the generated lmmsconfig.h - except LMMS_HAVE_WASM, which exists ONLY as this
compile definition". Adding the include would therefore be a no-op. The rows do not disappear.

The comment at `ControlReversibilityTableWasmRender.cpp:40-44` also states the design: a build
without the wasmtime C API compiles no `wasm.*` command, so it must carry no row for an id it
cannot answer. This is the identical false mechanism reported and adjudicated in batch-08
(`docs/bunny-fixes-2026-10-05/control/batch-08.md`, Cluster 4994).

Commit: none

Test: none

Red/green: none

### Cluster 4998 — `ControlRoom::setState` can index trims out of bounds

Verdict: FIXED

Evidence: `src/core/ControlRoom.cpp:57-66` copied the caller's `State` verbatim and published it,
so `publish` (`ControlRoom.cpp:57`) indexed `state.trimsDb[state.speaker]` with an unchecked
speaker, and folded a NaN/inf trim into the gain the audio thread reads. The exported setter is a
plain C++ entry point on a public struct, and nothing in it validated the range.

No in-tree command path reaches the bug — `src/core/ControlCommandsMixerMeters.cpp:183-188`
rejects speaker D and `loadPreferences` clamps with `std::clamp` at `ControlRoom.cpp:82` — so the
fix is defence in depth for the exported entry point rather than a behaviour change for any
command. `setState` now clamps the speaker to 0..2 and replaces non-finite trims and `dimDb`
with 0.f, mirroring `storedTrim` (`ControlRoom.cpp:50-55`). No command, parameter or result
schema changed, so the `ControlCommandsSnapshot` canary is untouched.

Commit: e729346b4

Test: MixerToolsTest::theControlRoomSurvivesAnOutOfRangeState

Red/green:
- Red: `cmake --build build --target zene MixerToolsTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0; `cd build/tests && ctest -R '^MixerToolsTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` -> EXIT=8 — `FAIL! theControlRoomSurvivesAnOutOfRangeState() 'settled.speaker >= 0 && settled.speaker <= 2' returned FALSE.`
- Green: same build -> EXIT=0; same ctest -> EXIT=0, 100% tests passed (18 cases).
- Gates: `bash tests/complexity-gate.sh --check` -> EXIT=0; `bash tests/file-length-gate.sh --check` -> EXIT=0.

### Cluster 3105 — Malformed custom scale degrees silently become C

Verdict: FIXED

Evidence: `src/core/ControlScaleSupport.cpp:78-83` read every entry of `scales/custom` with
`toInt()`, which returns 0 for anything it cannot read, so a hand-edited `"Odd":[true]` produced
a one-degree scale and a misleading pitch mask. The writer `defineScale`
(`src/core/ControlCommandsNoteHarmony.cpp:136-140`) validates 0..11 and defaults missing values,
so the read side now applies the same window in a `customScaleDegree` helper: whole numbers in
0..11 are kept, other entries are ignored, and an entry with nothing usable falls through to the
engine table instead of answering with a one-note C. Only a hand-edited config can reach this, so
this is defence in depth; `scale.define` behaviour is unchanged.

Commit: d53d36aec

Test: ControlNoteScaleVerbsTest::malformedCustomScaleEntriesAreIgnored

Red/green:
- Red: `cmake --build build --target zene ControlNoteScaleVerbsTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0; `cd build/tests && ctest -R '^ControlNoteScaleVerbsTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` -> EXIT=8 — `FAIL! malformedCustomScaleEntriesAreIgnored() scaleDegreesOf("Odd").empty() returned FALSE.`
- Green: same build -> EXIT=0; same ctest -> EXIT=0, 100% tests passed.
- Gates: `bash tests/complexity-gate.sh --check` -> EXIT=0; `bash tests/file-length-gate.sh --check` -> EXIT=0. (The first run of the complexity gate flagged `scaleDegreesOf` at CCN 11 after the in-loop validation was added; the per-entry test was moved into `customScaleDegree` rather than re-anchoring the ratchet.)

### Cluster 5019 — Required fields are skipped when a schema omits `properties`

Verdict: FIXED

Evidence: `src/core/ControlSchema.cpp:137` began with
`if (!value.isObject() || !spec.contains(QStringLiteral("properties"))) { return QString(); }`, so
the `required` loop on the next lines was unreachable for a schema that declares only
`{"required":[...]}`, and an empty args object passed `ControlRegistry::validateArgs` — a public
static validator. The required loop now runs independently of `properties`, and the properties
check is kept for the `additionalProperties`/per-property validation it guards.

No registered schema hits the gap today: `control::objectSchema`
(`src/core/ControlVocabulary.cpp:60-67`) always inserts `properties`, which is why no
command-level test could expose it. The fix hardens the validator without touching any command
schema, so the `ControlCommandsSnapshot` canary does not move.

Commit: a76468b63

Test: ControlRegistryTest::requiredIsCheckedWithoutAPropertiesObject

Red/green:
- Red: `cmake --build build --target zene ControlRegistryTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0; `cd build/tests && ctest -R '^ControlRegistryTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` -> EXIT=8 — `FAIL! requiredIsCheckedWithoutAPropertiesObject() ... returned FALSE.`
- Green: same build -> EXIT=0; same ctest -> EXIT=0, 100% tests passed. Also ran the other
  test covering the changed file, `ControlGrooveCommandsTest` -> EXIT=0.
- Gates: `bash tests/complexity-gate.sh --check` -> EXIT=0; `bash tests/file-length-gate.sh --check` -> EXIT=0.

### Cluster 5010 — Draining an oversized request can monopolize the event loop

Verdict: REAL-UNTESTED

Evidence: `src/core/ControlServer.cpp:242-252` — `drainPeer` loops on `::read` until EOF or
EAGAIN, so a peer that keeps the socket supplied can hold the activation. The mechanism is
correct as described: `refuseOverCapLine` (`ControlServer.cpp:384-395`) sets `draining` and the
readable branch (`ControlServer.cpp:322-336`) drains before handling more.

No product code was changed. Reproducing it deterministically needs a real socket peer that keeps
writing while the event loop is observed, with timing — a unit test would assert on scheduling,
not on a defect, and would be flaky headlessly. Per procedure (step 2) an untestable finding
leaves the code alone.

Commit: none

Test: none

Red/green: none

Leftovers: Cluster 5010's bounded-drain fix is still owed to whoever can build a socket-level
test harness for the control server (a peer that floods while the Qt loop is pumped); clusters
4996 and 4995 need no work unless the parent reclassifies the `plugin.record` reversibility row
or the WASM macro's provenance changes.