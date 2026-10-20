# cluster review-e39984ffb

Verdict: FIXED

`record.remaining` gives the recording budget: the free space of the filesystem the armed routes
write into, at the rate they write. The budget is per FILESYSTEM, because the free bytes are the
filesystem's - two armed directories on one filesystem draw on one pool, two armed routes on two
filesystems draw on two.

## What was wrong

The branch base (06c5dcf48) already stopped grouping by directory, so the finding's first half
("separate totals overstate remaining duration") was gone: the base pools the routes of one
volume. What the base still got wrong was the IDENTITY of that volume key -
src/core/ControlCommandsRecordTools.cpp:156 (base):

    const QString volume = storage.device().isEmpty() ? storage.rootPath() : QString::fromUtf8(storage.device());

`QStorageInfo::device()` is the DEVICE LABEL, not the filesystem: every tmpfs mount answers
"tmpfs". Two independent filesystems that report the same label were therefore merged into one
budget, their routes pooled, and the answer halved - the remaining time was reported for a pool of
free space that does not exist. The reverse error (one filesystem split into two budgets, and the
answer overstated) is what the mount-point key (rootPath) makes: one filesystem mounted twice - a
bind mount, a second mount point - is two keys for one pool of free bytes, and each half of it
then claims the whole pool.

Fix: the budget is keyed by the device the kernel mounts the filesystem from (st_dev) -
src/core/ControlCommandsRecordTools.cpp:155-186, `groupByVolume()`:

- src/core/ControlCommandsRecordTools.cpp:166-171 - `::stat()` of the armed directory gives the
  filesystem's st_dev; that is the key (`dev:<st_dev>`).
- src/core/ControlCommandsRecordTools.cpp:172-176 - the fallback for Windows and for a stat that
  fails: the mount point (`path:<rootPath>`), as before.
- src/core/ControlCommandsRecordTools.cpp:177-183 - the routes of one filesystem are summed into
  one budget, and the free bytes are read once for it.
- src/core/ControlCommandsRecordTools.cpp:210-219 - `remaining()` divides each filesystem's free
  bytes by the routes writing into it and answers the smallest.

## Round 3 (Luna's rework: the second identity case was not covered)

Luna was right that the same-filesystem case did not catch the round-one rootPath() regression: Qt
RESOLVES a symlink, so a link and its target get the SAME rootPath() and the same case passes
against the rejected implementation. The report's claim that a link and its target are two
mount-point keys was wrong, and is corrected above and in the test's own comment.

