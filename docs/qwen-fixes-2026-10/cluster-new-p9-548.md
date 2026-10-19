# Cluster new-p9-548: Profiling writes to disk once per audio period

## Verdict
**FIXED**

## Summary
Round 4 (Luna rework r3). The bounded queue from rounds 1-2 keeps the formatting and the
disk I/O out of `finishPeriod()`. What r3 found was still wrong: `QFile` was a MEMBER of
`AudioEngineProfiler`, so the object was constructed on the thread that creates the profiler
and destroyed with it, while `writerLoop()` - a different thread - called `setFileName()`,
`open()`, `write()` and `close()` on it. `QFile` is a `QObject`, and Qt does not support
using a `QObject` from a thread other than the one it lives in.

The fix: the file object is now a **stack-local of the writer thread**. It is created,
used and destroyed inside `writerLoop()`, so it belongs to that thread alone; the profiler
itself owns no file type at all.

* `include/AudioEngineProfiler.h:108` - `QString m_outputFileName` is the only file-related
  member left; the `QFile m_outputFile` member is gone and the header no longer includes
  the file type.
* `src/core/AudioEngineProfiler.cpp:83` - `QFile outputFile;` is a local of `writerLoop()`;
  open (`:87`), write (`:103`) and close (`:122`) all use that local, so the object is
  created, used and destroyed on the writer thread.

## Evidence

| Check | Command | Exit |
|---|---|---:|
| RED - new regression on the unfixed product code | `cmake --build build --target AudioEngineProfilerTest -j12` then `ctest -R '^AudioEngineProfilerTest$' --output-on-failure` (from `build/tests`) | 0 / **8** (1 test, 1 failed) |
| GREEN - after the fix | `cmake --build build --target zene AudioEngineProfilerTest -j12` then `ctest -R '^AudioEngineProfilerTest$' --output-on-failure` | 0 / **0** (1/1 passed) |
| Proof - fix reverted | `git apply -R logs/fix.patch`; rebuild; `ctest -R '^AudioEngineProfilerTest$' --output-on-failure` | 0 / 0 / **8** |
| Proof - fix restored | `git apply logs/fix.patch`; rebuild; `ctest -R '^AudioEngineProfilerTest$' --output-on-failure` | 0 / 0 / **0** (1/1 passed) |
| Affected-family tests | `ctest -R '^(AudioEngineProfilerTest\|AllocationProbeTest\|AudioEngineTeardownTest)$' --output-on-failure` | **0** (3/3 passed) |
| Realtime sweep | `python3 tests/rt-safety-sweep.py --check` | **0** |
| Complexity gate (check mode) | `bash tests/complexity-gate.sh --check` | **1** (pre-existing train violations - identical set before and after this change, none in these files) |
| File-length gate (check mode) | `bash tests/file-length-gate.sh --check` | **1** (pre-existing train violations - identical set before and after this change, none in these files) |
| Feature gate of record | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** (`RESULT: PASS`, 8/8 rows PASS) |
| Binary smoke | `QT_QPA_PLATFORM=offscreen build/zene --version` | **0** |

Gate logs: `logs/gate-complexity-baseline.log`, `logs/gate-complexity-final.log`,
`logs/gate-filelength-baseline.log`, `logs/gate-filelength-final.log`,
`logs/feature-check.log` (before the manifest fix), `logs/feature-check-2.log` (after).

Both check-mode gates are red on the train on purpose. The diff between the before and
after logs is empty for the `REGRESSION:` lines, i.e. this change adds no gate item.

## Files changed
| File | Change |
|---|---|
| `include/AudioEngineProfiler.h` | `QFile m_outputFile` member removed; `<QFile>` include replaced by `<QString>` (the header uses `QString`, it no longer needs the file type) |
| `src/core/AudioEngineProfiler.cpp` | `<QFile>` included here; `writerLoop()` creates, uses and closes a **stack-local** `QFile`; the constructor no longer initialises a file member |
| `src/core/BandLimitedWave.cpp` | includes `<QFile>` itself - it used the copy `AudioEngineProfiler.h` dragged in, and that header no longer carries the file type |
| `tests/src/core/AudioEngineProfilerTest.cpp` | regression extended: the file object must be created on the writer thread; the behavioural test lost its sleep |
| `tests/upstream-modifications.txt` | ledger entries for the three upstream files above |
| `tests/all-sources.txt` | whole-tree scope manifest re-derived (the test file added in round 1 was missing from it, which is what `fork-sources` / `all-sources-reproduce` reported) |

## What the regression proves
`tests/src/core/AudioEngineProfilerTest.cpp` - `outputFileObjectIsCreatedOnTheWriterThread()`:

1. `include/AudioEngineProfiler.h` must not mention the file type at all - a member would
   be constructed on the profiler's creating thread and destroyed with the profiler;
2. every use of the file type in `src/core/AudioEngineProfiler.cpp` must sit inside
   `writerLoop()` (the `#include` line excepted) - that is the only place where the object
   may be created;
3. `writerLoop()` must contain the open/write/close of that object.

This is the check r3 asked for: it fails on a `QFile` that is still constructed outside the
writer thread. On the pre-fix tree it fails with

