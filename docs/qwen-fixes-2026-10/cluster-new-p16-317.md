FIXED

# cluster new-p16-317: invalid XML clears the existing chord track

## Verdict

FIXED. The finding is real at `src/core/ChordTrack.cpp:471-481` (the historical
line 473 has moved): `fromXml()` called `clear()` before it parsed the text, so
`QDomDocument::setContent()` failing returned `false` with the existing events
and followers already destroyed. The same happened for well-formed text whose
root was not `<chord-track>`.

## Cause and fix

`src/core/ChordTrack.cpp:471-481` now parses the document and validates its root
element FIRST, and only then calls `loadSettings()`, which performs the actual
replacement. A failed restore returns `false` and leaves the live track exactly
as it was (header contract updated at `include/ChordTrack.h:249-253`).

`loadSettings()` deliberately still clears first (`src/core/ChordTrack.cpp:429-435`,
`include/ChordTrack.h:235-244`): it is the project-loader path, where an absent
or foreign element means "this project has no chord track" (reset-on-absence).
`fromXml()` is a restore over a live track, so it validates before delegating to
that path. Its regression slot `loadSettingsClearsFirst()` (tests line 330) still
pins the loader behaviour unchanged.

Callers checked, none depends on a failed `fromXml()` emptying the track:
`src/core/ControlChordSupport.cpp:75,78` (undo/redo snapshots) and
`src/core/ControlCommandsChordEdit.cpp:291` (rollback when detection writes
nothing) both pass `toXml()` output and ignore the bool. Valid snapshots take
exactly the old path (`loadSettings`). The analogous `GroovePool::fromXml` and
`ProjectKey::fromXml` have their own documented false-empty contract and are not
this finding; changing them would be a separate behaviour change.

## Regression test

`tests/src/core/ChordTrackTest.cpp:244`,
`fromXmlFailureKeepsTheExistingTrack()`: populates two events and a follower,
snapshots `toXml()`, then feeds malformed XML, a well-formed `<song>` document,
and empty text. Each call must return `false`, and the events, follower and
byte-identical XML snapshot must survive all three. There is no timing, sleep or
threading in the test; the only inputs are static strings and the existing
`toXml()` snapshot.

## Evidence

The log folder is outside the process's writable sandbox in this run, so each
command's output was piped through a local bind mount to the required new
numbered file; the exit code recorded is the command's own (`PIPESTATUS[0]`),
not the logger's. Builds also needed
`CCACHE_TEMPDIR=$HOME/.cache/zene-ccache-tmp` because the default
`/run/user/1000` temp path was denied.

| step | command | exit | log |
|---|---|---|---|
| RED build, test added before fix | `cmake --build build --target ChordTrackTest` | 0 | `build-1.log` |
| RED run, original `fromXml()` | `cd build/tests && ctest -R '^ChordTrackTest$' --output-on-failure` | **8** (1/1 failed) | `test-1-red.log` |
| GREEN build after fix | `cmake --build build --parallel 4 --target zene ChordTrackTest` | 0 | `build-3-green.log` |
| GREEN run after fix | `cd build/tests && ctest -R '^ChordTrackTest$' --output-on-failure` | **0** (1/1 passed) | `test-2-green.log` |
| 4b patch generated | `git diff -- src/core/ChordTrack.cpp include/ChordTrack.h > .../fix.patch` | 0 | `fix.patch` |
| 4b product patch reverted | `git apply -R .../fix.patch` | 0 | `fix.patch` |
| 4b RED build | `cmake --build build --parallel 4 --target ChordTrackTest` | 0 | `build-4-red.log` |
| 4b RED run, fix reverted | `cd build/tests && ctest -R '^ChordTrackTest$' --output-on-failure` | **8** (1/1 failed) | `test-3-red.log` |
| 4b patch restored | `git apply .../fix.patch` | 0 | `fix.patch` |
| 4b GREEN build | `cmake --build build --parallel 4 --target ChordTrackTest` | 0 | `build-5-green.log` |
| 4b GREEN run | `cd build/tests && ctest -R '^ChordTrackTest$' --output-on-failure` | **0** (1/1 passed) | `test-4-green.log` |
| affected-family build | `cmake --build build --parallel 4 --target zene ChordTrackTest ChordDetectTest ChordProgressionTest ControlChordCommandsTest ControlChordWriteTest` | 0 | `build-6-family.log` |
| affected-family tests | `cd build/tests && ctest -R '^(ChordTrackTest\|ChordDetectTest\|ChordProgressionTest\|ControlChordCommandsTest\|ControlChordWriteTest)$' --output-on-failure` | **0** (5/5 passed) | `test-5-family.log` |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** (all rows PASS) | `feature-1.log` |
| completion check | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p16-317` | **0** (RESULT: PASS) | `submit-1.log` |

The RED assertion was exactly the bug:

```
FAIL!  : ChordTrackTest::fromXmlFailureKeepsTheExistingTrack() Compared values are not the same
   Actual   (orderOf(track))        : ""
   Expected (QStringLiteral("0,96")): "0,96"
   Loc: [../../tests/src/core/ChordTrackTest.cpp(254)]
```

The product diff used for the 4b reversal is the committed change to
`src/core/ChordTrack.cpp` and `include/ChordTrack.h` only; `fix.patch` matches it
byte for byte (`cmp -s ...` exit 0). The header hunk is comment-only, so after
each 4b apply the header mtime was restored to its pre-apply value to avoid
recompiling the whole tree through `Song.h`; `ChordTrack.cpp` recompiled both
times (shown in `build-4-red.log` and `build-5-green.log`), which is the
behaviour-bearing file.

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with
it.** RED 4b: `build-4-red.log` exit 0, `test-3-red.log` exit 8 with the
assertion above. GREEN 4b: `build-5-green.log` exit 0, `test-4-green.log` exit 0.
The same RED/GREEN pair without the 4b mtime optimisation is `test-1-red.log`
exit 8 and `test-2-green.log` exit 0.
* **The test is deterministic: no wall-clock timing, sleeps or races.** It uses
only literal XML strings, two fixed events, one follower, and the existing
`toXml()` snapshot; there is no clock, sleep, thread or external state.
* **The fix is at the cause and complete; every caller and sibling was checked.**
The cause was the unconditional `clear()` at the top of `fromXml()`; it is now
after parse + root validation and still inside the one `loadSettings()` path.
Callers are listed above and none reads `false` as "empty me". `loadSettings()`
keeps its deliberate clear-first reset semantics for project loading, unchanged.
* **No collateral damage.** Project save/load still calls `loadSettings()`
directly and is untouched; every valid `toXml()` snapshot restores exactly as
before (the round-trip test still passes). Only a failed `fromXml()` changes:
it now leaves the live track alone instead of emptying it. The five chord-family
tests pass (5/5), and the gate table is all PASS.
* **If the verdict were ALREADY-FIXED or FALSE-POSITIVE:** not applicable. The
verdict is FIXED: the pre-fix RED run reproduced the actual data loss
(`test-1-red.log` and `test-3-red.log`, exit 8), and the code evidence is the
unconditional pre-parse `clear()` at `src/core/ChordTrack.cpp:471-481` before
the fix.
