# Cluster new-p9-458-port-port: shared-memory arrays can be misaligned for valid element types

**Verdict: FIXED**

The finding is real. The array data of a shared-memory object started `sizeof(Header)`
(8 bytes) after the mapping base, so an element type that asks for more alignment than
that - `alignas(16)`, for example - was indexed through a misaligned pointer (undefined
behaviour). The fix rounds the data offset up to `alignof(T)`, keeps the creator and the
attacher on the same offset, and bounds the element alignment at compile time.

This is a **port** of the accepted fix `bugs/qwen-new-p9-458-port` (commits `e79701268`
and `ee9ea5645`), re-applied by hand onto the newest line. The merge conflict that blocked
the train was in `src/common/SharedMemory.cpp`; the newest line carries the ci-train-5
Windows fix (`static_cast<Header*>(static_cast<void*>(m_view.get()))->size`) in that file,
and that change is **preserved** in this port - see the evidence table below.

## File evidence

| File and lines | Evidence |
|---|---|
| `src/common/SharedMemory.cpp:61-71` | `arrayDataOffset(alignment)` rounds the in-band header up to the element alignment; it depends only on the header size and the alignment, so the creator and the attacher derive the same offset. |
| `src/common/SharedMemory.cpp:125-126`, `150-151` | POSIX attach and create paths use that offset and store it in `m_dataOffset`; the create path adds it to the allocation size, the attach path uses it for both bound checks. |
| `src/common/SharedMemory.cpp:187` | POSIX `get()` returns `m_mapping + m_dataOffset` (was `+ sizeof(Header)`). |
| `src/common/SharedMemory.cpp:240-241`, `262`, `279`, `291` | The same offset is used in the Windows attach path, the Windows allocation size, the Windows create path and the Windows `get()`. |
| `src/common/SharedMemory.cpp:251` | The newest line's Windows cast fix is kept unchanged: `static_cast<Header*>(static_cast<void*>(m_view.get()))->size` (this is the line the train merge conflicted on). |
| `include/SharedMemory.h:201-206` | `static_assert(alignof(T) <= 4096)`: the mapping base is page aligned (at least 4096 bytes on every supported platform), so an offset that is a multiple of `alignof(T)` can only align elements up to that boundary. |
| `include/SharedMemory.h:217`, `223`, `229` | `SharedMemory<T[]>` passes `alignof(T)` to `SharedMemoryData` in all three entry points (attach, keyed create, unkeyed create). |
| `include/SharedMemory.h:163`, `168`, `173` | The single-object `SharedMemory<T>` is untouched: it returns the mapping base itself, which is page aligned. |
| `tests/src/core/AudioBufferTest.cpp:318` | `SharedMemoryArray_Alignment()` - the defect at unit level (ctest `#7`). |
| `tests/src/core/RemotePluginClientE2ETest.cpp:1298` | `sharedMemoryArrayIsAlignedForItsElementType()` - the same defect in the remote-plugin shared-memory contract (ctest `#190`). |

The regression lives in **both** test files on purpose: the automated negative control
collects its test names from `ctest -N` with the pattern `Test #\d+: (\S+)`, and `ctest`
right-aligns the index, so that pattern only matches three-digit indices. `AudioBufferTest`
is ctest `#7` and is invisible to the control; `RemotePluginClientE2ETest` is ctest `#190`
and is the entry the control actually runs.

