FIXED

# cluster new-p14-204 - project-loaded note volume and panning bypass their normal bounds

## Verdict: REAL, fixed

`Note::loadSettings()` assigned the two parsed attributes straight into the members:

```
src/core/Note.cpp:648  m_volume = _this.attribute( "vol" ).toInt();
src/core/Note.cpp:649  m_panning = _this.attribute( "pan" ).toInt();
```
(as the branch stood before this commit; `loadSettings` now begins at `src/core/Note.cpp:666`)

while every other route into the note bounds them first:

- constructor, `src/core/Note.cpp:195-196`: `m_volume( std::clamp(volume, MinVolume, MaxVolume) )`, `m_panning( std::clamp(panning, PanningLeft, PanningRight) )`
- `Note::setVolume`, `src/core/Note.cpp:387` and `Note::setPanning`, `src/core/Note.cpp:414`: `std::clamp` to the same bounds

The two members are narrower than `int`, which makes this worse than "unclamped":

- `include/LmmsTypes.h:36` `using volume_t = std::uint8_t;`
- `include/LmmsTypes.h:37` `using panning_t = std::int8_t;`
- `include/volume.h:36` `MaxVolume = 200`, `:37` `DefaultVolume = 100`
- `include/panning.h:38-41` `PanningRight = 100`, `PanningLeft = -100`, `DefaultPanning = 0`

so the value was NARROWED before it was ever bounded: `<note vol="300">` loaded as volume 44,
`<note vol="-1">` as 255 (past `MaxVolume`), `<note pan="200">` as -56. Out of range is audible, not
cosmetic: `panningToVolumeVector()` (`include/panning.h:43`) returns a NEGATIVE gain once
`|panning| > PanningRight` (inverted phase on one side), `panningToMidi()` (`include/panning.h:53`)
leaves `MidiMinPanning..MidiMaxPanning`, and a plugin that scales by `getVolume() / DefaultVolume`
(plugins/Organic/Organic.cpp:556-600, Lb302, ...) goes past unity. `Note::midiVelocity()`
(`include/Note.h:376`) still clamps at `MidiMaxVelocity`, but it is the only consumer that does.

Second half of the same line: `QString::toInt()` reports a failed conversion by yielding 0, so an
absent or unreadable `vol` loaded as volume 0 - a silent note - and an unreadable `pan` as centre.
The file's own rule for such attributes already exists (`loadMidiDepthValue`, `src/core/Note.cpp:100`,
"treated as absent rather than as zero"): the documented default is the neutral value.

## Fix (product code)

- `src/core/Note.cpp:52-71`: new file-local helper `noteRangeValue()` - `toInt( &ok )`, the documented
  fallback when the attribute is absent or unparseable, and `std::clamp` AS AN INT, before the
  narrowing cast.
- `src/core/Note.cpp:670-676`: `loadSettings()` now goes through the clamping setters, so the two
  members cannot hold a value no setter would accept:
  `setVolume( static_cast<volume_t>( noteRangeValue( _this, "vol", DefaultVolume, MinVolume, MaxVolume ) ) )`
  and `setPanning( static_cast<panning_t>( noteRangeValue( _this, "pan", DefaultPanning, PanningLeft, PanningRight ) ) )`.
- No header changed (`Note.h` untouched), so no rebuild of headers was needed; `zene` and the test
  binaries were rebuilt anyway (log below).
- No control command added; no undo change; `loadSettings()` runs on the editing thread only when a
  clip is read, and the helper is a stack-only parse (no allocation, lock, I/O or log).

## Regression test

`tests/src/core/MidiProbabilityPersistenceTest.cpp:549` `projectLoadedNoteVolumeAndPanningStayInBounds()`
- the existing home of the `Note::loadSettings()` robustness tests (cases (e), (f), (g) at
  `:383`, `:442`, `:485` are the earlier malformed-value regressions). It uses the file's existing
  `noteElementWith()` helper (`:144`) so the attribute text reaches `loadSettings()` exactly as a
  hand-edited project spells it. No new test file, so no registration change: the file is already in
  `tests/fork-sources.txt:2885` and `tests/CMakeLists.txt:604`.

Matrix it covers:

- normal path: `vol=0/100/200`, `pan=-100/0/100` load unchanged; a note the engine set to vol 150 /
  pan -60 round trips through its own saved element.
