# Cluster new-p10-98: unknown DAWproject time units

**Verdict: FIXED**

The finding is real. The reader treated every unit except `seconds` as beats. It now accepts only `beats` for note and automation timelines. The existing `seconds` refusal is preserved, and an unrecognized unit is refused with an error naming that unit.

## File and line evidence

| Location | Evidence |
| --- | --- |
| `src/core/DawProjectRead.cpp:131-147` | Automation timelines accept `DawProjectTimeUnit` (`beats`); `seconds` keeps its existing refusal and other values receive an unsupported-unit error. |
| `src/core/DawProjectReadTracks.cpp:70-92` | Note parsing now rejects every effective unit except `beats`, including inherited lane units. |
| `tests/src/core/DawProjectInterchangeRoundTripTest.cpp:962-1002` | Separate regressions cover `timeUnit="milliseconds"` on an inherited note lane and on tempo automation. |

## Regression and verification

The DAWproject test family has one CTest entry. Its QtTest run contains 36 cases. With the product patch reversed, both new cases failed (34 passed, 2 failed); after restoring the patch, all 36 passed.

Commands below were run from the worktree root except the `ctest` commands, which were run from `build/tests`. Build and test output was redirected to the named log files.

**Reverse the product patch — EXIT=0:**

```sh
git diff -- src/core/DawProjectRead.cpp src/core/DawProjectReadTracks.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-98.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-98.r1/logs/fix.patch; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"
```

**Build with the fix reversed — EXIT=0:**

```sh
cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-98.r1/logs/build-6.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"
```

**RED regression command — EXIT=8:**

```sh
ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-98.r1/logs/test-7.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"
```

The log shows `unknownNoteLaneTimeUnitsAreRefused()` and `unknownAutomationTimeUnitsAreRefused()` failing because each document was imported as beats.

**Reapply the product patch — EXIT=0:**

```sh
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-98.r1/logs/fix.patch; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"
```

**Build with the fix restored — EXIT=0:**

```sh
cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-98.r1/logs/build-7.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"
```

**GREEN regression command — EXIT=0:**

```sh
ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-98.r1/logs/test-8.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"
```

**QtTest case count — EXIT=0:**

```sh
ctest -V -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-98.r1/logs/test-9.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"
```

The verbose log reports `36 passed, 0 failed`; CTest reports `1/1` passed.

**Required gates:**

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-98.r1/logs/gate-3.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"
```

EXIT=0.

```sh
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-98.r1/logs/gate-4.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"
```

EXIT=0.

## Files in the commit

- `src/core/DawProjectRead.cpp`
- `src/core/DawProjectReadTracks.cpp`
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-98.md`
