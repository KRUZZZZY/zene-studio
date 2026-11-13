FIXED

Cluster new-p9-557: AudioBuffer move assignment can leave group pointers dangling.

The held-out verdict is FIXED: the bug is real at the type level and the
registration above adds rebind coverage. The Luna patrol could not inspect the
repo, so its not-real claim is ungrounded. Static inspection is conclusive for
C++20 `std::pmr::vector`: `propagate_on_container_move_assignment` is false
for `polymorphic_allocator`, so a move assignment between buffers with
different `memory_resource`s copies element-wise into the destination
resource's new storage instead of stealing. The old group tables were then
freed. Only the Luna-draft verdict is supplied; FIXED matches the code.

## Bug and fix

`include/AudioBuffer.h:134-136` (pre-fix) declared move construction and move
assignment as `= default`. `AudioBuffer` maintains two raw-pointer views that
must stay inside its own storage: `m_accessBuffer` entries point into
`m_sourceBuffer`, and each `ChannelGroup::m_buffers` points into
`m_accessBuffer`. A defaulted move copies the group pointer values verbatim.
With a shared resource the vectors steal so the addresses stay valid, but with
different resources `std::pmr::vector` copies to new storage, leaving groups
pointing into the moved-from access table (freed) and access entries pointing
into the moved-from source buffer (freed). `addGroup` at
`src/core/AudioBuffer.cpp:217-223` already shows the intended invariant by
rebinding every group after resizing.

The fix replaces the defaulted moves with custom moves in
`include/AudioBuffer.h:134-136` and `src/core/AudioBuffer.cpp:136-186`: members
move exactly as before, then `rebindAfterMove()` rebuilds every access entry
from the current `m_sourceBuffer.data()` stepping `m_frames`, and rebinds each
group to `&m_accessBuffer[offset]`, mirroring `addGroup`/`setGroups`. Both move
construction and move assignment call it, self-move is a guarded no-op, and an
out-of-range group gets `nullptr` instead of an out-of-bounds pointer. No
allocation, lock, or log is added; the loop is bounded by channels plus groups
(128 + 64). Callers such as `std::vector<AudioBuffer> m_outputs` in
`include/RoutingNode.h:102`, buffer-style returns
(`tests/src/plugins/RnnoiseDenoiserEffectTest.cpp:40-42`), verification via
steal-compatible moves, and move-constructed test buffers are unchanged.

## Regression coverage

Three new slots in `tests/src/core/AudioBufferTest.cpp`:

- `MoveAssignment_AcrossResources_RebindsGroupsAndAccessBuffer` — two
  distinct `monotonic_buffer_resource`s over separate aligned arrays, a
  6-channel 2-group source with an interleaved buffer, move assignment into a
  2-channel destination, then pointer rebinding and value checks. This is the
  RED/GREEN proof.
- `MoveAssignment_SameResource_PreservesGroupsAndData` — one shared resource;
  verifies stealing keeps groups and data intact.
- `MoveConstructor_MovesGroups` — move construction keeps groups valid so
  by-value returns and `vector<AudioBuffer>` remain safe.

Plus `movedSidechainKeepsGroupViews` in
`tests/src/core/SidechainInputsTest.cpp`, which exercises the same
cross-resource move through `sidechainInputs` (which reads `group(0)`). The
automated proof harness derives its test set from `ctest -N` (single-space
`Test #N` parsing), which only enumerates `#293 SidechainInputsTest` among
the covering tests, so the round-1 negctl ran `SidechainInputsTest` alone and
saw it pass without the fix. The sidechain slot removes that gap: it fails
without the fix and passes with it.

All slots are deterministic: fixed frames, channels, and value patterns with
exact pointer equality, no clocks, sleeps, or threads.

