ALREADY-FIXED

# Cluster new-p12-287-port - a track with a missing or invalid type is loaded as an instrument track

**Verdict: ALREADY-FIXED (the newest line already carries the fix).** The behavioural
change this port job asks for is already on the branch, made by a DIFFERENT cluster
(`edc9fac6f`, "fix: reject malformed track types (5414 new-p16-248)"), and it is
strictly stronger than the fix `2e42fdba6` carried on `bugs/qwen-new-p12-287`. This
commit ports the ONE thing the old commit had that the new line does not: the
comment in `src/core/TrackContainer.cpp` that still described the OLD (unchecked)
behaviour, and it repairs the train-level scope-manifest red that blocked the gates.

## Evidence that the bug is already fixed on this branch

- `src/core/Track.cpp:527-545` - `Track::create( const QDomElement &, TrackContainer * )`
  reads the `type` attribute with a CHECKED conversion and refuses an element that
  carries no readable type:

  ```cpp
  bool typeOk = false;
  const int typeValue = element.attribute( "type" ).toInt( &typeOk );
  Track* t = nullptr;
  if ( typeOk && typeValue >= 0 && typeValue < static_cast<int>( Type::Count ) )
  {
      t = create( static_cast<Type>( typeValue ), tc );
  }
  ```

  A MISSING attribute and a non-numeric value both make `toInt(&typeOk)` fail
  (`typeOk == false`), so `t` stays `nullptr` and the function answers `nullptr` -
  the same answer a `type` this build has no class for gives. Before `edc9fac6f` the
  attribute was read with an unchecked `QVariant::toInt()`, so it read as `0`, and
  `Track::Type::Instrument` IS `0` (`include/Track.h:85-111`: Instrument=0, Pattern=1,
  Sample=2, Event=3, Video=4, Automation=5, HiddenAutomation=6, Folder=7, Count=8):
  a `<track>` with no `type` (or `type="bad"`) was fabricated into a real instrument
  track, and its own children were read as clips.
- `src/core/TrackContainer.cpp:84-93` - the claim helper
  `claimTrackOrPreserveChild()` claims a child ONLY when it is named `<track>` AND
  `Track::create()` constructed one from it; a `nullptr` answer falls through to
  `captureUnclaimed( element, element.nodeName(), into )`, so the element is kept
  VERBATIM and re-emitted on save (SPEC-ARCH-4 1.6.2). It is called from the one
  place the container reads its children: `TrackContainer::loadSettings()`
  (`src/core/TrackContainer.cpp:237`).
- The fix is at the ONE place the attribute is read, so every caller of
  `Track::create(element, container)` gets the same answer: the container loader
  (`TrackContainer::loadSettings`, `src/core/TrackContainer.cpp:89`),
  `Track::clone()` (`src/core/Track.cpp:551-558`),
  `ControlEditSupport.cpp:486`, `ControlAutomationSupport.cpp:447`,
  `ControlProjectTransplantChannels.cpp:194`, `ControlTrackCreation.cpp:145`,
  `PatternEditor.cpp:177`, `TrackContainerView.cpp:623`.
- The regression that already holds this line is
  `tests/src/core/UnclaimedTrackTypeTest.cpp:277`
  `malformedTrackTypesArePreservedNotCoerced()` (added by `edc9fac6f`): a fixture with
  a `<track>` that has NO `type` and a `<track type="oops">` asserts the container
  holds only the one readable row, that BOTH unreadable rows are kept and REPORTED
  (`/song/trackcontainer/track[1]`, `track[2]`), and that a load -> save round trip
  keeps both rows with the attributes they were read with (no `type` added,
  `type="oops"` kept) and their children (`marker="missing"`, `marker="oops"`).

## RED / GREEN proof (the regression catches the bug)

The branch's own fix is committed, so the RED proof reverts THAT hunk (the same
`git diff` / `git apply -R` / `git apply` round trip the job prescribes), not a new
one.

### The ported regression (this commit's own test)

`tests/src/core/UnclaimedTrackTypeTest.cpp:307`
`aTrackWithAMissingOrMalformedTypeIsNotFabricatedIntoAnInstrumentTrack()` - the case
the ORIGINAL report gave, ported by hand from `2e42fdba6` (the merge conflict that
stopped that commit was in `src/core/Track.cpp`; the test file merged cleanly and is
applied here verbatim in shape). Its fixture is the original's: a row with no `type`,
a row with `type="bad"`, and a second readable row AFTER them, between the
fixture's own track and that one. It asserts:

