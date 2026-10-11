# Cluster new-p11-22: frame trim options on MIDI clips

**Verdict: FIXED**

## Evidence

`src/core/ControlCommandsClipFrames.cpp:116-120` makes `wanted()` false for a non-`SampleClip`. `clip.trim` then falls through to its tick-based trim path, which ignores `start_frames`, `end_frames`, and an explicit `zero_crossing` option.

`src/core/ControlCommandsClipTrim.cpp:139-143` now identifies explicit frame trim options. At lines 183-189 the handler refuses those options with `InvalidArgs` when the resolved clip is not a `SampleClip`; ordinary MIDI tick trims without frame options retain their existing path. The regression at `tests/src/core/ControlVerbInverseTest.cpp:221-239` resolves a MIDI clip, supplies a valid start tick plus `start_frames: 12`, and checks the refusal and unchanged position and length.

## Verification

Build command, run seven times with exit code **0** each time (`build-1.log` through `build-7.log`):

```text
cmake --build build --target zene ControlVerbInverseTest -- -j12
```

Test command, run from `build/tests` (`test-1.log` through `test-7.log`):

```text
ctest -R ControlVerbInverseTest --output-on-failure
```

| Run(s) | Exit | Result |
| --- | ---: | --- |
| `test-1.log`, `test-3.log`, `test-6.log` | 8 | RED. The new assertion failed with `!trimmed.ok` returned false; the Qt totals were 8 passed, 1 failed. `test-6.log` is the final fix-removal proof. |
| `test-2.log`, `test-4.log`, `test-5.log`, `test-7.log` | 0 | GREEN. `test-7.log` is the final proof with the fix restored; CTest reports 1/1 test passed. |

The final proof cycle used these commands (each exit code **0**):

```text
git diff -- src/core/ControlCommandsClipTrim.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-22.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-22.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-22.r1/logs/fix.patch
```

The first command removed the production patch before `build-6.log` / `test-6.log`; the second restored it before `build-7.log` / `test-7.log`.

Both required gates passed (exit code **0** each; `gate-3.log` and `gate-4.log`):

```text
bash tests/complexity-gate.sh --check
bash tests/file-length-gate.sh --check
```

Affected family: **1 CTest target**, `ControlVerbInverseTest`; the final green run executed it and reported 1/1 passed.

## Files committed

- `src/core/ControlCommandsClipTrim.cpp`
- `tests/src/core/ControlVerbInverseTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-22.md`
