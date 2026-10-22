# Cluster 3417-port — Incoming messages can run Qt updates and logging on the audio thread

ALREADY-FIXED

The defect the finding describes does not reproduce on this line: the deferral of the Qt
model update and of the logging is already in the tree (the evidence is below, with the
file:line of every site). What the accepted branch `bugs/qwen-3417` added on top of it -
and what this port re-applies by hand, because the branch could not merge - is:

1. the regression that pins the **LOGGING** half of the finding (the model-update half was
   already covered by `channelCountRequestDuringPeriodIsAppliedOnTheMainThread()`), and
2. the last **allocation** in the audio-side handler: the answer to a client message was
   built as a `message`, and a message's vector of arguments allocates. The finding's fix
   asks for an audio-side handler that is "bounded and allocation-free"; the parse was
   already bounded here, the answer was not.

Part 2 is a product fix and is FIXED by this port. Part 1 is a regression, not a behaviour
change: the deferral it pins is the tree's, unchanged.

## Evidence — the deferral is in the tree (this line)

The audio thread reads the client's messages while it waits for a period's reply
(`RemotePlugin::collectProcessingReplies()`, `src/core/RemotePlugin.cpp:641`), and every
message goes through `RemotePlugin::processMessage()` (`src/core/RemotePlugin.cpp:1068`).
On that path, in this tree:

| what the finding names | where it is handled now | evidence |
|---|---|---|
| `setChannelCounts()` on the audio thread | the counts are only *recorded* during a period (`m_pendingInCount`, `m_pendingOutCount`, `m_countChangePending`); the model is changed later, on the main thread | `src/core/RemotePlugin.cpp:1158-1167`, applied in `applyDeferredClientRequests()` at `src/core/RemotePlugin.cpp:701-709` |
| `fprintf`/`qCritical` on the audio thread | a debug text is copied into a fixed slot (`m_pendingDebug`, 256 bytes) during the period; the retired id and the arity refusal are recorded the same way | `src/core/RemotePlugin.cpp:1175-1192` (debug text), `:1122-1128` (retired id), `:1051-1057` (arity refusal) |
| the logging itself | `reportDeferredClientRequests()` prints the recorded text, and `applyDeferredClientRequests()` applies the recorded change - both on the main thread, driven by the 10 ms timer | `src/core/RemotePlugin.cpp:682-712` and `:717-754` |
| an unbounded parse on the audio thread | the frame is read *bounded*: the arguments are views into a preallocated frame buffer, no string and no vector is built | `src/core/RemotePlugin.cpp:661` (`receiveFrameBefore(slice, m, true)`), `include/RemotePluginBase.h:576-584` and `:639-671` |

So the reported symptom is fixed on this line; the port's job was to carry the missing
regression and to close the remaining allocation.

## The fix (this port)

`src/core/RemotePlugin.cpp` — the answer to a client message is built as plain values
(`replyValue`, `replyArgumentCount`) instead of a `message`:

* `src/core/RemotePlugin.cpp:1074-1075` — the message is gone from the top of
  `processMessage()`; the answer is two plain ints.
* `src/core/RemotePlugin.cpp:1094-1104` — `IdSampleRateInformation` and
  `IdBufferSizeInformation` (the only two ids that are answered) set those values.
* `src/core/RemotePlugin.cpp:1213-1220` — on the audio thread the answer is written with
  `frameFromInts()`, which puts the decimal digits into a stack buffer: no allocation, no
  free.
* `src/core/RemotePlugin.cpp:1229-1231` — the main thread's path is unchanged in
  behaviour: it still builds a `message` and sends it with `sendMessage()`.

The bytes on the wire are identical: `frameFromInts()` writes the same length-prefixed
decimal argument `message::addInt()` + `sendMessage()` write (its own comment says so,
`src/common/RemotePluginBase.cpp:102-127`), and the id and the argument count are the
same. The client cannot tell the difference - the handshake that asks for the buffer size
(`IdBufferSizeInformation`, answered on the main thread) is unchanged, and the whole
remote-plugin family still passes.

## The regressions (this port)

`tests/src/plugins/FakeRemotePluginClient.cpp` — a new `"debug"` mode (`:132-137`, `:487-497`,
`:677`): every period the client answers with a debug text of 200 bytes (starting with
`zene-remote-debug-marker`) **and** a sample-rate request before its reply, so the host
reads a client message that carries an argument - and has to answer it - while it waits
for the period's reply, i.e. on the audio thread.

`tests/src/core/RemotePluginClientE2ETest.cpp`:

* `StderrCapture` (`:190-227`) - captures stderr into a file, so a test can see *when* the
  host printed the text.
