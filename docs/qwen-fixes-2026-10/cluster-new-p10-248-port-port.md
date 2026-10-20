# Cluster new-p10-248-port-port

Verdict: **FIXED**

Port job: the fix accepted as `bugs/qwen-new-p10-248-port` (commits `084c7e911`,
`60a43ab11`, `42a601420`) could not go on the newest train - it conflicted in
`src/core/Mixer.cpp`. This branch is a fresh branch off the newest line; the fix
was re-applied **by hand** (`git show` of the three commits, no merge, no
cherry-pick, no rebase) and adapted to the newer code.

The finding is REAL on this branch: before the fix, `Mixer::allocateChannelsTo`
had no bound and was called with a number straight out of the project file.

## Round 2 (Luna's review of round 1)

Luna's review (`~/.local/share/zene-bunny-fix/qwen/qa/reviews/new-p10-248-port-port.r1.md`)
sent the round-1 fix back with one blocking point:

> The new 0-511 project index cap is inconsistent with the public channel
> creation and save paths. The application can create channel 512 and serialize
> it, but the loader rejects and skips it; reopening such a project loses that
> channel and its saved state.

That is true, and it was a data-loss regression of round 1's own making: the
fixed cap (512) was a constant the application knows nothing about, while
`Mixer::createChannel()` (`include/Mixer.h:658`, called by
`src/core/ControlCommandsMixer.cpp:272`) appends without a limit and
`Mixer::saveSettings()` (`src/core/Mixer.cpp:2490`) writes every channel's
index into the file. Round 2 removes the constant and derives the bound from
the project file itself, so the loader and the application cannot disagree.

## What round 2 changed

The bound is now **the number of channels the file describes**: the application
writes one `<mixerchannel>` element per channel (the master included -
`src/core/Mixer.cpp:2465`), so the elements ARE the channels the file has. A
project-supplied index may address `0..describedChannels` - the channels the
file describes, plus the one channel a file may name without describing it
(the behaviour the loader has always had). An index beyond that names a
channel the file does not have, and honouring it would let the FILE choose how
much memory the load takes - which is the bug this cluster is about.

| Location (after round 2) | Behaviour |
| --- | --- |
| `src/core/Mixer.cpp:93` `countDescribedChannels` | counts the `<mixerchannel>` elements of the file being loaded |
| `src/core/Mixer.cpp:109` `isSupportedProjectMixerIndex` | the supported range is `0..describedChannels` - the file's own, not a constant |
| `src/core/Mixer.cpp:118` `parseProjectChannelIndex` | reads the attribute as `qlonglong`; an index the file does not back up is `OutOfRange`, a value that is not a number is `NotANumber` |
| `src/core/Mixer.cpp:2631` `Mixer::allocateChannelsTo(num, describedChannels)` | the allocation itself refuses an index outside the file's range (second line of defence) |
| `include/Mixer.h:759` | the private method's new parameter (no public declaration changes) |
| `src/core/Mixer.cpp:2659` `describedChannels` | counted once per load, before the bus pre-pass |
| `src/core/Mixer.cpp:2660` `reportUnsupportedIndex` | the load error names the range the file describes |
| `src/core/Mixer.cpp:2687` (bus pre-pass), `:2723` (channel number), `:2799` (regular send), `:2842` (sidechain send), `:2949` (VCA member) | every project-supplied index goes through the checked read with the file's own bound |

Round 1's other two changes are unchanged: the wide (64-bit) conversion, so a
number that does not fit in an `int` can never become channel 0 (the master),
and the whitespace-aware overflow classification.

### Why the bound cannot reject a project the application produced