## Evidence

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-557.r1/logs`

| Evidence | Command (cwd noted) | Exit | Result |
|---|---|---:|---|
| RED build (first) | `cmake --build build --target AudioBufferTest > build-1.log 2>&1` (worktree) | 2 | `f_cnt_t`/`ch_cnt_t` unqualified errors |
| RED build (second) | `cmake --build build --target AudioBufferTest > build-2.log 2>&1` (worktree) | 2 | `-Werror=self-move` on the draft self-move check (removed) |
| RED build | `cmake --build build --target AudioBufferTest > build-3.log 2>&1` (worktree) | 0 | Regression binaries built |
| RED test | `ctest -R '^AudioBufferTest$' --output-on-failure > test-1.log 2>&1; echo EXIT=$?` (build/tests) | 8 | `47 passed, 1 failed`: `MoveAssignment_AcrossResources_RebindsGroupsAndAccessBuffer` — `0x7ffd976d8600` vs `0x7ffd976da810` at `AudioBufferTest.cpp:1237` |
| GREEN build | `cmake --build build --target AudioBufferTest > build-4.log 2>&1` (worktree) | 0 | Fix rebuilt |
| GREEN test | `./AudioBufferTest > test-2.log 2>&1; echo EXIT=$?` (build/tests) | 0 | `Totals: 48 passed, 0 failed` |
| `zene` rebuild | `cmake --build build --target zene > build-5.log 2>&1` (worktree) | 0 | Header consumers rebuilt |
| Family build | `cmake --build build --target SidechainInputsTest RnnoiseDenoiserEffectTest > build-6.log 2>&1` (worktree) | 0 | Family targets rebuilt |
| Family test | `ctest -R '^(AudioBufferTest|SidechainInputsTest|RnnoiseDenoiserEffectTest)$' --output-on-failure > test-3.log 2>&1` (build/tests) | 0 | `100% tests passed out of 3` |
| Sidechain count | `./SidechainInputsTest > test-sidechain.log` (build/tests) | 0 | `4 passed, 0 failed` |
| Rnnoise count | `./RnnoiseDenoiserEffectTest > test-rnnoise.log` (build/tests) | 0 | `4 passed, 0 failed` |
| Patch capture | `git diff -- include/AudioBuffer.h src/core/AudioBuffer.cpp > fix.patch` (worktree) | 0 | 95-line product patch |
| Reverse apply | `git apply -R fix.patch` (worktree) | 0 | Product reverted, tests kept |
| Sensitivity RED build | `cmake --build build --target AudioBufferTest > build-red.log 2>&1` (worktree) | 0 | Rebuilt without fix |
| Sensitivity RED test | `ctest -R '^AudioBufferTest$' --output-on-failure > test-red2.log 2>&1` (build/tests) | 8 | `47 passed, 1 failed`, same `AudioBufferTest.cpp:1237` assertion |
| Restore apply | `git apply fix.patch` (worktree) | 0 | Fix restored |
| Sensitivity GREEN build | `cmake --build build --target AudioBufferTest zene > build-green2.log 2>&1` (worktree) | 0 | Rebuilt with fix |
| Sensitivity GREEN test | `ctest -R '^AudioBufferTest$' --output-on-failure > test-green2.log 2>&1` (build/tests) | 0 | `1/1` passed; full binary `48 passed, 0 failed` in `test-2.log` |
| Final family build | `cmake --build build --target SidechainInputsTest RnnoiseDenoiserEffectTest > build-7.log 2>&1` (worktree) | 0 | Rebuilt |
| Final family test | `ctest -R '^(AudioBufferTest|SidechainInputsTest|RnnoiseDenoiserEffectTest)$' --output-on-failure > test-4.log 2>&1` (build/tests) | 0 | `100% tests passed out of 3` |
| Round-2 build | `cmake --build build --target SidechainInputsTest AudioBufferTest > build-8.log 2>&1` (worktree) | 0 | Sidechain regression rebuilt with fix |
| Round-2 GREEN | `ctest -R '^(AudioBufferTest|SidechainInputsTest)$' --output-on-failure > test-5.log 2>&1` (build/tests) | 0 | `100% tests passed out of 2`; `./SidechainInputsTest > test-side2.log` exit 0 (`5 passed`) |
| Round-2 RED build | `cmake --build build --target SidechainInputsTest AudioBufferTest > build-red2.log 2>&1` (worktree) | 0 | Reverted product, tests kept |
| Round-2 RED test | `ctest -R '^(AudioBufferTest|SidechainInputsTest)$' --output-on-failure > test-red3.log 2>&1` (build/tests) | 8 | AudioBuffer `1 failed` at `AudioBufferTest.cpp:1237`; Sidechain `1 failed` at `SidechainInputsTest.cpp:102` |
| Round-2 GREEN build | `cmake --build build --target SidechainInputsTest AudioBufferTest zene > build-green3.log 2>&1` (worktree) | 0 | Fix restored and rebuilt |
| Round-2 GREEN test | `ctest -R '^(AudioBufferTest|SidechainInputsTest)$' --output-on-failure > test-green3.log 2>&1` (build/tests) | 0 | `100% tests passed out of 2` |
| Gates | `python3 feature_check.py` (worktree) | 0 | `RESULT: PASS`, all eight gates pass |

The prior wrong-directory invocation produced a stale-binary false RED; the
correct `build`/`build/tests` split above is the claimed result.

## Files committed

- `include/AudioBuffer.h`
- `src/core/AudioBuffer.cpp`
- `tests/src/core/AudioBufferTest.cpp`
- `tests/src/core/SidechainInputsTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-557.md`

## Self-check

- The regression test FAILS with the product change reverted and PASSES with
  it: reverted build `build-red.log` exit 0, test `test-red2.log` exit 8
  (`47 passed, 1 failed`, `AudioBufferTest.cpp:1237`); restored build
  `build-green2.log` exit 0, test `test-green2.log` exit 0, full binary
  `test-2.log` exit 0 (`48 passed`). Original RED `test-1.log` exit 8 and
  GREEN `test-2.log` exit 0 show the same without the patch dance.
  `git diff` exit 0, `git apply -R` exit 0, `git apply` exit 0. Round 2:
  `ctest -R '^(AudioBufferTest|SidechainInputsTest)$'` exit 8 reverted
  (`test-red3.log`, both `1 failed`, `SidechainInputsTest.cpp:102`) and
  exit 0 restored (`test-green3.log`, `test-5.log`); the sidechain slot is
  what the automated negctl enumerates (`#293`), so it now proves the fix.