* `debugTextDuringPeriodIsPrintedOnTheMainThread()` (`:1448`) - the text is **not** printed
  while the audio thread is inside the period, and **is** printed afterwards, on the main
  thread.
* `clientMessageDuringPeriodIsParsedWithoutAllocating()` (`:1490`) - handling the client's
  message during a period allocates nothing and frees nothing on that thread (the
  allocation probe of `tests/src/core/AllocationProbe.h`).

## The commands and their exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3417-port.r1/logs`.

### RED - the product fix reverse-applied, the tests untouched

```
git diff -- src/core/RemotePlugin.cpp > $LOGS/fix.patch
git apply -R $LOGS/fix.patch
cmake --build build --target RemotePluginClientE2ETest -j 8 > $LOGS/build-red-1.log 2>&1     # EXIT=0
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^RemotePluginClientE2ETest$' --output-on-failure > $LOGS/test-red-1.log 2>&1    # EXIT=8
```

```
FAIL!  : RemotePluginClientE2ETest::clientMessageDuringPeriodIsParsedWithoutAllocating() Compared values are not the same
   Actual   (allocations)     : 3
   Expected (std::uint64_t{0}): 0
Totals: 32 passed, 1 failed, 0 skipped, 0 blacklisted, 11345ms
```

Three allocations in three periods - one per period: the vector of the answer's
arguments, allocated and freed on the audio thread.

### GREEN - the fix applied again

```
git apply $LOGS/fix.patch
cmake --build build --target RemotePluginClientE2ETest -j 8 > $LOGS/build-green-1.log 2>&1     # EXIT=0
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^RemotePluginClientE2ETest$' --output-on-failure > $LOGS/test-green-1.log 2>&1    # EXIT=0
cd build/tests && QT_QPA_PLATFORM=offscreen ./RemotePluginClientE2ETest > $LOGS/test-green-2.log 2>&1    # EXIT=0
```

```
Totals: 33 passed, 0 failed, 0 skipped, 0 blacklisted, 11330ms
```

### RED - the deferral of the logging removed (the half this port pins)

`$LOGS/unfix-3417-logging.patch` takes the deferral out of `processMessage()`'s
`IdDebugMessage` case (the text is printed right there, on the audio thread). It is a
temporary artefact, not part of the commit.

```
build > $LOGS/build-red-2.log 2>&1        # EXIT=0
./RemotePluginClientE2ETest > $LOGS/test-red-2.log 2>&1    # EXIT=2
```

```
FAIL!  : RemotePluginClientE2ETest::debugTextDuringPeriodIsPrintedOnTheMainThread() '!capture.text().contains(QStringLiteral("zene-remote-debug-marker"))' returned FALSE. (the client's debug text was printed while the audio thread was inside the period)
FAIL!  : RemotePluginClientE2ETest::clientMessageDuringPeriodIsParsedWithoutAllocating() Compared values are not the same
Totals: 31 passed, 2 failed, 0 skipped, 0 blacklisted, 11366ms
```

The patch was reverted again and the suite is green: `build-green-2.log` (EXIT=0),
`test-green-3.log` (EXIT=0) - `Totals: 33 passed, 0 failed, 0 skipped, 0 blacklisted`.

### The family

```
cmake --build build --target zene RemotePluginAudioPortsTest RemotePluginShmFifoTest RemotePluginClientE2ETest -j 8 > $LOGS/build-2.log 2>&1    # EXIT=0
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R 'RemotePlugin' --output-on-failure > $LOGS/test-family-1.log 2>&1    # EXIT=0
```

```
1/3 Test #201: RemotePluginAudioPortsTest .......   Passed   43.89 sec
2/3 Test #202: RemotePluginClientE2ETest ........   Passed   26.50 sec
3/3 Test #537: RemotePluginShmFifoTest ..........   Passed    2.40 sec
100% tests passed out of 3
```

### Gates

```
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > $LOGS/gates-2.log 2>&1    # EXIT=0
```

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

The first gate run (`$LOGS/gates-1.log`, EXIT=1) was red on `fork-sources` and
`all-sources-reproduce`. Both failures are about `tests/all-sources.txt`, a file this port
did not touch: the manifest still listed the vendored `plugins/SfzPlayer/sfizz/**` tree
(1148 paths) that its own header recipe excludes, so the manifest did not reproduce from
its recipe. The proof that this predates the job: the base commit's own manifest fails
the same check -

```
git show dbb15dded:tests/all-sources.txt > /tmp/base-all-sources.txt
bash tests/all-sources-reproduce.sh /tmp/base-all-sources.txt    # EXIT=1
```

