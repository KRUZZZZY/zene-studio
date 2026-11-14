# gui-1114-tab-mixer-mute-master — evidence logs

Verbatim output of every command cited by `../cluster-gui-1114-tab-mixer-mute-master.md`. The lane's
LOG FOLDER (`~/.local/share/zene-bunny-fix/qwen/runs/gui-1114-tab-mixer-mute-master.r1/logs`) is blocked
by the session sandbox (`15-log-folder-write.log` below), so the logs are kept here, next to the report.
Each `## NN-name` section is the `NN-name.log` the report names.

## 01-probe-strip-and-region.log

```text
********* Start testing of MixerSelectionIdentityTest *********
Config: Using QtTest library 6.12.0, Qt 6.12.0 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 16.2.1 20260810), cachyos unknown
INFO   : MixerSelectionIdentityTest::initTestCase() entering
QDEBUG : MixerSelectionIdentityTest::initTestCase() Lv2 plugin SUMMARY: 36 of 36  loaded in 8 msecs.
MainWindow: unattended run: skipping the first-run setup dialog; configure audio and MIDI in the --config file
QWARN  : MixerSelectionIdentityTest::initTestCase() This plugin does not support propagateSizeHints()
QINFO  : MixerSelectionIdentityTest::initTestCase() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QWARN  : MixerSelectionIdentityTest::initTestCase() QWidget::setMinimumSize: (/lmms::gui::PositionLine) Negative sizes (0,-32) are not possible
QWARN  : MixerSelectionIdentityTest::initTestCase() QWidget::setMaximumSize: (/lmms::gui::PositionLine) Negative sizes (16777215,-32) are not possible
PASS   : MixerSelectionIdentityTest::initTestCase()
INFO   : MixerSelectionIdentityTest::probeSplitterHints() entering
QWARN  : MixerSelectionIdentityTest::probeSplitterHints() This plugin does not support propagateSizeHints()
QWARN  : MixerSelectionIdentityTest::probeSplitterHints() PROBE t0 rowsMinHint=301 rowsH=885 sizes=583,300 bottomH=300 bottomMinHint=474 winMinH=416 muteY=966
QWARN  : MixerSelectionIdentityTest::probeSplitterHints() This plugin does not support propagateSizeHints()
QWARN  : MixerSelectionIdentityTest::probeSplitterHints() PROBE t1 rowsMinHint=353 sizes=583,300 bottomH=300 muteY=966 winMinH=468
QWARN  : MixerSelectionIdentityTest::probeSplitterHints() PROBE t2 sizes=409,474 bottomH=474 muteY=792 winMinH=468
QWARN  : MixerSelectionIdentityTest::probeSplitterHints() PROBE t3 sizes=409,474 bottomH=474 muteY=792 onScreen=1 winMinH=468
QWARN  : MixerSelectionIdentityTest::probeSplitterHints() PROBE t4 mute=1 checked=1 devices=0
PASS   : MixerSelectionIdentityTest::probeSplitterHints()
INFO   : MixerSelectionIdentityTest::cleanupTestCase() entering
PASS   : MixerSelectionIdentityTest::cleanupTestCase()
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 2468ms
********* Finished testing of MixerSelectionIdentityTest *********
```

## 02-probe-footer-hit.log

