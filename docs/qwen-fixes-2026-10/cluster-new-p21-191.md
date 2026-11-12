**Verdict: FIXED**

# Cluster new-p21-191 — InstrumentMidiParts keeps a stale per-key channel after the parts are cleared

## What is wrong (evidence)

The finding points at `loadSettings` (`src/core/InstrumentMidiParts.cpp:98-102`) and proposes resetting `m_keyChannel` there. Reading the code shows that is not the cause, and that the proposed reset would be a regression:

- The stale value is written by `noteOnChannel`. Its early return `if (!active() || !validKey(key)) { return fallback; }` (`src/core/InstrumentMidiParts.cpp:56`, before the fix) skips the `m_keyChannel` store whenever no parts exist. So after the parts are cleared, a note-on leaves the previous note's channel in place.
- The track reads that value on a retrigger and on the note-off. `src/tracks/InstrumentTrackNotes.cpp:331-332` reads `channelOfKey` and then calls `noteOnChannel` for a key whose note is still running. It sends the note-off to the previous channel and the new note-on to the fallback. `src/tracks/InstrumentTrackNotes.cpp:351-353` then calls `noteOffChannel`, which returns the stale channel. The note-on on the fallback channel is never ended, and a note-off goes to the old part's channel.
- Resetting every entry in `loadSettings` would break the contract stated at `src/core/InstrumentMidiParts.cpp:67` ("a note that started on a part still ends there") and tested at `tests/src/core/InstrumentRoutingTest.cpp:521`. A note held across a preset load or undo step would then end on the fallback channel while its note-on sits on its part, so that note hangs. The finding's fix was therefore not applied.

Reachability: a held key re-struck while the parts are cleared, as in overlapping same-key notes from a sequencer, or a retrigger during an undo that removed the parts. Both paths go through `sendNoteOn`/`sendNoteOff` on the audio thread (`src/tracks/InstrumentTrack.cpp:673-709`).

## The fix

`src/core/InstrumentMidiParts.cpp:54-66` (`noteOnChannel`): when no parts are active the function now returns the fallback and forgets the key's remembered channel (`store(-1)`). Only the buggy path changes. A note-off after such a note-on uses its own fallback, as before, and a note that started on a part still ends there, as before. The routing path with parts is unchanged. It is one atomic relaxed store, with no allocation, lock, log or wait, so it is realtime-safe.

I considered storing the fallback channel instead, which would have matched the parts path. I rejected it because it changes the no-parts note-off from its own fallback to the note-on's, which the finding does not ask for.

## Regression test

`tests/src/core/InstrumentRoutingTest.cpp:540-551`, `midiPartsForgetChannelAfterClear` (new private slot next to `midiPartsPickChannels`). It is deterministic: no timing, no threads, no sleeps. Its matrix:

1. Normal path with parts: two notes resolve to channel 1 (`:543-545`); the existing `midiPartsPickChannels` covers first-match routing.
2. Clear while two notes still sound: the note that started on a part still ends on it (`:547`, the contract at `InstrumentMidiParts.cpp:67`).
3. Retrigger of a key with no parts: the fallback channel is used (`:548`).
4. Key pressure and note-off after that retrigger: nothing is remembered, so key pressure keeps its channel (`:549`, the regression assertion, which fails before the fix) and the note-off keeps its own fallback and never returns channel 1 (`:550`).
5. Constraints: the test runs on the caller's thread with plain value types. Nothing on the audio thread, no Qt build-only API.

## Commands run and exit codes

Build and test commands ran from the worktree root (builds) and from `build/tests` (ctest). Logs are in the LOG FOLDER `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-191.r1/logs`.

| Step | Command | EXIT | Evidence |
|---|---|---:|---|
| First RED build | `cmake --build build --target InstrumentRoutingTest -j4 > .../build-1-red.log 2>&1; echo EXIT=$?` | 2 | ccache could not create a temp file in the bunny-fix cache (`build-1-red.log:18-25`). Not a code error; nothing built. |
| RED build (ccache off) | `CCACHE_DISABLE=1 cmake --build build --target InstrumentRoutingTest -j4 > .../build-2-red.log 2>&1; echo EXIT=$?` | 0 | build succeeded, 0 errors |
| (discarded) ctest before any build | `ctest -R '^InstrumentRoutingTest$' ... > .../test-1-red.log` | 8 | Not evidence: the executable did not exist yet (`test-1-red.log:3`, "Could not find executable"), because the build had failed on ccache. |
| RED test (ctest, first version of the test, unfixed code) | `ctest -R '^InstrumentRoutingTest$' --output-on-failure > .../test-2-red.log 2>&1; echo EXIT=$?` | 8 | `FAIL! : InstrumentRoutingTest::midiPartsForgetChannelAfterClear()`, Actual `noteOffChannel(60, 9)` = 1, Expected 0 (`test-2-red.log:23-26`). The earlier assertions in the test passed. |
| GREEN build (first version of the fix) | `CCACHE_DISABLE=1 cmake --build build --target InstrumentRoutingTest InstrumentSetupUiTest InstrumentLayersCommandsTest -j4 > .../build-3-green.log 2>&1; echo EXIT=$?` | 0 | 0 errors |
| GREEN test (first version) | `ctest -R '^(InstrumentRoutingTest\|InstrumentSetupUiTest\|InstrumentLayersCommandsTest)$' ...` | 0 | 100% passed out of 3 (`test-3-green.log`) |
| Step 4b, RED proof (first version) | `git diff -- src/core/InstrumentMidiParts.cpp > .../fix.patch && git apply -R .../fix.patch`, rebuild, ctest | 0 / 0 / 8 | `test-4-unfixed-red.log:23-25`: Actual 1, Expected 0 at the same assertion. |
| GREEN build (final fix and test) | `CCACHE_DISABLE=1 cmake --build build --target InstrumentRoutingTest InstrumentSetupUiTest InstrumentLayersCommandsTest -j4 > .../build-6-final-green.log 2>&1; echo EXIT=$?` | 0 | 0 errors |
| GREEN test (final) | `ctest -R '^(InstrumentRoutingTest\|InstrumentSetupUiTest\|InstrumentLayersCommandsTest)$' --output-on-failure > .../test-6-final-green.log 2>&1; echo EXIT=$?` | 0 | 100% passed out of 3 |
| Step 4b, RED proof (final) | `git diff -- src/core/InstrumentMidiParts.cpp > .../fix.patch && git apply -R .../fix.patch`; `CCACHE_DISABLE=1 cmake --build build --target InstrumentRoutingTest -j4 > .../build-7-final-unfixed.log 2>&1` (EXIT 0); ctest `test-7-final-red.log` | 0 / 0 / 8 | `InstrumentRoutingTest.cpp:549`: Actual `channelOfKey(60, 9)` = 1, Expected 9 |
| Step 4b, GREEN proof (final) | `git apply .../fix.patch`; rebuild `build-8-reapplied.log` (EXIT 0); ctest `test-8-reapplied-green.log` | 0 / 0 / 0 | 100% passed out of 3 |
| Gates (final code) | `python3 -I .../gates_wrapper.py <worktree> .../gates-final` (see Self-check) | 0 | `gates-2-final.log`: 8 of 8 PASS, RESULT: PASS |

