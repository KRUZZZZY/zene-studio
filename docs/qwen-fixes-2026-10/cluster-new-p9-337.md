# Cluster new-p9-337

**Verdict: FIXED.**

Finding (Luna, `new:needs_context`): `PhaseScopeTap::latest()` snapshots `m_head`, copies the ring, then
checks `m_reserved`; a channel change (`setChannel()` -> `reset()`) racing a copy rebases the ring
(`m_head`/`m_reserved` go back to a fresh push's end), and the unsigned
`m_reserved - start <= Capacity` check can then pass on a window stitched from the old channel's slots and
the new channel's. The finding's line 65 is now `src/core/PhaseScopeTap.cpp:65` (the write section).

## Why it is reachable

`PhaseScopeTap.h:50` documents `setChannel()`/`latest()` as GUI **or control** thread methods, so they may
be called from two different threads while the mixer worker calls `push()` on the audio thread. The two
existing regression tests for the file exercise only a single reader/writer pair on one ring, so nothing
covered a channel change racing an in-flight copy. `tests/src/core/PhaseScopeTapTest.cpp` now does.

## Reproduction (RED, before the fix)

`tests/src/core/PhaseScopeTapTest.cpp::aChannelChangeDuringACopyIsRefusedNotMixed` fills the ring with one
constant channel (all `(1,1)`), then a writer thread alternates `setChannel(8/9)` + `push` of a full
`Capacity` block of the other constant (`(-1,-1)`) while the main thread repeatedly calls `latest()`.
A returned window whose frames do not all agree with the first is a mixed window.

```sh
cmake --build build --target PhaseScopeTapTest > .../logs/build-2.log 2>&1; echo EXIT=$?
# EXIT=0
ctest -R '^PhaseScopeTapTest$' --output-on-failure > .../logs/test-2-red.log 2>&1; echo EXIT=$?
# EXIT=8
# FAIL!  : aChannelChangeDuringACopyIsRefusedNotMixed() '!mixed' returned FALSE.
#          (latest() returned a window that mixed two channels)  [0.37 s]
```

## Fix

A per-ring seqlock `m_generation` (even idle, odd while a push is changing slots) brackets every push
write and moves by two in `reset()`:

- `src/core/PhaseScopeTap.cpp:68` / `:79` — `push()` increments the generation before/after its write.
- `include/PhaseScopeTap.h:88-89` — `reset()` moves it by two after setting `m_resetPending`.
- `src/core/PhaseScopeTap.cpp:88-89`, `:101` — `latest()` refuses an odd generation, or one that changed
  across the copy, in addition to the existing overrun check.

This is exactly the "generation or sequence counter that brackets writes and resets" the finding asks for.
It is behaviour-preserving otherwise: the audio path still allocates nothing (atomics only), `push()` stays
lock-free and branch-equivalent, and the normal-path overrun check is unchanged. The only observable change
is that a copy overlapped by a write/reset is now refused (the caller already documents reading again on its
next refresh).

## Proof the test catches the bug (QA step 4b)

```sh
git diff -- include/PhaseScopeTap.h src/core/PhaseScopeTap.cpp > .../logs/fix.patch
git apply -R .../logs/fix.patch                                  # product fix removed, test kept
cmake --build build --target PhaseScopeTapTest > .../logs/build-9-reversed.log 2>&1; echo EXIT=$?
# EXIT=0
ctest -R '^PhaseScopeTapTest$' --output-on-failure > .../logs/test-9-reversed-red.log 2>&1; echo EXIT=$?
# EXIT=8  -> FAIL, mixed window returned
git apply .../logs/fix.patch                                     # fix restored
cmake --build build --target PhaseScopeTapTest > .../logs/build-10-reapplied.log 2>&1; echo EXIT=$?
# EXIT=0
ctest -R '^PhaseScopeTapTest$' --output-on-failure > .../logs/test-10-reapplied-green.log 2>&1; echo EXIT=$?
# EXIT=0  -> PASS
```

## Evidence

| Item | Exact command | Exit |
| --- | --- | --- |
| RED (original code, regression only) | `ctest -R '^PhaseScopeTapTest$' --output-on-failure` from `build/tests`, log `test-2-red.log` | 8 |
| GREEN (after fix) | `ctest -R '^PhaseScopeTapTest$' --output-on-failure` from `build/tests`, log `test-7-green.log` / `test-10-reapplied-green.log` | 0 |
| Affected family | `ctest -R '^(PhaseScopeTapTest|MixerMeteringTest)$' --output-on-failure`, log `test-8-family-final.log` — 2 tests, 2 passed | 0 |
| Reversed-fix proof | `git apply -R fix.patch` then ctest, log `test-9-reversed-red.log` | 8 |
| Reapplied proof | `git apply fix.patch` then ctest, log `test-10-reapplied-green.log` | 0 |
| `zene` rebuild (header dependents) | `cmake --build build --target zene`, log `build-8-final.log` | 0 |

Log directory: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-337.r1/logs`.

## Gate note

Both ratchets were run and fail, but **identically at HEAD and with this change** — the branch's baselines
are stale for unrelated files, and re-anchoring is forbidden. This change adds **zero** new entries:

| Gate | HEAD (no change) | With this change | New from this change |
| --- | --- | --- | --- |
| `bash tests/complexity-gate.sh --check` | exit 1, 6 REGRESSION entries (`gate-complexity-head.log`) | exit 1, the same 6 (`gate-complexity-2.log`) | none (diff of REGRESSION lines is empty) |
| `bash tests/file-length-gate.sh --check` | exit 1, 14 REGRESSION entries (`gate-filelength-head.log`) | exit 1, the same 14 (`gate-filelength-2.log`) | none (diff empty) |

The pre-existing complexity entries are `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`,
`ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`,
`ControlCommandsShortcuts.cpp`; the file-length entries are the long test/source files listed in the log.
`PhaseScopeTap::push` was kept at its baseline CCN by making the bracket unconditional; the new test
helpers/functions are all <= CCN 10.

## Files committed

- `include/PhaseScopeTap.h`
- `src/core/PhaseScopeTap.cpp`
- `tests/src/core/PhaseScopeTapTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-337.md`

No new files, so `tests/fork-sources.txt`/`tests/upstream-modifications.txt` are unchanged
(`include/PhaseScopeTap.h`, `src/core/PhaseScopeTap.cpp` and the test are already registered fork-NEW).
