# Unfixed raw-pointer overload: rebuilt chain at a different address; test exit 1.

```text
********* Start testing of DeviceMovesUndoTest *********
Config: Using QtTest library 6.4.2, Qt 6.4.2 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 13.2.0), ubuntu 24.04
QDEBUG : DeviceMovesUndoTest::initTestCase() Lv2 plugin SUMMARY: 73 of 87  loaded in 113 msecs.
QDEBUG : DeviceMovesUndoTest::initTestCase() For details about not loaded plugins, please set
  environment variable "LMMS_LV2_DEBUG" to nonempty.
QDEBUG : DeviceMovesUndoTest::initTestCase() Blocked Lv2 Plugins: 10 of 87 
  If you want to enable them (dangerous!), please set
  environment variable "LMMS_ENABLE_BLOCKED_PLUGINS" to nonempty.
PASS   : DeviceMovesUndoTest::initTestCase()
QINFO  : DeviceMovesUndoTest::thePointerOverloadFindsARebuiltChain() plugin-scan: 5 file(s), 0 quarantined, 5 served from cache, 0 known-bad skipped, 0 scanned, 5 plugin(s)
FAIL!  : DeviceMovesUndoTest::thePointerOverloadFindsARebuiltChain() Compared lists have different sizes.
   Actual   (names(target)) size: 0
   Expected (QStringList{QStringLiteral("amplifier")}) size: 1
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-luna-r7/tests/src/core/DeviceMovesUndoTest.cpp(168)]
PASS   : DeviceMovesUndoTest::cleanupTestCase()
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 1278ms
********* Finished testing of DeviceMovesUndoTest *********
```