Affected family: `InstrumentRoutingTest`, `InstrumentSetupUiTest`, `InstrumentLayersCommandsTest`. Three ctest tests ran, all passed.

Note on the step 4b commands: the revert and re-apply use the same `fix.patch` written from the final product diff, so the RED and GREEN proofs are for the same code that is committed.

## Files committed

- `src/core/InstrumentMidiParts.cpp` (the fix)
- `tests/src/core/InstrumentRoutingTest.cpp` (the regression)
- `docs/qwen-fixes-2026-10/cluster-new-p21-191.md` (this report)

No new source files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are unchanged. The fix changes no header, no control command and no saved-data format.

## Not fixed (residual, out of this finding's scope)

`InstrumentTrack::silenceAllNotes` (`src/tracks/InstrumentTrack.cpp:715-722`) resets `m_runningMidiNotes` without going through a note-off, so the remembered channel survives a silence. The only read of that stale value after a silence is `channelOfKey`, which gives key pressure on a key that is no longer sounding its old channel. Note-off is not affected, because `sendNoteOff` only consults the memory while `m_runningMidiNotes > 0`. This is narrow and it is not the cluster's reported symptom, so it is left for a separate finding.

## Self-check

Result of `submit_check.py` run before the commit (`.../logs/submit-check-1.log`, EXIT=1):

- commits on the branch and working tree clean: FAIL before the commit, expected; both are the commit step itself.
- tests added or changed: FAIL before the commit, because `tests/src/core/InstrumentRoutingTest.cpp` was uncommitted; it is in the commit.
- **gates (feature_check.py): FAIL, environment only.** `feature_check.py` writes its logs to `<worktree>/../<name>-feature-check`, which is the `projects/zene-studio-program` directory. This lane's write-jail does not allow writes there, so `logs.mkdir` raises `PermissionError` (direct run: EXIT=1, `.../logs/gates-0-direct.log`). The gate logic was run unchanged through a wrapper (`.../logs/gates_wrapper.py`) that imports `SIMPLE`, `RATCHET`, `run`, `key`, `item_file`, `base_listings` from `feature_check.py` and writes the logs to the LOG FOLDER. Result on the final code: 8 of 8 PASS, EXIT=0 (`gates-2-final.log`). This is not a claim that `feature_check.py` itself passed. It needs to be re-run from a location it can write to.
- The three "?" items:
  1. RED/GREEN proof: yes, on the final code. Reverted product file → `InstrumentRoutingTest` EXIT=8, failing at `InstrumentRoutingTest.cpp:549` (Actual 1, Expected 9). Re-applied → ctest family EXIT=0, 3 of 3 passed. Commands and exit codes are in the table above.
  2. Deterministic: the test has no clock, sleep, thread or random input. Every input is a fixed integer key, velocity and channel, and the zones are built in memory with `zoneOf`, the same helper `midiPartsPickChannels` uses. The only state is the object under test.
  3. At the cause and complete: the callers of `noteOnChannel` are `sendNoteOn` only (`src/tracks/InstrumentTrackNotes.cpp:332`), and it reads the value through `channelOfKey` (`:331`). The readers of the memory are `noteOffChannel` (`:353`) and `channelOfKey` (`:331`, `:383`), all unchanged. `channelMask` and `loadSettings` are unchanged. `noteOffChannel` still returns and clears the memory on note-off, which keeps the held-note contract. The residual on the silence path is listed above and is not part of this fix.
  4. No collateral: with parts active the code runs the same resolve and the same store as before. With no parts, the only change is that a stale entry is dropped; a note-off then uses its own fallback, which is what it already did when no entry was present. No save or load format changed, no limit was added, and no header changed, so no other target needed a rebuild. `InstrumentSetupUiTest` and `InstrumentLayersCommandsTest`, which exercise MIDI parts through the UI and the command layer, pass (3 of 3 with `InstrumentRoutingTest`).
- ALREADY-FIXED / FALSE-POSITIVE claim: not used. The verdict is FIXED.
