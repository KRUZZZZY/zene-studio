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

## Round 5 (Luna's rework: the Windows branch still keyed the budget by the mount point)

Luna's round-4 blocker was exact: on Windows the round-3 implementation used
`QStorageInfo::rootPath()` as the key, and rootPath() names the MOUNT, not the volume. One volume
mounted at several paths (a folder mount, a subst drive) therefore got one budget per mount
point, and each budget claimed the whole pool of free bytes - the answer the finding is about,
still open on Windows. Qt names a Windows volume by its GUID, which is `QStorageInfo::device()`.

Fix (this round): on Windows the volume's GUID names the volume, and the mount point is only the
last-resort fallback for a storage info that knows no GUID. POSIX keeps the kernel's device
(st_dev) as the identity - the device LABEL is not an identity there, so the two rules stay apart.

The rule is now ONE function, asked of the platform instead of being written twice:

- include/ControlRecordingSupport.h:135 - `enum class VolumeNaming { KernelDevice, VolumeId }`:
  how a platform names the filesystem a place sits on (POSIX: the device the kernel mounts it
  from; Windows: the volume's GUID).
- include/ControlRecordingSupport.h:147 / src/core/ControlRecordingSupport.cpp:310 -
  `platformVolumeNaming()`: which of the two this platform uses.
- include/ControlRecordingSupport.h:156 / src/core/ControlRecordingSupport.cpp:321 -
  `filesystemIdentityKey(path, naming, deviceId, mountPoint)`: the key that pools the free bytes.
  With `VolumeId` it is the GUID when the storage info has one, the mount point otherwise; with
  `KernelDevice` it is the kernel's st_dev of the path, the mount point otherwise.
- src/core/ControlCommandsRecordTools.cpp:163 - `groupByVolume()` asks it for every armed place:
  `filesystemIdentityKey(it.key(), platformVolumeNaming(), QString::fromUtf8(storage.device()),
  storage.rootPath())`.

The rule moved into the record group's shared helpers first, in a behaviour-preserving commit
(5d980d33d), so that the Windows rule could be covered by a test that runs on any host: the
platform's facts are passed in as DATA (the device id, the mount point), and the naming rule is
chosen by the platform. The fix commit changes only that rule. That split is what makes the RED
proof below an assertion failure and not a build failure.

## Tests

tests/src/core/RecordToolsTest.cpp (the area's test file; it covers record.remaining):

- armedRoutesOnOneVolumeShareItsFreeSpace (pre-existing) - two directories on ONE filesystem share
  its free space: the volume lasts free / (2 routes * 3 bytes * rate). Still passes.
- armedRoutesOnIndependentMountsDoNotShare (round 3) - two independent mounts that answer the same
  device label (/tmp and /dev/shm, both tmpfs) are two budgets: the winning volume's free bytes
  last ONE route's rate, not half of it.
- armedRoutesOnSameFilesystemCoalesce (round 3) - two paths onto ONE filesystem (a directory and a
  link to it) are one budget: its free bytes last two routes' rate.
- armedRoutesOnOneFilesystemUnderTwoMountPointsShareItsFreeSpace (round 3) - ONE filesystem under
  TWO mount points: the take directories are on the same filesystem (the kernel's st_dev matches)
  and on the same pool of free bytes, but Qt gives them different mount points (rootPath differs).
  The budget belongs to the filesystem, so both routes divide that one pool: the free bytes last
  two routes' rate. This is the case that fails against the rejected rootPath() implementation.
- theBudgetKeyIsTheFilesystemIdentity (NEW, round 5) - the identity rule itself, on any host:
  one volume GUID under two mount points gives ONE key (so the two routes share the pool); the
  mount point is the fallback only (it names no volume, and two mount points stay two keys when
  there is no GUID); POSIX names the filesystem by the kernel's device (two paths onto one
  filesystem share the key whatever the device label says); and the naming this platform uses for
  itself is the one it should be.
- cleanup() - every test starts from a clean recorder, so a failing test cannot refuse the next
  test's arming.

The two-mount-points case needs a pair of paths on one filesystem under two mount points, and a
host need not have one. The test makes it: tests/src/core/MountNamespaceTestSupport.h puts the
test process in a mount namespace of its own and binds a directory onto another there - the
namespace has to be made before the process has any thread (the kernel refuses a user namespace
for a threaded process), so the test's main() makes it before the application starts. The bind
mount is made in the namespace, so it never reaches the host.

## Commands (all from the worktree; logs in the run's log folder)

| what | command | exit |
|---|---|---|
| refactor build | `cmake --build build --target zene RecordToolsTest -j$(nproc)` | 0 |
| refactor test | `ctest -R RecordToolsTest --output-on-failure` (from build/tests) | 0 |
| fix build | `cmake --build build --target zene RecordToolsTest -j$(nproc)` | 0 |
| fix test (GREEN) | `ctest -R RecordToolsTest --output-on-failure` | 0 |
| RED: revert the product change | `git apply -R logs/fix.patch` (fix.patch = `git diff -- src/core/ControlRecordingSupport.cpp`) | 0 |
| RED build | `cmake --build build --target zene RecordToolsTest -j$(nproc)` | 0 |
| RED test | `ctest -R RecordToolsTest --output-on-failure` | 8 |
| GREEN: restore the product change | `git apply logs/fix.patch` | 0 |
| GREEN build | `cmake --build build --target zene RecordToolsTest -j$(nproc)` | 0 |
| GREEN test | `ctest -R RecordToolsTest --output-on-failure` | 0 |
| GREEN verbose | `./RecordToolsTest -v1` (23 passed, 0 failed, 0 skipped) | 0 |
| family build | `cmake --build build --target RecordToolsTest RecordToolsNoEngineTest ControlRecordingRoutesTest RecordingInputPathTest ControlSessionRecordLandTest MultiTrackRecorderTest RecordingCrashLossTest RecordingPassTest RecordRemainingTest -j$(nproc)` | 0 |
| family | `ctest -R "RecordTools\|ControlRecordingRoutes\|RecordingInputPath\|ControlSessionRecordLand\|MultiTrackRecorder\|RecordingCrashLoss\|RecordingPass\|RecordRemaining" --output-on-failure` (9/9 passed) | 0 |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (8/8 PASS) | 0 |
| ratchet | `bash tests/file-length-gate.sh --check` (14 pre-existing items, none in my files) | 1 |
| ratchet | `bash tests/complexity-gate.sh --check` (6 pre-existing items, none in my files) | 1 |
| whitespace | `git diff --check` | 0 |

Logs: build-1-refactor.log, test-1-refactor.log, build-2-fix.log, test-2-fix-green.log,
build-3-red.log, test-3-red.log, build-4-green.log, test-4-green.log, test-5-green-verbose.log,
build-5-family.log, build-6-family.log, build-7-remaining.log, test-6-family.log,
test-7-family.log, gate-feature-check.log, gate-file-length.log, gate-complexity.log, fix.patch.

RED failure with the product change reverted (logs/test-3-red.log):

    FAIL!  : RecordToolsTest::theBudgetKeyIsTheFilesystemIdentity() Compared values are not the same
       Actual   (control::filesystemIdentityKey(firstMount, windows, volumeId, firstMount))  : "path:/mnt/first"
       Expected (control::filesystemIdentityKey(secondMount, windows, volumeId, secondMount)): "path:/mnt/second"
       Loc: [../../tests/src/core/RecordToolsTest.cpp(617)]
    Totals: 22 passed, 1 failed, 0 skipped, 0 blacklisted, 1138ms
    0% tests passed, 1 tests failed out of 1

The reverted rule answered the MOUNT POINT where the volume's GUID belongs: the same volume under
two mount points got two keys - two budgets - and each claimed the whole pool.

GREEN: `100% tests passed out of 1` (ctest, logs/test-4-green.log), and the binary itself reports
`Totals: 23 passed, 0 failed, 0 skipped, 0 blacklisted` (logs/test-5-green-verbose.log).

## Self-check

**1. The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
Yes - `ctest -R RecordToolsTest` exits 8 with the product change reverted (logs/test-3-red.log:
theBudgetKeyIsTheFilesystemIdentity, "Actual: path:/mnt/first, Expected: path:/mnt/second") and 0
with it (logs/test-4-green.log, logs/test-2-fix-green.log). The reverted patch is the product
change of the fix commit only: `git diff -- src/core/ControlRecordingSupport.cpp > logs/fix.patch`,
then `git apply -R logs/fix.patch` - the test file is never in the patch. The fix is split over
two commits on purpose: 5d980d33d moves the identity rule into shared helpers WITHOUT changing
behaviour (the Windows rule was still the mount point there), and the fix commit changes only that
rule. Reverting the fix therefore leaves a compiling tree whose Windows rule is the rejected
one, so the RED is a failing ASSERTION and not a build failure. The round-3 cases stay green
throughout: the POSIX rule (st_dev) is untouched by this round.

**2. The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).**
The seam is the identity rule itself, which takes the platform's facts as DATA: the storage
info's device id (the Windows volume GUID) and its mount point are injected by the test, and the
naming rule (KernelDevice / VolumeId) is passed in as well, so the Windows rule is exercised on a
POSIX host with no clock, no sleep, no timer, no thread and no signal. The POSIX half of the test
reads the kernel's st_dev of two directories the test makes itself (QTemporaryDir), so it is
deterministic too. No wall-clock anywhere.

**3. The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.**
The cause is the identity of the free-space budget, and there is exactly one place that computes
one: `filesystemIdentityKey()` (src/core/ControlRecordingSupport.cpp:321), asked for every armed
place by `groupByVolume()` (src/core/ControlCommandsRecordTools.cpp:163), which is called once,
from `remaining()` (src/core/ControlCommandsRecordTools.cpp:188). `groupByVolume()` and
`armedTakePlaces()` (src/core/ControlCommandsRecordTools.cpp:123) are file-local functions in an
anonymous namespace - no other caller in src/ or tests/ (grep for groupByVolume/armedTakePlaces
outside the file returns nothing). The upstream of the value, `armedTakePlaces()`, is unchanged: it
still counts the armed routes per directory, and its comment says the directories are pooled by
filesystem. The downstream of the value, the reply of `record.remaining`
(src/core/ControlCommandsRecordTools.cpp:209-210), keeps the same four keys with the same
meanings - only WHICH filesystem wins can change, never the shape. The sibling that reads the
answer, the header's readout (src/gui/shell/TransportReadouts.cpp:281 `recordRemainingLabel`,
bound at :412), is untouched and its own test passes (RecordRemainingTest, in the family run
above). The only other place in src/ that asks Qt about storage is the save dialog's volume list
(src/gui/modals/FileDialog.cpp:93), which lists mount points for the user and computes no
recording budget - so no sibling duplicates the budget, and none needs the same key. The new
helpers live in the record group's shared unit (ControlRecordingSupport), whose other helpers
(journal read-back, schemas, route json) are untouched.