- failure path: `vol=201/255/300/2147483647` clamp to 200, `vol=-1/-300` to 0,
  `pan=101/127/200/2147483647` clamp to 100, `pan=-101/-128` to -100 - and the value that reaches
  `midiVelocity(63)` and `panningToVolumeVector()` is in range (0..127, gains 0..1, never negative).
- malformed / absent: `vol=""`, `"bad"`, `"1.5"` and no attribute at all fall back to
  `DefaultVolume`/`DefaultPanning` instead of 0.
- exhaustion and reconfiguration paths: not applicable - none exists. A load is one pass over one
  element with no queue, budget or reused buffer to exhaust or reconfigure.
- audio-thread constraints: the code is not on the audio path; nothing is allocated, freed, locked,
  waited on or logged.
- platform: plain `QDomElement`/`QString`/`QtTest` API that Qt 5.15, MSVC and macOS have; no
  POSIX-only path, no Qt-6-only call.

## Commands and EXIT codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p14-204.r1/logs)

RED (test written first, product code still buggy):

```
cmake --build build --target MidiProbabilityPersistenceTest -j4 > .../logs/build-1-red.log 2>&1      EXIT=0
cd build/tests && ctest -R MidiProbabilityPersistenceTest --output-on-failure > .../logs/test-1-red.log 2>&1
                                                                                                     EXIT=8
   FAIL!  : projectLoadedNoteVolumeAndPanningStayInBounds() 'static_cast<int>( loaded.getVolume() ) == row.second' returned FALSE. (300)
```

GREEN (fix applied):

```
cmake --build build --target MidiProbabilityPersistenceTest -j4 > .../logs/build-2-green.log 2>&1    EXIT=0
cd build/tests && ctest -R MidiProbabilityPersistenceTest --output-on-failure > .../logs/test-2-green.log 2>&1
                                                                                                     EXIT=0
```

Step 4b - the test catches the bug (product hunk reverted, test kept):

```
git diff -- src/core/Note.cpp > .../logs/fix.patch                                     EXIT=0
git apply -R .../logs/fix.patch                                                        EXIT=0
cmake --build build --target MidiProbabilityPersistenceTest -j4 > .../logs/build-4-reverted.log 2>&1  EXIT=0
cd build/tests && ctest -R MidiProbabilityPersistenceTest --output-on-failure > .../logs/test-4-red-proof.log 2>&1
                                                                                       EXIT=8
   FAIL!  : projectLoadedNoteVolumeAndPanningStayInBounds() (300)
git apply .../logs/fix.patch                                                           EXIT=0
cmake --build build --target MidiProbabilityPersistenceTest -j4 > .../logs/build-5-reapplied.log 2>&1 EXIT=0
cd build/tests && ctest -R MidiProbabilityPersistenceTest --output-on-failure > .../logs/test-5-green-proof.log 2>&1
                                                                                       EXIT=0
```

Family (same rebuild, `zene` included):

```
cmake --build build -j4 --target zene SlideNotesTest MpeNoteStorageTest NotePlaybackTest NoteRandomTest
   UpgradeExtendedNoteRangeTest NoteTransformTest NoteSelectionIdentityTest NoteToolsTest
   > .../logs/build-3-family.log 2>&1                                                  EXIT=0
cmake --build build -j4 --target zene DataFileFormatTest DocumentIndexTest
   > .../logs/build-6-zene-load.log 2>&1                                               EXIT=0
cd build/tests && ctest -R 'MidiProbabilityPersistenceTest|SlideNotesTest|MpeNoteStorageTest|NotePlaybackTest|
   NoteRandomTest|UpgradeExtendedNoteRangeTest|NoteTransformTest|NoteSelectionIdentityTest|NoteToolsTest|
   DataFileFormatTest|DocumentIndexTest' --output-on-failure > .../logs/test-6-family-final.log 2>&1
                                                                                       EXIT=0  (11/11 passed)
```

Gates (each command of the feature_check table run on its own, because the aggregator cannot start
here - see Self-check):

