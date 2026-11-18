# Cluster review-dd604aa40 — InputChannelRing regression has wrong registry path

Verdict: **FIXED**

## Finding

`tests/fork-sources.txt` registered the InputChannelRing regression test as
`src/core/InputChannelRingTest.cpp`, but the file on disk (and in git) is
`tests/src/core/InputChannelRingTest.cpp`. Gate 9 (fork-sources-gate.sh) reported
both halves of the mismatch:

- `tests/src/core/InputChannelRingTest.cpp` — NOT IN tests/fork-sources.txt (and not in
  tests/all-sources.txt or tests/tools-sources.txt)
- `src/core/InputChannelRingTest.cpp` — stale entry in tests/fork-sources.txt (no such file)

The same gate run also showed `tests/all-sources.txt` no longer reproducing from its own
header recipe: the recipe derives `tests/src/core/BounceInPlaceTest.cpp` and
`tests/src/core/InputChannelRingTest.cpp` and the manifest was missing both.

## Evidence

- `tests/fork-sources.txt:2094` (before): `src/core/InputChannelRingTest.cpp`
- `tests/all-sources.txt:2418` (before): missing; `include/InputChannelRing.h` present at line 329
- `tests/src/core/InputChannelRingTest.cpp` and `include/InputChannelRing.h` exist and are tracked.

## Fix

1. `tests/fork-sources.txt`: corrected the entry to the exact path
   `tests/src/core/InputChannelRingTest.cpp` (no other entry touched).
2. `tests/all-sources.txt`: re-derived the entry list from the manifest's own
   "Regenerate with" recipe (header kept verbatim; entries replaced by the recipe's
   byte-exact output), which adds exactly the two missing entries
   `tests/src/core/BounceInPlaceTest.cpp` and `tests/src/core/InputChannelRingTest.cpp`.
   This is the manifest's documented repair path ("Fix by re-deriving the list"); no
   gate was re-anchored and no entry was removed.

No test, gate, or assertion was weakened; the ring capacity guard code was not touched
(reviewed as sound in the prior round).

## Commands run (copied verbatim, with the EXIT codes I saw)

RED — gate fails before the fix:

```
$ bash tests/fork-sources-gate.sh > "$LOGS/gate-1-fork-sources-red.log" 2>&1; echo EXIT=$?
EXIT=1
```

(`$LOGS` = /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-dd604aa40.r2/logs;
the log names `tests/src/core/InputChannelRingTest.cpp` as NOT IN fork-sources.txt and
`src/core/InputChannelRingTest.cpp` as a stale entry, and reports all-sources.txt does not
match its recipe.)

GREEN — gate after the fix:

```
$ bash tests/fork-sources-gate.sh > "$LOGS/gate-2-fork-sources-green.log" 2>&1; echo EXIT=$?
EXIT=0
```

(log tail: `PASS: every tracked source in scope is registered (1512 fork-NEW, 1196 inherited, 48 tooling).`)

Build the test binary (ctest initially found no executable — stale build, not a regression):

```
$ cmake --build build --target InputChannelRingTest > "$LOGS/build-1-inputchannelring.log" 2>&1; echo EXIT=$?
EXIT=0
```

Test run before rebuild (binary missing, "Not Run"):

```
$ QT_QPA_PLATFORM=offscreen ctest -R InputChannelRing --output-on-failure > "$LOGS/test-1-inputchannelring.log" 2>&1; echo EXIT=$?
EXIT=8
```

Test run after rebuild:

```
$ (cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R InputChannelRing --output-on-failure > "$LOGS/test-2-inputchannelring.log" 2>&1); echo EXIT=$?
EXIT=0
```

Required gates:

```
$ bash tests/complexity-gate.sh --check > "$LOGS/gate-3-complexity.log" 2>&1; echo EXIT=$?
EXIT=0
$ bash tests/file-length-gate.sh --check > "$LOGS/gate-4-file-length.log" 2>&1; echo EXIT=$?
EXIT=0
```

## Notes

- The registration gate now sees the test: Gate 9's file→registered scan finds
  `tests/src/core/InputChannelRingTest.cpp` in tests/fork-sources.txt, and the whole-tree
  manifest reproduces from its own recipe.
- `tests/src/core/BounceInPlaceTest.cpp` was already registered in fork-sources.txt
  (line 1912); it was only missing from all-sources.txt, which the re-derivation adds.