```text
********* Start testing of MixerSelectionIdentityTest *********
Config: Using QtTest library 6.12.0, Qt 6.12.0 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 16.2.1 20260810), cachyos unknown
INFO   : MixerSelectionIdentityTest::initTestCase() entering
QDEBUG : MixerSelectionIdentityTest::initTestCase() Lv2 plugin SUMMARY: 36 of 36  loaded in 8 msecs.
MainWindow: unattended run: skipping the first-run setup dialog; configure audio and MIDI in the --config file
QWARN  : MixerSelectionIdentityTest::initTestCase() This plugin does not support propagateSizeHints()
QINFO  : MixerSelectionIdentityTest::initTestCase() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QWARN  : MixerSelectionIdentityTest::initTestCase() QWidget::setMinimumSize: (/lmms::gui::PositionLine) Negative sizes (0,-32) are not possible
QWARN  : MixerSelectionIdentityTest::initTestCase() QWidget::setMaximumSize: (/lmms::gui::PositionLine) Negative sizes (16777215,-32) are not possible
PASS   : MixerSelectionIdentityTest::initTestCase()
INFO   : MixerSelectionIdentityTest::probeFooterSwitch() entering
QWARN  : MixerSelectionIdentityTest::probeFooterSwitch() This plugin does not support propagateSizeHints()
QWARN  : MixerSelectionIdentityTest::probeFooterSwitch() PROBE at(292,976) class=lmms::gui::ShellButton name= acc=Devices tip=Devices (Alt+D) cmd= geom=268,973 77x24
QWARN  : MixerSelectionIdentityTest::probeFooterSwitch() PROBE mute=282,966 20x20 modelIsMasterMute=1
QWARN  : MixerSelectionIdentityTest::probeFooterSwitch() PROBE after footer click: devicesVisible=1 mute=0
PASS   : MixerSelectionIdentityTest::probeFooterSwitch()
INFO   : MixerSelectionIdentityTest::cleanupTestCase() entering
QWARN  : MixerSelectionIdentityTest::cleanupTestCase() This plugin does not support propagateSizeHints()
PASS   : MixerSelectionIdentityTest::cleanupTestCase()
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 2464ms
********* Finished testing of MixerSelectionIdentityTest *********
```

## 03-probe-fit-at-heights.log

```text
********* Start testing of MixerSelectionIdentityTest *********
Config: Using QtTest library 6.12.0, Qt 6.12.0 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 16.2.1 20260810), cachyos unknown
INFO   : MixerSelectionIdentityTest::initTestCase() entering
QDEBUG : MixerSelectionIdentityTest::initTestCase() Lv2 plugin SUMMARY: 36 of 36  loaded in 10 msecs.
MainWindow: unattended run: skipping the first-run setup dialog; configure audio and MIDI in the --config file
QWARN  : MixerSelectionIdentityTest::initTestCase() This plugin does not support propagateSizeHints()
QINFO  : MixerSelectionIdentityTest::initTestCase() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QWARN  : MixerSelectionIdentityTest::initTestCase() QWidget::setMinimumSize: (/lmms::gui::PositionLine) Negative sizes (0,-32) are not possible
QWARN  : MixerSelectionIdentityTest::initTestCase() QWidget::setMaximumSize: (/lmms::gui::PositionLine) Negative sizes (16777215,-32) are not possible
PASS   : MixerSelectionIdentityTest::initTestCase()
INFO   : MixerSelectionIdentityTest::probeSizesFit() entering
QWARN  : MixerSelectionIdentityTest::probeSizesFit() This plugin does not support propagateSizeHints()
QWARN  : MixerSelectionIdentityTest::probeSizesFit() This plugin does not support propagateSizeHints()
QWARN  : MixerSelectionIdentityTest::probeSizesFit() PROBE req=1600x1000 win=1600x1000 winMinH=468 rowsH=885 sizes=409,474 need=474 shown=20x20
QWARN  : MixerSelectionIdentityTest::probeSizesFit() PROBE req=1280x800 win=1280x800 winMinH=468 rowsH=685 sizes=231,452 need=474 shown=20x20
QWARN  : MixerSelectionIdentityTest::probeSizesFit() PROBE req=1024x768 win=1024x768 winMinH=468 rowsH=653 sizes=231,420 need=474 shown=20x20
QWARN  : MixerSelectionIdentityTest::probeSizesFit() PROBE req=1280x720 win=1280x720 winMinH=468 rowsH=605 sizes=231,372 need=474 shown=20x20
QWARN  : MixerSelectionIdentityTest::probeSizesFit() PROBE req=1600x1000 win=1600x1000 winMinH=468 rowsH=885 sizes=409,474 need=474 shown=20x20
PASS   : MixerSelectionIdentityTest::probeSizesFit()
INFO   : MixerSelectionIdentityTest::cleanupTestCase() entering
PASS   : MixerSelectionIdentityTest::cleanupTestCase()
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 2648ms
********* Finished testing of MixerSelectionIdentityTest *********
```

## 04-build-strict-variant.log