```
bash tests/fork-sources-gate.sh                        EXIT=0   (logs/gate-fork-sources.log)
bash tests/all-sources-reproduce.sh                     EXIT=0   (logs/gate-all-sources-reproduce.log)
bash tests/no-upstream-regression-gate.sh               EXIT=0   (logs/gate-no-upstream-regression.log)
tests/scripted/check-namespace                          EXIT=0   (logs/gate-check-namespace.log)
bash tests/no-tautology-gate.sh                         EXIT=0   (logs/gate-no-tautology.log)
python3 tests/doc-figures-gate.py                       EXIT=0   (logs/gate-doc-figures.log)
bash tests/complexity-gate.sh --check                   EXIT=0   (logs/gate-complexity.log: "28176 functions
    in the fork scope; 60 exceed CCN 10" - none of them in src/core/Note.cpp; the new noteRangeValue() is
    CCN 2, and complexity-gate.sh is unchanged)
bash tests/file-length-gate.sh --check                  EXIT=0   (logs/gate-file-length.log: "PASS (check mode:
    no regressions ...)"; none of the 4 over-500 files is mine)
```

```
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>                  EXIT=1
```
with

```
git -C .../qwen/gate-baselines/tree checkout -q --detach 9913d36d53... → fatal: cannot create directory at
'plugins/ZeneRack': Read-only file system            (logs/feature_check.log, exit status 128)
```

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Reverted: `test-4-red-proof.log`, ctest EXIT=8, one failure, the new slot, on `vol="300"`.
  Restored: `test-5-green-proof.log`, ctest EXIT=0. Both after a rebuild of the same target
  (`build-4-reverted.log` EXIT=0, `build-5-reapplied.log` EXIT=0). The first RED run, before the fix
  ever existed, is `test-1-red.log` EXIT=8.
- **The test is deterministic.** No clock, sleep, timer or thread: it builds `QDomElement`s with
  QDomElement::setAttribute and calls `Note::restoreState()` (which is `loadSettings`) synchronously.
  The injected values are the attribute texts themselves - `"300"`, `"-1"`, `"2147483647"`, `"1.5"`,
  `"bad"`, `""`, and absence - and the expected values are the constants `MaxVolume`, `MinVolume`,
  `DefaultVolume`, `PanningLeft`, `PanningRight`, `DefaultPanning`. Nothing depends on attribute
  order (each row builds its own element) so Qt's per-process hash seed cannot move a result.
- **The fix is at the cause and complete.** `Note::loadSettings()` is the ONLY reader of a note
  element's `vol` / `pan` (`multi_grep` over `src/` for `attribute( "vol"` and `attribute( "pan"`
  returns src/core/Note.cpp:648-649 plus src/core/DataFile.cpp:1344-1360 and src/core/Mixer.cpp,
  which are the file-upgrade and mixer-channel paths, not notes). Everything that WRITES those
  members already clamps: the constructor (`:195-196`), `setVolume` (`:387`), `setPanning` (`:414`),
  and the control commands that reach them (`ControlCommandsNoteEdit.cpp:333-334`,
  ControlCommandsNoteShape.cpp:436, ControlCommandsNoteStep.cpp:503, NoteTransform.cpp, GrooveTemplate.cpp).
  `Note::saveSettings()` (`:572-573`) writes the members, so a reload of a saved note is unchanged.
  The sibling attributes in the same function that were already safe stay as they are: `key` via
  `setKey` + clamp, `prob`/`veljit` via `setProbability`/`setVelocityJitter` and `loadMidiDepthValue`,
  `relvel` via `setReleaseVelocity`. The helper follows the same contract as `loadMidiDepthValue`.
- **No collateral damage.** In-range files load bit-identically: the test's normal-path rows
  (`vol` 0/100/200, `pan` -100/0/100) and the 150/-60 round trip assert exactly that, and
  `DataFileFormatTest` + `DocumentIndexTest` + `SlideNotesTest` + `MpeNoteStorageTest` +
  `NotePlaybackTest` pass (11/11, `test-6-family-final.log`). The bounds I route through are the ones
  the setters already enforced, not a new limit: 0..200 for volume (`MaxVolume`) and -100..100 for
  panning (`PanningRight`), and no legitimate caller exceeds them - the engine, the piano roll and the
  control surface cannot, because every one of them goes through those setters. Old projects whose
  note element has no `vol` at all now load at `DefaultVolume` instead of 0; `DataFile.cpp:1350-1359`
  already sets `vol="100"` on upgrade for exactly that reason, so the fallback matches the file
  upgrader rather than inventing a value.
