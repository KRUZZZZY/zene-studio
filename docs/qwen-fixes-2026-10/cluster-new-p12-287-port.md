ALREADY-FIXED

# Cluster new-p12-287-port - a track with a missing or invalid type is loaded as an instrument track

**Verdict: ALREADY-FIXED for the finding as reported, and this port COMPLETES that
fix at the cause.**

Two things are true at once, and the report keeps them apart:

1. **The reported defect was already fixed on the newest line**, by a different
   cluster: `edc9fac6f` ("fix: reject malformed track types (5414 new-p16-248)")
   reads the track's `type` attribute with a CHECKED conversion, so a `<track>` with
   no `type` (or with `type="bad"`) is refused instead of being fabricated into an
   Instrument track - `0` IS `Track::Type::Instrument`. Proven RED/GREEN below.
2. **The port adds the missing half of the SAME rule at the cause.** The rule that
   makes an element a track is TWO tests - the element must be a `<track>` element,
   and its `type` must be one this build can name. The type half was in the factory;
   the NAME half lived only in the LOADER's claim helper, so every other caller of
   the factory fabricated an instrument track out of an element that is not a track
   at all. This commit puts the name rule in the factory too, next to the type rule
   - the ONE place an element is turned into a track - so every caller gets the same
   answer. It is proved RED/GREEN below, with the product change reverted.

## Evidence that the reported defect is already fixed on this branch

- `src/core/Track.cpp:555-580` - `Track::create( const QDomElement &, TrackContainer * )`
  reads the `type` attribute with a CHECKED conversion and refuses an element that
  carries no readable type:

  ```cpp
  bool typeOk = false;
  const int typeValue = element.attribute( "type" ).toInt( &typeOk );
  if( !typeOk || typeValue < 0 || typeValue >= static_cast<int>( Type::Count ) )
  {
      return nullptr;
  }
  ```

  A MISSING attribute and a non-numeric value both make `toInt(&typeOk)` fail
  (`typeOk == false`), so the element is refused; and `0` IS `Track::Type::Instrument`
  (`include/Track.h:85-113`: Instrument=0, Pattern=1, Sample=2, Event=3, Video=4,
  Automation=5, HiddenAutomation=6, Folder=7, Count=8), so the UNCHECKED read that
  `edc9fac6f` replaced fabricated a real instrument track, with its own children read
  as clips.
- `src/core/TrackContainer.cpp:85-93` - the claim helper `claimTrackOrPreserveChild()`
  claims a child ONLY when it is named `<track>` AND `Track::create()` constructed one
  from it; a `nullptr` answer falls through to `captureUnclaimed( element,
  element.nodeName(), into )`, so the element is kept VERBATIM and re-emitted on save
  (SPEC-ARCH-4 1.6.2). It is called from the one place the container reads its
  children: `TrackContainer::loadSettings()` (`src/core/TrackContainer.cpp:240`).

## What this commit changes (the product change)

`src/core/Track.cpp:555-580` - `Track::create( const QDomElement &, TrackContainer * )`
now applies BOTH halves of the rule, at the ONE place an element becomes a track:

```cpp
if( element.nodeName() != QStringLiteral( "track" ) )
{
    return nullptr;
}

bool typeOk = false;
const int typeValue = element.attribute( "type" ).toInt( &typeOk );
if( !typeOk || typeValue < 0 || typeValue >= static_cast<int>( Type::Count ) )
{
    return nullptr;
}

Engine::audioEngine()->requestChangeInModel();
...
```

**Why the name rule belongs here.** The loader carried it in its own claim helper
(`src/core/TrackContainer.cpp:87`), and every OTHER caller hands the factory whatever
element it has:

| caller | the element it hands over | is it a `<track>`? |
|---|---|---|
| `TrackContainer::loadSettings` (`src/core/TrackContainer.cpp:89`) | a child of `<trackcontainer>` the claim helper already named | yes |
| `Track::clone()` (`src/core/Track.cpp:594`) | `saveState()`'s element - `Track::saveTrack` renames it to `"track"` (`src/core/Track.cpp:582`) | yes |
| `control::restoreTrackFromXml` (`src/core/ControlEditSupport.cpp:481`) | `captureTrackXml`'s element - renamed to `"track"` the same way | yes |
| `ControlAutomationSupport.cpp:447` | `saveState()`'s element | yes |
| `ControlTrackCreation.cpp:145` | `saveState()`'s element | yes |
| `ControlProjectTransplantChannels.cpp:194` | a `<track>` child of the transplanted document | yes |
| `PatternEditor::dropEvent` (`src/gui/editors/PatternEditor.cpp:177`) | the drag payload's first child | yes |
| `TrackContainerView::dropEvent` (`src/gui/editors/TrackContainerView.cpp:623`) | the drag payload's first child | yes |