```text
[  0%] Built target zene_api_autogen_timestamp_deps
[  0%] Built target ringbuffer
[  0%] Built target zene_samplerate
[  0%] Built target lua
[  0%] Built target zene_ableton_link_autogen_timestamp_deps
[  0%] Built target zene_api_autogen
[  0%] Built target weakjack
[  0%] Built target zene_ableton_link_autogen
[  0%] Built target zene_ableton_link
[  6%] Built target zene_api
[  6%] Built target lmmsobjs_autogen_timestamp_deps
[  6%] Built target lmmsobjs_autogen
[100%] Built target lmmsobjs
[100%] Built target MixerSelectionIdentityTest_autogen_timestamp_deps
[100%] Automatic MOC for target MixerSelectionIdentityTest
[100%] Built target MixerSelectionIdentityTest_autogen
[100%] Building CXX object tests/CMakeFiles/MixerSelectionIdentityTest.dir/src/gui/MixerSelectionIdentityTest.cpp.o
[100%] Linking CXX executable MixerSelectionIdentityTest
[100%] Built target MixerSelectionIdentityTest
```

## 05-test-strict-variant.log

```text
********* Start testing of MixerSelectionIdentityTest *********
Config: Using QtTest library 6.12.0, Qt 6.12.0 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 16.2.1 20260810), cachyos unknown
INFO   : MixerSelectionIdentityTest::initTestCase() entering
QDEBUG : MixerSelectionIdentityTest::initTestCase() Lv2 plugin SUMMARY: 36 of 36  loaded in 9 msecs.
MainWindow: unattended run: skipping the first-run setup dialog; configure audio and MIDI in the --config file
QWARN  : MixerSelectionIdentityTest::initTestCase() This plugin does not support propagateSizeHints()
QINFO  : MixerSelectionIdentityTest::initTestCase() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QWARN  : MixerSelectionIdentityTest::initTestCase() QWidget::setMinimumSize: (/lmms::gui::PositionLine) Negative sizes (0,-32) are not possible
QWARN  : MixerSelectionIdentityTest::initTestCase() QWidget::setMaximumSize: (/lmms::gui::PositionLine) Negative sizes (16777215,-32) are not possible
PASS   : MixerSelectionIdentityTest::initTestCase()
INFO   : MixerSelectionIdentityTest::theMasterMuteButtonTogglesAndDoesNotOpenDevices() entering
QWARN  : MixerSelectionIdentityTest::theMasterMuteButtonTogglesAndDoesNotOpenDevices() This plugin does not support propagateSizeHints()
QWARN  : MixerSelectionIdentityTest::theMasterMuteButtonTogglesAndDoesNotOpenDevices() This plugin does not support propagateSizeHints()
FAIL!  : MixerSelectionIdentityTest::theMasterMuteButtonTogglesAndDoesNotOpenDevices() 'regionShown == inRegion' returned FALSE. (only 20x4 of the 20x20 mute at 282,966 is on screen in the bottom region)
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1114-tab-mixer-mute-master/tests/src/gui/MixerSelectionIdentityTest.cpp(346)]
INFO   : MixerSelectionIdentityTest::cleanupTestCase() entering
QWARN  : MixerSelectionIdentityTest::cleanupTestCase() This plugin does not support propagateSizeHints()
PASS   : MixerSelectionIdentityTest::cleanupTestCase()
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 2418ms
********* Finished testing of MixerSelectionIdentityTest *********
```

## 06-build-guard.log

```text
[  0%] Built target zene_ableton_link_autogen_timestamp_deps
[  0%] Built target zene_api_autogen_timestamp_deps
[  0%] Built target zene_samplerate
[  0%] Built target ringbuffer
[  0%] Built target lua
[  0%] Built target zene_ableton_link_autogen
[  0%] Built target zene_api_autogen
[  0%] Built target weakjack
[  0%] Built target zene_ableton_link
[  6%] Built target zene_api
[  6%] Built target lmmsobjs_autogen_timestamp_deps
[  6%] Built target lmmsobjs_autogen
[100%] Built target lmmsobjs
[100%] Built target MixerSelectionIdentityTest_autogen_timestamp_deps
[100%] Built target MixerSelectionIdentityTest_autogen
[100%] Built target MixerSelectionIdentityTest
```

