# Cluster new-p12-192

## Verdict: FIXED

**BUG 2 — "Reused note handles can resume an old sample voice"** (Luna's patrol,
`src/core/RackZoneInputs.cpp`, reported at the historical line 113).

The finding is **real**. `RackZoneInputs::voiceFor` matches a voice to a note by the play
handle pointer alone (`RackZoneVoice::owner == RackZoneNote::id`). When a pooled
`NotePlayHandle` is released and reacquired for a new note, the pointer is the same. The
only reuse guard was `note.framesPlayed < voice.lastFramesPlayed`. A late-starting note
plays fewer frames in its first period than a full period, so a handle that played 112
frames (offset 400 of 512) and is then reused by a note that plays the full 512 frames
satisfies `512 >= 112`: the guard does not fire, and the new note's sample resumes at the
old voice's position (~frame 112), with the old fade/`ended` state still in place.

### Evidence

Original code, `src/core/RackZoneInputs.cpp:113-116`:

```cpp
if (voice.owner.load(std::memory_order_acquire) != note.id) { continue; }
// A pooled play handle reused for a new note has played fewer frames than its voice last saw.
if (note.framesPlayed < voice.lastFramesPlayed) { voice.restart(); }
return &voice;
```

The RED assertion the new test prints (`test-red-1.log`):

```
FAIL!  : RackZoneRoutingTest::aReusedHandleRestartsTheVoice() 'second[0][0] == ramp[0][0]' returned FALSE. (reused handle resumed at 113, expected the sample start 1)
Loc: [../../tests/src/core/RackZoneRoutingTest.cpp(358)]
```

`second[0][0] == 113` is the ramp sample at source frame 112 — the new note resumed the old
voice instead of starting the sample over.

### The fix

`src/core/RackZoneInputs.cpp:113-120` (product, 4 lines changed). A voice is restarted when
the incoming note is in its **first period**, which a continuing note can never be:

```cpp
if (note.framesPlayed <= m_frames || note.framesPlayed < voice.lastFramesPlayed) { voice.restart(); }
```

`RackZoneFeed::noteOf` reads `framesPlayed` from `NotePlayHandle::totalFramesPlayed()` after
`NotePlayHandle::play`, and that count is `fpp - offset` on the first period and at least
`2*fpp - offset` afterwards (`NotePlayHandle.cpp:287-289,366` with `offset < fpp`). So
`framesPlayed <= m_frames` (the prepared period length, `Rack.cpp:106,169`) is exactly "this
is the first period", i.e. a new note instance. The existing decreasing-count check is kept
for a handle whose old voice saw a later period. This is the generation check the finding
asks for, keyed on the frame counter the engine already exposes, so no upstream
`NotePlayHandle` file is touched and the audio path stays a single integer compare.

The concern that a legitimate continuation could be restarted is disproved by the same
arithmetic: after a first sounding period a note has played `>= 1` frame, and its next
period adds `fpp` frames, so `framesPlayed > m_frames` from then on. The new test's case 2
asserts that a continuation still works (a second period renders from the old position).

Regression proof (step 4b): `git diff -- src/core/RackZoneInputs.cpp` was saved to
`logs/fix.patch`; `git apply -R` removed the product fix, the test was rebuilt and **failed
(EXIT 8)**; `git apply` restored it, the test was rebuilt and **passed (EXIT 0)**.

### Evidence table

| what | command (worktree root / `build/tests` for ctest) | log | EXIT |
| --- | --- | --- | --- |
| build the regression target | `cmake --build build --target RackZoneRoutingTest` | `build-2.log` | `0` |
| **new test + original product — RED** | `ctest -R '^RackZoneRoutingTest$' --output-on-failure` | `test-red-1.log` | `8` |
| build with the fix | `cmake --build build --target RackZoneRoutingTest` | `build-3.log` | `0` |
| **new test + fix — GREEN** | `ctest -R '^RackZoneRoutingTest$' --output-on-failure` | `test-green-1.log` | `0` |
| reverse the product patch | `git apply -R logs/fix.patch` | — | `0` |
| build reverted | `cmake --build build --target RackZoneRoutingTest` | `build-4-reverted.log` | `0` |
| **reverted — RED again** | `ctest -R '^RackZoneRoutingTest$' --output-on-failure` | `test-reverted.log` | `8` |
| restore the product patch | `git apply logs/fix.patch` | — | `0` |
| build restored | `cmake --build build --target RackZoneRoutingTest` | `build-5-refixed.log` | `0` |
| **restored — GREEN again** | `ctest -R '^RackZoneRoutingTest$' --output-on-failure` | `test-refixed.log` | `0` |
| affected family, 4 tests | `ctest -R 'RackZone\|RackMacros' --output-on-failure` | `test-family.log` | `0` — 4/4 passed |

