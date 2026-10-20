# Cluster new-p10-248-port-port

Verdict: **FIXED**

Port job: the fix accepted as `bugs/qwen-new-p10-248-port` (commits `084c7e911`,
`60a43ab11`, `42a601420`) could not go on the newest train - it conflicted in
`src/core/Mixer.cpp`. This branch is a fresh branch off the newest line; the fix
was re-applied **by hand** (`git show` of the three commits, no merge, no
cherry-pick, no rebase) and adapted to the newer code.

The finding is REAL on this branch: before the fix, `Mixer::allocateChannelsTo`
had no bound and was called with a number straight out of the project file.

| Location (this branch, before the fix) | Code | Consequence |
| --- | --- | --- |
| `src/core/Mixer.cpp:2550` `Mixer::allocateChannelsTo` | `while (static_cast<std::size_t>(num) > m_mixerChannels.size() - 1) { createChannel(); ... }` | an unbounded `num` allocates one channel per step |
| `src/core/Mixer.cpp:2580` (bus pre-pass) | `mixch.attribute( "num" ).toInt()` -> `allocateChannelsTo( busNum )` | `num="2147483647"` allocates two billion channels and then indexes `m_mixerChannels[busNum]` |
| `src/core/Mixer.cpp:2612` (channel number) | `mixch.attribute( "num" ).toInt( &numOk )` -> `allocateChannelsTo( num )` | same, plus `m_mixerChannels[num]` is indexed with the same number |
| `src/core/Mixer.cpp:2683` (regular `<send channel>`) | `chDataItem.attribute( "channel" ).toInt( &sendToOk )` -> `allocateChannelsTo( sendTo )` | a send to channel 2147483647 allocates two billion channels |
| `src/core/Mixer.cpp:2718` (sidechain `<sidechain-send channel>`) | same | same |
| `src/core/Mixer.cpp:2817` (VCA `<member channel>`) | `memberDom.attribute( "channel" ).toInt( &parsed )` | already bounded on this line (Luna cluster 3295): the value was checked against `m_mixerChannels.size()`, but only when it fitted an `int` - a value that overflowed an `int` was dropped in silence, with no load error |

Measured on this branch before the fix: the new regression loaded
`<mixerchannel num="512">` and the mixer came back with **513 channels**
(`Actual (mixer->numChannels()): 513, Expected: 1`) - the allocation of a
project-named index, unbounded. The same path with `num="2147483647"` would
have created 2147483647 channels.

## The fix (ported from the accepted branch, unchanged in substance)

The newest line's differences from the port's base were kept: the master mix
(`MixHelpers::addMultipliedByBuffer`), the duplicate-VCA-group-id handling, and
the `sendToOk` success flags of cluster `new-p12-142-port` (which the checked
read subsumes). `git diff bugs/qwen-new-p10-248-port -- src/core/Mixer.cpp` on
this branch shows **only** those newest-line hunks plus two added comments -
the fix itself is byte-for-byte the accepted one.

| Location (this branch, after the fix) | Behaviour |
| --- | --- |
| `src/core/Mixer.cpp:65` `MaxProjectMixerChannels` | 512 - the most channels a project file may name (valid indices 0-511) |
| `src/core/Mixer.cpp:67` `isSupportedProjectMixerIndex` | the range test |
| `src/core/Mixer.cpp:73` `enum class ProjectIndex` | `Ok` / `NotANumber` / `OutOfRange` |
| `src/core/Mixer.cpp:82` `looksLikeDecimalInteger` | tells a number that did not fit 64 bits from a value that is not a number; it trims the text, exactly as `QString::toLongLong()` does, so a whitespace-padded overflow is still a number |
| `src/core/Mixer.cpp:104` `parseProjectChannelIndex` | reads the attribute as `qlonglong`; a number outside `0-511` (negative, `>= 512`, or too large for 64 bits) is `OutOfRange`, anything that is not a number is `NotANumber` |
| `src/core/Mixer.cpp:2617` `Mixer::allocateChannelsTo` | refuses any index outside the supported range (the second line of defence) |
| `src/core/Mixer.cpp:2639` `reportUnsupportedIndex` | reports the refused index through `Song::collectError` |
| `src/core/Mixer.cpp:2666` (bus pre-pass) | uses the checked read, so an overflowing `num` cannot become the master bus; silent, because the main pass reports the element once |
| `src/core/Mixer.cpp:2701` (channel number) | `OutOfRange` -> refused **and reported**; `NotANumber` -> skipped whole (the pre-existing contract) |
| `src/core/Mixer.cpp:2777` (regular send target) | refused **and reported**; the index can no longer become channel 0 (the master), so no route to the master is created |
| `src/core/Mixer.cpp:2820` (sidechain send target) | the same rule as the regular send |
| `src/core/Mixer.cpp:2926` (VCA member index) | refused **and reported**; the member does not join the group |

