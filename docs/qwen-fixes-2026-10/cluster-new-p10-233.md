# Cluster new-p10-233 — FIXED

## Finding and change

The finding was real. `OutMessage` previously held only a frame offset, so a deferred message could be sorted after a frame-0 message from a later callback cycle. The queue item now carries a monotonic source-cycle ID. The bounded drain orders messages by source cycle and then frame; `JackMidiWrite()` sends messages from earlier cycles at frame 0 and advances the ID once per callback.

Evidence:

- `include/MidiJackOutSchedule.h:44-49` adds the cycle ID; `:59-75` orders by cycle and frame; `:87-90` maps overdue messages to frame 0.
- `include/MidiJack.h:111` owns the atomic cycle ID.
- `src/core/midi/MidiJack.cpp:234` stamps queued messages; `:246-269` schedules them and advances the cycle ID.
- `tests/src/core/MidiJackOutScheduleTest.cpp:109-123` covers a deferred frame-320 message followed by a later-cycle frame-0 message.

## Regression and build evidence

The focused family contains **1 test** (`MidiJackOutScheduleTest`, ctest #224).

Log directory: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-233.r1/logs/`.

| Phase | Exact command | Exit | Evidence |
|---|---|---:|---|
| Original RED build | `cmake --build build --target MidiJackOutScheduleTest` | 0 | `logs/build-1.log` |
| Original RED test, from `build/tests` | `ctest -R '^MidiJackOutScheduleTest$' --output-on-failure` | 8 | `logs/test-1.log`: key 63 appeared before expected key 62 |
| GREEN build | `cmake --build build --target zene MidiJackOutScheduleTest` | 0 | `logs/build-2.log` |
| GREEN test, from `build/tests` | `ctest -R '^MidiJackOutScheduleTest$' --output-on-failure` | 0 | `logs/test-2.log`: 1/1 passed |

The required inverse-patch proof used `logs/fix.patch`, generated from only the three product files:

| Proof step | Exact command | Exit | Evidence |
|---|---|---:|---|
| Capture product patch | `git diff -- include/MidiJackOutSchedule.h include/MidiJack.h src/core/midi/MidiJack.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-233.r1/logs/fix.patch` | 0 | `logs/proof-1.log` |
| Remove fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-233.r1/logs/fix.patch` | 0 | `logs/proof-1.log` |
| Rebuild without fix | `cmake --build build --target zene MidiJackOutScheduleTest` | 0 | `logs/build-3.log` |
| Re-run regression without fix, from `build/tests` | `ctest -R '^MidiJackOutScheduleTest$' --output-on-failure` | 8 | `logs/test-3.log`: same key 63 vs key 62 failure |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-233.r1/logs/fix.patch` | 0 | `logs/proof-2.log` |
| Rebuild with fix | `cmake --build build --target zene MidiJackOutScheduleTest` | 0 | `logs/build-4.log` |
| Re-run regression with fix, from `build/tests` | `ctest -R '^MidiJackOutScheduleTest$' --output-on-failure` | 0 | `logs/test-4.log`: 1/1 passed |
| Final dependent rebuild after header comment update | `cmake --build build --target zene MidiJackOutScheduleTest` | 0 | `logs/build-5.log`; includes `MidiJack.cpp` |
| Final focused test, from `build/tests` | `ctest -R '^MidiJackOutScheduleTest$' --output-on-failure` | 0 | `logs/test-5.log`: 1/1 passed |

## Ratchets

Both required checks were run after the final source edit. They exit 1 on unchanged files elsewhere in the branch; neither log names a file changed for this fix. The ratchets and their baselines were left unchanged.

- `bash tests/complexity-gate.sh --check` — **EXIT=1**, `logs/gate-5.log`; it lists six unrelated functions over the CCN target, including `dotCentreAlong` and `parseNotes`.
- `bash tests/file-length-gate.sh --check` — **EXIT=1**, `logs/gate-6.log`; it flags unrelated unchanged sources over 500 lines, including `SmfInterchangeReader.cpp` and `ArrangerTimelineTest.cpp`.

## Files in this commit

- `include/MidiJack.h`
- `include/MidiJackOutSchedule.h`
- `src/core/midi/MidiJack.cpp`
- `tests/src/core/MidiJackOutScheduleTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-233.md`
