# AutomationModesTest SIGSEGV — stacks and the reproduction, before the fix
> Committed evidence for branch `fix/automation-touch-race` (base ec07dd3a0 = main =
> tag v0.2.1-alpha). Raw tool output, quoted verbatim.

AutomationModesTest SIGSEGV on all seven CI platforms - the real stack, and the
reproduction this branch was cut from (base ec07dd3a0 = main = tag v0.2.1-alpha).

=== 1. CI, run 34757467632 / job linux-x86_64 (103724228380) ===
Test #9 failed after 2.32 s as "Subprocess aborted" (Received signal 11). The workflow's
own "gdb backtrace of failed test AutomationModesTest" step:

For help, type "help".
Type "apropos word" to search for commands related to "word".
Attaching to process 39255
(gdb) === End of stack trace ===
QFATAL : AutomationModesTest::testTouchWritesWhileHeldAndReturnsToReading() Received signal 11
         Function time: 0ms Total time: 1333ms

Thread 1 "AutomationModes" received signal SIGSEGV, Segmentation fault.
0x00007ffff62e3e48 in QObjectPrivate::maybeSignalConnected(unsigned int) const () from /lib/x86_64-linux-gnu/libQt5Core.so.5

Thread 8 (Thread 0x7ffff23ff640 (LWP 40348) "lmms::AudioEngi"):
#0  0x00007ffff58910d7 in ?? () from /lib/x86_64-linux-gnu/libc.so.6
#1  0x00007ffff5893e5b in pthread_cond_timedwait () from /lib/x86_64-linux-gnu/libc.so.6
#2  0x00007ffff60d2c7c in QWaitCondition::wait(QMutex*, QDeadlineTimer) () from /lib/x86_64-linux-gnu/libQt5Core.so.5
#3  0x00007ffff60d2d7b in QWaitCondition::wait(QMutex*, unsigned long) () from /lib/x86_64-linux-gnu/libQt5Core.so.5
#4  0x00005555556876fc in lmms::AudioEngineWorkerThread::run (this=0x5555561c6400) at /home/runner/work/zene-studio/zene-studio/src/core/AudioEngineWorkerThread.cpp:218
#5  0x00007ffff60ccca1 in ?? () from /lib/x86_64-linux-gnu/libQt5Core.so.5
#6  0x00007ffff5894a83 in ?? () from /lib/x86_64-linux-gnu/libc.so.6
#7  0x00007ffff59268e0 in ?? () from /lib/x86_64-linux-gnu/libc.so.6

Thread 7 (Thread 0x7ffff03fd640 (LWP 40347) "lmms::AudioEngi"):
#0  0x00007ffff58910d7 in ?? () from /lib/x86_64-linux-gnu/libc.so.6
#1  0x00007ffff5893e5b in pthread_cond_timedwait () from /lib/x86_64-linux-gnu/libc.so.6
#2  0x00007ffff60d2c7c in QWaitCondition::wait(QMutex*, QDeadlineTimer) () from /lib/x86_64-linux-gnu/libQt5Core.so.5
#3  0x00007ffff60d2d7b in QWaitCondition::wait(QMutex*, unsigned long) () from /lib/x86_64-linux-gnu/libQt5Core.so.5
#4  0x00005555556876fc in lmms::AudioEngineWorkerThread::run (this=0x55555620b040) at /home/runner/work/zene-studio/zene-studio/src/core/AudioEngineWorkerThread.cpp:218
#5  0x00007ffff60ccca1 in ?? () from /lib/x86_64-linux-gnu/libQt5Core.so.5
#6  0x00007ffff5894a83 in ?? () from /lib/x86_64-linux-gnu/libc.so.6
#7  0x00007ffff59268e0 in ?? () from /lib/x86_64-linux-gnu/libc.so.6

Thread 6 (Thread 0x7fffef3fc640 (LWP 40346) "lmms::AudioEngi"):
#0  0x00007ffff58910d7 in ?? () from /lib/x86_64-linux-gnu/libc.so.6
#1  0x00007ffff5893e5b in pthread_cond_timedwait () from /lib/x86_64-linux-gnu/libc.so.6
#2  0x00007ffff60d2c7c in QWaitCondition::wait(QMutex*, QDeadlineTimer) () from /lib/x86_64-linux-gnu/libQt5Core.so.5
#3  0x00007ffff60d2d7b in QWaitCondition::wait(QMutex*, unsigned long) () from /lib/x86_64-linux-gnu/libQt5Core.so.5
#4  0x00005555556876fc in lmms::AudioEngineWorkerThread::run (this=0x5555561c6240) at /home/runner/work/zene-studio/zene-studio/src/core/AudioEngineWorkerThread.cpp:218
#5  0x00007ffff60ccca1 in ?? () from /lib/x86_64-linux-gnu/libQt5Core.so.5
#6  0x00007ffff5894a83 in ?? () from /lib/x86_64-linux-gnu/libc.so.6
#7  0x00007ffff59268e0 in ?? () from /lib/x86_64-linux-gnu/libc.so.6

