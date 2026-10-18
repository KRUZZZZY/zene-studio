# Cluster new-p12-141 — FIXED

## Finding

Repeated refusals could lose intermediate MIDI recovery cuts: `MidiChannelOverflow` retained only 257 pending cut positions, while Carla uses a 512-event queue and can produce more than 257 distinct cut boundaries before a cut becomes due. With 257 slots, intermediate cuts after slot exhaustion were deferred/collapsed and skipped.

`MidiChannelOverflow::MaxPendingCuts` is now 513 (`512 + 1`), derived from the largest queue capacity in use. Each distinct pending stamp can now be retained for Carla's 512-event queue plus the next cut boundary. Slot scans and cut-stamp updates remain bounded, lock-free, and allocation-free.

Evidence: `include/MidiHandoffQueue.h:350` defines the bound; `:432`, `:507` store and scan the bounded per-channel slots; `:358` and `:368` record stamps; `:396` and `:409` select the effective stamp. The regression is `tests/src/core/MidiHandoffQueueTest.cpp::fiveTwelveCapacityKeepsAllRecoveryCuts` (`:523`), using a 512-capacity queue and 300 distinct refusals interleaved with one-event drains.

## Verification

The RED test failed before the product change (257 slots): 15 passed, 1 failed (`QCOMPARE(cutCount, 300)`). With the product change (513 slots), it passes: 16 passed, 0 failed. Reversing only `include/MidiHandoffQueue.h` restored 257 slots and made the same test fail again (CTest exit 8). Restoring the header made both affected binaries pass (CTest exit 0). `zene` and both dependent test targets rebuilt successfully.

| Check | Log | Exit | Result |
|---|---|---:|---|
| RED before product change | `test-1.log` | 8 | `MidiHandoffQueueTest` failed, `CarlaMidiHandoffTest` passed |
| RED with product header reversed | `test-2.log` | 8 | `fiveTwelveCapacityKeepsAllRecoveryCuts` failed |
| GREEN with product header restored | `test-3.log` | 0 | 2/2 CTest binaries passed |
| Affected-family build | `build-11.log` | 0 | `zene`, both test targets built |
| Complexity gate | `gate-complexity.log` | 1 | Unrelated pre-existing over-target entries only |
| File-length gate | `gate-filelength.log` | 1 | Unrelated pre-existing over-length entries only |

Exact RED command before the product change (run from `build/tests`):

```sh
ctest -R '^(MidiHandoffQueueTest|CarlaMidiHandoffTest)$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r3/logs/test-1.log' 2>&1; echo EXIT=$?
```

Exact proof command with product header reversed:

```sh
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r3/logs/fix.patch
/usr/bin/cmake --build build --target MidiHandoffQueueTest
ctest -R '^MidiHandoffQueueTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r3/logs/test-2.log' 2>&1; echo EXIT=$?
```

Exact restoration command:

```sh
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r3/logs/fix.patch
```

Exact GREEN command (run from `build/tests`):

```sh
cmake --build build --target zene MidiHandoffQueueTest CarlaMidiHandoffTest
ctest -R '^(MidiHandoffQueueTest|CarlaMidiHandoffTest)$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r3/logs/test-3.log' 2>&1; echo EXIT=$?
```

Ratchet gates rerun in check mode:

```sh
bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r3/logs/gate-complexity.log' 2>&1; echo EXIT=$?
bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r3/logs/gate-filelength.log' 2>&1; echo EXIT=$?
```

The gate exit codes in this worktree remain 1 from pre-existing unrelated entries; neither log lists `include/MidiHandoffQueue.h` or `tests/src/core/MidiHandoffQueueTest.cpp`.

## Files in the commit

- `include/MidiHandoffQueue.h`
- `tests/src/core/MidiHandoffQueueTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-141.md`
