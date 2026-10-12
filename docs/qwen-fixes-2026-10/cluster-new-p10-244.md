# Cluster new-p10-244

Verdict: **FIXED**

The finding is real. `MidiClientRaw::processOutEvent()` previously omitted
pitch bend, channel pressure, and program change, sending those event types to
the warning branch. The encoder now emits status plus LSB/MSB for pitch bend,
and status plus one data byte for each of the other two events
([`MidiClient.cpp:376`](../../src/core/midi/MidiClient.cpp:376)). The regression
asserts the exact bytes for all three events
([`MidiClockTest.cpp:525`](../../tests/src/core/MidiClockTest.cpp:525)). The
existing fork-source registration covers `MidiClockTest.cpp`; the reason for
the inherited production-file change is recorded in
[`upstream-modifications.txt:730`](../../tests/upstream-modifications.txt:730).

## Evidence

| Check | Exact command | Exit |
|---|---|---:|
| RED on original code, after rebuilding `zene` and `MidiClockTest` | `ctest -R '^MidiClockTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-244.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 8 |
| GREEN with the fix | `ctest -R '^MidiClockTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-244.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 0 |
| Proof RED after reversing only the product patch | `ctest -R '^MidiClockTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-244.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 8 |
| Proof GREEN after restoring the product patch | `ctest -R '^MidiClockTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-244.r1/logs/test-5.log 2>&1; echo EXIT=$?` | 0 |
| Affected family count | One registered CTest (`MidiClockTest`, test #70), containing 15 QtTest cases. The RED output records 14 passed and the new case failed; green CTest runs exited 0. | — |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-244.r1/logs/gate-3.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-244.r1/logs/gate-4.log 2>&1; echo EXIT=$?` | 0 |

## Build and proof commands

All build and test output is in the log folder shown above. The five builds
included `zene` before accepting RED results and after every product-patch
change.

```text
cmake --build build --target MidiClockTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-244.r1/logs/build-1.log 2>&1; echo EXIT=$?  # 0
cmake --build build --target zene MidiClockTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-244.r1/logs/build-2.log 2>&1; echo EXIT=$?  # 0
cmake --build build --target zene MidiClockTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-244.r1/logs/build-3.log 2>&1; echo EXIT=$?  # 0
git diff -- src/core/midi/MidiClient.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-244.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-244.r1/logs/fix.patch; echo EXIT=$?  # 0
cmake --build build --target zene MidiClockTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-244.r1/logs/build-4.log 2>&1; echo EXIT=$?  # 0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-244.r1/logs/fix.patch; echo EXIT=$?  # 0
cmake --build build --target zene MidiClockTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-244.r1/logs/build-5.log 2>&1; echo EXIT=$?  # 0
```

The initial `test-1.log` also recorded RED:

```text
ctest -R '^MidiClockTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-244.r1/logs/test-1.log 2>&1; echo EXIT=$?  # 8
```

The original-code failure was repeated after the explicit `zene` rebuild in
`test-2.log`.

## Files in the cluster commit

- `src/core/midi/MidiClient.cpp`
- `tests/src/core/MidiClockTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-244.md`