RED (`test-red-1.log`) totals: `9 passed, 1 failed, 0 skipped` (10 QtTest slots). GREEN
(`test-green-1.log`): `100% tests passed out of 1`. Family (`test-family.log`):
`RackZoneFeedRunTest`, `RackMacrosTest`, `RackZonesTest`, `RackZoneRoutingTest` — all
`Passed`, 4/4. The family executables were rebuilt first
(`cmake --build build --target RackZoneFeedRunTest RackZonesTest RackMacrosTest
RackZoneRoutingTest`, `build-family.log`, EXIT `0`); the core library was rebuilt with each
`RackZoneRoutingTest` build, and the other family binaries are dynamic against it.

### Gates (unpiped, `--check`)

| gate | log | EXIT |
| --- | --- | --- |
| `bash tests/complexity-gate.sh --check` | `gate-complexity.log` | `1` |
| `bash tests/file-length-gate.sh --check` | `gate-filelength.log` | `1` |

**Both gate failures are pre-existing on this branch and unrelated to this change.** The
violations reported are all in files this diff does not touch:

* file-length: `19 exceed 500 code lines`; the 14 not in `tests/file-length-baseline.tsv`
  are `src/core/SmfInterchangeReader.cpp`, `src/core/TakeLane.cpp`,
  `tests/src/core/CollabTransportTest.cpp`, `ControlAutomationScriptTest.cpp`,
  `ControlDeviceCatalogueTest.cpp`, `ControlEditCommandsTest.cpp`, `CustomAccessTest.cpp`,
  `DeviceDspTest.cpp`, `HardwareIoTest.cpp`, `NotePlaybackTest.cpp`, `RackMacrosTest.cpp`,
  `RevisionTimelineTest.cpp`, `VideoTrackTest.cpp`, `tests/src/gui/ArrangerTimelineTest.cpp`.
* complexity: `68 exceed CCN 10`, of which the 6 not in `tests/complexity-baseline.tsv` are
  `dotCentreAlong@tests/src/gui/XyPadPairTest.cpp`, `parseNotes@src/core/DawProjectReadTracks.cpp`,
  `adjust@src/core/ControlCommandsMixerTools.cpp`, `setStrip@src/core/ControlCommandsMixerStrip.cpp`,
  `rotateProjectRevision@src/core/ProjectRevisions.cpp`, `importedOverrides@src/gui/ControlCommandsShortcuts.cpp`.

`git diff HEAD` for the two changed files touches neither list (the new test slot
`aReusedHandleRestartsTheVoice` is ~40 lines in a sub-500 file and its CCN is 1), and
`git status` is otherwise clean, so the gates report the same at HEAD. No limit was changed
and neither baseline was re-anchored.

### Registries

No registry change was needed: `src/core/RackZoneInputs.cpp` and
`tests/src/core/RackZoneRoutingTest.cpp` are fork-NEW and already listed in
`tests/fork-sources.txt:1760` and `:2366`. No inherited file was changed, so
`tests/upstream-modifications.txt` is untouched.

### Files committed

* `src/core/RackZoneInputs.cpp`
* `tests/src/core/RackZoneRoutingTest.cpp`
* `docs/qwen-fixes-2026-10/cluster-new-p12-192.md`

Logs (not committed): `logs/build-*.log`, `logs/test-*.log`, `logs/gate-*.log`,
`logs/fix.patch`.
