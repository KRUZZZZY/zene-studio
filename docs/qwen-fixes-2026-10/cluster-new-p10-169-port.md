# Cluster new-p10-169-port — Ring storage size multiplication can wrap

**Verdict: ALREADY-FIXED** (on this branch, before this session).

## Finding

`include/InputChannelRing.h` (historical line 83): `m_capacity * m_maxChannels` was
multiplied unchecked when sizing the ring's storage, so on a 64-bit system a
capacity of `1ULL << 63` with two channels wrapped the product to zero and left
an undersized (empty) buffer; a subsequent write indexed `m_data[...]` out of
bounds.

## Evidence that the newest line already fixes it

The defect was already addressed on this branch by commit `dd604aa40`
("fix(input): an impossible ring capacity hung or under-allocated the storage",
Luna cluster 3185), which is an ancestor of this worktree. The storage product is
now computed by a checked helper:

- `include/InputChannelRing.h:85` — `m_data(storageSize(m_capacity, std::max(channels < 1 ? 1 : channels, maxChannels)))`.
- `include/InputChannelRing.h:208-219` — `storageSize()` rejects a product that
  would overflow `size_t`:

  ```cpp
  static std::size_t storageSize(std::size_t capacity, int maxChannels)
  {
      const auto width = static_cast<std::size_t>(maxChannels);
      if (width != 0 && capacity > std::numeric_limits<std::size_t>::max() / width)
      {
          throw std::length_error("ring storage size overflows size_t");
      }
      return capacity * width;
  }
  ```

- The accepted fix's regression scenario (capacity `1ULL << 63`, 2 channels) was
  verified to throw `std::length_error` on this tree: a standalone compile of
  `#include "InputChannelRing.h"` constructing `lmms::InputChannelRing(1ULL << 63, 2)`
  printed `THREW: ring storage size overflows size_t` (exit of the test program 0).
  The ring test target also already covers the equivalent overflow case in
  `tests/src/core/InputChannelRingTest.cpp::refusesAStorageSizeThatOverflows`.

The accepted fix on `bugs/qwen-new-p10-169` (commit `7cb391780`) only renamed
this helper to `checkedStorageSize` and added a redundant `std::vector::max_size()`
check on top of the same overflow guard; the vector constructor already throws
`std::length_error` for sizes above `max_size()`, so no behaviour differs. No
further code change is needed; nothing was edited this session.

## Verification commands (this session)

All logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-169-port.r1/logs`:

```
cmake --build build --target zene InputChannelRingTest RecordRingBufferTest RetroMidiRingTest > logs/build-1.log 2>&1   EXIT=0
cd build/tests && ctest -R 'InputChannelRingTest|RecordRingBufferTest|RetroMidiRingTest' --output-on-failure > logs/test-1.log 2>&1   EXIT=0
```

3/3 tests passed (`InputChannelRingTest`, `RecordRingBufferTest`,
`RetroMidiRingTest`).

```
cmake --build build --target zene > logs/build-zene.log 2>&1   EXIT=0
```

## Gates

```
bash tests/complexity-gate.sh --check   > logs/gate-1.log 2>&1   EXIT=1
bash tests/file-length-gate.sh --check >> logs/gate-1.log 2>&1   EXIT=1
```

Both gates report pre-existing ratchet debt on this branch that is unrelated to
this bug (new over-target functions / new files over 500 lines introduced by
merged lanes, e.g. `dotCentreAlong`, `parseNotes`, `RackMacrosTest.cpp`,
`RevisionTimelineTest.cpp`). No entry for `InputChannelRing` is among them, and
this session added no code, so no new regression was introduced by this work.

## Action

No code change; only this report is committed.
EOF
echo done