# Cluster new-p10-138-port-port: DrumSynth NaN handling

**Verdict: FIXED**

## Evidence

`GetPrivateProfileFloat` accepted `%f`'s `nan` result in `src/core/DrumSynth.cpp`; `General.Level` feeds `DGain` and generated samples were not checked for finiteness before integer conversion. The fix rejects nonfinite profile and envelope values, and guards generated samples before the bit-depth `int` conversion and before output clipping/conversion. A deterministic `Level=nan` render comparison was added to `tests/src/core/SampleBufferResampleTest.cpp`.

## Verification commands

| Phase | Command | EXIT | Evidence |
|---|---|---:|---|
| Build regression target (fixed) | `cmake --build build --target zene SampleBufferResampleTest` | 0 | build-1.log |
| GREEN after fix | `cd build/tests && ctest -R '^SampleBufferResampleTest$' --output-on-failure` | 0 | test-1.log: 1/1 passed. |
| Remove production patch | `git diff -- src/core/DrumSynth.cpp > .../fix.patch && git apply -R .../fix.patch` | 0 | required removal check. |
| Rebuild without fix | `cmake --build build --target zene SampleBufferResampleTest` | 0 | build-2.log |
| RED with fix removed | `cd build/tests && ctest -R '^SampleBufferResampleTest$' --output-on-failure` | 8 | test-2.log: 1/1 failed. |
| Reapply production patch | `git apply .../fix.patch` | 0 | required restoration check. |
| Rebuild with fix restored | `cmake --build build --target zene SampleBufferResampleTest` | 0 | build-3.log |
| GREEN with fix restored | `cd build/tests && ctest -R '^SampleBufferResampleTest$' --output-on-failure` | 0 | test-3.log: 1/1 passed. |
| Rebuild zene | `cmake --build build --target zene` | 0 | build-4.log |
| Complexity/file-length gate | `python3 feature_check.py <worktree>` | 0 | gate-1.log: RESULT: PASS |

## Files committed

- `src/core/DrumSynth.cpp`
- `tests/src/core/SampleBufferResampleTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-138-port-port.md`

## Self-check

- The regression test FAILS with product change reverted and PASSES with it: reverted DrumSynth.cpp via `git apply -R fix.patch` (exit 0), rebuild (exit 0), ctest exit 8 (1/1 failed, test-2.log); reapply (exit 0), rebuild (exit 0), ctest exit 0 (test-3.log).
- The test is deterministic: no sleeps/timing/races; it injects a `Level=nan` profile value and compares the rendered int16 sample vector to the `Level=0` default render, using QTemporaryDir and in-process GetDSFileSamples.
- The fix is at the cause and complete: changed storeEnvelopeValue (all envelope points), GetPrivateProfileFloat (all profile floats incl. Level), and the two sample conversion sites (bit-resolution int conversion and output clipping/conversion); callers unchanged and consistent.
- No collateral damage: nonfinite values fall back to the documented defaults (0 for env/profile as before for malformed tokens), finite values untouched; no new limits beyond finiteness.
- Verdict is FIXED (not ALREADY-FIXED/FALSE-POSITIVE).