**4. No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.**
No saved data, project format, save/load path, header or serialization is touched: the change is
one identity rule plus its test. Old projects therefore load exactly as before. No limit and no
new check was added: the refusal path is unchanged (an unmeasurable filesystem still refuses with
"cannot measure the volume holding '<dir>'", src/core/ControlCommandsRecordTools.cpp:190-191), and
the number of routes per budget is still unbounded - a hundred armed routes on one filesystem are
still summed into that one budget, and a hundred armed routes on a hundred filesystems are still a
hundred budgets. The POSIX behaviour is byte-for-byte the one the branch had (the key is still
the kernel's st_dev, "dev:<st_dev>", and the mount point is still the fallback when the stat
fails). The only behaviour that changes is the one the finding names: which volumes share a
budget, and on Windows only.

**5. Sibling commands in the command files you changed - confirm each is consistent with your fix (or say why it is not affected).**
The file I changed registers transport.metronome_options, transport.punch_here, record.route_source
and record.remaining; the sibling set the check asks about is the whole "record" group. Each one,
and why it is (or is not) affected:

- record.arm_track (src/core/ControlCommandsRecordingRoutes.cpp:366) - arms a route with the file it
  writes to. It only feeds the armed routes' paths into armedTakePlaces(); the budget key does not
  reach it. Unchanged, consistent.