Thread 1 (Thread 0x7ffff2873e00 (LWP 40339) "AutomationModes"):
#0  0x00007ffff62e3e48 in QObjectPrivate::maybeSignalConnected(unsigned int) const () from /lib/x86_64-linux-gnu/libQt5Core.so.5
#1  0x00007ffff62f132f in ?? () from /lib/x86_64-linux-gnu/libQt5Core.so.5
#2  0x00005555558202fe in lmms::Song::stop (this=0x555556263120) at /home/runner/work/zene-studio/zene-studio/src/core/Song.cpp:778
#3  lmms::Song::stop (this=0x555556263120) at /home/runner/work/zene-studio/zene-studio/src/core/Song.cpp:718
#4  0x000055555564e7ff in AutomationModesTest::init (this=<optimized out>) at /home/runner/work/zene-studio/zene-studio/tests/src/core/AutomationModesTest.cpp:197
#5  AutomationModesTest::qt_static_metacall (_o=<optimized out>, _id=<optimized out>, _a=<optimized out>, _c=<optimized out>) at /home/runner/work/zene-studio/zene-studio/build/tests/AutomationModesTe
#6  0x00007ffff62c525b in QMetaMethod::invoke(QObject*, Qt::ConnectionType, QGenericReturnArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericAr
#7  0x00007ffff7d154b2 in ?? () from /lib/x86_64-linux-gnu/libQt5Test.so.5
#8  0x00007ffff7d15e1b in ?? () from /lib/x86_64-linux-gnu/libQt5Test.so.5
#9  0x00007ffff7d16379 in ?? () from /lib/x86_64-linux-gnu/libQt5Test.so.5
#10 0x00007ffff7d16864 in QTest::qRun() () from /lib/x86_64-linux-gnu/libQt5Test.so.5
#11 0x00007ffff7d16c40 in QTest::qExec(QObject*, int, char**) () from /lib/x86_64-linux-gnu/libQt5Test.so.5
#12 0x000055555563c6bb in main (argc=<optimized out>, argv=0x7fffffffd538) at /home/runner/work/zene-studio/zene-studio/tests/src/core/AutomationModesTest.cpp:667
=== end of signal backtraces ===

=== 2. The same crash reproduced locally (RelWithDebInfo, Qt 6.4.2) ===
$ MALLOC_PERTURB_=17 gdb -batch -ex run -ex "bt 25" --args ./AutomationModesTest


Thread 1 "AutomationModes" received signal SIGSEGV, Segmentation fault.
0x00007ffff6174108 in QObjectPrivate::maybeSignalConnected(unsigned int) const () from /lib/x86_64-linux-gnu/libQt6Core.so.6
#0  0x00007ffff6174108 in QObjectPrivate::maybeSignalConnected(unsigned int) const () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#1  0x00007ffff6183bce in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#2  0x0000555555875d7e in lmms::Song::stop (this=0x5555562a7ee0) at /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-touch-race/src/core/Song.cpp:778
#3  lmms::Song::stop (this=0x5555562a7ee0) at /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-touch-race/src/core/Song.cpp:718
#4  0x000055555566ba5f in AutomationModesTest::init (this=<optimized out>) at /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-touch-race/tests/src/core/AutomationModesTest.cpp:1
#5  AutomationModesTest::qt_static_metacall (_id=<optimized out>, _a=<optimized out>, _c=<optimized out>, _o=<optimized out>) at /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-
#6  0x00007ffff614700d in QMetaMethod::invoke(QObject*, Qt::ConnectionType, QGenericReturnArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericAr
#7  0x00007ffff75d03a5 in ??? () at /lib/x86_64-linux-gnu/libQt6Test.so.6
#8  0x00007ffff75ddecf in QTest::qRun() () at /lib/x86_64-linux-gnu/libQt6Test.so.6
#9  0x00007ffff75d3397 in QTest::qExec(QObject*, int, char**) () at /lib/x86_64-linux-gnu/libQt6Test.so.6

=== 3. The regression test this branch adds, before the fix ===
$ ./AutomationModesTest testDestroyedControlIsNotDereferenced      # no perturbation


Thread 1 "AutomationModes" received signal SIGSEGV, Segmentation fault.
0x00007ffff6183b14 in ?? () from /lib/x86_64-linux-gnu/libQt6Core.so.6
#0  0x00007ffff6183b14 in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#1  0x0000555555877951 in lmms::Song::processAutomations (this=this@entry=0x5555562a8000, tracklist=std::vector of length 1, capacity 1 = {...}, timeStart=...) at /home/kruzzzzy/Documents/AI_KOS_PROJE
#2  0x0000555555878263 in lmms::Song::processNextBuffer (this=this@entry=0x5555562a8000) at /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-touch-race/src/core/Song.cpp:366
#3  0x0000555555667698 in AutomationModesTest::testDestroyedControlIsNotDereferenced (this=<optimized out>) at /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-touch-race/tests/s
#4  0x00007ffff614700d in QMetaMethod::invoke(QObject*, Qt::ConnectionType, QGenericReturnArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericAr
#5  0x00007ffff75d05b0 in ??? () at /lib/x86_64-linux-gnu/libQt6Test.so.6

=== 4. MALLOC_PERTURB_ sweep, before the fix (exit codes, unpiped) ===
    MALLOC_PERTURB_=1   EXIT=139     MALLOC_PERTURB_=5   EXIT=139
    MALLOC_PERTURB_=17  EXIT=139     MALLOC_PERTURB_=85  EXIT=139
    MALLOC_PERTURB_=2   EXIT=0       MALLOC_PERTURB_=170 EXIT=0    MALLOC_PERTURB_=255 EXIT=0