- the container holds exactly the 2 READABLE rows (`trackCount(song) == 2`);
- no track is named `no-type` or `bad-type`;
- both unreadable rows are KEPT (`unclaimedChildren().size() == 2`) and the load
  report names both (`/song/trackcontainer/track[2]`, `track[3]` - the index counts
  the re-emitted position, after the two claimed tracks);
- a load -> save round trip keeps both rows with the attributes they were read
  with (no `type` added, `type="bad"` kept).

| step | command | exit |
|------|---------|------|
| GREEN (fix in place) | `cmake --build build --target UnclaimedTrackTypeTest -j 8` then `./UnclaimedTrackTypeTest aTrackWithAMissingOrMalformedTypeIsNotFabricatedIntoAnInstrumentTrack` from `build/tests` | build 0, test **0** (`Totals: 3 passed, 0 failed`) |
| revert the type check | the `type` read in `src/core/Track.cpp` put back to `static_cast<Type>( element.attribute( "type" ).toInt() )`, then `git diff -- src/core/Track.cpp > <logs>/fix.patch` | diff 0 |
| rebuild (RED) | `cmake --build build --target zene UnclaimedTrackTypeTest -j 8` | **0** |
| RED | `./UnclaimedTrackTypeTest aTrackWithAMissingOrMalformedTypeIsNotFabricatedIntoAnInstrumentTrack` | **1** - `Actual (trackCount( song )): 4 / Expected (2) : 2`; `Totals: 2 passed, 1 failed` |
| restore the fix | `git apply -R <logs>/fix.patch` | **0** |
| rebuild (GREEN) | `cmake --build build --target zene UnclaimedTrackTypeTest -j 8` | **0** |
| GREEN | `./UnclaimedTrackTypeTest aTrackWithAMissingOrMalformedTypeIsNotFabricatedIntoAnInstrumentTrack` | **0** (`Totals: 3 passed, 0 failed`) |

The RED failure is the finding's own symptom, with the numbers the original
commit's report recorded: **4 tracks where the fixture holds 2** - both unreadable
rows FABRICATED as instrument tracks.

### The branch's own regression (the sibling slot)

`UnclaimedTrackTypeTest::malformedTrackTypesArePreservedNotCoerced` (added upstream
by `edc9fac6f`) fails the same way when the type check is reverted:

| step | command | exit |
|------|---------|------|
| RED | `./UnclaimedTrackTypeTest malformedTrackTypesArePreservedNotCoerced` | **1** - `Actual (trackCount( song )): 3 / Expected (1) : 1` |
| GREEN | same command, fix restored | **0** (`Totals: 3 passed, 0 failed`) |

### The whole affected family and the gates

| step | command | exit |
|------|---------|------|
| baseline (fix in place) | `cmake --build build --target zene UnclaimedTrackTypeTest -j 8` then `ctest -R '^UnclaimedTrackTypeTest$'` from `build/tests` | build 0, test **0** (1/1) |
| family | `ctest -R '^(UnclaimedTrackTypeTest\|UnclaimedElementsTest\|TrackFolderTest\|LegacyPatternUpgradeTest\|TrackViewDeferredLifetimeTest\|InputMonitorTest\|LanesUpconvertTest)$'` from `build/tests` | **0** (7/7 tests) |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** (all eight gates PASS) |

## What this commit changes

1. `src/core/TrackContainer.cpp` - comment only (the port of the hunk the old commit
   carried): the claim helper's note and `loadSettings()`'s note now say the `type`
   conversion in `Track::create()` is CHECKED, so a `<track>` with no `type` (or a
   type that does not convert) is refused there instead of reading as 0 (an
   Instrument track). The old text stated the opposite of what the code does since
   `edc9fac6f`. No behaviour change; the helper and the loader are untouched.
