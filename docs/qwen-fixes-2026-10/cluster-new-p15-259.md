FIXED

# Cluster new-p15-259 - LAME initialisation failure reaches the null encoder handle

Finding: `src/core/audio/AudioFileMP3.cpp` - "`lame_init()` can return null, but the code passes `m_lame` to
LAME functions regardless; a failed initialisation must be reported, not used."

This supersedes the earlier FALSE-POSITIVE verdict (`c4f54333c`), which was wrong. The LAME setters are
null-safe in the installed LAME 4.0 (`logs/probe-lame-null-1.log`), but that is not the defect. The defect is in the
cleanup path: `ProjectRenderer` deletes a rejected device (`src/core/ProjectRenderer.cpp:185-187`), and the
device's destructor then flushed the null handle (`AudioFileMP3.cpp` destructor -> `flushRemainingBuffers()`),
calling `lame_encode_flush(NULL)`, which answers -3. The flush's `assert (bytesWritten >= 0)`
(`AudioFileMP3.cpp:122`) rejects that in a debug build, and in every build the -3 went on as a byte count to
`writeData()`.

## Fix (current code)

- `src/core/audio/AudioFileMP3.cpp:154-158` `initEncoder()`: returns false straight after `lame_init()` returns null,
  before any setter, ID3 or flush call.
- `:60-65` `writeBuffer()`: a device with no encoder records a write error and returns false.
- `:108-113` `flushRemainingBuffers()`: no encoder means no tail; it records a write error and makes no LAME call.
  The destructor and `finishEncoding()` both go through this, so a rejected device reports failure.
- `:178-185` `tearDownEncoder()`: closes only a live handle.

Callers and siblings checked: the only construction path is `&AudioFileMP3::getInst`
(`src/core/ProjectRenderer.cpp:134`); the only consumer of a rejected device is `src/core/ProjectRenderer.cpp:185-187`
(deleted, never used). `writeBuffer`, `flushRemainingBuffers` and `tearDownEncoder` are the only places that
call LAME after construction.

Not changed, recorded as residual: when `lame_init()` succeeds but `lame_init_params()` fails (`AudioFileMP3.cpp:175`), the handle
stays live and the destructor still flushes it as before. I did not reproduce that failure with valid settings
(a parameter probe in this session crashed and was not resolved), and it is outside the blocking points of this
round. It is worth a follow-up.

## Commits (on `bugs/qwen-new-p15-259`)

- `3d3f371b1` test(cluster new-p15-259): the regression, the test seams, and the registration. RED state.
- `a7530be9a` fix(cluster new-p15-259): the null-handle guards. GREEN state.
- this commit: the report.

The QA commits `1167a6034` and `1971284ed` are not reachable from this branch (no ref contains them), so they were not
cherry-picked. I used them as a reference for the design (guard placement, the one-shot seam, the flush counter), but
the code, the test and the manifests here are written again on `c4f54333c`.

## Regression test

`tests/src/core/AudioFileMP3InitTest.cpp`, registered in `tests/CMakeLists.txt` under `LMMS_HAVE_MP3LAME` (with
`QT_QPA_PLATFORM=offscreen`), in `tests/fork-sources.txt`, and in `tests/all-sources.txt`; the inherited files it
touches are noted in `tests/upstream-modifications.txt`.

Deterministic, with no timing or races: `AudioFileMP3::failNextLameInitForTest()` (one-shot, `LMMS_TESTING` only)
makes the next `lame_init()` return null; `AudioFileMP3::flushCallsForTest()` counts `lame_encode_flush()` calls.
Frame counts are fixed.

Test matrix:
1. `normalEncoderFlushesItsTailOnce` - normal stereo path: device accepted, writes, finishes, exactly one flush,
   non-empty file.
2. `rejectedDeviceIsDestroyedWithoutTouchingLame` - the `ProjectRenderer` path: a rejected device is destroyed with no
   LAME flush and no bytes written.
3. `rejectedDeviceReportsItsFailureWithoutTouchingLame` - a rejected device refuses writes, `finishEncoding()` is
   false, `hasWriteError()` is true, and no flush reaches LAME.
4. Audio-thread constraints: not applicable (export and control thread only).
5. Platform: the test is built only where LAME is enabled, as with the Ogg test.

## Evidence (EXIT codes; logs in `logs/` under the round-2 run folder, `new-p15-259.r2`)