- record.disarm_all (src/core/ControlCommandsRecordingRoutes.cpp:427) - disarms every route; with no
  armed route left, remaining() falls back to the recovery directory with one route
  (src/core/ControlCommandsRecordTools.cpp:184-185), exactly as before. Unchanged, consistent.
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
- record.remaining (src/core/ControlCommandsRecordTools.cpp:176) - the fixed command: the budget is
  keyed by the filesystem (the kernel's device on POSIX, the volume's GUID on Windows). Its reply
  keeps the same keys, and it stays a read-only command (registered with mutating = false,
  src/core/ControlCommandsRecordTools.cpp:287), so it adds no undo step and no new command id - the
  command snapshot (tools/mcp-zene-control/zene_control/commands_snapshot.json) is untouched, and
  the canary does not move.
- record.retro_capture_arm (src/core/ControlCommandsRecordingRetro.cpp:226) - arms the retrospective
  capture ring (a mode flag over a bounded ring of the input bus). It writes no take and takes no
  disk space; it does not arm a record route, so it is not part of any budget. Unchanged.

The transport siblings (transport.metronome_options, transport.punch_here) and record.route_source
are in the same file and are untouched by this change; record.route_source only moves a tap
point, which the budget key never sees.

**6. Realtime, undo and commands.**
Nothing new runs on the audio thread: `record.remaining` is a read-only command, driven from the
control surface, and the new helpers only read the filesystem's identity (a stat and two strings).
No allocation, free, lock, wait, I/O or logging was added to any audio path - the diff touches
no process/render/play/mixer code. Undo is unchanged: the command is registered with mutating =
false, so it records no undo step, and no new control command was added, so the command snapshot's
canary does not move.

## Files committed

- include/ControlRecordingSupport.h (the naming rule, shared and testable)
- src/core/ControlRecordingSupport.cpp (the identity rule: the volume's GUID on Windows, the kernel's device on POSIX)
- src/core/ControlCommandsRecordTools.cpp (the budget asks the shared rule for its key)
- tests/src/core/RecordToolsTest.cpp (the identity rule's own regression test)
- docs/qwen-fixes-2026-10/cluster-review-e39984ffb.md

FIXED