- The test is deterministic: fixed `frames=16`, fixed channel counts, fixed
  value patterns (`ch*1000+f`, `ch*100+f`), exact pointer equality. Seam is
  two separate `monotonic_buffer_resource`s over distinct aligned backing
  arrays with `null_memory_resource` upstream; no wall-clock, sleep, or race.
- The fix is at the cause and complete: custom move construction and move
  assignment plus `rebindAfterMove()` rebuild both the access table and every
  group, mirroring `addGroup` (`AudioBuffer.cpp:217-223`). Checked callers:
  `vector<AudioBuffer> m_outputs` (`RoutingNode.h:102` via move construction),
  by-value returns in `RnnoiseDenoiserEffectTest`, `ReceiverEffectTest`
  helpers, and same-resource stealing; all remain consistent and empty buffers
  loop zero times. No change to `addGroup`, `setGroups`, `allocateInterleavedBuffer`,
  or interleaved data layout.
- No collateral damage: no limit added, no data-format or persistence touched;
  move semantics for same-resource buffers are unchanged (steal preserves
  addresses), and rebind writes the same values `addGroup` would. Family
  `test-4.log` exit 0 (`3/3`) and round-2 `test-5.log`/`test-green3.log`
  exit 0 (`48+5` slots) and `feature_check.py` exit 0 confirm no regression.
- Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so no such citation
  is owed; the cited code is the pre-fix defaulted move at
  `AudioBuffer.h:134-136` and the invariant at `AudioBuffer.cpp:217-223`.
