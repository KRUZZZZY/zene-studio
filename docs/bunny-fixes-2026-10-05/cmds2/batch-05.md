# Bunny fixer lane `cmds2` — batch-05 report

Six findings, three of them real and fixed, one a well-argued false positive, two real but
without a deterministic headless test (no product code was changed for those). Every fixed
defect has a regression test that was seen RED first, both gates green, and its own commit.

## Cluster 2057 — Project relink's transaction inverse is not a valid relink command

- **Verdict:** FIXED
- **Evidence:** `src/core/ControlCommandsProjectArchive.cpp:385` built
  `__transaction.inverse` as `project.relink` carrying only `project` and a `note`, while the
  command's own `argsSchema` requires `project`, `from` and `to`
  (`ControlCommandsProjectArchive.cpp:288-320`). A client replaying the advertised inverse
  failed argument validation. `kRelinkMechanism` (`:75-77`) already named `control.undo` as
  the action checkpoint, so the payload now matches the mechanism: `control.undo` with empty
  args, the same shape the commit `8c3e916c1` used for plugin state load.
- **Commit:** `d4a154433`
- **Test:** `ControlProjectArchiveTest::theRelinkInverseIsACommandAClientCanReplay`
- **Red/green:**
  - RED: `cmake --build build --target zene ControlProjectArchiveTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0
  - RED: `cd build/tests && ctest -R '^ControlProjectArchiveTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` -> EXIT=8
    ("the advertised inverse project.relink is missing the required argument 'from'"; 13 passed, 1 failed)
  - GREEN: same build -> EXIT=0; same ctest -> EXIT=0 (14 passed, 0 failed)
  - Gates: `bash tests/complexity-gate.sh --check` -> EXIT=0; `bash tests/file-length-gate.sh --check` -> EXIT=0

## Cluster 5736 — A refused save can rotate and discard revision history

- **Verdict:** FIXED
- **Evidence:** `src/core/ControlCommandsProjectFiles.cpp:148` rotated the revision chain
  before `saveProjectFile()` at `:155` decided whether the save was allowed.
  `rotateProjectRevision` (`src/core/ProjectRevisions.cpp:81-126`) shifts rev0 -> rev1 -> rev2
  and drops the oldest, and `ProjectRevisionPolicy::Keep` is 3 (`include/ProjectRevisions.h:56-57`),
  so a full set lost its oldest entry to a write that never happened. The engine's stated
  refusal precondition is `Song::isPartialLoad()` (`include/Song.h:142`, set at
  `src/core/Song.cpp:1916`), and `saveRefusal()` stays EMPTY until a save is attempted — so
  the gate is placed before the rotation and names the same fact the engine would, built from
  the public `Song::notLoadedSections()` (`include/Song.h:135`). The post-save failure path is
  unchanged and still quotes the engine's own wording.
- **Commit:** `26e80cf8c`
- **Test:** `PartialLoadTest::aRefusedProjectSaveDoesNotRotateTheRevisionSet`
- **Red/green:**
  - RED: `cmake --build build --target zene PartialLoadTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0
  - RED: `cd build/tests && ctest -R '^PartialLoadTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` -> EXIT=8
    (`aRefusedProjectSaveDoesNotRotateTheRevisionSet() Compared values are not the same`; 8 passed, 1 failed)
  - GREEN: same build -> EXIT=0; same ctest -> EXIT=0
  - Also green: `cd build/tests && ctest -R '^ControlSaveState$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` -> EXIT=0
  - Gates: complexity -> EXIT=0; file-length -> EXIT=0

## Cluster 3941 — Render errors are discarded

- **Verdict:** FIXED
- **Evidence:** `runCliRender` pointed the render child's stdout AND stderr at the null device
  (`src/core/ControlCommandsProject.cpp:301-302`) and `renderSession` answered a failure with
  nothing but `"the render failed (exit 1); the session is unchanged"` (`:359`). Probing the
  shipped CLI directly showed the child does say why — `Could not open file <path> for
  writing.` followed by its advice line — and both lines were thrown away. The child's stderr
  now goes to a `QTemporaryFile` read back only on failure and bounded to
  `RENDER_DIAGNOSTIC` (512) characters: the whole tail while it fits (cause and advice are
  separate lines), the last line alone when it does not. stdout stays discarded — it carries
  progress, not a reason. The refusal keeps its old wording when the child said nothing, and
  `tests/control-render-format-capability.py:120` keys on the unchanged `"the render failed"`
  prefix. The message-building was split into `renderFailureMessage` because
  `renderSession`'s CCN would otherwise have risen 11 -> 12 past the ratchet.
- **Commit:** `6bdda402e`
- **Test:** `ControlRenderPresets::check_failure_says_why` (renders into a directory that does
  not exist — a deterministic child failure in this build, no timeout involved)
- **Red/green:**
  - RED: `cmake --build build --target zene > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0
  - RED: `cd build/tests && ctest -R '^ControlRenderPresets$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` -> EXIT=8
    (`the refusal carries the renderer's own reason, not only an exit code FAILED` /
    `message='the render failed (exit 1); the session is unchanged'`)
  - GREEN: same build -> EXIT=0; same ctest -> EXIT=0
  - Also green: `cd build/tests && ctest -R '^(ControlRenderFormatCapability|ControlExportSettings|ControlStemExportVerb|ControlStemCommands|RenderSoftwareTag)$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` -> EXIT=0 (6 tests, 0 failed)
  - Gates: complexity -> EXIT=0; file-length -> EXIT=0