Every caller passes a `<track>`, so the rule costs them nothing - and it now protects
the paths that did NOT have it: an element that is not a `<track>` but carries a
`type` was fabricated into an Instrument track (again `0` is Instrument), which
violated a contract this codebase already states in writing:

> `include/ControlEdit.h:167-168` - "Recreates a track from XML captured by
> captureTrackXml, through the same Track::create(element, container) the project
> loader uses. **nullptr when the XML does not parse or names no track element.**"

The promise was not kept: `restoreTrackFromXml()` handed the factory a snapshot
element that names no track element, and the factory built an instrument track out of
it. The name rule is where that promise is kept.

The other two edits in this commit:

- `src/core/TrackContainer.cpp` - comment only: the claim helper's note and
  `loadSettings()`'s note now say the `type` conversion in `Track::create()` is
  CHECKED (the old text stated the opposite of what the code does since
  `edc9fac6f`).
- `tests/upstream-modifications.txt` - the registration rows for the two changed
  inherited files.
- `tests/all-sources.txt` - the train-level scope-manifest repair (below).
- `docs/qwen-fixes-2026-10/cluster-new-p12-287-port.md` - this report.

## The regressions

All three live in `tests/src/core/UnclaimedTrackTypeTest.cpp` (the test file the
completion check runs):

| slot | line | what it pins |
|---|---|---|
| `malformedTrackTypesArePreservedNotCoerced` | 279 | the branch's own slot (added upstream by `edc9fac6f`): a missing and a nonnumeric `type` are preserved and REPORTED |
| `aTrackWithAMissingOrMalformedTypeIsNotFabricatedIntoAnInstrumentTrack` | 325 | the port of the ORIGINAL commit's case (`2e42fdba6`): the two unreadable rows sit BETWEEN two readable ones; the container holds exactly the 2 readable rows, no track is named `no-type`/`bad-type`, both rows are KEPT and named by the load report (`/song/trackcontainer/track[2]`, `track[3]`), and a load -> save round trip keeps both rows with the attributes they were read with |
| `aTrackIsNotFabricatedFromAnElementThatIsNotATrack` | 401 | THIS commit's rule: a snapshot whose element is not a `<track>` produces NO track - the contract of `include/ControlEdit.h:168` |

Every one of them is deterministic: a literal fixture written to a `QTemporaryDir`
(or a literal XML string), the song loaded from it, and every assertion read from the
loaded model or from the saved file. No clock, no sleep, no thread, no injected time.

## RED / GREEN proof

### This commit's product change (the name rule) - the proof the completion check runs

The product diff is `src/core/Track.cpp` alone; the test file is NOT reverted.

| step | command | exit |
|------|---------|------|
| GREEN (product change in place) | `cmake --build build --target zene UnclaimedTrackTypeTest -j 8` then `./UnclaimedTrackTypeTest` from `build/tests` | build **0**, test **0** (`Totals: 9 passed, 0 failed`) |
| record the product diff | `git diff -- src/core/Track.cpp > <logs>/fix-product.patch` | **0** |
| revert it | `git apply -R <logs>/fix-product.patch` | **0** |
| rebuild (RED) | `cmake --build build --target zene UnclaimedTrackTypeTest -j 8` | **0** |
| RED | `./UnclaimedTrackTypeTest` from `build/tests` | **1** - `FAIL! : UnclaimedTrackTypeTest::aTrackIsNotFabricatedFromAnElementThatIsNotATrack() 'control::restoreTrackFromXml( snapshot, song ) == nullptr' returned FALSE. (an element that is not a <track> was fabricated into an instrument track)`; `Totals: 8 passed, 1 failed` |
| restore it | `git apply <logs>/fix-product.patch` | **0** |
| rebuild (GREEN) | `cmake --build build --target zene UnclaimedTrackTypeTest -j 8` | **0** |
| GREEN | `./UnclaimedTrackTypeTest` from `build/tests` | **0** (`Totals: 9 passed, 0 failed`) |

The RED failure is the trap itself: with the name rule reverted, an element that is
not a `<track>` becomes a REAL instrument track in the song.

### The reported defect (the type rule) - the branch's own fix, reverted

`edc9fac6f`'s check is committed on the branch, so the RED proof reverts THAT hunk.

