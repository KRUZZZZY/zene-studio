# cluster new-p8-279 — A malformed follower track ID becomes track 0

Verdict: **FIXED**

## Finding

`src/core/ChordTrack.cpp:450` (historical line): the `<follow track=... mode=.../>`
loader called `child.attribute(kFollowTrack, QStringLiteral("-1")).toInt()` and passed
the result straight to `setFollow`. `QString::toInt()` returns **0** when the
conversion fails, and 0 is a valid track ID, so
`<chord-track><follow track="garbage" mode="scale"/></chord-track>` silently made
track 0 follow the scale.

## Evidence

- `src/core/ChordTrack.cpp:450` (before):
  `setFollow(child.attribute(kFollowTrack, QStringLiteral("-1")).toInt(), mode);`
- `src/core/ChordTrack.cpp:151-153` `setFollow` only rejects `trackId < 0`, so a
  failed parse (0) was accepted as the real track 0.
- The sibling rule already documented in the same file: `malformedNumbersAreSkipped`
  ("a number that does not parse is not a zero") — the follower parse was the one
  attribute that did not follow it.

## Fix

`src/core/ChordTrack.cpp` — parse with the two-argument `toInt(&trackIdParsed)` and
only call `setFollow` when the conversion succeeded; a malformed follower ID is
skipped, its neighbours still read. The missing-attribute default `"-1"` still
parses to -1 and is rejected by `setFollow`, so behaviour is otherwise unchanged.

## Regression

New test `ChordTrackTest::malformedFollowerTrackIdsAreSkipped` in the existing
`tests/src/core/ChordTrackTest.cpp`: loads `<follow track="garbage" mode="scale"/>`
and asserts no follower is created (`followOf(0) == Off`), plus a mixed document
where a malformed follower sits between two valid ones.

## Commands and exit codes (logs in the run's log folder)

RED on the original code:

```
cmake --build build --target ChordTrackTest                       # build-1.log  EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R ChordTrackTest --output-on-failure   # test-1.log  EXIT=8
```

The failing assertion recorded in test-1.log:

```
FAIL!  : ChordTrackTest::malformedFollowerTrackIdsAreSkipped() 'track.followers().empty()' returned FALSE. (a malformed follower track ID became track 0)
```

GREEN after the fix:

```
cmake --build build --target ChordTrackTest zene                  # build-2.log  EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R ChordTrackTest --output-on-failure   # test-2.log  EXIT=0  (1/1 passed)
```

Affected family (all Chord tests; the four siblings had no binaries yet, so they
were built first — an intermediate family run, test-3.log, showed them "Not Run"
with EXIT=8 before that build):

```
cmake --build build --target ChordDetectTest ChordProgressionTest ControlChordCommandsTest ControlChordWriteTest   # build-3.log EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R Chord --output-on-failure      # test-4.log  EXIT=0  (5/5 passed)
```

Gates:

```
bash tests/complexity-gate.sh --check                             # gate-1.log  EXIT=0
bash tests/file-length-gate.sh --check                            # gate-1.log  EXIT=0
```

## Files committed

- `src/core/ChordTrack.cpp` (already registered in `tests/fork-sources.txt` line 1066 — no registry change)
- `tests/src/core/ChordTrackTest.cpp` (existing test file extended)
- `docs/qwen-fixes-2026-10/cluster-new-p8-279.md` (this report)
