# Cluster new-p12-216: FALSE-POSITIVE

## Verdict

The "merged gap shifts audio out of time" finding describes a documented design
trade-off, not a bug. The gap table's class comment, the commit that introduced
this class, and an existing test all assert exactly the behavior the finding
calls buggy. No production change was made.

## Evidence

The behaviour the finding calls a bug is the documented contract.

- [include/SampleRecordAccumulator.h:67-73](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-216/include/SampleRecordAccumulator.h:67)
  is the gap table's class comment, which spells out the trade-off in plain
  language:

  > "The newest record stays open: a loss that finds the table full is added to
  > it, so no frame is ever uncounted, and the take keeps its length. ... A
  > merged loss sits at the newest record's position: when the table is full
  > and frames were staged after that record, the padding lands earlier than
  > the loss by those frames (only after the drain has stalled for that many
  > records)."

  The comment names the exact symptom the finding describes ("the padding
  lands earlier than the loss by those frames") and presents it as the price
  the design pays for a bounded, lock-free, allocation-free audio-thread
  producer. The first sentence states the invariant the design preserves
  ("no frame is ever uncounted, and the take keeps its length").

- [include/SampleRecordAccumulator.h:97-99](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-216/include/SampleRecordAccumulator.h:97)
  is the code the finding points at: the audio-thread side of the merge.

- [include/SampleRecordAccumulator.h:265-278](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-216/include/SampleRecordAccumulator.h:265)
  is the drain-thread side: the gap is padded at `gap.position + framesPadded`,
  which is exactly the index that the class comment says the padding lands at.

- Commit `ff37cf254` ("fix(recording): a full gap table merges the loss into its
  newest record, so the take keeps its length", cluster 5312 / new-p8-208)
  introduced the `SampleRecordGaps` class with this exact comment and code.
  The body of that commit message describes it as the design:

  > "When it is full, a loss merges into the newest record, which stays open
  > until a newer record exists or the take finishes, so the consumer never
  > reads a record that can still grow. The drain pads each gap at its
  > position, so a take's length and timing are kept below the table limit."

- [tests/src/core/RecordingRealtimeTest.cpp:641-660](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-216/tests/src/core/RecordingRealtimeTest.cpp:641)
  asserts the behaviour the finding calls a bug. It pushes 300 losses into the
  default 256-record table, then drains with `finishing=true`. It checks
  `gap->position == records < 256 ? records : std::uint64_t{255}` for every
  record (the merged overflow records all sit at position 255, with the
  original positions lost), and `lastLength == 45` (1 original + 44 merged).
  The comment on that assertion line is the label "timing kept".

- [tests/src/core/RecordingRealtimeTest.cpp:662-693](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-216/tests/src/core/RecordingRealtimeTest.cpp:662)
  goes end-to-end through the accumulator with a 2-slot gap table and 200
  overflow bursts, and asserts that `take->size() == offered` and that the
  silent-frame count equals `framesDropped`. That is the "no frame is ever
  uncounted, and the take keeps its length" contract, end-to-end.

The finding suggests two alternative fixes ("preserve each loss's position"
or "use a bounded overflow representation that retains intervening staged-frame
counts"). Both would force the audio thread to either allocate or expose a
slot count that grows with the number of in-flight gaps. The header's own
class comment [SampleRecordAccumulator.h:140-163](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-216/include/SampleRecordAccumulator.h:140)
sets the RT contract that prevents this: "the audio thread only calls
`append()`, which is a store into a fixed-capacity, pre-allocated ring
... no allocation, no locking, no syscalls, no unbounded growth". The merge
in `note()` is the simplest design that keeps the audio thread within that
contract.

A real bug would be one that breaks the documented invariant
("no frame is ever uncounted, and the take keeps its length") or that breaks
the RT contract (allocation, lock, blocking, growth). Neither is the case here:
the test that goes end-to-end through the overflow path keeps every dropped
frame as silence in the take, and `note()` is a fixed-time, allocation-free
fetch_add. The position inaccuracy the finding describes is the documented
price of staying within the contract, and the existing tests assert it.

## Validation evidence

No code change was made; the table below records the checks that confirm the
finding is not real on the current branch.

| Check | Exact command | Exit / result |
|---|---|---|
| Build the affected test target | `cmake --build build --target zene RecordingRealtimeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-216.r1/logs/build-1.log 2>&1; echo EXIT=$?` | `EXIT=0` |
| Run the affected test family | `ctest -R '^RecordingRealtimeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-216.r1/logs/test-1.log 2>&1; echo EXIT=$?` | `EXIT=0`, `100% tests passed out of 1` |
| Family size | the same `ctest -R '^RecordingRealtimeTest$'` invocation | 1 test executable, all of its cases passed (the two overflow-path cases above are inside it) |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-216.r1/logs/gate-complexity-1.log 2>&1; echo EXIT=$?` | `EXIT=1` (pre-existing regressions in unrelated files: `tests/src/gui/XyPadPairTest.cpp:76`, `src/core/DawProjectReadTracks.cpp:94`, `src/core/ControlCommandsMixerTools.cpp:215`, `src/core/ControlCommandsMixerStrip.cpp:250`, `src/core/ProjectRevisions.cpp:158`, `src/gui/ControlCommandsShortcuts.cpp:168`. None of them are `SampleRecordAccumulator.h`. The gate cannot move from this report because it is a check-only run.) |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-216.r1/logs/gate-file-length-1.log 2>&1; echo EXIT=$?` | `EXIT=1` (pre-existing regressions in unrelated files: `src/core/SmfInterchangeReader.cpp`, `src/core/TakeLane.cpp`, and a list of `tests/src/...` test files. None of them is `SampleRecordAccumulator.h`. Same check-only constraint as the complexity gate.) |

Both gates are in `--check` mode (the brief says "Never re-anchor a gate");
their exit codes here reflect the state of the branch before this run and
are not caused by anything this report did.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p12-216.md` (this report)