| step | command | exit |
|------|---------|------|
| RED (type check reverted to `static_cast<Type>( element.attribute( "type" ).toInt() )`) | `./UnclaimedTrackTypeTest aTrackWithAMissingOrMalformedTypeIsNotFabricatedIntoAnInstrumentTrack` | **1** - `Actual (trackCount( song )): 4 / Expected (2) : 2` |
| RED (the branch's own slot) | `./UnclaimedTrackTypeTest malformedTrackTypesArePreservedNotCoerced` | **1** - `Actual (trackCount( song )): 3 / Expected (1) : 1` |
| restore the fix | `git apply -R <logs>/fix.patch` | **0** |
| GREEN | the same two commands | **0** / **0** |

Both RED numbers are the finding's own symptom: the unreadable rows FABRICATED as
instrument tracks (4 tracks where the fixture holds 2, and 3 where it holds 1).

### The affected family and the gates

| step | command | exit |
|------|---------|------|
| family (build) | `cmake --build build --target ImportTrackIoTest ProjectToolsTest ArrangeToolsTest UnclaimedElementsTest TrackFolderTest LegacyPatternUpgradeTest TrackViewDeferredLifetimeTest InputMonitorTest LanesUpconvertTest -j 8` | **0** |
| family (run) | `ctest -R '^(UnclaimedTrackTypeTest\|UnclaimedElementsTest\|TrackFolderTest\|LegacyPatternUpgradeTest\|TrackViewDeferredLifetimeTest\|InputMonitorTest\|LanesUpconvertTest\|ImportTrackIoTest\|ProjectToolsTest\|ArrangeToolsTest)$'` from `build/tests` | **0** (10/10 tests) |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** (all eight gates PASS) |
| completion | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p12-287-port` | **0** (`RESULT: PASS - submit`) |

`ImportTrackIoTest` and `ProjectToolsTest` are in that family on purpose: they are the
track-transplant and copy/paste paths, the ones that hand the factory a transplanted
element.

## No collateral damage

- The name rule refuses nothing any caller sends: every caller of the element-based
  factory hands it a `<track>` element (the table above), so the loader, the capture
  and restore path, the clone path, the drag-and-drop path and the transplant path
  all behave exactly as before. The family run above includes the transplant and
  paste tests for that reason.
- The type rule is unchanged in effect: every type this build has a class for still
  builds, including the folder track (7 - `Folder` sits immediately before `Count`
  in `include/Track.h:95-113` precisely so the persisted number never moves); `Event`
  (3) and `Video` (4) have no class and still answer `nullptr` through the factory's
  `default: break` arm, as before; and a negative or out-of-range number was already
  refused by that arm.
- The check now runs BEFORE `Engine::audioEngine()->requestChangeInModel()`, so a
  refused element does not take the engine's change-in-model guard - the structure
  the original commit `2e42fdba6` used. The guard is still taken around every
  construction that really happens, and every construction still releases it.
- No tracked file is added or removed, no project file changes its bytes, and no
  old project loads differently: a project this build wrote has a `type` on every
  `<track>`, and a project a newer writer wrote keeps its refused rows verbatim, as
  before.
- Nothing on the audio thread changed; no control command was added; undo is
  untouched (the capture/restore path is covered by `ArrangeToolsTest` and
  `ImportTrackIoTest` in the family run).

## The train-level scope-manifest repair (why `tests/all-sources.txt` is in this commit)

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

## Self-check

- `feature_check.py`: **exit 0** (`gate-feature-check-6.log`): `fork-sources` PASS,
  `all-sources-reproduce` PASS, `no-upstream-regression` PASS, `check-namespace`
  PASS, `no-tautology` PASS, `doc-figures` PASS, `complexity` "no new item in your
  files", `file-length` "no new item in your files", `RESULT: PASS`. Before the
  manifest re-derivation the same command was **exit 1** with `fork-sources` and
  `all-sources-reproduce` FAIL (`gate-feature-check-1.log`), both red at the branch
  base.
- `submit_check.py`: **exit 0** (`submit-3.log` and `submit-4.log`): "RESULT: PASS -
  submit" - commits on the branch PASS, working tree clean PASS, report PASS, tests
  added or changed PASS (`tests/src/core/UnclaimedTrackTypeTest.cpp`), gates PASS.
  Its five ASK items are answered here, one by one.

### The ASK items, answered with evidence

1. **"The regression test FAILS with your product change reverted and PASSES with it
   (exit codes)."** The product change of this commit is `src/core/Track.cpp`. With it
   reverted (`git apply -R <logs>/fix-product.patch`, rebuild), the test binary exits
   **1** - `aTrackIsNotFabricatedFromAnElementThatIsNotATrack` fails with
   "'control::restoreTrackFromXml( snapshot, song ) == nullptr' returned FALSE. (an
   element that is not a <track> was fabricated into an instrument track)",
   `Totals: 8 passed, 1 failed`. With it restored, the same binary exits **0**
   (`Totals: 9 passed, 0 failed`). The two slots that pin the reported defect were
   proved the same way against the branch's own type check: exit **1** with
   `4 != 2` and `3 != 1` tracks, exit **0** with it in place.

2. **"The test is deterministic: no wall-clock timing, sleeps or races (name the
   seam or injected values it uses)."** The seam is the FILE and the MODEL. The two
   loader slots write a project file with `writeText()` into a `QTemporaryDir`, load
   it with the file-local `loadFresh()` helper (`song->clearProject()`,
   `song->setLoadOnLaunch(false)`, `song->loadProject(project)`) and assert on the
   loaded model (`song->tracks()`, `song->unclaimedChildren()`,
   `song->unclaimedElements()`) or on the saved file read back
   (`song->saveProjectFile()` -> `parse()` -> `childWith()`). The third slot hands
   `control::restoreTrackFromXml()` a LITERAL XML string and asserts on the returned
   pointer and on `song->tracks()`. No clock, no sleep, no thread, no injected time
   value, and no assertion on a timestamp a save stamps.

3. **"The fix is at the cause and complete: every caller and sibling of what you
   changed was checked and stays consistent."** The cause is the ONE place an element
   becomes a track: `Track::create(const QDomElement&, TrackContainer*)`
   (`src/core/Track.cpp:555`). Every caller of it was enumerated and checked - the
   table above lists all eight, with the element each one hands over - and each
   passes a `<track>` element, so the name rule is consistent with all of them. The
   SIBLING of that read, `Track::loadTrack()`'s type comparison
   (`src/core/Track.cpp:783`), only WARNS when the file's type differs from the
   track's own type, which is a different question and was left alone. The other
   `type` readers in the tree (`DataFile.cpp:2418`, `Song.cpp:2034`,
   `UpgradeExtendedNoteRange.cpp:208,218,259`) classify elements they have already
   accepted and were left alone. The loader's claim helper
   (`src/core/TrackContainer.cpp:85-93`) keeps its own name test - it is now the
   second place the same rule is stated, and its comment says so.

4. **"No collateral damage: old projects and saved data still load as before, and
   any limit or check you added does not hit other callers that legitimately exceed
   it."** The check added is a NAME test, and every caller of the factory passes a
   `<track>` element (the table above), so no caller legitimately exceeds it. The
   type rule is unchanged in effect (the folder track's persisted number 7 still
   builds; the two types with no class still answer `nullptr`). A project this build
   wrote has a `type` on every `<track>`, and a project a newer writer wrote keeps
   its refused rows verbatim, exactly as before. The affected family (10 tests,
   including the transplant and paste paths) passes.

5. **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or
   the intended contract that shows it."** The verdict is ALREADY-FIXED for the
   finding as reported, and the code that shows it is `src/core/Track.cpp:555-580` -
   the factory reads `type` with a CHECKED `QVariant::toInt(&typeOk)` and refuses an
   element whose `type` is missing or does not convert (`typeOk == false`), and
   refuses a value outside `[0, Type::Count)`. `TrackContainer::claimTrackOrPreserveChild()`
   (`src/core/TrackContainer.cpp:85-93`) therefore keeps such an element VERBATIM
   (`captureUnclaimed()`) instead of modelling a track nobody wrote - the intended
   contract of SPEC-ARCH-4 1.6.2, "an element the loader does not claim is kept, not
   coerced". The commit that brought that here is `edc9fac6f` (cluster 5414 /
   new-p16-248). This port's own contribution is the NAME half of the same rule,
   which `include/ControlEdit.h:168` already promised ("nullptr when the XML does
   not parse or names no track element") and which the factory did not keep.

### Files committed

- `src/core/Track.cpp` - the product change: the factory refuses an element that is
  not a `<track>`, and checks the type before it touches the engine.
- `src/core/TrackContainer.cpp` - comment only: the two notes that described the old
  (unchecked) behaviour.
- `tests/src/core/UnclaimedTrackTypeTest.cpp` - the ported regression
  (`aTrackWithAMissingOrMalformedTypeIsNotFabricatedIntoAnInstrumentTrack`) and this
  commit's own (`aTrackIsNotFabricatedFromAnElementThatIsNotATrack`), plus the
  `#include "ControlEdit.h"` they need.
- `tests/upstream-modifications.txt` - the registration rows for the two changed
  inherited files.
- `tests/all-sources.txt` - the train-level scope-manifest re-derivation.
- `docs/qwen-fixes-2026-10/cluster-new-p12-287-port.md` - this report.