## 07-test-guard.log

```text
********* Start testing of MixerSelectionIdentityTest *********
Config: Using QtTest library 6.12.0, Qt 6.12.0 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 16.2.1 20260810), cachyos unknown
INFO   : MixerSelectionIdentityTest::initTestCase() entering
QDEBUG : MixerSelectionIdentityTest::initTestCase() Lv2 plugin SUMMARY: 36 of 36  loaded in 6 msecs.
MainWindow: unattended run: skipping the first-run setup dialog; configure audio and MIDI in the --config file
QWARN  : MixerSelectionIdentityTest::initTestCase() This plugin does not support propagateSizeHints()
QINFO  : MixerSelectionIdentityTest::initTestCase() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QWARN  : MixerSelectionIdentityTest::initTestCase() QWidget::setMinimumSize: (/lmms::gui::PositionLine) Negative sizes (0,-32) are not possible
QWARN  : MixerSelectionIdentityTest::initTestCase() QWidget::setMaximumSize: (/lmms::gui::PositionLine) Negative sizes (16777215,-32) are not possible
PASS   : MixerSelectionIdentityTest::initTestCase()
INFO   : MixerSelectionIdentityTest::aRemovalBeforeTheSelectedChannelKeepsTheSelection() entering
PASS   : MixerSelectionIdentityTest::aRemovalBeforeTheSelectedChannelKeepsTheSelection()
INFO   : MixerSelectionIdentityTest::removingTheSelectedLastChannelFallsBackToMaster() entering
PASS   : MixerSelectionIdentityTest::removingTheSelectedLastChannelFallsBackToMaster()
INFO   : MixerSelectionIdentityTest::aReorderKeepsTheSelectedChannelSelected() entering
PASS   : MixerSelectionIdentityTest::aReorderKeepsTheSelectedChannelSelected()
INFO   : MixerSelectionIdentityTest::aReorderRebindsEveryStripAndKeepsTheSelection() entering
PASS   : MixerSelectionIdentityTest::aReorderRebindsEveryStripAndKeepsTheSelection()
INFO   : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() entering
QWARN  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() This plugin does not support propagateSizeHints()
QWARN  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() This plugin does not support raise()
QWARN  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() This plugin does not support propagateSizeHints()
QWARN  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() This plugin does not support raise()
QWARN  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() This plugin does not support propagateSizeHints()
QWARN  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() This plugin does not support raise()
QWARN  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() This plugin does not support propagateSizeHints()
QWARN  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() This plugin does not support raise()
PASS   : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment()
INFO   : MixerSelectionIdentityTest::theMasterMuteButtonTogglesInTheMixerView() entering
QWARN  : MixerSelectionIdentityTest::theMasterMuteButtonTogglesInTheMixerView() This plugin does not support propagateSizeHints()
PASS   : MixerSelectionIdentityTest::theMasterMuteButtonTogglesInTheMixerView()
INFO   : MixerSelectionIdentityTest::aPressOnTheVisiblePartOfTheBottomRegionsMasterMuteTogglesIt() entering
QWARN  : MixerSelectionIdentityTest::aPressOnTheVisiblePartOfTheBottomRegionsMasterMuteTogglesIt() This plugin does not support propagateSizeHints()
PASS   : MixerSelectionIdentityTest::aPressOnTheVisiblePartOfTheBottomRegionsMasterMuteTogglesIt()
INFO   : MixerSelectionIdentityTest::aNewReturnStripIsLabelledReturnAtOnce() entering
PASS   : MixerSelectionIdentityTest::aNewReturnStripIsLabelledReturnAtOnce()
INFO   : MixerSelectionIdentityTest::theReturnButtonStaysOnAThousandPixelScreen() entering
QWARN  : MixerSelectionIdentityTest::theReturnButtonStaysOnAThousandPixelScreen() This plugin does not support propagateSizeHints()
QWARN  : MixerSelectionIdentityTest::theReturnButtonStaysOnAThousandPixelScreen() This plugin does not support propagateSizeHints()
PASS   : MixerSelectionIdentityTest::theReturnButtonStaysOnAThousandPixelScreen()
INFO   : MixerSelectionIdentityTest::theAddEffectButtonStaysOnAThousandPixelScreen() entering
PASS   : MixerSelectionIdentityTest::theAddEffectButtonStaysOnAThousandPixelScreen()
INFO   : MixerSelectionIdentityTest::cleanupTestCase() entering
PASS   : MixerSelectionIdentityTest::cleanupTestCase()
Totals: 12 passed, 0 failed, 0 skipped, 0 blacklisted, 2841ms
********* Finished testing of MixerSelectionIdentityTest *********
```