The contract kept from the accepted fix: an attribute that holds **no number at
all** (missing, empty, `"abc"`) names no channel and is skipped without a
report; a **number** outside the supported range is refused *and* reported.

## Regression

`tests/src/core/MixerRoutingBackwardCompatTest.cpp` - the ported table-driven
case (`kRefusedIndexCases`, line 94; the case itself at line 222) over the four
paths a project index reaches the mixer on (`<mixerchannel num>`,
`<send channel>`, `<sidechain-send channel>`, `<member channel>`), with these
values:

* `512` - the first number outside the supported range,
* `2147483648` - a number that does not fit in an `int` at all,
* ` 99999999999999999999999 ` - a number that does not fit in 64 bits either,
  padded with whitespace (the conversion trims it, so it is a number),
* `"abc"` - a value that is not a number.

For every case the test asserts the channel count, that the load error is
collected and names the offending value (for the numeric ones), that **no route
to the master exists** (`receiverIndex() != 0` for every regular and sidechain
route), and that a refused VCA member did not join its group.

The test is deterministic: it loads fixed XML fixtures through
`Mixer::loadSettings` and reads the resulting mixer state; the only values it
injects are the fixture strings and the song's error list
(`Song::clearErrors()` / `hasErrors()` / `errorSummary()`). No timer, no sleep,
no thread, no audio period.

The newest line's own malformed-input tests
(`aMalformedSendTargetIsSkippedNotRoutedToMaster`,
`aNegativeChannelNumberIsRejectedNotIndexed`,
`aMalformedVcaMemberChannelIsSkipped`,
`aMalformedChannelNumberDoesNotOverwriteTheMaster`) were **kept** - they exist
on this line and not on the port's base.

## Verification evidence

| What | Command | EXIT |
| --- | --- | --- |
| RED (test on the unfixed product) | `cmake --build build --target MixerRoutingBackwardCompatTest -j12` then `cd build/tests && ctest -R '^MixerRoutingBackwardCompatTest$' --output-on-failure` | build **0**, ctest **8** (`Actual (mixer->numChannels()): 513, Expected: 1`) - `logs/test-1-red.log` |
| GREEN (fix applied) | `cmake --build build --target zene MixerRoutingBackwardCompatTest -j12` then the same ctest | build **0**, ctest **0** (1/1 passed) - `logs/test-2-green.log` |
| Affected family (mixer/VCA/routing) | `cd build/tests && ctest -R 'Mixer|Mixing|Vca|VCA|PdcMixer' --output-on-failure` | **0** (24/24 passed) - `logs/test-3-mixer-family.log` |
| Project-load family | `cd build/tests && ctest -R '^(ProjectOpenIntegrityTest\|DataFileSaveIntegrityTest\|StableTrackIdsTest\|ProjectStateTest\|ProjectSectionTest\|ProjectContainerTest\|ProjectVersionTest\|LibraryProjectsTest)$' --output-on-failure` | **0** (8/8 passed) - `logs/test-4-project-family.log` |
| Whole gate set | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** (8/8 rows PASS) - `logs/gate-1-feature-check.log` |
| Complexity ratchet (raw) | `bash tests/complexity-gate.sh --check` | **1** - red on the train on purpose; none of the listed regressions is in my files (`logs/gate-2-complexity.log`) |
| File-length ratchet (raw) | `bash tests/file-length-gate.sh --check` | **1** - red on the train on purpose; none of the listed regressions is in my files (`logs/gate-3-file-length.log`) |

### Reverse-patch proof (step 4b)

1. `git diff -- src/core/Mixer.cpp > logs/fix.patch && git apply -R logs/fix.patch` - EXIT **0**
2. `cmake --build build --target zene MixerRoutingBackwardCompatTest -j12` - EXIT **0** (`logs/build-6-reverted.log`)
3. `cd build/tests && ctest -R '^MixerRoutingBackwardCompatTest$' --output-on-failure` - EXIT **8** (`logs/test-5-reverted.log`):
   `FAIL! : MixerRoutingBackwardCompatTest::aProjectIndexTheMixerCannotAddressIsRefusedAndReported() Compared values are not the same. Actual (mixer->numChannels()): 513, Expected (refused.channels): 1`
