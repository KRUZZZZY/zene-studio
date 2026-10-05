# Full-feature gate run — unresolved arranging crash

Command: `env -u LMMS_PLUGIN_DIR QT_QPA_PLATFORM=offscreen bash tests/run-all-gates.sh`.
Actual exit: **1**. Source restored; git status clean after mutations.

AAF/FFmpeg were enabled before this run. Command coverage passes at 806/809,
with the three declared unreached commands. The suite is 399/400; the first
less-featured run was 400/400. Gates 3–15 pass; coverage gate 2 was not run.
This lane has not passed the complete gate bar.

## Suite and command coverage

```text
99% tests passed, 1 tests failed out of 400

Total Test time (real) = 971.21 sec

The following tests FAILED:
	190 - ArrangingCatchUpTest (SEGFAULT)
Errors while running CTest
checked-coverage: 806 of 809 commands reached their success path under result checks
PASS: 3 unreached, every one of them listed

```

## Crash trace

```text
        Start 190: ArrangingCatchUpTest
190/400 Test #190: ArrangingCatchUpTest ..................***Exception: SegFault  4.15 sec
********* Start testing of ArrangingCatchUpTest *********
Config: Using QtTest library 6.4.2, Qt 6.4.2 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 13.2.0), ubuntu 24.04
QDEBUG : ArrangingCatchUpTest::initTestCase() Lv2 plugin SUMMARY: 73 of 87  loaded in 114 msecs.
QDEBUG : ArrangingCatchUpTest::initTestCase() For details about not loaded plugins, please set
  environment variable "LMMS_LV2_DEBUG" to nonempty.
QDEBUG : ArrangingCatchUpTest::initTestCase() Blocked Lv2 Plugins: 10 of 87 
  If you want to enable them (dangerous!), please set
  environment variable "LMMS_ENABLE_BLOCKED_PLUGINS" to nonempty.
PASS   : ArrangingCatchUpTest::initTestCase()
QWARN  : ArrangingCatchUpTest::aSplitCutsAtAFrameBetweenTwoTicks() qt.core.qobject.connect: QObject::connect: No such slot lmms::SampleClip::updateLength()
PASS   : ArrangingCatchUpTest::aSplitCutsAtAFrameBetweenTwoTicks()
QWARN  : ArrangingCatchUpTest::zeroCrossingSnapMovesTheCutOntoACrossing() qt.core.qobject.connect: QObject::connect: No such slot lmms::SampleClip::updateLength()
PASS   : ArrangingCatchUpTest::zeroCrossingSnapMovesTheCutOntoACrossing()
PASS   : ArrangingCatchUpTest::aFrameCutThatCannotBeIsRefused()
PASS   : ArrangingCatchUpTest::aTrimMovesTheStartToAFrameAndUndoesWholly()
JO-ID 8408425 already in use by sampleclip!
JO-ID 8408425 already in use by sampleclip!
PASS   : ArrangingCatchUpTest::theLeadSavesLoadsAndIsResetWhenAbsent()
PASS   : ArrangingCatchUpTest::theZeroCrossingToggleIsAMode()
PASS   : ArrangingCatchUpTest::aSpacerIsOneStepAndSurvivesASave()
QINFO  : ArrangingCatchUpTest::aProjectWithoutSpacersWritesNone() plugin-scan: 75 file(s), 0 quarantined, 62 served from cache, 7 known-bad skipped, 6 scanned, 68 plugin(s)
PASS   : ArrangingCatchUpTest::aProjectWithoutSpacersWritesNone()
PASS   : ArrangingCatchUpTest::theReferenceCopiesTracksAndClips()
PASS   : ArrangingCatchUpTest::theReferenceCopiesDevices()
PASS   : ArrangingCatchUpTest::theReferenceCloses()
PASS   : ArrangingCatchUpTest::aSwitchKeepsTheProjectLeftAsTheReference()
PASS   : ArrangingCatchUpTest::aSwitchRefusesToLoseAnUntitledSession()
QWARN  : ArrangingCatchUpTest::newDocumentsCarryTheirTypesTemplate() "The plugin \"analyzer\" wasn't found or could not be loaded! Reason: \"Plugin not found.\""
Received signal 11 (SIGSEGV), code 1, for address 0x0000000000000150
         newDocumentsCarryTheirTypesTemplate function time: 45ms, total time: 1463ms

=== Stack trace ===
PASS   : ArrangingCatchUpTest::newDocumentsCarryTheirTypesTemplate()
[New LWP 1487021]
[New LWP 1486859]
[New LWP 1486858]
[New LWP 1486857]
[New LWP 1486856]
[New LWP 1486855]
[New LWP 1486854]
[New LWP 1486853]
[New LWP 1486852]
[New LWP 1486851]
[New LWP 1486850]
[New LWP 1486849]
[New LWP 1486848]
[New LWP 1486847]
[New LWP 1486846]
[New LWP 1486845]
[New LWP 1486844]
[New LWP 1486843]
[New LWP 1486842]
[New LWP 1486841]
[New LWP 1486066]
[New LWP 1486044]

This GDB supports auto-downloading debuginfo from the following URLs:
  <https://debuginfod.ubuntu.com>
Enable debuginfod for this session? (y or [n]) [answered N; input not from terminal]
Debuginfod has been disabled.
To make this setting permanent, add 'set debuginfod enabled off' to .gdbinit.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/lib/x86_64-linux-gnu/libthread_db.so.1".
0x0000758047e98e51 in __futex_abstimed_wait_common64 (private=22468, cancel=true, abstime=0x7fff3ed1fc00, op=137, expected=0, futex_word=0x57c4cc0165f0) at ./nptl/futex-internal.c:57

warning: 57	./nptl/futex-internal.c: No such file or directory

Thread 23 (Thread 0x75803ddff6c0 (LWP 1486044) "QDBusConnection"):
#0  0x0000758047f1b79d in __GI___poll (fds=0x57c4cb5dd630, nfds=2, timeout=-1) at ../sysdeps/unix/sysv/linux/poll.c:29
#1  0x0000758048173b4e in ??? () at /lib/x86_64-linux-gnu/libglib-2.0.so.0
#2  0x0000758048113ad3 in g_main_context_iteration () at /lib/x86_64-linux-gnu/libglib-2.0.so.0
#3  0x00007580489315ef in QEventDispatcherGlib::processEvents(QFlags<QEventLoop::ProcessEventsFlag>) () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x00007580487429a3 in QEventLoop::exec(QFlags<QEventLoop::ProcessEventsFlag>) () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#5  0x0000758048810683 in QThread::exec() () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#6  0x0000758048b7ad4d in ??? () at /lib/x86_64-linux-gnu/libQt6DBus.so.6
#7  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#8  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#9  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 22 (Thread 0x75803d5fe6c0 (LWP 1486066) "QtTest Watchdog"):
#0  0x0000758047e98e51 in __futex_abstimed_wait_common64 (private=-2147483648, cancel=true, abstime=0x0, op=393, expected=0, futex_word=0x57c4cb667ee0) at ./nptl/futex-internal.c:57
#1  __futex_abstimed_wait_common (cancel=true, private=-2147483648, abstime=0x0, clockid=0, expected=0, futex_word=0x57c4cb667ee0) at ./nptl/futex-internal.c:87
#2  __GI___futex_abstimed_wait_cancelable64 (futex_word=futex_word@entry=0x57c4cb667ee0, expected=expected@entry=0, clockid=clockid@entry=0, abstime=abstime@entry=0x0, private=private@entry=0) at ./nptl/futex-internal.c:139
#3  0x0000758047e9b8cd in __pthread_cond_wait_common (abstime=0x0, clockid=0, mutex=0x57c4cb667e90, cond=0x57c4cb667eb8) at ./nptl/pthread_cond_wait.c:503
#4  ___pthread_cond_wait (cond=0x57c4cb667eb8, mutex=0x57c4cb667e90) at ./nptl/pthread_cond_wait.c:627
#5  0x000075804d202ddb in ??? () at /lib/x86_64-linux-gnu/libQt6Test.so.6
#6  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#7  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#8  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 21 (Thread 0x758036ffd6c0 (LWP 1486841) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 20 (Thread 0x7580377fe6c0 (LWP 1486842) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 19 (Thread 0x758037fff6c0 (LWP 1486843) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 18 (Thread 0x75803cdfd6c0 (LWP 1486844) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 17 (Thread 0x7580235ff6c0 (LWP 1486845) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 16 (Thread 0x75802b7fe6c0 (LWP 1486846) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 15 (Thread 0x75802affd6c0 (LWP 1486847) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 14 (Thread 0x75802a7fc6c0 (LWP 1486848) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 13 (Thread 0x758029ffb6c0 (LWP 1486849) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 12 (Thread 0x7580297fa6c0 (LWP 1486850) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 11 (Thread 0x758028ff96c0 (LWP 1486851) "lmms::AudioEngi"):
#0  0x0000758047f10913 in __GI___wait4 (pid=1487456, stat_loc=0x0, options=0, usage=0x0) at ../sysdeps/unix/sysv/linux/wait4.c:30
#1  0x000075804d207f25 in ??? () at /lib/x86_64-linux-gnu/libQt6Test.so.6
#2  0x000075804d2031b3 in ??? () at /lib/x86_64-linux-gnu/libQt6Test.so.6
#3  0x0000758047e45330 in <signal handler called> () at /lib/x86_64-linux-gnu/libc.so.6
#4  0x0000000000000150 in ??? ()
#5  0x000057c4b4f9b2cc in lmms::InstrumentPlayHandle::play(std::span<lmms::SampleFrame, 18446744073709551615ul>) ()
#6  0x000057c4b4af0832 in lmms::AudioEngineWorkerThread::JobQueue::run() ()
#7  0x000057c4b4af0acb in lmms::AudioEngineWorkerThread::run() ()
#8  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#9  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#10 0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 10 (Thread 0x758023fff6c0 (LWP 1486852) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 9 (Thread 0x758022dfe6c0 (LWP 1486853) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 8 (Thread 0x7580225fd6c0 (LWP 1486854) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 7 (Thread 0x758021dfc6c0 (LWP 1486855) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 6 (Thread 0x7580215fb6c0 (LWP 1486856) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 5 (Thread 0x758020dfa6c0 (LWP 1486857) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 4 (Thread 0x757fe7fff6c0 (LWP 1486858) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 3 (Thread 0x757fe77fe6c0 (LWP 1486859) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x000057c4b4af065a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x000057c4b4af0aaa in lmms::AudioEngineWorkerThread::run() ()
#3  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 2 (Thread 0x757fe65ff6c0 (LWP 1487021) "lmms::AudioDumm"):
#0  0x000057c4b4af0a12 in lmms::AudioEngineWorkerThread::startAndWaitForJobs() ()
#1  0x000057c4b4ae9d89 in lmms::AudioEngine::renderStageInstruments() ()
#2  0x000057c4b4aecf1a in lmms::AudioEngine::renderNextPeriod() ()
#3  0x000057c4b4ac68cc in lmms::AudioDummy::run() ()
#4  0x000075804887c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#5  0x0000758047e9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#6  0x0000758047f29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 1 (Thread 0x75803e6ed240 (LWP 1485903) "ArrangingCatchU"):
#0  0x0000758047e98e51 in __futex_abstimed_wait_common64 (private=22468, cancel=true, abstime=0x7fff3ed1fc00, op=137, expected=0, futex_word=0x57c4cc0165f0) at ./nptl/futex-internal.c:57
#1  __futex_abstimed_wait_common (cancel=true, private=22468, abstime=0x7fff3ed1fc00, clockid=32767, expected=0, futex_word=0x57c4cc0165f0) at ./nptl/futex-internal.c:87
#2  __GI___futex_abstimed_wait_cancelable64 (futex_word=futex_word@entry=0x57c4cc0165f0, expected=expected@entry=0, clockid=clockid@entry=1, abstime=abstime@entry=0x7fff3ed1fc00, private=private@entry=0) at ./nptl/futex-internal.c:139
#3  0x0000758047e9bd6e in __pthread_cond_wait_common (abstime=0x7fff3ed1fc00, clockid=1, mutex=0x57c4cc0165a0, cond=0x57c4cc0165c8) at ./nptl/pthread_cond_wait.c:503
#4  ___pthread_cond_timedwait64 (cond=0x57c4cc0165c8, mutex=0x57c4cc0165a0, abstime=0x7fff3ed1fc00) at ./nptl/pthread_cond_wait.c:652
#5  0x0000758048876b93 in QWaitCondition::wait(QMutex*, QDeadlineTimer) () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#6  0x00007580488770f2 in QThread::wait(QDeadlineTimer) () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#7  0x000057c4b52cf3d5 in lmms::AudioDevice::stopProcessingThread(QThread*) ()
#8  0x000057c4b4f7b74c in lmms::Engine::destroy() ()
#9  0x000075804874700d in QMetaMethod::invoke(QObject*, Qt::ConnectionType, QGenericReturnArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument) const () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#10 0x000075804d20fe95 in QTest::qRun() () at /lib/x86_64-linux-gnu/libQt6Test.so.6
#11 0x000075804d206397 in QTest::qExec(QObject*, int, char**) () at /lib/x86_64-linux-gnu/libQt6Test.so.6
#12 0x000057c4b4a7833e in main ()
[Inferior 1 (process 1485903) detached]
=== End of stack trace ===

```