## 08-mutation.patch

```text
diff --git a/src/gui/shell/ShellSelection.cpp b/src/gui/shell/ShellSelection.cpp
index 8b4f4f05ee..3fbb8dff8e 100644
--- a/src/gui/shell/ShellSelection.cpp
+++ b/src/gui/shell/ShellSelection.cpp
@@ -187,7 +187,7 @@ bool ShellSelection::eventFilter(QObject* watched, QEvent* event)
 			break;
 		}
 	}
-	return false;
+	return true;
 }
 
 } // namespace lmms::gui
```

## 09-build-mutation.log

```text
[  0%] Built target zene_ableton_link_autogen_timestamp_deps
[  0%] Built target zene_api_autogen_timestamp_deps
[  0%] Built target zene_samplerate
[  0%] Built target ringbuffer
[  0%] Built target lua
[  0%] Built target zene_ableton_link_autogen
[  0%] Built target weakjack
[  0%] Built target zene_api_autogen
[  0%] Built target zene_ableton_link
[  6%] Built target zene_api
[  6%] Built target lmmsobjs_autogen_timestamp_deps
[  6%] Automatic MOC and UIC for target lmmsobjs
[  6%] Built target lmmsobjs_autogen
[  6%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/shell/ShellSelection.cpp.o
[100%] Built target lmmsobjs
[100%] Built target zene_autogen_timestamp_deps
[100%] Built target zene_autogen
[100%] Linking CXX executable ../zene
[100%] Built target zene
[  0%] Built target ringbuffer
[  0%] Built target zene_ableton_link_autogen_timestamp_deps
[  0%] Built target zene_api_autogen_timestamp_deps
[  0%] Built target lua
[  0%] Built target zene_samplerate
[  0%] Built target zene_api_autogen
[  0%] Built target weakjack
[  0%] Built target zene_ableton_link_autogen
[  0%] Built target zene_ableton_link
[  6%] Built target zene_api
[  6%] Built target lmmsobjs_autogen_timestamp_deps
[  6%] Built target lmmsobjs_autogen
[100%] Built target lmmsobjs
[100%] Built target MixerSelectionIdentityTest_autogen_timestamp_deps
[100%] Built target MixerSelectionIdentityTest_autogen
[100%] Linking CXX executable MixerSelectionIdentityTest
[100%] Built target MixerSelectionIdentityTest
```

## 10-test-mutation.log

