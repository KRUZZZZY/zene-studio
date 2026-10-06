# Core batch-28 — Luna findings 3417, 2440, 5254, 5275, 4374, 5276

Worktree `zene-bunny-core`, branch `bugs/bunny-core-2026-10-05`. Every verdict below was
decided from the CURRENT tree; the line numbers in the reports came from older revisions.

### Cluster 3417 — Incoming messages can run Qt updates and logging on the audio thread

Verdict: SKIPPED-SCOPE

Evidence: `src/core/RemotePlugin.cpp:414` (inside `process()`, line 398) pumps pending messages on the
audio thread, and `processMessage()` (`src/core/RemotePlugin.cpp:580-651`) does call the ports
model (`setChannelCounts`, line 646) and `qCritical`/`fprintf` (lines 620-622, 651).
The finding is real as a description of where the work happens, but the proposed fix — defer the
model change and the logs to a non-audio thread — is a threading redesign: it needs a lock-free
message queue with a bounded, allocation-free enqueue on the audio side and a consumer that owns
the model writes. That is far beyond a behaviour-preserving minimal change and would put
allocation and/or locking on the audio path, which rule 6 forbids. The code was left alone.

Commit: none

Test: none

Red/green: none

### Cluster 2440 — Failed remote-client connections are recorded as starts

Verdict: FIXED

Evidence: `init()` ran `oop::HostTracker::noteStarted()` unconditionally after the
poll/accept switch, so a poll timeout (`case 0`) or an `accept()` failure still counted a start —
and `starts` is what `oop.*` reports and what the crash-loop refusal is measured against. The
accept result now sets a local `connected` flag and the record is made only on a successful
accept (`src/core/RemotePlugin.cpp:307-352`); the shm-fifo transport, which has no accept, stays
unconditional.

Commit: 9512bd1d3

Test: RemotePluginAudioPortsTest::clientThatNeverConnectsIsNotRecordedAsAStart()

Red/green:
- Red: `cmake --build build --target zene RemotePluginAudioPortsTest` EXIT=0; `cd build/tests && ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure` EXIT=8 — `FAIL! ... Actual (tracker.record(client).starts): 1, Expected (0)` (the stand-in peer was given a new `no-connect` mode: it launches and exits without ever reaching the socket, so the poll times out).
- Green: same build EXIT=0; `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure` EXIT=0. `ctest -R '^(RemotePluginClientE2ETest|OutOfProcessHostTest)$' --output-on-failure` EXIT=0. `bash tests/complexity-gate.sh --check` EXIT=0; `bash tests/file-length-gate.sh --check` EXIT=0.

### Cluster 5254 — Unvalidated channel counts wrap and can trigger huge allocations

Verdict: FIXED

Evidence: `processMessage()` cast the signed wire ints of `IdChangeInputOutputCount` straight to
the unsigned `ch_cnt_t` (`std::uint16_t`, `include/LmmsTypes.h:44`), and
`AudioPortsModel::setProcessorChannelCountsImpl()` (`src/core/AudioPortsModel.cpp:168-197`) has
no upper bound, so `-1` became 65535 and the shared block was sized from it (red evidence below
shows 65528 for `-8`). Counts outside `[0, MaxChannelsPerAudioBuffer]` are now refused with a
warning at the wire boundary and leave the counts and the allocation untouched
(`src/core/RemotePlugin.cpp:627-648`). The second site the report names,
`plugins/VstBase/RemoteVstPlugin.cpp` (a VST reporting a negative `numInputs`), is the client
half of the same cast and is outside this lane's core scope.

Commit: 2fe70d746

Test: RemotePluginAudioPortsTest::outOfRangeChannelCountMessagesAreRefused()

Red/green:
- Red: `cmake --build build --target zene RemotePluginAudioPortsTest` EXIT=0; `cd build/tests && ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure` EXIT=8 — `FAIL! ... Actual (ports.audioPortsModel().in().channelCount()): 65528, Expected (channelsIn): 2`.
- Green: same build EXIT=0; `ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure` EXIT=0. `ctest -R '^(RemotePluginClientE2ETest|OutOfProcessHostTest)$' --output-on-failure` EXIT=0. `bash tests/complexity-gate.sh --check` EXIT=0; `bash tests/file-length-gate.sh --check` EXIT=0.

### Cluster 5275 — Audio take writes can report success after a flush or close failure

Verdict: REAL-UNTESTED

Evidence: `writeWindow()` ignores both returns — `sf_write_sync(sf); sf_close(sf);` at
`src/core/RetroAudioCapture.cpp:174-175` — and then reports success from the value
`sf_writef_float()` returned, so a failing flush or close still yields `true` at
`src/core/RetroAudioCapture.cpp:177-193`. No deterministic headless failure could be produced:
the defect needs `sf_write_sync()`/`sf_close()` to fail while `sf_writef_float()` accepted every
frame, which means a failing device or a full filesystem mid-write; an `RLIMIT_FSIZE` trick
fails at `sf_writef_float()` instead (a short write, which the existing check at line 177 already
reports), and a read-only or unopenable destination fails at `sf_open()` (line 153), not at the
close. Per the procedure the product code was left unchanged rather than fixed untested.

Commit: none

Test: none

Red/green: none

### Cluster 4374 — Optional output pointers are dereferenced

Verdict: FALSE-POSITIVE

Evidence: the dereferences the report points at are in `writeWindow()`
(`src/core/RetroAudioCapture.cpp:156-157, 179-180, 191`), which is file-local and has exactly one
caller, `writeRetroAudioTake()` — and that caller passes the addresses of two locals, never
pointers it received (`src/core/RetroAudioCapture.cpp:211-213`). The public entry point
null-checks both optional outputs up front (`src/core/RetroAudioCapture.cpp:201-202`) and copies
the local results back under the same guards (`214-215`), so a caller passing `nullptr` for
either one is safe; the report's "a caller passes framesWritten == nullptr" case cannot reach an
unchecked dereference.

Commit: none

Test: none

Red/green: none

### Cluster 5276 — A failed rename can destroy an existing retrospective take

Verdict: REAL-UNTESTED

Evidence: `writeWindow()` removes the destination (`QFile::remove(filePath)`,
`src/core/RetroAudioCapture.cpp:184`) before renaming the staged `.partial` over it (line 185), so
a rename failure at line 185-190 is reported after the old take is already gone. The window
between the two calls is the defect, and it could not be exercised deterministically headlessly:
every way of making `QFile::rename()` fail (destination is a non-empty directory, parent not
writable, source missing) makes the preceding `QFile::remove()` fail as well, so the old take
survives and the test would be green against the unfixed code. Per the procedure the product code
was left unchanged rather than fixed untested.

Commit: none

Test: none

Red/green: none

Leftovers: cluster 5275 and 5276 are real defects in `writeWindow()` that this lane did not
change — both need fault injection at the filesystem level (a failing flush/close, or a rename
failure after a successful remove) that no headless test can force; cluster 3417 needs a
threading redesign deferred off the audio thread, and its VST-side counterpart of cluster 5254
lives in `plugins/VstBase/RemoteVstPlugin.cpp`, outside this lane's core scope.