The application creates channels without a limit and writes one
`<mixerchannel>` element per channel, so an application-written project with N
channels describes N channels and names the indices `0..N-1`. The loader's
bound is that same N, so such a project always loads - with all N channels.
The regression `aChannelTheApplicationCanCreateSurvivesSaveAndLoad` proves it
for the channel at index 512 (the index round 1's constant cut off).

The bound is also the *allocation* bound: the loader can never create more
channels than the file holds elements, so a hostile file cannot ask for two
billion channels no matter how small it is.

## What was wrong (the pre-fix code, this branch's base `2da2ed356`)

| Location (pre-fix line numbers) | Code | Consequence |
| --- | --- | --- |
| `src/core/Mixer.cpp:2550` `Mixer::allocateChannelsTo` | `while (static_cast<std::size_t>(num) > m_mixerChannels.size() - 1) { createChannel(); ... }` | an unbounded `num` allocates one channel per step |
| `src/core/Mixer.cpp:2580` (bus pre-pass) | `mixch.attribute( "num" ).toInt()` -> `allocateChannelsTo( busNum )` | `num="2147483647"` allocates two billion channels and then indexes `m_mixerChannels[busNum]` |
| `src/core/Mixer.cpp:2612` (channel number) | `mixch.attribute( "num" ).toInt( &numOk )` -> `allocateChannelsTo( num )` | same, plus `m_mixerChannels[num]` is indexed with the same number |
| `src/core/Mixer.cpp:2683` (regular `<send channel>`) | `chDataItem.attribute( "channel" ).toInt( &sendToOk )` -> `allocateChannelsTo( sendTo )` | a send to channel 2147483647 allocates two billion channels |
| `src/core/Mixer.cpp:2718` (sidechain `<sidechain-send channel>`) | same | same |
| `src/core/Mixer.cpp:2817` (VCA `<member channel>`) | `memberDom.attribute( "channel" ).toInt( &parsed )` | already bounded on this line (Luna cluster 3295): the value was checked against `m_mixerChannels.size()`, but only when it fitted an `int` - a value that overflowed an `int` was dropped in silence, with no load error |

Measured before the fix: the regression loaded `<mixerchannel num="512">` and
the mixer came back with **513 channels** (`Actual (mixer->numChannels()): 513,
Expected: 1`) - the allocation of a project-named index, unbounded. The same
path with `num="2147483647"` would have created 2147483647 channels.

## Regression

`tests/src/core/MixerRoutingBackwardCompatTest.cpp`

1. `aProjectIndexTheMixerCannotAddressIsRefusedAndReported` (line 227) - the
   table-driven case (`kRefusedIndexCases`, line 95) over the four paths a
   project index reaches the mixer on (`<mixerchannel num>`, `<send channel>`,
   `<sidechain-send channel>`, `<member channel>`), with these values:
   * `512` - an index the one-channel fixture does not describe,
   * `2147483648` - a number that does not fit in an `int` at all,
   * ` 99999999999999999999999 ` - a number that does not fit in 64 bits
     either, padded with whitespace (the conversion trims it, so it is still a
     number),
   * `"abc"` - a value that is not a number,
   * one row with a file that describes two channels and names a third one that
     is not there (`num="2147483647"`): the two it describes must load, the
     third must be refused.

   For every case the test asserts the channel count, that the load error is
   collected and names the offending value (for the numeric ones), that **no
   route to the master exists** (`receiverIndex() != 0` for every regular and
   sidechain route), and that a refused VCA member did not join its group.

2. `aChannelTheApplicationCanCreateSurvivesSaveAndLoad` (line 292) - the
   review's round-trip regression: the test creates 513 channels the way the
   application does (`Mixer::createChannel()`, no limit), gives channel 512 a
   name and a volume, saves the mixer with `Mixer::saveSettings()`, loads it
   back and asserts that all 513 channels are there, that channel 512 kept its
   name and its volume, and that the load collected **no** error - the
   application's own project must not draw a complaint.

The newest line's own malformed-input tests
(`aMalformedSendTargetIsSkippedNotRoutedToMaster`,
`aNegativeChannelNumberIsRejectedNotIndexed`,
`aMalformedVcaMemberChannelIsSkipped`,
`aMalformedChannelNumberDoesNotOverwriteTheMaster`) were **kept** - they exist
on this line and not on the port's base.

Both tests are deterministic: they use fixed XML fixtures and the mixer's own
save/load (`Mixer::saveSettings` / `Mixer::loadSettings`); the only injected
values are the fixture strings and the song's error list
(`Song::clearErrors()` / `hasErrors()` / `errorSummary()`). No timer, no
sleep, no thread, no audio period.

## Verification evidence

| What | Command | EXIT |
| --- | --- | --- |
| GREEN round 2 (fix applied) | `cmake --build build --target zene MixerRoutingBackwardCompatTest -j12` then `cd build/tests && ctest -R '^MixerRoutingBackwardCompatTest$' --output-on-failure` | build **0**, ctest **0** (1/1 passed; the binary itself reports `18 passed, 0 failed` and `ROUND_TRIP_512 channels=513 name=Round trip 512`) - `logs/build-9-rework.log`, `logs/test-8-rework-green.log`, `logs/test-9-direct-binary.log` |
| Affected family (mixer/VCA/routing/rack) | `cd build/tests && ctest -R 'Mixer\|Mixing\|Vca\|VCA\|PdcMixer\|RackTest\|RackMacrosTest' -E '^VcaStripViewTest$' --output-on-failure` | **0** (25/25 passed) - `logs/test-17-rework-family-final.log` |
| Project-load family | `cd build/tests && ctest -R '^(ProjectOpenIntegrityTest\|DataFileSaveIntegrityTest\|StableTrackIdsTest\|ProjectStateTest\|ProjectSectionTest\|ProjectContainerTest\|ProjectVersionTest\|LibraryProjectsTest)$' --output-on-failure` | **0** (8/8 passed) - `logs/test-18-rework-project-family.log` |
| Whole gate set | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** (8/8 rows PASS) - `logs/gate-6-feature-check-rework.log` |
| Complexity ratchet (raw) | `bash tests/complexity-gate.sh --check` | **1** - red on the train on purpose; none of the listed items is in my files (`logs/gate-7-complexity-rework.log`) |
| File-length ratchet (raw) | `bash tests/file-length-gate.sh --check` | **1** - red on the train on purpose; none of the listed items is in my files (`logs/gate-8-file-length-rework.log`) |

### Reverse-patch proof of round 2 (step 4b)

1. `git diff -- src/core/Mixer.cpp include/Mixer.h > logs/fix-round2.patch && git apply -R logs/fix-round2.patch` - EXIT **0**
2. `cmake --build build --target zene MixerRoutingBackwardCompatTest -j12` - EXIT **0** (`logs/build-10-rework-reverted.log`)
3. `cd build/tests && ctest -R '^MixerRoutingBackwardCompatTest$' --output-on-failure` - EXIT **8** (`logs/test-10-rework-reverted.log`):
   `FAIL! : MixerRoutingBackwardCompatTest::aChannelTheApplicationCanCreateSurvivesSaveAndLoad() Compared values are not the same. Actual (mixer->numChannels()): 512, Expected (513): 513`
   - that is Luna's finding reproduced exactly: with round 1's constant, the
   channel at index 512 is skipped and its state is lost.
4. `git apply logs/fix-round2.patch` - EXIT **0**
5. `cmake --build build --target zene MixerRoutingBackwardCompatTest -j12` - EXIT **0** (`logs/build-11-rework-restored.log`)
6. `cd build/tests && ctest -R '^MixerRoutingBackwardCompatTest$' --output-on-failure` - EXIT **0** (1/1 passed, `logs/test-11-rework-restored.log`)

### The original bug is still caught (round 1's proof, repeated against the base code)

1. `git checkout 2da2ed356 -- src/core/Mixer.cpp include/Mixer.h` (the pre-fix
   product code) and rebuild - EXIT **0** (`logs/build-12-prefix-code.log`)
2. `cd build/tests && ctest -R '^MixerRoutingBackwardCompatTest$' --output-on-failure` - EXIT **8** (`logs/test-12-prefix-code.log`):
   `FAIL! : ...aProjectIndexTheMixerCannotAddressIsRefusedAndReported() Compared values are not the same. Actual (mixer->numChannels()): 513, Expected (refused.channels): 1`
3. `git checkout HEAD -- src/core/Mixer.cpp include/Mixer.h && git apply logs/fix-round2.patch` and rebuild - EXIT **0** (`logs/build-13-rework-final.log`)
4. `cd build/tests && ctest -R '^MixerRoutingBackwardCompatTest$' --output-on-failure` - EXIT **0** (1/1 passed, `logs/test-13-rework-final.log`)

So: the pre-fix code fails the refused-index case, round 1's constant fails the
round-trip case, and round 2 passes both.

### Pre-existing failure in the neighbourhood (not caused by this change)

* `VcaStripViewTest` aborts with `double free or corruption (out)` /
  SIGABRT in `aStripWhoseGroupIsRemovedRetiresAtOnce()`. Reproduced with the
  **pre-fix base product code** (`git checkout 2da2ed356 -- src/core/Mixer.cpp
  include/Mixer.h`, rebuild, run): EXIT **134** with the same SIGABRT
  (`logs/test-16-vcastripview-base.log`). The test creates its VCA groups
  programmatically and never loads a project, so the loader change cannot
  reach it. It is therefore excluded from the family run above, exactly as the
  accepted branch's report did.

All logs are in
`~/.local/share/zene-bunny-fix/qwen/runs/new-p10-248-port-port.r1/logs/`.

## Files committed

- `src/core/Mixer.cpp` - the checked wide read of every project-supplied mixer
  channel index, the file-derived bound, the refusal and the load error.
- `include/Mixer.h` - the private `Mixer::allocateChannelsTo()` takes the
  number of channels the loaded file describes (no public declaration changes).
- `tests/src/core/MixerRoutingBackwardCompatTest.cpp` - the table-driven
  refused-index regression (15 cases) and the round-trip regression for a
  channel the application can create at index 512.
- `tests/upstream-modifications.txt` - the divergence entries for
  `src/core/Mixer.cpp` and `include/Mixer.h`, with the reason and the
  regressions that cover them.
- `docs/qwen-fixes-2026-10/cluster-new-p10-248-port-port.md` - this report.

No new product file: the test file already exists on this line and is
registered in `tests/all-sources.txt` and `tests/CMakeLists.txt`, so no
`tests/fork-sources.txt` entry was needed.

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with
  it (exit codes).** Round 2: with `git apply -R logs/fix-round2.patch` (EXIT
  0) and a rebuild (EXIT 0), `ctest -R '^MixerRoutingBackwardCompatTest$'` from
  `build/tests` exits **8** with `Actual (mixer->numChannels()): 512, Expected
  (513): 513` (the round-trip case); with the patch restored (`git apply
  logs/fix-round2.patch`, EXIT 0) and a rebuild (EXIT 0) the same ctest exits
  **0** (1/1 passed). Against the pre-fix base code the refused-index case
  fails instead (`Actual (mixer->numChannels()): 513, Expected: 1`, EXIT **8**),
  so both halves of the contract are proven by a failing run.
* **The test is deterministic: no wall-clock timing, sleeps or races (name the
  seam or injected values it uses).** The seam is `Mixer::loadSettings` (with
  fixed XML fixtures) and `Mixer::saveSettings` (the application's own save);
  the injected values are the fixture strings and the song's error list
  (`Song::clearErrors()` / `hasErrors()` / `errorSummary()`). No timer, no
  sleep, no thread, no audio period is involved.
* **The fix is at the cause and complete: every caller and sibling of what you
  changed was checked and stays consistent.** The cause is an unchecked
  project-supplied index reaching `Mixer::allocateChannelsTo`, so the fix is at
  the conversion and at the allocation, not at each symptom: one helper
  (`parseProjectChannelIndex`, `src/core/Mixer.cpp:118`) reads every index
  with the file's own bound (`describedChannels`), and
  `allocateChannelsTo(num, describedChannels)` refuses an index outside it
  itself. Every index read in `Mixer::loadSettings` was checked and uses it -
  the bus pre-pass (`:2687`), the channel number (`:2723`), the regular send
  (`:2799`), the sidechain send (`:2842`) and the VCA member (`:2949`).
  `rg -n "allocateChannelsTo"` over `src/` and `include/` shows
  `Mixer::allocateChannelsTo` has **no other caller** than
  `Mixer::loadSettings` (four call sites, all project-driven) and that it is
  private, so the interactive `Mixer::createChannel()` path is untouched. The
  remaining `toInt()` reads in the mixer load path are not channel indices and
  were left alone on purpose: the `<mixerchannel id>` stable id, the VCA group
  `id`/`parent` and the `<edittrack track>` id are ids, and none of them
  reaches `allocateChannelsTo`. The creation path itself
  (`src/core/Mixer.cpp:968`, `src/core/ControlCommandsMixer.cpp:272`) and the
  save path (`src/core/Mixer.cpp:2465`) were read as well: they are what the
  bound is derived from, and neither was changed.
* **No collateral damage: old projects and saved data still load as before, and
  any limit or check you added does not hit other callers that legitimately
  exceed it.** The bound is the file's own channel count, so it cannot be
  exceeded by anything the application produces: the application writes one
  `<mixerchannel>` element per channel, and every one of the 31 shipped `.mmp`
  fixtures has contiguous indices (`elements == max(num) + 1`, checked with a
  script over `**/*.mmp`) - `tests/emptyproject.mmp` has 65 elements and the
  highest index 64. The legacy routing/render equality test and the Phase D
  round-trip test in the same file still pass, as do the 25 mixer/VCA/rack
  tests and the 8 project-load tests listed above. The behaviour change is
  confined to a file that names a channel it does not describe: such an index
  is refused and reported (it used to be dropped in silence, or - on a send
  target - to become a send to the master, or - on a channel number - to
  allocate billions of channels); a send target that is not a number is now
  refused instead of becoming a send to the master; and a *negative* index is
  now reported as well as refused (it was already refused). A padded number
  that fits (`" 42 "`) is unaffected - `toLongLong()` has always trimmed it.
* **Realtime, undo and commands.** The change lives entirely in the project
  load path (`Mixer::loadSettings`), which runs on the control thread while a
  file is read: nothing new runs on the audio thread, nothing allocates,
  frees, locks, waits or logs there, and no undo step and no control command
  was added. The only new work off the audio thread is one
  `Song::collectError` call per refused index (a `QMap` insert in the song).
* **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or
  the intended contract that shows it.** Not applicable - the verdict is FIXED.
  The pre-fix code is quoted in the table above (`src/core/Mixer.cpp:2550`,
  `:2580`, `:2612`, `:2683`, `:2718`, `:2817` of the pre-fix tree), and the
  measured pre-fix behaviour is the RED run against the base code (513
  channels for a project that named 512).
