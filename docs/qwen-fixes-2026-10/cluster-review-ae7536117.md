# Cluster review-ae7536117

## Verdict
ALREADY-FIXED

## Finding
For note start exactly INT32_MAX, clamped end==start and event sort puts note-off before note-on, yielding stuck note.

## Evidence
The bug was fixed in commit `969aff1b4` ("fix(rework ae7536117): a note whose end passed the largest tick was saved with its note-off clamped onto its note-on").

The fix added `notesFitInSmf()` at `src/core/ControlCommandsLibraryClips.cpp:104-113` which checks if any note's end would exceed `INT32_MAX` (computed as `qint64` to avoid overflow) and refuses the save if so. This is called before `smfOf()` at line 172.

The existing test at `tests/src/core/LibraryBrowserTest.cpp:220` already covers the exact endpoint:
```cpp
QCOMPARE(saveNote(top, 1, QStringLiteral("exact")).errorKind, ControlErrorKind::Refused);
```

## Commands and Results
```
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-review-ae7536117/build/tests
QT_QPA_PLATFORM=offscreen ctest -R LibraryBrowserTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/review-ae7536117.r1/logs/test-1.log 2>&1
EXIT=0
```

Test output:
```
Test project /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-review-ae7536117/build/tests
    Start 263: LibraryBrowserTest
1/1 Test #263: LibraryBrowserTest ...............   Passed    1.64 sec

100% tests passed out of 1

Total Test time (real) =   1.66 sec
```

## Files
- `src/core/ControlCommandsLibraryClips.cpp:104-113` - `notesFitInSmf()` validation
- `src/core/ControlCommandsLibraryClips.cpp:172-176` - refusal check in `writeClip()`
- `tests/src/core/LibraryBrowserTest.cpp:206-267` - test `aNoteNearTheTopOfTheRangeIsRefusedOrWrittenExactly()`