What this round adds is the case that does catch it: ONE filesystem under TWO mount points, with
the take directories on that one filesystem (the kernel's st_dev matches) and on that one pool of
free bytes, but under two different mount points (QStorageInfo::rootPath() differs). The budget
belongs to the filesystem, so the pool is divided by both routes. The pair is made by the test
itself, in a mount namespace of its own (tests/src/core/MountNamespaceTestSupport.h), because a
host need not have one and a threaded process may not make a user namespace itself.

## Tests

tests/src/core/RecordToolsTest.cpp (the area's test file; it covers record.remaining):

- armedRoutesOnOneVolumeShareItsFreeSpace (pre-existing) - two directories on ONE filesystem share
  its free space: the volume lasts free / (2 routes * 3 bytes * rate). Still passes.
- armedRoutesOnIndependentMountsDoNotShare (new) - two independent mounts that answer the same
  device label (/tmp and /dev/shm, both tmpfs) are two budgets: the winning volume's free bytes
  last ONE route's rate, not half of it. This is the test that fails without the fix.
- armedRoutesOnSameFilesystemCoalesce (new) - two paths onto ONE filesystem (a directory and a link
  to it) are one budget: its free bytes last two routes' rate. Qt RESOLVES a link, so a link and
  its target are ONE mount point: this case is about the per-directory grouping, not about the
  mount-point key. (The earlier report called it a mount-point case; that was wrong - the
  mount-point case is the next one.)
- armedRoutesOnOneFilesystemUnderTwoMountPointsShareItsFreeSpace (new, round 3) - ONE filesystem
  under TWO mount points: the take directories are on the same filesystem (the kernel's st_dev
  matches) and on the same pool of free bytes, but Qt gives them different mount points
  (rootPath differs). The budget belongs to the filesystem, so both routes divide that one pool:
  the free bytes last two routes' rate. This is the case that fails against the rejected
  rootPath() implementation.
- cleanup() (new) - every test starts from a clean recorder, so a failing test cannot refuse the
  next test's arming.

The two-mount-points case needs a pair of paths on one filesystem under two mount points, and a
host need not have one. The test makes it: tests/src/core/MountNamespaceTestSupport.h (new) puts
the test process in a mount namespace of its own and binds a directory onto another there - the
namespace has to be made before the process has any thread (the kernel refuses a user namespace
for a threaded process), so the test's main() (tests/src/core/RecordToolsTest.cpp:603) makes it
before the application starts. The bind mount is made in the namespace, so it never reaches the
host, and the process keeps its own uid (the uid map is written by a child process, which is the
only identity the kernel still accepts it from).

## Commands (all from the worktree; logs in the run's log folder)

| what | command | exit |
|---|---|---|
| RED (fix reverted) | `git apply -R logs/fix.patch` (fix.patch = `git diff 06c5dcf48 -- src/core/ControlCommandsRecordTools.cpp`) | 0 |
| RED build | `cmake --build build --target RecordToolsTest zene -j$(nproc)` | 0 |
| RED test | `ctest -R RecordToolsTest --output-on-failure` | 8 |
| GREEN (fix restored) | `git apply logs/fix.patch` | 0 |
| GREEN build | `cmake --build build --target RecordToolsTest zene -j$(nproc)` | 0 |
| GREEN test | `ctest -R RecordToolsTest --output-on-failure` | 0 |
| RED (rootPath key) | the rejected round-one key restored in groupByVolume (`storage.rootPath()`), rebuild | 0 |
| RED (rootPath key) test | `./RecordToolsTest -v1` | 1 |
| GREEN (st_dev key) test | `./RecordToolsTest -v1` | 0 |
| family | `ctest -R "RecordTools\|RecordRemaining\|ControlRecordingRoutes\|RecordingInputPath\|ControlSessionRecordLand" --output-on-failure` (6/6 passed) | 0 |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (8/8 PASS) | 0 |
| ratchet | `bash tests/complexity-gate.sh --check` (6 pre-existing items, none in my files) | 1 |
| ratchet | `bash tests/file-length-gate.sh --check` (15 pre-existing items, none in my files) | 1 |

RED failure with the fix reverted (the branch base's device-label key,
logs/test-24-red-ctest.log):

    FAIL!  : RecordToolsTest::armedRoutesOnIndependentMountsDoNotShare() 'qAbs(seconds - expected) <= 1e-9 * qMax(1.0, expected)' returned FALSE. (seconds 61914.4, expected 123829 for one route a mount)
    0% tests passed, 1 tests failed out of 1

The answer was exactly half of the truth: the two mounts were pooled into one budget.

RED failure with the rejected rootPath() key restored (logs/test-22-red-rootpath.log):

    FAIL!  : RecordToolsTest::armedRoutesOnOneFilesystemUnderTwoMountPointsShareItsFreeSpace() 'qAbs(seconds - expected) <= 1e-9 * qMax(1.0, expected)' returned FALSE. (seconds 864672, expected 432336 for two routes on one filesystem under two mount points)
    Totals: 21 passed, 1 failed, 0 skipped, 0 blacklisted, 2147ms

The answer was exactly double the truth: the one filesystem was split into two budgets, and each
half of it claimed the whole pool.

GREEN: `100% tests passed out of 1` (ctest), and the binary itself reports
`Totals: 22 passed, 0 failed, 0 skipped, 0 blacklisted` (logs/test-23-green.log).

## Self-check

**1. The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
Yes - `ctest -R RecordToolsTest` exits 8 with the product change reverted
(logs/test-24-red-ctest.log: armedRoutesOnIndependentMountsDoNotShare, "seconds 61914.4, expected
123829") and 0 with it (logs/test-25-green-ctest.log). The reverted file is exactly the branch base
(`git diff 06c5dcf48 -- src/core/ControlCommandsRecordTools.cpp > logs/fix.patch`, then
`git apply -R logs/fix.patch`); the patch touches the product file only, never the test.
The second identity case - ONE filesystem under TWO mount points - was proved the same way against
the rejected round-one key: with `storage.rootPath()` as the key the test exits 1
(logs/test-22-red-rootpath.log: "seconds 864672, expected 432336", exactly double) and with the
st_dev key it exits 0 (logs/test-23-green.log, 22 passed, 0 failed, 0 skipped).

**2. The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).**
The seam is the COMMAND itself: the test drives `record.arm_track` and reads `record.remaining`'s
own reply, and it compares that reply with the contract written down in the command's own result
("seconds", "free_bytes", "directory", "armed_routes"). The expectations use only values the
command reports plus `Engine::audioEngine()->outputSampleRate()` - no clock, no sleep, no timer, no
thread, no signal. The one live value, the filesystem's free bytes, is read back through the same
command (`free_bytes`) and cross-checked against `QStorageInfo::bytesAvailable()` of the directory
the command names with a generous tolerance (1 MiB or 5%), so a file written by another process in
between cannot flip the result. The host premise is checked, not assumed: the test stats /tmp and
/dev/shm and skips (QSKIP) when the host has not two independent mounts with one device label
(it has them here: st_dev 61 and 26, both labelled "tmpfs").
The two-mount-points case (tests/src/core/RecordToolsTest.cpp:544) injects its own filesystem
identity instead of hoping for one: the test process enters a mount namespace of its own
(tests/src/core/MountNamespaceTestSupport.h:86) and binds a directory onto another there
(:122), so the two take directories are on ONE filesystem (st_dev equal, asserted at the take
directories) under TWO mount points (QStorageInfo::rootPath() different, asserted there too). The
pair is deterministic - it is the pair the test makes - and the case skips with a clear reason
when the host forbids the namespace. No wall-clock anywhere.

**3. The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.**
The cause is the identity of the free-space budget, and there is exactly one place that computes
one: `groupByVolume()` (src/core/ControlCommandsRecordTools.cpp:155), called once, from
`remaining()` (src/core/ControlCommandsRecordTools.cpp:200). It is a file-local function in an
anonymous namespace - no other caller in src/ or tests/ (grep for groupByVolume/armedTakePlaces
outside the file returns nothing). The upstream of the value, `armedTakePlaces()`
(src/core/ControlCommandsRecordTools.cpp:126), is unchanged: it still counts the armed routes per
directory, and its comment now says the directories are pooled by filesystem. The downstream of the
value, the reply of `record.remaining` (src/core/ControlCommandsRecordTools.cpp:221-222), keeps the
same four keys with the same meanings - only WHICH filesystem wins can change, never the shape. The
sibling that reads the answer, the header's readout (src/gui/shell/TransportReadouts.cpp:281
`recordRemainingLabel`, bound at :412), is untouched and its own test passes (RecordRemainingTest).
The only other place in src/ that asks Qt about storage is the save dialog's volume list
(src/gui/modals/FileDialog.cpp:93), which lists mount points for the user and computes no recording
budget - so no sibling duplicates the budget, and none needs the same key.

**4. No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.**
No saved data, project format, save/load path, header or serialization is touched: the diff is one
command handler's estimate plus its test and the test's own helper header (git diff 06c5dcf48 --stat:
6 files - the product file, the test file, the new test helper header, the two source manifests and
this report). Old projects therefore load exactly as before. No limit and no new check was added: the
refusal path is unchanged (an unmeasurable filesystem still refuses with "cannot measure the volume
holding '<dir>'", src/core/ControlCommandsRecordTools.cpp:200-204), and the number of routes per
budget is still unbounded - a hundred armed routes on one filesystem are still summed into that
one budget, and a hundred armed routes on a hundred filesystems are still a hundred budgets. The
only behaviour that changes is the one the finding names: which filesystems share a budget.
The test's own machinery changes nothing outside the test: the mount namespace is made in the test
process before it starts any thread (tests/src/core/RecordToolsTest.cpp:603) and the bind mount
lives in that namespace, so the host's mounts, the host's files and the process's own uid are
untouched (the uid map keeps the process's own uid, so the files it writes stay its own).

**5. If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.**
Not applicable - the verdict is FIXED. For the record, the finding's first half (grouping by
directory) was already gone on the branch base: the base keys the budget by a volume
(src/core/ControlCommandsRecordTools.cpp:156 at 06c5dcf48) and the pre-existing test
armedRoutesOnOneVolumeShareItsFreeSpace passes there, so the branch base did not overstate the
answer for two directories on one volume. What the base still got wrong, and what Luna's REWORK
note names ("Group actual free-space budgets by filesystem identity"), is the key itself: a device
label is not a filesystem, and the RED run above shows the answer being halved for two independent
mounts. That is what this fix changes.

**6. Sibling commands in the command files you changed - confirm each is consistent with your fix (or say why it is not affected).**
The file I changed registers transport.metronome_options, transport.punch_here, record.route_source
and record.remaining; the sibling set the check asks about is the whole "record" group. Each one,
and why it is (or is not) affected:

- record.arm_track (src/core/ControlCommandsRecordingRoutes.cpp:366) - arms a route with the file it
  writes to. It only feeds the armed routes' paths into armedTakePlaces(); the budget key does not
  reach it. Unchanged, consistent.
- record.disarm_all (src/core/ControlCommandsRecordingRoutes.cpp:427, handler at :268) - disarms
  every route; with no armed route left, remaining() falls back to the recovery directory with one
  route (src/core/ControlCommandsRecordTools.cpp:196-197), exactly as before. Unchanged, consistent.
- record.disarm_track (src/core/ControlCommandsRecordingRoutes.cpp:398) - disarms one route, so it
  removes one route from a budget; the key does not change. Unchanged, consistent.
- record.get_state (src/core/ControlCommandsRecordingRoutes.cpp:333) - reports the recorder's state
  (routes, armed flags, take paths). It measures no free space; the reply's shape is untouched.
  Unchanged, consistent.
- record.input_get_state (src/core/ControlCommandsRecordingInput.cpp:198) - the input devices' state.
  Unrelated to disk space. Unchanged.
- record.input_set (src/core/ControlCommandsRecordingInput.cpp:220) - which input a route records
  from. Unrelated to disk space. Unchanged.
- record.journal_begin (src/core/ControlCommandsRecording.cpp:233) - opens a take journal for a
  capture. The journal is written beside the take; the budget is measured from the armed route's
  take path, not from the journal, so the key does not reach it. Unchanged, consistent.
- record.journal_update (src/core/ControlCommandsRecording.cpp:254) - updates the journal's frames.
  Unchanged.
- record.journal_finish (src/core/ControlCommandsRecording.cpp:276) - closes the journal. Unchanged.
- record.recovery_discard (src/core/ControlCommandsRecordingRecovery.cpp:239) - discards a recovered
  take file; it deletes a file and measures no volume. Unchanged.
- record.recovery_get_state (src/core/ControlCommandsRecordingRecovery.cpp:171) - reports the
  recovery offers. Unchanged.
- record.recovery_place (src/core/ControlCommandsRecordingPlace.cpp:138) - brings an interrupted
  capture into the project; it moves no take file and measures no volume. Unchanged.
- record.recovery_restore (src/core/ControlCommandsRecordingRecovery.cpp:201) - resolves a recovery
  offer into a restored take. Unchanged.
- record.remaining (src/core/ControlCommandsRecordTools.cpp:200) - the fixed command: the budget is
  now keyed by the filesystem (st_dev). Its reply keeps the same keys, and it stays a read-only
  command (registered with mutating = false, src/core/ControlCommandsRecordTools.cpp:299), so it adds
  no undo step and no new command id - the command snapshot
  (tools/mcp-zene-control/zene_control/commands_snapshot.json) is untouched, and the canary does
  not move.
- record.retro_capture_arm (src/core/ControlCommandsRecordingRetro.cpp:226) - arms the retrospective
  capture ring (a mode flag over a bounded ring of the input bus). It writes no take and takes no
  disk space; it does not arm a record route, so it is not part of any budget. Unchanged.

No command was added, removed or renumbered, and no control command was changed outside the
estimate itself.

## Files committed

- src/core/ControlCommandsRecordTools.cpp
- tests/src/core/RecordToolsTest.cpp
- tests/src/core/MountNamespaceTestSupport.h (new: the mount namespace the two-mount-points case needs)
- tests/fork-sources.txt (the new test helper is registered)
- tests/all-sources.txt (the same, re-derived from its own recipe)
- docs/qwen-fixes-2026-10/cluster-review-e39984ffb.md