- **Verdict is REAL, not ALREADY-FIXED / FALSE-POSITIVE**, so the last ASK does not apply; the
  evidence is the pre-fix source quoted at the top and the RED run.
- **Verifier ASK item - gates.** `feature_check.py` itself cannot run to completion in this sandbox:
  its baseline cache is `~/.local/share/zene-bunny-fix/qwen/gate-baselines`, which is a READ-ONLY
  mount here (`touch .../gate-baselines/.probe` → `Read-only file system`, EXIT=1), and its first
  action for an uncached base is `git -C <cache>/tree checkout -q --detach 9913d36d53...`, which
  fails with exit status 128 (`fatal: cannot create directory at 'plugins/ZeneRack': Read-only file
  system`, `logs/feature_check.log`). The base it needs a listing for is this branch's own merge-base
  with `FIX-BASE` (`050/m0-train-52`), i.e. the tip the branch was cut from, so no cache entry exists.
  Instead every command the table aggregates was run on this worktree and its exit code recorded
  above: the six simple gates EXIT=0 and both ratchet gates EXIT=0 in `--check` mode, which reports
  "no regressions" and lists no item in `src/core/Note.cpp` or
  `tests/src/core/MidiProbabilityPersistenceTest.cpp`. That is exactly the test `feature_check.py`
  applies to a ratchet row: it collects only lines matching `^REGRESSION: ` and drops those already
  in the base listing, so with no `REGRESSION` line at all both rows are PASS for ANY base -
  `gate-complexity.log` (96 lines) and `gate-file-length.log` (10 lines) each end in
  `PASS (check mode: no regressions; baseline not written)` and contain no such line. `lizard`, which
  `complexity-gate.sh` needs and the image lacks, was installed into the run's own log folder and used
  through `PYTHONPATH` (`pip-lizard.log`, EXIT=0); the gate script itself was not modified.
  `submit_check.py` reports the same single FAIL for the same reason
  (`logs/submit_check.log`), with every other item PASS: commits on the branch, working tree clean,
  the report, and tests added or changed.

## Other findings (not touched here)

- `Note::loadSettings()` still assigns `m_type` and `m_slide` directly
  (`src/core/Note.cpp:684-685`): `<note type="99">` stores an out-of-enum `Type` and `m_slide` reads
  any non-zero text as slide. Same family as this bug, different behaviour, not this finding.
- `Note::saveSettings()` writes `vol`/`pan` unconditionally, so no file can express "volume absent";
  the `DefaultVolume` fallback therefore only ever fires for hand-edited or third-party documents
  (and for the `vol` a very old file's upgrade path fills in).

## Round 2 re-verification (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p14-204.r2/logs)

Round 1 was returned with "QA-ERROR: two unusable reviews" - a review-infrastructure failure, not a
code finding. The fix and test above are unchanged; this section re-proves them on the current branch
tip (merged with 050/m0-train-53) with fresh logs.

```
git show f67476ba6b -- src/core/Note.cpp > .../logs/fix.patch                                EXIT=0
git apply -R .../logs/fix.patch                                                              EXIT=0
cmake --build build --target MidiProbabilityPersistenceTest -j4 > .../logs/build-1-reverted.log   EXIT=0
cd build/tests && ctest -R MidiProbabilityPersistenceTest --output-on-failure > .../logs/test-1-red.log   EXIT=8
   FAIL!  : MidiProbabilityPersistenceTest::projectLoadedNoteVolumeAndPanningStayInBounds()
            'static_cast<int>( loaded.getVolume() ) == row.second' returned FALSE. (300)
git apply .../logs/fix.patch                                                                 EXIT=0
cmake --build build --target MidiProbabilityPersistenceTest -j4 > .../logs/build-2-green.log      EXIT=0
cd build/tests && ctest -R MidiProbabilityPersistenceTest --output-on-failure > .../logs/test-2-green.log   EXIT=0
   (re-run once more, test-3-green-verify.log: EXIT=0; git status clean after the cycle)
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/feature_check-1.log   EXIT=0
   RESULT: PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
   no-tautology, doc-figures, complexity, file-length all PASS). The aggregator now runs on this
   branch; the round-1 "cannot start" note above no longer applies.
```

Delegation: Haiku ran the revert / rebuild / red / reapply / rebuild / green cycle and reported the
exit codes; I confirmed the red and green results from the logs and re-ran the green test myself.