```text
********* Start testing of MixerSelectionIdentityTest *********
Config: Using QtTest library 6.12.0, Qt 6.12.0 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 16.2.1 20260810), cachyos unknown
INFO   : MixerSelectionIdentityTest::initTestCase() entering
QDEBUG : MixerSelectionIdentityTest::initTestCase() Lv2 plugin SUMMARY: 36 of 36  loaded in 8 msecs.
MainWindow: unattended run: skipping the first-run setup dialog; configure audio and MIDI in the --config file
QWARN  : MixerSelectionIdentityTest::initTestCase() This plugin does not support propagateSizeHints()
QINFO  : MixerSelectionIdentityTest::initTestCase() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QWARN  : MixerSelectionIdentityTest::initTestCase() QWidget::setMinimumSize: (/lmms::gui::PositionLine) Negative sizes (0,-32) are not possible
QWARN  : MixerSelectionIdentityTest::initTestCase() QWidget::setMaximumSize: (/lmms::gui::PositionLine) Negative sizes (16777215,-32) are not possible
PASS   : MixerSelectionIdentityTest::initTestCase()
INFO   : MixerSelectionIdentityTest::aRemovalBeforeTheSelectedChannelKeepsTheSelection() entering
PASS   : MixerSelectionIdentityTest::aRemovalBeforeTheSelectedChannelKeepsTheSelection()
INFO   : MixerSelectionIdentityTest::removingTheSelectedLastChannelFallsBackToMaster() entering
PASS   : MixerSelectionIdentityTest::removingTheSelectedLastChannelFallsBackToMaster()
INFO   : MixerSelectionIdentityTest::aReorderKeepsTheSelectedChannelSelected() entering
PASS   : MixerSelectionIdentityTest::aReorderKeepsTheSelectedChannelSelected()
INFO   : MixerSelectionIdentityTest::aReorderRebindsEveryStripAndKeepsTheSelection() entering
PASS   : MixerSelectionIdentityTest::aReorderRebindsEveryStripAndKeepsTheSelection()
INFO   : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() entering
FAIL!  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() 'selectedWhenActionRan' returned FALSE. ()
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1114-tab-mixer-mute-master/tests/src/gui/MixerSelectionIdentityTest.cpp(246)]
FAIL!  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() 'selectedWhenActionRan' returned FALSE. ()
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1114-tab-mixer-mute-master/tests/src/gui/MixerSelectionIdentityTest.cpp(246)]
FAIL!  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() 'selectedWhenActionRan' returned FALSE. ()
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1114-tab-mixer-mute-master/tests/src/gui/MixerSelectionIdentityTest.cpp(246)]
FAIL!  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() 'selectedWhenMacroActionRan' returned FALSE. ()
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1114-tab-mixer-mute-master/tests/src/gui/MixerSelectionIdentityTest.cpp(276)]
INFO   : MixerSelectionIdentityTest::theMasterMuteButtonTogglesInTheMixerView() entering
QWARN  : MixerSelectionIdentityTest::theMasterMuteButtonTogglesInTheMixerView() This plugin does not support propagateSizeHints()
FAIL!  : MixerSelectionIdentityTest::theMasterMuteButtonTogglesInTheMixerView() 'on' returned FALSE. (a real press on Master mute did not turn the mute model on)
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1114-tab-mixer-mute-master/tests/src/gui/MixerSelectionIdentityTest.cpp(332)]
INFO   : MixerSelectionIdentityTest::aPressOnTheVisiblePartOfTheBottomRegionsMasterMuteTogglesIt() entering
QWARN  : MixerSelectionIdentityTest::aPressOnTheVisiblePartOfTheBottomRegionsMasterMuteTogglesIt() This plugin does not support propagateSizeHints()
FAIL!  : MixerSelectionIdentityTest::aPressOnTheVisiblePartOfTheBottomRegionsMasterMuteTogglesIt() 'on' returned FALSE. (a press on the visible part of the bottom region's mute did not toggle it)
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1114-tab-mixer-mute-master/tests/src/gui/MixerSelectionIdentityTest.cpp(385)]
INFO   : MixerSelectionIdentityTest::aNewReturnStripIsLabelledReturnAtOnce() entering
FAIL!  : MixerSelectionIdentityTest::aNewReturnStripIsLabelledReturnAtOnce() Compared values are not the same
   Actual   (static_cast<int>(Engine::mixer()->numChannels())): 8
   Expected (before + 1)                                      : 9
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1114-tab-mixer-mute-master/tests/src/gui/MixerSelectionIdentityTest.cpp(405)]
INFO   : MixerSelectionIdentityTest::theReturnButtonStaysOnAThousandPixelScreen() entering
QWARN  : MixerSelectionIdentityTest::theReturnButtonStaysOnAThousandPixelScreen() This plugin does not support propagateSizeHints()
PASS   : MixerSelectionIdentityTest::theReturnButtonStaysOnAThousandPixelScreen()
INFO   : MixerSelectionIdentityTest::theAddEffectButtonStaysOnAThousandPixelScreen() entering
QWARN  : MixerSelectionIdentityTest::theAddEffectButtonStaysOnAThousandPixelScreen() This plugin does not support propagateSizeHints()
PASS   : MixerSelectionIdentityTest::theAddEffectButtonStaysOnAThousandPixelScreen()
INFO   : MixerSelectionIdentityTest::cleanupTestCase() entering
PASS   : MixerSelectionIdentityTest::cleanupTestCase()
Totals: 8 passed, 4 failed, 0 skipped, 0 blacklisted, 2703ms
********* Finished testing of MixerSelectionIdentityTest *********
```