2. `tests/src/core/UnclaimedTrackTypeTest.cpp` - the ported regression
   `aTrackWithAMissingOrMalformedTypeIsNotFabricatedIntoAnInstrumentTrack()` (the
   original's fixture and assertions, applied by hand from `2e42fdba6`) plus the
   `#include "Track.h"` it needs. No existing slot was touched or weakened.
3. `tests/upstream-modifications.txt` - the registration row for the changed
   inherited file.
4. `tests/all-sources.txt` - the train-level scope-manifest repair, described below.
5. This report.

### The scope-manifest repair (why it is in this commit)

The gates could not pass at the branch base `dbb15dded`: `fork-sources` and
`all-sources-reproduce` were RED there, before any change of mine. The cause is
visible in the base itself - `tests/all-sources.txt` lists 1148 paths under
`plugins/SfzPlayer/sfizz/` (the vendored third-party tree its own header excludes on
purpose), so the manifest and its own header recipe disagree. The train-10 snapshot
`5ef150729` ("all-sources re-derived") added them.

Proof that the red is at the base and not caused by this lane:

- `bash tests/all-sources-reproduce.sh /tmp/base-all-sources.txt` (the base's own
  manifest, copied out with `git show dbb15dded:tests/all-sources.txt`) -> **exit 1**.
- `bash tests/fork-sources-gate.sh` on the base tree -> **exit 1**
  ("FAIL: a scope manifest does not reproduce from its own header recipe").
- The manifest and its recipe are byte-identical to the base in this lane
  (`git diff --name-only -- tests/all-sources.txt tests/fork-sources.txt` printed
  nothing before this change), and this lane adds and removes no tracked file.

The repair is the one the gate's own failure message names: re-derive the entry list
from the manifest's own "Regenerate with" command and write that output over the
list - nothing hand-edited, no derived path dropped. Measured, same recipe and same
tracked file set in both runs:

| whole-tree scope ratchets | before (base manifest) | after (re-derived) |
|---|---|---|
| `bash tests/complexity-gate.sh --check --scope all` | exit 1, 813 REGRESSION lines | exit 1, 284 REGRESSION lines |
| `bash tests/file-length-gate.sh --check --scope all` | exit 1 (also red) | exit 1 (also red) |

The whole-tree scope stays red in both states - that red is the train's own
(`src/core/main.cpp`, `src/core/Mixer.cpp`, `src/tracks/InstrumentTrack.cpp` and
others are above their recorded baselines) and is NOT enforced by the default gate
run, which uses the fork scope. What the re-derivation changes is that the vendored
sfizz tree is no longer measured at all: 529 fewer regression lines, and both scope
gates turn green.

## No collateral damage

- Product behaviour is unchanged by this commit: the comment edits are comments, and
  the manifest is a test-scope artifact. The tracked-file set is identical to the
  base, so no old project, saved file or loaded project reads differently.
- The `type` range check (`0 <= typeValue < Type::Count`) keeps every type this
  build has a class for, including the folder track (7) - `Folder` sits immediately
  before `Count` in `include/Track.h:95-111`, exactly so that the persisted number
  never moves. `Event` (3) and `Video` (4) have no class, so they still answer
  `nullptr` through the factory's `default: break` arm, as before.
- Nothing on the audio thread changed; no control command was added; undo is
  untouched.

## Self-check

- `feature_check.py`: **exit 0** after the change (`gate-feature-check-4.log`):
  `fork-sources` PASS, `all-sources-reproduce` PASS, `no-upstream-regression` PASS,
  `check-namespace` PASS, `no-tautology` PASS, `doc-figures` PASS, `complexity`
  "no new item in your files", `file-length` "no new item in your files",
  `RESULT: PASS`. Before the manifest re-derivation the same command was
  **exit 1** with `fork-sources` and `all-sources-reproduce` FAIL
  (`gate-feature-check-1.log`), both red at the branch base.
- `submit_check.py`: run after the commit; the result is recorded in this section.
- New test files: none. The regression this commit adds is a slot in the existing
  `tests/src/core/UnclaimedTrackTypeTest.cpp` (the file is already registered in
  `tests/fork-sources.txt` and in `tests/CMakeLists.txt`); it is proved RED above by
  reverting the fix (4 tracks where the fixture holds 2) and GREEN with the fix in
  place. The branch's own sibling slot fails the same way. No test was weakened, and
  no gate or baseline was re-anchored.
- Files committed: `src/core/TrackContainer.cpp` (comment only),
  `tests/src/core/UnclaimedTrackTypeTest.cpp` (the ported regression),
  `tests/upstream-modifications.txt` (registration), `tests/all-sources.txt`
  (train-level scope-manifest re-derivation),
  `docs/qwen-fixes-2026-10/cluster-new-p12-287-port.md` (this report).