| # | Command (from the worktree unless noted) | EXIT | Result | Log |
|---|---|---|---|---|
| 1 | `cmake --build build --target cmake_check_build_system` | 0 | build tree regenerated | `build-0-regen.log` |
| 2 | `cmake --build build --target AudioFileMP3InitTest -j4` (unguarded, seams only) | 0 | built | `build-1-red-unguarded.log` |
| 3 | `cd build/tests && ctest -R AudioFileMP3InitTest --output-on-failure` (unguarded) | 8 | RED: 2 failed; flush delta 1, expected 0 | `test-1-red-unguarded.log` |
| 4 | same, after rebuilding the committed RED state (`3d3f371b1`) | 8 | RED confirmed on the committed source | `test-3-red-commitA.log` |
| 5 | `cmake --build build --target AudioFileMP3InitTest -j4` (guards in, `a7530be9a`) | 0 | built | `build-2-green.log` |
| 6 | `cd build/tests && ctest -R AudioFileMP3InitTest --output-on-failure` (guards in) | 0 | GREEN: 1/1 executed, passed | `test-2-green.log` |
| 7 | `git diff HEAD~1 HEAD -- src/core/audio/AudioFileMP3.cpp > fix.patch && git apply -R fix.patch`, rebuild, ctest | 8 | RED with the guards reversed: the same 2 failures | `test-4-red-reversed.log` |
| 8 | `git apply fix.patch`, rebuild, ctest | 0 | GREEN restored | `test-5-restored.log` |
| 9 | `cd build/tests && ./AudioFileMP3InitTest` (guards in) | 0 | 5 passed, 0 failed (incl. init/cleanup) | `test-6-binary-green.log` |
| 10 | `cmake --build build --target zene -j4` | 0 | `zene` built against this HEAD | `build-7-zene-final.log` |

Affected test family: `AudioFileMP3InitTest`, 1 ctest test executed (`100% tests passed ... out of 1`) in row 6.
Build environment: `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-bunny/tmp-new-p15-259` was set for the builds (see
Environment notes).

### Gates (step 5)

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` as shipped: EXIT 1 with
`PermissionError` at its log folder `<worktree>-feature-check` (`logs/gate-feature-r2-asis.log`), outside the
write-jail. I did not work around that. I ran each of its checks with the same commands from the worktree, logs in
`logs/feature-check/`:

| Check | Command | EXIT |
|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

The two ratchets are red on the train on purpose, and they fail only on items in files this branch changed. Their
output has 7 (complexity) and 22 (file-length) items, none of which names `AudioFileMP3`, `AudioFileMP3InitTest`, or any
other changed file (`rg` over both logs returns nothing for them). The changed files are:
`include/AudioFileMP3.h`, `src/core/audio/AudioFileMP3.cpp`, `tests/CMakeLists.txt`, `tests/all-sources.txt`,
`tests/fork-sources.txt`, `tests/src/core/AudioFileMP3InitTest.cpp`, `tests/upstream-modifications.txt`, and this report.
Result: no ratchet item is new in a changed file. I could not compute the baseline comparison itself, because that
needs a worktree inside `zene-cachyos`, outside the jail.

## Self-check

- **The regression test FAILS with the fix reverted and PASSES with it.** Yes. Reversed guards: ctest exit 8, with
  `rejectedDeviceIsDestroyedWithoutTouchingLame` and `rejectedDeviceReportsItsFailureWithoutTouchingLame` failing on
  `flushCallsForTest() - flushesBefore`, Actual 1, Expected 0 (`test-4-red-reversed.log`, rows 7-8). Restored: exit 0
  (`test-5-restored.log`, `test-6-binary-green.log`).
- **Deterministic.** The injected failure is a one-shot flag set by the test before construction; the flush
  count is read as a delta from a counter. No sleeps, timing, threads or races. The seams compile only under
  `LMMS_TESTING`, which only the test targets and `lmmsobjs` in a testing build define.
- **Fix at the cause; callers and siblings checked.** See the Fix section: the single construction path, the single
  consumer (`ProjectRenderer.cpp:185-187`), and the three LAME call sites after construction. Residual: the
  `lame_init_params()` failure path with a live handle (`AudioFileMP3.cpp:175`), not changed, not reproduced.
- **No collateral damage.** A normal stereo export is unchanged: the normal-path test flushes once and writes a
  non-empty file. No file format, saved data or control command changed. The guards are control-thread code, not
  audio-thread code. Nothing is logged or allocated on any audio path.
- **FIXED evidence (file:line).** `src/core/audio/AudioFileMP3.cpp:122` (the unguarded flush and its assert),
  `:154-158` (the init guard), `:108-113` (the flush guard), `src/core/ProjectRenderer.cpp:185-187` (the rejected device
  is deleted), `:134` (the factory).

## Environment notes

- The first build attempt failed in ccache (`Permission denied` for its temp file under the sandbox path). The builds
  used `CCACHE_TEMPDIR` inside the ccache directory (set for those commands only; no config change).
- The build tree was regenerated with `cmake --build build --target cmake_check_build_system`; the wrapper refuses an
  explicit `cmake -S/-B`.
- The Debug-only assertion at `AudioFileMP3.cpp:122` was not run: the tree here is RelWithDebInfo (`NDEBUG`), and no
  Debug build exists. The regression signal is the LAME flush call count, which is the call that assertion guards.
- A parameter probe of LAME's `lame_init_params()` crashed in this session (exit 139) and was not resolved, so the
  residual above is recorded as unverified rather than as a finding.