```
FAIL!  : AudioEngineProfilerTest::outputFileObjectIsCreatedOnTheWriterThread()
'header.find( "QFile" ) == std::string::npos' returned FALSE.
(the profiler still declares a QFile: a member is constructed on the profiler's creating
thread and destroyed with it, while the writer thread uses it - the file object must be a
local of writerLoop())
```

## RED/GREEN proof (the exact runs)
```
$ git diff -- include/AudioEngineProfiler.h src/core/AudioEngineProfiler.cpp > logs/fix.patch
$ git apply -R logs/fix.patch                                   # EXIT 0
$ cmake --build build --target zene AudioEngineProfilerTest -j12 # EXIT 0
$ cd build/tests && ctest -R '^AudioEngineProfilerTest$' --output-on-failure   # EXIT 8
    FAIL!  : AudioEngineProfilerTest::outputFileObjectIsCreatedOnTheWriterThread()
$ git apply logs/fix.patch                                      # EXIT 0
$ cmake --build build --target zene AudioEngineProfilerTest -j12 # EXIT 0
$ cd build/tests && ctest -R '^AudioEngineProfilerTest$' --output-on-failure   # EXIT 0
    100% tests passed out of 1
```
`logs/fix.patch` holds the two product files of the fix. `src/core/BandLimitedWave.cpp` is
a compile-only consequence of the header change (it now includes `<QFile>` itself); it is
declared in the ledger and was left in place during the revert so that the tree still
compiles and the RED is a test failure, not a build failure.

## Self-check

### ? The regression test FAILS with your product change reverted and PASSES with it (exit codes).
Reverted: `git apply -R logs/fix.patch` (EXIT 0), rebuild (EXIT 0),
`ctest -R '^AudioEngineProfilerTest$' --output-on-failure` -> **EXIT 8**, 1 test, 1 failed
(`outputFileObjectIsCreatedOnTheWriterThread`). Restored: `git apply logs/fix.patch`
(EXIT 0), rebuild (EXIT 0), same ctest -> **EXIT 0**, 1/1 passed. The same RED was
recorded before the fix was written (`logs/test-red-1.log`, EXIT 8).

### ? The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).
The regression reads two files as text - `../../include/AudioEngineProfiler.h` and
`../../src/core/AudioEngineProfiler.cpp` - from the working directory ctest uses
(`build/tests`); the injected value is the source text of the two files, nothing else.
No clock, no sleep, no polling, no thread rendezvous. The behavioural slot
`writesMeasurementToOutputFile()` injects the sample rate and the period size
(`finishPeriod(48000, 256)`) instead of measuring time, and uses the profiler's
destructor as its synchronisation point: the destructor joins the writer thread, and the
writer drains the queue and closes the file before it exits, so the file is complete when
the scope ends. The `QThread::msleep(50)` of the earlier revision was removed for that
reason.

### ? The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.
* Callers: `AudioEngineProfiler::setOutputFile()` has exactly one caller in the tree,
  `src/core/main.cpp:1428` (the `--profiler` output file). Its signature and behaviour are
  unchanged - it stores the name and starts the writer thread.
* Siblings of the removed member: the header declares no other file type; `m_outputFileName`
  (`include/AudioEngineProfiler.h:108`) is still the only file-related state and is written
  by `setOutputFile()` before the writer thread is started and read by `writerLoop()` after,
  so the thread creation still gives the happens-before.
* The header lost `<QFile>`, which used to drag in `QString`, `QIODevice` and `QFileDevice`
  for the whole tree. `QString` is now included explicitly by the header. Every source in
  `build/compile_commands.json` that uses `QFile`, `QIODevice` or `QFileDevice` without
  including it was compiled syntax-only after the change (30 + 215 candidates): the only
  file that lost the transitive include was `src/core/BandLimitedWave.cpp`, which now
  includes `<QFile>` itself. The `zene` target and the test targets build clean.
* The audio-thread path is untouched by this round: `finishPeriod()` still only fills the
  bounded queue (`src/core/AudioEngineProfiler.cpp:45`).

### ? No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.
Nothing in this change touches project or session data: no save/load code, no schema, no
file layout, no migration. The only behavioural difference is where the `QFile` object
lives - a thread-ownership detail with the same bytes written to the same path. The bounded
queue, its capacity (`BufferCapacity = 4096`) and its overflow behaviour are unchanged from
the earlier rounds, so no caller can hit a new limit. The new checks are source-text gates
scoped to the two profiler files; they constrain no other caller.

### ? If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.
Not applicable - the verdict is FIXED. The defect was real and is now closed:
`include/AudioEngineProfiler.h` has no `QFile` member (the pre-fix line was
`QFile m_outputFile;`), and `src/core/AudioEngineProfiler.cpp:83` creates the file object
inside `writerLoop()`, the thread that opens, writes and closes it.

## Notes for the next reviewer
* `tests/all-sources.txt` was re-derived because round 1 added a test file without adding it
  to the whole-tree scope manifest; `feature_check.py` reported `fork-sources` and
  `all-sources-reproduce` as FAIL until the manifest was regenerated from its own recipe
  (`logs/feature-check.log` -> FAIL, `logs/feature-check-2.log` -> PASS). The recipe's
  output differs from the old manifest by exactly one path.
* The two check-mode gates stay red on the train for files this cluster does not touch; the
  regression sets before and after this change are identical.