4. `git apply logs/fix.patch` - EXIT **0**
5. `cmake --build build --target zene MixerRoutingBackwardCompatTest -j12` - EXIT **0** (`logs/build-7-restored.log`)
6. `cd build/tests && ctest -R '^MixerRoutingBackwardCompatTest$' --output-on-failure` - EXIT **0** (1/1 passed, `logs/test-6-restored.log`)

All logs are in
`~/.local/share/zene-bunny-fix/qwen/runs/new-p10-248-port-port.r1/logs/`.

## Files committed

- `src/core/Mixer.cpp` - the bounded, checked wide read of every project-supplied
  mixer channel index, the refusal and the load error.
- `tests/src/core/MixerRoutingBackwardCompatTest.cpp` - the table-driven
  regression (14 cases: four paths x three values plus the two whitespace-padded
  overflow rows).
- `tests/upstream-modifications.txt` - the divergence entry for
  `src/core/Mixer.cpp` with the reason and the regression that covers it.
- `docs/qwen-fixes-2026-10/cluster-new-p10-248-port-port.md` - this report.

No new product file: the test file already exists on this line and is
registered in `tests/all-sources.txt` and `tests/CMakeLists.txt`, so no
`tests/fork-sources.txt` entry was needed.

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with
  it (exit codes).** Reverted (`git apply -R logs/fix.patch`, EXIT 0; rebuild
  EXIT 0): `ctest -R '^MixerRoutingBackwardCompatTest$'` EXIT **8**, with
  `Actual (mixer->numChannels()): 513, Expected (refused.channels): 1`.
  Restored (`git apply logs/fix.patch`, EXIT 0; rebuild EXIT 0): the same
  ctest EXIT **0** (1/1 passed).
* **The test is deterministic: no wall-clock timing, sleeps or races (name the
  seam or injected values it uses).** The seam is `Mixer::loadSettings` with
  fixed XML fixtures; the injected values are the fixture strings and the
  song's error list (`Song::clearErrors()` / `hasErrors()` / `errorSummary()`).
  No timer, no sleep, no thread, no audio period is involved.
* **The fix is at the cause and complete: every caller and sibling of what you
  changed was checked and stays consistent.** The cause is an unchecked
  project-supplied index reaching `Mixer::allocateChannelsTo`, so the fix is at
  the conversion, not at each symptom: one helper
  (`parseProjectChannelIndex`, `src/core/Mixer.cpp:104`) reads every index, and
  `allocateChannelsTo` refuses an unsupported index itself. Every index read in
  `Mixer::loadSettings` was checked and uses it - the bus pre-pass (`:2666`),
  the channel number (`:2701`), the regular send (`:2777`), the sidechain send
  (`:2820`) and the VCA member (`:2926`). `rg -n "allocateChannelsTo"` over
  `src/` and `include/` shows `Mixer::allocateChannelsTo` has **no other
  caller** than `Mixer::loadSettings` (four call sites, all project-driven), so
  the interactive `Mixer::createChannel()` path is untouched. The remaining
  `toInt()` reads in the mixer load path are not channel indices and were left
  alone on purpose: the `<mixerchannel id>` stable id, the VCA group
  `id`/`parent` and the `<edittrack track>` id are ids, and none of them
  reaches `allocateChannelsTo`.
* **No collateral damage: old projects and saved data still load as before, and
  any limit or check you added does not hit other callers that legitimately
  exceed it.** The 512-channel bound is the accepted fix's and only binds a
  *project file*: the highest channel number in the 31 shipped `.mmp` fixtures
  is 64 (`rg -o 'num="[0-9]+"' --glob '*.mmp' .`), so every shipped project
  loads exactly as before - the legacy routing/render equality test and the
  Phase D round-trip test in the same file still pass, as do the 24 mixer/VCA
  tests and the 8 project-load tests listed above. The behaviour change is
  confined to malformed input: an index that is a *number* outside `0-511` now
  produces a load error (it used to be dropped in silence, or - on a send
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
  The pre-fix code is quoted in the first table of this report
  (`src/core/Mixer.cpp:2550`, `:2580`, `:2612`, `:2683`, `:2718`, `:2817` of
  the pre-fix tree), and the measured pre-fix behaviour is the RED run above
  (513 channels for a project that named 512).