## Cluster 4823 — Invalid port direction loses its validation error

- **Verdict:** FALSE-POSITIVE
- **Evidence:** The smell is real — `writableModel` passes a local `ControlResult ignored;` to
  `matrixForDirection` (`src/core/ControlCommandsPorts.cpp:201-207`), so `*error` is left
  unset and the caller `handlePortSetPin` (`:250`) would return the default-`ok` `ControlResult`
  (`include/ControlRegistry.h:70-79`). It is UNREACHABLE: `port.set_pin` declares `direction`
  as `enumProperty({"in","out"})` (`ControlCommandsPorts.cpp:346-348`), and
  `ControlRegistry::invoke` runs `validateArgs` before the handler
  (`src/core/ControlRegistry.cpp:251-255`, `checkEnum` at `src/core/ControlSchema.cpp:102-110`),
  so a direction other than in/out is already refused as `InvalidArgs` and the handler never
  runs. No code was changed.
- **Commit:** none
- **Test:** none (unreachable; the path is additionally unreachable from a test — `writableModel`
  and `handlePortSetPin` are in an anonymous namespace, `Effect::audioPortsModel()` is virtual
  returning `nullptr` by default (`include/Effect.h:96`), and `AudioPortsModel` has no default
  constructor (`include/AudioPortsModel.h:193-194`))
- **Red/green:** none

## Cluster 4835 — WAV frame counts include chunks after the audio data

- **Verdict:** REAL-UNTESTED
- **Evidence:** Real. `wavFrameCount` computes `audioBytes = fileInfo.size() - info.dataOffset`
  (`src/core/ControlCommandsProject.cpp:186`), ignoring the `declaredDataSize` it just parsed
  from the RIFF `data` chunk header (`:166`, `WavInfo` at `:104-109`), so any chunk written
  AFTER `data` is counted as audio. The reference implementation for the same job,
  `BounceInPlace::wavFrameCount` (`src/core/BounceInPlace.cpp:421-428`), uses libsndfile's
  `info.frames`, which respects the data chunk. No product code was changed: `wavFrameCount` is
  file-local in an anonymous namespace with exactly one caller (`ControlCommandsProject.cpp:440`,
  inside `renderSession`), so a test can only reach it by rendering, and the only WAV it can
  ever see is the one the shipped CLI encoder produced. Probing that encoder's real output
  (`zene render tests/data/automation-audio-fixture.mmp -o out.wav`) shows its chunk layout is
  `fmt `, `LIST` (44 bytes, BEFORE `data`), `data`, with the file ending exactly at the end of
  `data` — so the observable output never has a trailing chunk and the defect cannot be
  provoked through the command. Testing it would require either exporting the helper (a new
  public seam) or a purpose-built renderer stub, i.e. a design change.
- **Commit:** none
- **Test:** none
- **Red/green:** none

## Cluster 5735 — Failed renders can leave a partial output file

- **Verdict:** REAL-UNTESTED
- **Evidence:** Real. `renderSession` hands the caller's own output path straight to the child
  (`ControlCommandsProject.cpp:412-417`), and on failure it removes only the temp PROJECT
  (`:419`) — the partially written WAV at `out` is left where the caller asked for it
  (`:422-427` reports the failure and stops). Both halves were confirmed by inspection and by
  driving the shipped CLI: rendering to a nonexistent directory and to a bad bit depth both
  fail with EXIT=1 and leave NO file, which is why the failure modes reachable in this
  environment cannot demonstrate the defect. A partial file needs the child to fail AFTER it
  has opened and begun writing `out` — a mid-write encoder or disk error, or the 600 s
  `waitForFinished` timeout firing mid-render (`ControlCommandsProject.cpp:311`) — none of
  which is reproducible deterministically headlessly. The fix the report proposes (render to a
  temporary path and publish on success) is a real behaviour change to the render contract
  with no test to hold it, so per procedure the code was left alone.
- **Commit:** none
- **Test:** none
- **Red/green:** none

## Summary

| Cluster | Verdict | Commit |
| --- | --- | --- |
| 2057 relink inverse | FIXED | `d4a154433` |
| 5736 refused save rotates revisions | FIXED | `26e80cf8c` |
| 3941 render errors discarded | FIXED | `6bdda402e` |
| 4823 port direction error dropped | FALSE-POSITIVE | none |
| 4835 WAV frame count trailing chunks | REAL-UNTESTED | none |
| 5735 failed render partial file | REAL-UNTESTED | none |

Leftovers: clusters 4835 and 5735 are confirmed real but carry NO fix, because neither has a
deterministic headless test and both would need a new seam (a public WAV-frame helper; a
temp-file render contract) to be pinned. Cluster 4835 in particular is a one-line bound by
`declaredDataSize` that a follow-up could land together with the seam that makes it testable.
Nothing else is half-done; the tree was clean at the end of the batch.