## 11-build-reapplied.log

```text
[  0%] Built target zene_ableton_link_autogen_timestamp_deps
[  0%] Built target zene_api_autogen_timestamp_deps
[  0%] Built target ringbuffer
[  0%] Built target lua
[  0%] Built target zene_samplerate
[  0%] Built target zene_api_autogen
[  0%] Built target weakjack
[  0%] Built target zene_ableton_link_autogen
[  0%] Built target zene_ableton_link
[  6%] Built target zene_api
[  6%] Built target lmmsobjs_autogen_timestamp_deps
[  6%] Automatic MOC and UIC for target lmmsobjs
[  6%] Built target lmmsobjs_autogen
[  6%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/shell/ShellSelection.cpp.o
[100%] Built target lmmsobjs
[100%] Built target zene_autogen_timestamp_deps
[100%] Built target zene_autogen
[100%] Linking CXX executable ../zene
[100%] Built target zene
[  0%] Built target zene_ableton_link_autogen_timestamp_deps
[  0%] Built target zene_api_autogen_timestamp_deps
[  0%] Built target ringbuffer
[  0%] Built target zene_samplerate
[  0%] Built target lua
[  0%] Built target zene_api_autogen
[  0%] Built target weakjack
[  0%] Built target zene_ableton_link_autogen
[  0%] Built target zene_ableton_link
[  6%] Built target zene_api
[  6%] Built target lmmsobjs_autogen_timestamp_deps
[  6%] Built target lmmsobjs_autogen
[100%] Built target lmmsobjs
[100%] Built target MixerSelectionIdentityTest_autogen_timestamp_deps
[100%] Built target MixerSelectionIdentityTest_autogen
[100%] Linking CXX executable MixerSelectionIdentityTest
[100%] Built target MixerSelectionIdentityTest
```

## 12-test-reapplied.log