## Final gate summary

```text
================ SUMMARY ================
gate   name                     result
1      ctest                    FAIL
2      coverage                 SKIP
3      no-tautology             PASS
4      complexity               PASS
5      mutation                 PASS
6      upstream-regression      PASS
7      file-length              PASS
8      duplication              PASS
9      fork-sources             PASS
10     unregistered-tests       PASS
11     evidence                 PASS
12     rt-safety                PASS
13     scripted-checks          PASS
14     release-fitness-selftest PASS
15     verification-debt-selftest PASS

scope: fork (tests/fork-sources.txt) + tools — the enforced scope, the same one CI's
       static-gates job runs. The WHOLE-TREE scope (gates 4/7/8 --scope all) was NOT
       measured by this run; use --whole-tree for it (tests/QA-GATES.md, 'Scope policy').

skipped: 1 of 15 gates did not run
  gate 2 (coverage): --with-coverage was not passed — to run it: pass --with-coverage

RESULT: FAIL — see the failing gate above
```

## Isolated reproduction

`ctest -R '^ArrangingCatchUpTest$' --output-on-failure --repeat until-fail:20`
from `build/tests`, same offscreen environment, exit **8**, 7.08 seconds.
The audio worker crashes in `InstrumentPlayHandle::play` while the main thread
is stopping the engine. The staged-handle removal code explicitly excludes
instrument handles; that is a candidate explanation, not a completed repair.
No assertions or fixture behaviour have been changed to hide this failure.