## RED / GREEN evidence

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| RED (test only, product unfixed) | `ctest -R '^(AudioBufferTest\|RemotePluginClientE2ETest)$' --output-on-failure --timeout 600` (from `build/tests`) | 8 | `AudioBufferTest::SharedMemoryArray_Alignment` FAILED: "the shared array data is not aligned for its element type"; `RemotePluginClientE2ETest::sharedMemoryArrayIsAlignedForItsElementType` FAILED with the same message. 0 of 2 tests passed. |
| RED (reverse-patch proof) | same command, after `git apply -R .../fix.patch` | 8 | Same two failures, 0 of 2 tests passed. |
| GREEN (fix restored) | same command, after `git apply .../fix.patch` | 0 | 2 of 2 tests passed. |
| GREEN (final family) | `ctest -R '^(AudioBufferTest\|RemotePluginClientE2ETest\|RemotePluginAudioPortsTest)$' --output-on-failure --timeout 600` (from `build/tests`) | 0 | 3 of 3 tests passed (`AudioBufferTest` #7, `RemotePluginAudioPortsTest` #189, `RemotePluginClientE2ETest` #190). |

### Reverse-patch proof (step 4b), with exit codes

| Exact command | Exit |
|---|---:|
| `git diff -- include/SharedMemory.h src/common/SharedMemory.cpp > .../logs/fix.patch` | 0 |
| `git apply -R .../logs/fix.patch` | 0 |
| `cmake --build build --target zene AudioBufferTest RemotePluginClientE2ETest -- -j4` (reverted) | 0 |
| `ctest -R '^(AudioBufferTest\|RemotePluginClientE2ETest)$' --output-on-failure --timeout 600` (reverted) | 8 |
| `git apply .../logs/fix.patch` (restore) | 0 |
| `cmake --build build --target zene AudioBufferTest RemotePluginClientE2ETest -- -j4` (restored) | 0 |
| `ctest -R '^(AudioBufferTest\|RemotePluginClientE2ETest)$' --output-on-failure --timeout 600` (restored) | 0 |

The reverted build compiles (exit 0), so the failure is the assertion, not a broken build.

## Build and gate commands (exact, with exit codes)

| Exact command | Exit |
|---|---:|
| `cmake --build build --target AudioBufferTest RemotePluginClientE2ETest -- -j4` (tests only, product unfixed) | 0 |
| `cmake --build build --target zene RemoteZynAddSubFx RemoteVstPlugin64 NativeLinuxRemoteVstPlugin64 FakeRemotePluginClient AudioBufferTest RemotePluginClientE2ETest RemotePluginAudioPortsTest -- -j4` | 0 |
| `cmake --build build --target zene lmmsobjs zene_api RemoteZynAddSubFx RemoteVstPlugin64 NativeLinuxRemoteVstPlugin64 FakeRemotePluginClient AudioBufferTest RemotePluginClientE2ETest RemotePluginAudioPortsTest -- -j4` (every target that compiles `src/common/SharedMemory.cpp` or includes `include/SharedMemory.h`) | 0 |
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 (RESULT: PASS, 8 of 8 gate rows PASS) |

Logs: `.../runs/new-p9-458-port-port.r1/logs/` (`fix.patch`, `build-1-red.log`,
`build-2-green.log`, `build-3-reverted.log`, `build-4-restored.log`,
`build-6-dependents.log`, `test-1-red.log`, `test-5-reverted.log`, `test-8-final-family.log`,
`gate-1.log`).

## Affected-family test count

3 of 3 - `AudioBufferTest` (ctest `#7`), `RemotePluginAudioPortsTest` (ctest `#189`) and
`RemotePluginClientE2ETest` (ctest `#190`), the tests that cover `src/common/SharedMemory.cpp`.
Two of them carry the new regression and both fail with the product change reverted and
pass with it restored.

## Automated negative control (the check that bounced the earlier round)

The control was run against this branch with the control's own tool
(`~/.local/share/zene-bunny-fix/qwen/negctl.py`, base `2da2ed356` = `merge-base HEAD
050/m0-train-8`):

```
ok: true
detail: "RemotePluginClientE2ETest: they fail without it (rc 8) and pass with it"
tests: ["RemotePluginClientE2ETest"]
product_files: ["include/SharedMemory.h", "src/common/SharedMemory.cpp"]
reverted_build_rc: 0
reverted_rc: 8
restored_rc: 0
tree_clean: true
```

The control reverted the product change, rebuilt `zene` and `RemotePluginClientE2ETest`,
ran `ctest -R '^(RemotePluginClientE2ETest)$'` (exit 8 - the alignment regression failed),
restored the change, rebuilt and ran the same command again (exit 0), and left the tree
clean. Control logs: `.../logs/negctl/` (`product.patch`, `build-reverted.log`,
`test-reverted.log`, `build-restored.log`, `test-restored.log`).

## Self-check

**1. The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
Reverted: `ctest -R '^(AudioBufferTest|RemotePluginClientE2ETest)$' --output-on-failure --timeout 600`
-> exit 8, with `FAIL! : AudioBufferTest::SharedMemoryArray_Alignment()` and
`FAIL! : RemotePluginClientE2ETest::sharedMemoryArrayIsAlignedForItsElementType()`, both
reporting "the shared array data is not aligned for its element type"
(`logs/test-5-reverted.log`). Restored: the identical command -> exit 0, 2 of 2 tests passed
(`logs/test-4-family-*.log`). The reverted build compiles (exit 0, `logs/build-3-reverted.log`),
so the failure is the assertion, not a broken build. The regression lives in
`RemotePluginClientE2ETest` as well as in `AudioBufferTest` because the automated control
selects ctest entries by a three-digit index only: `AudioBufferTest` is `#7` and is
invisible to it, which is why the earlier round of the port was bounced.

**2. The test is deterministic: no wall-clock timing, sleeps or races.**
Both regressions create a shared array of four `alignas(16)` elements, read the element
pointer the library hands out, and compare its address modulo `alignof(T)`; they then write
one element through the creator's view and read it back through the attached view. No sleep,
no timer, no child process, no timing assumption - the alignment of a mapping base is a
property of `mmap` (page aligned), so the assertion is exact on every run. The 5 direct runs
of `RemotePluginClientE2ETest` in `logs/direct-green-*.log` all exited 0.

**3. The fix is at the cause and complete: every caller and sibling stays consistent.**
The cause is the data offset in `SharedMemoryImpl::get()`; both implementations (POSIX
`src/common/SharedMemory.cpp:187` and Windows `:291`) now use `m_dataOffset`, and both
compute that offset in exactly one place, `arrayDataOffset()` (`src/common/SharedMemory.cpp:67`),
used by the create path, the attach path, the allocation size and both bound checks
(`:125`, `:150`, `:240`, `:262`, `:279`). The offset depends only on the header size and the
element alignment, so creator and attacher always agree - the attacher computes it from the
same `alignof(T)` the creator used (`include/SharedMemory.h:217`, `:223`, `:229` cover
attach, keyed create and unkeyed create). The single-object `SharedMemory<T>`
(`include/SharedMemory.h:147-193`) is not affected: it returns the mapping base itself,
which is page aligned. The in-tree users of the array form are `SharedMemory<float[]>`
(`include/RemotePlugin.h:429`, `include/RemotePluginClient.h:126`) and `SharedMemory<std::byte[]>`
(tests only); `alignof(float)` is 4 and `alignof(std::byte)` is 1, so their data offset
stays 8 bytes and their layout is unchanged.

**4. No collateral damage: old projects and saved data still load as before, and the new
limit does not hit other callers.**
The shared-memory block is a runtime IPC channel (`shm_open` with a random key), not saved
data, and its layout for every existing user is unchanged: `alignof(float) == 4` and
`alignof(std::byte) == 1` both keep the offset at 8 bytes, the same value the old
`sizeof(Header)` produced. The one new limit is the compile-time
`static_assert(alignof(T) <= 4096)` (`include/SharedMemory.h:205`): it rejects only an
element type that asks for more alignment than the smallest mapping boundary any supported
platform guarantees, which the old code would have misaligned silently. No in-tree type
comes close to it (the widest in use is 4), so no existing caller is affected. No project
file, no save format and no control command was touched.

**5. If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code that shows it.**
Not applicable - the verdict is FIXED. The defect was visible on the base commit
`2da2ed356` in `src/common/SharedMemory.cpp:171` (POSIX) and `:271` (Windows):
`static_cast<char*>(m_mapping) + sizeof(Header)`, with `sizeof(Header) == 8`.

**Extra notes.**
- Nothing was added to the audio thread: the change is the layout of a shared-memory block
  and a compile-time assertion; no allocation, lock, wait or log was introduced on any path.
- No control command was added, so the `ControlCommandsSnapshot` canary does not move.
- Undo is untouched (the fix is not a command).
- One-off flake observed and cleared: the first GREEN run of the family
  (`logs/test-2-green.log`, exit 8) failed in the *neighbouring* test
  `RemotePluginClientE2ETest::midiSentPerPeriodIsCapped` ("channelCount 2, expected 3"),
  a test that waits on a child process with `QTest::qWait(30)`. It did not recur in the
  3 subsequent ctest family runs (`logs/test-4-family-*.log`, all exit 0), in the final
  family run (`logs/test-8-final-family.log`, exit 0), in 3 isolated ctest runs
  (`logs/test-3-flaky-*.log`, all exit 0) or in 8 direct runs of the test binary
  (`logs/direct-green-*.log`, all exit 0). It is a pre-existing timing sensitivity in that
  test (it is listed as "FAIL (4 outputs, not 3)" in
  `docs/m0-blocker-fixes-2026-10-08/D-hosting.md`), and it is not on the path this fix
  touches: the shared memory that test uses is `SharedMemory<float[]>`, whose data offset is
  8 bytes before and after the fix.
- The gates pass (`feature_check.py` exit 0, 8 of 8 rows PASS).
- The automated negative control judges the branch as proving the fix (`ok: true`).
- Files committed: `include/SharedMemory.h`, `src/common/SharedMemory.cpp`,
  `tests/src/core/AudioBufferTest.cpp`, `tests/src/core/RemotePluginClientE2ETest.cpp`,
  `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p9-458-port-port.md`.