```text
********* Start testing of MixerSelectionIdentityTest *********
Config: Using QtTest library 6.12.0, Qt 6.12.0 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 16.2.1 20260810), cachyos unknown
INFO   : MixerSelectionIdentityTest::initTestCase() entering
QDEBUG : MixerSelectionIdentityTest::initTestCase() Lv2 plugin SUMMARY: 36 of 36  loaded in 8 msecs.
MainWindow: unattended run: skipping the first-run setup dialog; configure audio and MIDI in the --config file
QWARN  : MixerSelectionIdentityTest::initTestCase() This plugin does not support propagateSizeHints()
QINFO  : MixerSelectionIdentityTest::initTestCase() plugin-scan: 0 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 0 scanned, 0 plugin(s)
QWARN  : MixerSelectionIdentityTest::initTestCase() QWidget::setMinimumSize: (/lmms::gui::PositionLine) Negative sizes (0,-32) are not possible
QWARN  : MixerSelectionIdentityTest::initTestCase() QWidget::setMaximumSize: (/lmms::gui::PositionLine) Negative sizes (16777215,-32) are not possible
PASS   : MixerSelectionIdentityTest::initTestCase()
INFO   : MixerSelectionIdentityTest::aRemovalBeforeTheSelectedChannelKeepsTheSelection() entering
PASS   : MixerSelectionIdentityTest::aRemovalBeforeTheSelectedChannelKeepsTheSelection()
INFO   : MixerSelectionIdentityTest::removingTheSelectedLastChannelFallsBackToMaster() entering
PASS   : MixerSelectionIdentityTest::removingTheSelectedLastChannelFallsBackToMaster()
INFO   : MixerSelectionIdentityTest::aReorderKeepsTheSelectedChannelSelected() entering
PASS   : MixerSelectionIdentityTest::aReorderKeepsTheSelectedChannelSelected()
INFO   : MixerSelectionIdentityTest::aReorderRebindsEveryStripAndKeepsTheSelection() entering
PASS   : MixerSelectionIdentityTest::aReorderRebindsEveryStripAndKeepsTheSelection()
INFO   : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() entering
QWARN  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() This plugin does not support propagateSizeHints()
QWARN  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() This plugin does not support raise()
QWARN  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() This plugin does not support propagateSizeHints()
QWARN  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() This plugin does not support raise()
QWARN  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() This plugin does not support propagateSizeHints()
QWARN  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() This plugin does not support raise()
QWARN  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() This plugin does not support propagateSizeHints()
QWARN  : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment() This plugin does not support raise()
PASS   : MixerSelectionIdentityTest::pressingAChildControlSelectsItsStripAndRoutesKeyboardFaderAdjustment()
INFO   : MixerSelectionIdentityTest::theMasterMuteButtonTogglesInTheMixerView() entering
QWARN  : MixerSelectionIdentityTest::theMasterMuteButtonTogglesInTheMixerView() This plugin does not support propagateSizeHints()
PASS   : MixerSelectionIdentityTest::theMasterMuteButtonTogglesInTheMixerView()
INFO   : MixerSelectionIdentityTest::aPressOnTheVisiblePartOfTheBottomRegionsMasterMuteTogglesIt() entering
QWARN  : MixerSelectionIdentityTest::aPressOnTheVisiblePartOfTheBottomRegionsMasterMuteTogglesIt() This plugin does not support propagateSizeHints()
PASS   : MixerSelectionIdentityTest::aPressOnTheVisiblePartOfTheBottomRegionsMasterMuteTogglesIt()
INFO   : MixerSelectionIdentityTest::aNewReturnStripIsLabelledReturnAtOnce() entering
PASS   : MixerSelectionIdentityTest::aNewReturnStripIsLabelledReturnAtOnce()
INFO   : MixerSelectionIdentityTest::theReturnButtonStaysOnAThousandPixelScreen() entering
QWARN  : MixerSelectionIdentityTest::theReturnButtonStaysOnAThousandPixelScreen() This plugin does not support propagateSizeHints()
PASS   : MixerSelectionIdentityTest::theReturnButtonStaysOnAThousandPixelScreen()
INFO   : MixerSelectionIdentityTest::theAddEffectButtonStaysOnAThousandPixelScreen() entering
PASS   : MixerSelectionIdentityTest::theAddEffectButtonStaysOnAThousandPixelScreen()
INFO   : MixerSelectionIdentityTest::cleanupTestCase() entering
PASS   : MixerSelectionIdentityTest::cleanupTestCase()
Totals: 12 passed, 0 failed, 0 skipped, 0 blacklisted, 2863ms
********* Finished testing of MixerSelectionIdentityTest *********
```

## 13-family-ctest.log

```text
Test project /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1114-tab-mixer-mute-master/build/tests
    Start 437: ShellTest
1/8 Test #437: ShellTest ........................   Passed    0.35 sec
    Start 438: ShellFloatButtonTest
2/8 Test #438: ShellFloatButtonTest .............   Passed    0.15 sec
    Start 439: ShellHomesTest
3/8 Test #439: ShellHomesTest ...................   Passed    0.32 sec
    Start 484: MixerModesTest
4/8 Test #484: MixerModesTest ...................   Passed    2.69 sec
    Start 487: MixerZonesTest
5/8 Test #487: MixerZonesTest ...................   Passed    3.00 sec
    Start 488: MixerPinnedStripsTest
6/8 Test #488: MixerPinnedStripsTest ............   Passed    3.46 sec
    Start 489: MixerSelectionIdentityTest
7/8 Test #489: MixerSelectionIdentityTest .......   Passed    2.95 sec
    Start 604: PanelShortcutsTest
8/8 Test #604: PanelShortcutsTest ...............   Passed    3.37 sec

100% tests passed out of 8

Total Test time (real) =  16.30 sec
```

## 14-feature-check.log

```text
feature-check zene-qwen-gui-1114-tab-mixer-mute-master (base 779829655, 3 changed files; logs in /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-gui-1114-tab-mixer-mute-master/build/feature-check)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## 15-log-folder-write.log

```text
WRITE FAILED: PermissionError(13, 'Permission denied')
dir: True writable: True
```