- and this port adds and removes no tracked file. The manifest was therefore re-derived
from its own header recipe, as the gate prescribes ("run the 'Regenerate with' command
from the manifest's own header and write its output over the entry list"). The measured
scope does not change: the gate excludes the same vendored trees when it scans
(`tests/fork-sources-gate.sh`, `is_excluded()`), so those paths were never measured.
No gate limit and no baseline was changed: `tests/fork-sources.txt` is not touched by
this port at all, so the fork scope (1968 fork-NEW entries) is exactly what it was, and
the paths the recipe drops were never scanned by the gate anyway
(`tests/fork-sources-gate.sh`, `is_excluded()`), so the measured scope does not change.

## Self-check

* **The regression test FAILS with your product change reverted and PASSES with it
  (exit codes).** `git apply -R fix.patch` -> ctest EXIT=8
  (`clientMessageDuringPeriodIsParsedWithoutAllocating`: 3 allocations, expected 0);
  `git apply fix.patch` -> ctest EXIT=0 and `33 passed, 0 failed`. The logging half has
  its own RED: with the deferral removed (`unfix-3417-logging.patch`) the suite is EXIT=2
  with `debugTextDuringPeriodIsPrintedOnTheMainThread()` failing; restored, EXIT=0.
* **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or
  injected values it uses).** The client is the project's own deterministic fixture
  (`FakeRemotePluginClient`): it sends a fixed 200-byte text and a fixed
  `IdSampleRateInformation` request in every period - no timing, no randomness. The
  allocation count comes from the allocation probe (`tests/src/core/AllocationProbe.h`),
  which counts the thread's `operator new`/`delete` exactly, not a sample. The stderr
  capture reads an unbuffered file, so what was written is visible at once. The only
  time-based construct is the standard "wait for a state" of
  `QTRY_VERIFY_WITH_TIMEOUT` (the deferred print happens on the plugin's 10 ms timer) and
  the period's deadline, which is the project's own seam
  (`RemotePlugin::setPeriodBudgetForTesting()`, used by `ScopedReplyBudget`) - no
  assertion depends on a duration.
* **The fix is at the cause and complete: every caller and sibling of what you changed
  was checked and stays consistent.** The answer to a client message is built in exactly
  one place (`RemotePlugin::processMessage()`); the two ids that are answered
  (`IdSampleRateInformation`, `IdBufferSizeInformation`) both use the plain-value path,
  and no other id sets `reply`. The audio thread writes the frame with
  `frameFromInts()`, the main thread still sends a `message` - the two paths were
  checked against each other and produce the same bytes. The frame the answer is written
  into is the same 512-byte stack buffer as before, and the "could not be sent" verdict
  (which fails the plugin and is reported on the main thread) is unchanged. The sibling
  path that also runs on the audio thread - the parse of the client's frame - was checked
  and needed no change: it is already bounded
  (`src/core/RemotePlugin.cpp:661`). `frameFromMessage()` is left in place; it is the
  base class' encoder and is no longer called by this handler.
* **No collateral damage: old projects and saved data still load as before, and any
  limit or check you added does not hit other callers that legitimately exceed it.** No
  file format, no saved data and no project file is touched by this port. No limit and
  no check was added. The client's protocol is byte-identical (see above), so a client of
  any build sees the same answers as before; the handshake
  (`IdBufferSizeInformation`, answered on the main thread) and the whole
  remote-plugin family are unchanged and green.
* **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the
  intended contract that shows it.** The table at the top of this report is the code:
  `src/core/RemotePlugin.cpp:1051-1057`, `:1122-1128`, `:1158-1167`, `:1175-1192` (the
  work is recorded instead of done), `:682-712` and `:717-754` (the main thread applies
  and prints it), `:661` and `include/RemotePluginBase.h:576-584`, `:639-671` (the parse
  is bounded). The one hole that was still open - the answer built as a `message` - is
  the fix of this port.

## Files

Committed:

* `src/core/RemotePlugin.cpp` - the answer to a client message is built as plain values.
* `tests/src/core/RemotePluginClientE2ETest.cpp` - the two regressions and the stderr
  capture.
* `tests/src/plugins/FakeRemotePluginClient.cpp` - the `"debug"` mode.
* `tests/upstream-modifications.txt` - the registration of the three changed files and of
  the manifest.
* `tests/all-sources.txt` - the whole-tree manifest re-derived from its own recipe (the
  pre-existing red of `fork-sources`/`all-sources-reproduce`; the scope does not change).
* `docs/qwen-fixes-2026-10/cluster-3417-port.md` - this report.
