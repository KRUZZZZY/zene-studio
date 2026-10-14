# Cluster new-p10-492: OSS signed 16-bit clipping

**Verdict: FIXED**

`AudioOss::run()` converted rendered floating-point samples directly to `int16_t`. Out-of-range conversion could produce invalid PCM. It now calls the shared OSS converter, which maps NaN to silence and clips other samples with `AudioEngine::clip()` before scaling. In-range scaling is unchanged, and the audio loop adds no allocation, lock, or logging.

## File evidence

- [AudioOss.cpp:235](../../src/core/audio/AudioOss.cpp:235) uses the converter in the OSS output loop.
- [OssSampleConversion.h:27](../../src/core/audio/OssSampleConversion.h:27) sanitizes NaN and clips before signed 16-bit conversion.
- [DeviceDspTest.cpp:210](../../tests/src/core/DeviceDspTest.cpp:210) checks positive and negative over-range samples, in-range scaling, and NaN.
- The new helper is registered in `tests/fork-sources.txt`; the inherited OSS source change is recorded in `tests/upstream-modifications.txt`.

## Regression evidence

| Run | Exact command | Exit / result |
|---|---|---|
| RED build | `cmake --build build --target zene DeviceDspTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-492.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| RED test | `ctest -R DeviceDspTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-492.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8; assertion failed: actual `-32768`, expected clipped `-32767` |
| GREEN build | `cmake --build build --target zene DeviceDspTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-492.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| GREEN test | `ctest -R DeviceDspTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-492.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0; 1/1 CTest tests passed |

The required rollback proof used this product-only patch:

| Run | Exact command | Exit / result |
|---|---|---|
| Save fix patch | `git diff -- src/core/audio/AudioOss.cpp src/core/audio/OssSampleConversion.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-492.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Reverse fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-492.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rollback build | `cmake --build build --target zene DeviceDspTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-492.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Rollback test | `ctest -R DeviceDspTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-492.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | 8; the same assertion failed with actual `-32768`, expected `-32767` |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-492.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Final build | `cmake --build build --target zene DeviceDspTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-492.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Final test | `ctest -R DeviceDspTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-492.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0; 1/1 CTest tests passed |

## Gates

| Gate | Exact command | Exit |
|---|---|---|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-492.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-492.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

## Files committed

- `src/core/audio/AudioOss.cpp`
- `src/core/audio/OssSampleConversion.h`
- `tests/src/core/DeviceDspTest.cpp`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-492.md`
