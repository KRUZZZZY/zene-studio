# Cluster new-p10-364 — FIXED

**Verdict:** FIXED.

`ScriptClock::setSchedule()` appended a new ID without a count bound. The scheduler now permits at most 64 entries and returns a typed refusal for a new ID at capacity. Replacement of an existing ID remains available at capacity.

## Evidence

- [include/ScriptClock.h:158](../../include/ScriptClock.h:158) defines `MaxScheduleCount = 64`.
- [src/core/ScriptClock.cpp:249](../../src/core/ScriptClock.cpp:249) rejects an addition at capacity after the existing-ID replacement path.
- [tests/src/core/ScriptClockTest.cpp:293](../../tests/src/core/ScriptClockTest.cpp:293) fills all 64 slots, verifies replacement, then checks refusal of the 65th ID and cleans up.
- [tests/fork-sources.txt:2026](../../tests/fork-sources.txt:2026) registers the product test source.

The regression was RED on the original behavior (`registryCountIsBounded`: `!accepted` returned FALSE), GREEN with the fix, RED again after removing only the production patch, and GREEN after restoring it. The affected ctest family executed **1/1 registered test** on each run.

The build commands ran from the worktree root. The ctest commands ran from `build/tests`. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-364.r1/logs`. The fff grep tool was not exposed in this session, so matching log lines were inspected with `rg -n`.

### Initial regression (RED)

```sh
mkdir -p /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-364.r1/logs && cmake --build build --target zene ScriptClockTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-364.r1/logs/build-1.log 2>&1; build_status=$?; printf 'EXIT=%s\n' "$build_status"; exit "$build_status"
```

EXIT=0.

```sh
ctest -R '^ScriptClockTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-364.r1/logs/test-1.log 2>&1; test_status=$?; printf 'EXIT=%s\n' "$test_status"; exit "$test_status"
```

EXIT=8. The failure was `ScriptClockTest::registryCountIsBounded(): '!accepted' returned FALSE`.

### Fixed build and test (GREEN)

```sh
cmake --build build --target zene ScriptClockTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-364.r1/logs/build-2.log 2>&1; build_status=$?; printf 'EXIT=%s\n' "$build_status"; exit "$build_status"
```

EXIT=0.

```sh
ctest -R '^ScriptClockTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-364.r1/logs/test-2.log 2>&1; test_status=$?; printf 'EXIT=%s\n' "$test_status"; exit "$test_status"
```

EXIT=0; 1/1 test passed.

### Removal proof (RED without the production patch)

```sh
git diff -- include/ScriptClock.h src/core/ScriptClock.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-364.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-364.r1/logs/fix.patch; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"
```

EXIT=0.

```sh
cmake --build build --target zene ScriptClockTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-364.r1/logs/build-3.log 2>&1; build_status=$?; printf 'EXIT=%s\n' "$build_status"; exit "$build_status"
```

EXIT=0.

```sh
ctest -R '^ScriptClockTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-364.r1/logs/test-3.log 2>&1; test_status=$?; printf 'EXIT=%s\n' "$test_status"; exit "$test_status"
```

EXIT=8; the same `!accepted` assertion failed.

### Restored patch (GREEN)

```sh
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-364.r1/logs/fix.patch; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"
```

EXIT=0.

```sh
cmake --build build --target zene ScriptClockTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-364.r1/logs/build-4.log 2>&1; build_status=$?; printf 'EXIT=%s\n' "$build_status"; exit "$build_status"
```

EXIT=0.

```sh
ctest -R '^ScriptClockTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-364.r1/logs/test-4.log 2>&1; test_status=$?; printf 'EXIT=%s\n' "$test_status"; exit "$test_status"
```

EXIT=0; 1/1 test passed.

### Required gates

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-364.r1/logs/gate-1.log 2>&1; gate_status=$?; printf 'EXIT=%s\n' "$gate_status"; exit "$gate_status"
```

EXIT=0. The gate reported 13,994 functions in scope, 68 above CCN 10.

```sh
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-364.r1/logs/gate-2.log 2>&1; gate_status=$?; printf 'EXIT=%s\n' "$gate_status"; exit "$gate_status"
```

EXIT=0. The gate reported 1,510 sources measured, 5 above 500 code lines.

## Files in the commit

- `include/ScriptClock.h`
- `src/core/ScriptClock.cpp`
- `tests/src/core/ScriptClockTest.cpp`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-364.md`
