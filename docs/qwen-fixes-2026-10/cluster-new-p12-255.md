FIXED

Cluster new-p12-255 — Concurrent downloads overwrite the same partial file.

Cause and fix:
- `src/core/StemModelStore.cpp:414-422`: each download now stages into a unique
  `<name>.onnx.part.<uuid>` path, so concurrent downloads of one model never share
  or truncate one partial file.
- `src/core/StemModelStore.cpp:445-460`: after a verified staging file is ready, the
  final path is removed and the staging file renamed into place. If a concurrent
  installer wins that rename, `QFile::rename()` returns false because the destination
  now exists (Qt's documented contract: rename fails when the destination already
  exists), but the installed file verifies against the pinned spec, so this attempt
  reports success. The cleanup at `StemModelStore.cpp:458` removes this attempt's
  now-redundant staging file instead of leaving a full-size duplicate model behind.
  (`QFile::rename` does not atomically replace an existing destination on Linux;
  it returns false when the destination exists, which is exactly the case handled
  here.)

Test seam:
- `include/StemSeparation/StemModelStore.h` / `src/core/StemModelStore.cpp`:
  `StemModelStore::setDownloadRenameHook(...)` invokes a caller-supplied hook
  between `QFile::remove(finalPath)` and `QFile::rename(partPath, finalPath)`.
  Production leaves it unset.
- `tests/src/core/StemModelStoreTest.cpp`: `testDownloadRenameLoserReportsSuccessAndRemovesStaging`
  uses the hook to deterministically create a verified final file in that window,
  then asserts `download()` returns true, the final file verifies, and no staging
  file remains.

Evidence:
- RED (cleanup removed): build `cmake --build build --target StemModelStoreTest -j12` EXIT=0;
  run `cd build/tests && ./StemModelStoreTest -silent` EXIT=1,
  `21 passed, 1 failed` (`partialFiles(...).isEmpty()` returned FALSE).
- GREEN (cleanup restored): build `cmake --build build --target zene StemModelStoreTest -j12` EXIT=0;
  run `cd build/tests && ./StemModelStoreTest -silent` EXIT=0, `22 passed, 0 failed`.
- Focused ctest: `cd build/tests && ctest -R '^StemModelStoreTest$' --output-on-failure` EXIT=0, 1/1 passed.
- Stem family: `cd build/tests && ctest -R '^Stem' --output-on-failure` EXIT=0, 4/4 passed.
- Gate: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-255` EXIT=0, RESULT: PASS.

Files committed:
- include/StemSeparation/StemModelStore.h
- src/core/StemModelStore.cpp
- tests/src/core/StemModelStoreTest.cpp
- docs/qwen-fixes-2026-10/cluster-new-p12-255.md
