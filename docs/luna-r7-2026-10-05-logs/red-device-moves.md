# Device moves: unfixed regression evidence

Baseline product source: `bcac7c05c`, with the new regression test.

Command: `env -u LMMS_PLUGIN_DIR QT_QPA_PLATFORM=offscreen ctest -R "^(DeviceMovesUndoTest|ControlSuccessPaths)$" --output-on-failure`, from `build/tests`; exit 8.

This is the device-test portion of that run: two copy assertions fail, then rack-wrap undo crashes. The socket leg of this initial experiment had a test-helper argument error and is not evidence; it was corrected and rerun separately in red-bounce.md.

```text
Test project /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-luna-r7/build/tests
    Start 195: DeviceMovesUndoTest
1/2 Test #195: DeviceMovesUndoTest ..............***Exception: SegFault  3.66 sec
********* Start testing of DeviceMovesUndoTest *********
Config: Using QtTest library 6.4.2, Qt 6.4.2 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 13.2.0), ubuntu 24.04
QDEBUG : DeviceMovesUndoTest::initTestCase() Lv2 plugin SUMMARY: 73 of 87  loaded in 124 msecs.
QDEBUG : DeviceMovesUndoTest::initTestCase() For details about not loaded plugins, please set
  environment variable "LMMS_LV2_DEBUG" to nonempty.
QDEBUG : DeviceMovesUndoTest::initTestCase() Blocked Lv2 Plugins: 10 of 87 
  If you want to enable them (dangerous!), please set
  environment variable "LMMS_ENABLE_BLOCKED_PLUGINS" to nonempty.
PASS   : DeviceMovesUndoTest::initTestCase()
QINFO  : DeviceMovesUndoTest::copiedDevicesAreUndoneOnARebuiltTrack(plugin.copy) plugin-scan: 3 file(s), 0 quarantined, 0 served from cache, 0 known-bad skipped, 3 scanned, 3 plugin(s)
JO-ID 8410717 already in use by AmplifierControls!
JO-ID 8410817 already in use by effect!
FAIL!  : DeviceMovesUndoTest::copiedDevicesAreUndoneOnARebuiltTrack(plugin.copy) Compared lists have different sizes.
   Actual   (names(target)) size: 2
   Expected (QStringList{QStringLiteral("bassbooster")}) size: 1
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-luna-r7/tests/src/core/DeviceMovesUndoTest.cpp(128)]
JO-ID 8411118 already in use by AmplifierControls!
JO-ID 8419368 already in use by effect!
FAIL!  : DeviceMovesUndoTest::copiedDevicesAreUndoneOnARebuiltTrack(chain.copy) Compared lists have different sizes.
   Actual   (names(target)) size: 2
   Expected (QStringList{QStringLiteral("bassbooster")}) size: 1
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-luna-r7/tests/src/core/DeviceMovesUndoTest.cpp(128)]
JO-ID 8399272 already in use by AmplifierControls!
JO-ID 8404556 already in use by effect!
Received signal 11 (SIGSEGV), code 128, for address 0x0000000000000000
         aWrappedChannelCanDisappearBeforeUndo function time: 2ms, total time: 1348ms

=== Stack trace ===
[New LWP 27589]
[New LWP 27588]
[New LWP 27587]
[New LWP 27586]
[New LWP 27585]
[New LWP 27584]
[New LWP 27583]
[New LWP 27582]
[New LWP 27581]
[New LWP 27580]
[New LWP 27579]
[New LWP 27578]
[New LWP 27577]
[New LWP 27576]
[New LWP 27575]
[New LWP 27574]
[New LWP 27573]
[New LWP 27572]
[New LWP 27571]
[New LWP 27566]
[New LWP 27564]

This GDB supports auto-downloading debuginfo from the following URLs:
  <https://debuginfod.ubuntu.com>
Enable debuginfod for this session? (y or [n]) [answered N; input not from terminal]
Debuginfod has been disabled.
To make this setting permanent, add 'set debuginfod enabled off' to .gdbinit.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/lib/x86_64-linux-gnu/libthread_db.so.1".
0x0000795dbad10913 in __GI___wait4 (pid=27598, stat_loc=0x0, options=0, usage=0x0) at ../sysdeps/unix/sysv/linux/wait4.c:30

warning: 30	../sysdeps/unix/sysv/linux/wait4.c: No such file or directory

Thread 22 (Thread 0x795db5f636c0 (LWP 27564) "QDBusConnection"):
#0  0x0000795dbad1b79d in __GI___poll (fds=0x5cae7027c260, nfds=2, timeout=-1) at ../sysdeps/unix/sysv/linux/poll.c:29
#1  0x0000795dbab73b4e in ??? () at /lib/x86_64-linux-gnu/libglib-2.0.so.0
#2  0x0000795dbab13ad3 in g_main_context_iteration () at /lib/x86_64-linux-gnu/libglib-2.0.so.0
#3  0x0000795dbb7315ef in QEventDispatcherGlib::processEvents(QFlags<QEventLoop::ProcessEventsFlag>) () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbb5429a3 in QEventLoop::exec(QFlags<QEventLoop::ProcessEventsFlag>) () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#5  0x0000795dbb610683 in QThread::exec() () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#6  0x0000795dbaa31d4d in ??? () at /lib/x86_64-linux-gnu/libQt6DBus.so.6
#7  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#8  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#9  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 21 (Thread 0x795db57596c0 (LWP 27566) "QtTest Watchdog"):
#0  0x0000795dbac98e51 in __futex_abstimed_wait_common64 (private=-1250588880, cancel=true, abstime=0x795db5758890, op=137, expected=0, futex_word=0x5cae702fb930) at ./nptl/futex-internal.c:57
#1  __futex_abstimed_wait_common (cancel=true, private=-1250588880, abstime=0x795db5758890, clockid=31069, expected=0, futex_word=0x5cae702fb930) at ./nptl/futex-internal.c:87
#2  __GI___futex_abstimed_wait_cancelable64 (futex_word=futex_word@entry=0x5cae702fb930, expected=expected@entry=0, clockid=clockid@entry=1, abstime=abstime@entry=0x795db5758890, private=private@entry=0) at ./nptl/futex-internal.c:139
#3  0x0000795dbac9c1f6 in __pthread_cond_wait_common (abstime=<optimized out>, clockid=<optimized out>, mutex=0x5cae702fb8e0, cond=0x5cae702fb908) at ./nptl/pthread_cond_wait.c:503
#4  ___pthread_cond_clockwait64 (abstime=<optimized out>, clockid=<optimized out>, mutex=0x5cae702fb8e0, cond=0x5cae702fb908) at ./nptl/pthread_cond_wait.c:691
#5  ___pthread_cond_clockwait64 (cond=0x5cae702fb908, mutex=0x5cae702fb8e0, clockid=<optimized out>, abstime=<optimized out>) at ./nptl/pthread_cond_wait.c:679
#6  0x0000795dbd3a6d76 in ??? () at /lib/x86_64-linux-gnu/libQt6Test.so.6
#7  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#8  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#9  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 20 (Thread 0x795daeffd6c0 (LWP 27571) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 19 (Thread 0x795daf7fe6c0 (LWP 27572) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 18 (Thread 0x795daffff6c0 (LWP 27573) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 17 (Thread 0x795db4f586c0 (LWP 27574) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 16 (Thread 0x795da37fe6c0 (LWP 27575) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 15 (Thread 0x795da2ffd6c0 (LWP 27576) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 14 (Thread 0x795da27fc6c0 (LWP 27577) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 13 (Thread 0x795da1ffb6c0 (LWP 27578) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 12 (Thread 0x795da17fa6c0 (LWP 27579) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 11 (Thread 0x795da0ff96c0 (LWP 27580) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 10 (Thread 0x795d8ffff6c0 (LWP 27581) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 9 (Thread 0x795d8f7fe6c0 (LWP 27582) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 8 (Thread 0x795d8effd6c0 (LWP 27583) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 7 (Thread 0x795d8e7fc6c0 (LWP 27584) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 6 (Thread 0x795d8dffb6c0 (LWP 27585) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 5 (Thread 0x795d8d7fa6c0 (LWP 27586) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 4 (Thread 0x795d8cff96c0 (LWP 27587) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 3 (Thread 0x795d77fff6c0 (LWP 27588) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 2 (Thread 0x795d777fe6c0 (LWP 27589) "lmms::AudioEngi"):
#0  syscall () at ../sysdeps/unix/sysv/linux/x86_64/syscall.S:38
#1  0x00005cae418ab56a in void std::__atomic_wait_address_v<unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}>(unsigned int const*, unsigned int, std::__atomic_base<unsigned int>::wait(unsigned int, std::memory_order) const::{lambda()#1}) [clone .isra.0] ()
#2  0x00005cae418ab9ba in lmms::AudioEngineWorkerThread::run() ()
#3  0x0000795dbb67c48d in ??? () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#4  0x0000795dbac9cb84 in start_thread (arg=<optimized out>) at ./nptl/pthread_create.c:447
#5  0x0000795dbad29ecc in clone3 () at ../sysdeps/unix/sysv/linux/x86_64/clone3.S:78

Thread 1 (Thread 0x795db67a0c80 (LWP 27563) "DeviceMovesUndo"):
#0  0x0000795dbad10913 in __GI___wait4 (pid=27598, stat_loc=0x0, options=0, usage=0x0) at ../sysdeps/unix/sysv/linux/wait4.c:30
#1  0x0000795dbd3abf25 in ??? () at /lib/x86_64-linux-gnu/libQt6Test.so.6
#2  0x0000795dbd3a71b3 in ??? () at /lib/x86_64-linux-gnu/libQt6Test.so.6
#3  0x0000795dbac45330 in <signal handler called> () at /lib/x86_64-linux-gnu/libc.so.6
#4  0x0000795dbb57422c in QObject::thread() const () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#5  0x0000795dbb57aa0e in QObject::QObject(QObjectPrivate&, QObject*) () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#6  0x00005cae41db3aa6 in lmms::Model::Model(lmms::Model*, QString, bool) ()
#7  0x00005cae41dcfe78 in lmms::Plugin::Plugin(lmms::Plugin::Descriptor const*, lmms::Model*, lmms::Plugin::Descriptor::SubPluginFeatures::Key const*) ()
#8  0x00005cae41d271af in lmms::Effect::Effect(lmms::Plugin::Descriptor const*, lmms::Model*, lmms::Plugin::Descriptor::SubPluginFeatures::Key const*) ()
#9  0x0000795dac2cfa1c in lmms::detail::AudioPlugin<lmms::Effect, lmms::AudioPortsSettings{(lmms::AudioDataKind)0, true, (unsigned short)2, (unsigned short)2, true}, lmms::detail::PluginAudioPorts<lmms::AudioPortsSettings{(lmms::AudioDataKind)0, true, (unsigned short)2, (unsigned short)2, true}, lmms::PluginAudioPortsBuffer> >::AudioPlugin<>(lmms::Plugin::Descriptor const*, lmms::Model*, lmms::Plugin::Descriptor::SubPluginFeatures::Key const*) (key=<optimized out>, parent=<optimized out>, desc=0x795dac2da220 <amplifier_plugin_descriptor>, this=0x5cae7165d610) at /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-luna-r7/include/AudioPlugin.h:218
#10 lmms::AudioPlugin<lmms::Effect, lmms::AudioPortsSettings{(lmms::AudioDataKind)0, true, (unsigned short)2, (unsigned short)2, true}, lmms::detail::PluginAudioPorts<lmms::AudioPortsSettings{(lmms::AudioDataKind)0, true, (unsigned short)2, (unsigned short)2, true}, lmms::PluginAudioPortsBuffer> >::AudioPlugin<>(lmms::Plugin::Descriptor const*, lmms::Model*, lmms::Plugin::Descriptor::SubPluginFeatures::Key const*) (this=0x5cae7165d610) at /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-luna-r7/include/AudioPlugin.h:404
#11 lmms::AmplifierEffect::AmplifierEffect (this=0x5cae7165d610, parent=<optimized out>, key=<optimized out>) at /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-luna-r7/plugins/Amplifier/Amplifier.cpp:55
#12 0x0000795dac2cfc49 in lmms::lmms_plugin_main (parent=0x5cae71b970d0, data=0x0) at /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-luna-r7/plugins/Amplifier/Amplifier.cpp:98
#13 0x00005cae41dd013b in lmms::Plugin::instantiate(QString const&, lmms::Model*, void*) ()
#14 0x00005cae41dd0b2a in lmms::Plugin::instantiateWithKey(QString const&, lmms::Model*, lmms::Plugin::Descriptor::SubPluginFeatures::Key const*, bool) ()
#15 0x00005cae41d26022 in lmms::Effect::instantiate(QString const&, lmms::Model*, lmms::Plugin::Descriptor::SubPluginFeatures::Key*) ()
#16 0x00005cae41d058ef in lmms::control::recreateEffectFromState(lmms::EffectChain*, QString const&, int) ()
#17 0x00005cae419ef91e in std::_Function_handler<void (), lmms::control::removeDevicesRecorded(std::function<lmms::EffectChain* ()> const&, std::vector<lmms::Effect*, std::allocator<lmms::Effect*> > const&)::{lambda()#1}>::_M_invoke(std::_Any_data const&) ()
#18 0x00005cae41df26c1 in lmms::ProjectJournal::undo() ()
#19 0x00005cae41995e08 in lmms::(anonymous namespace)::undoThroughJournal(QString const&, int) ()
#20 0x00005cae419972ba in lmms::(anonymous namespace)::undoLastCommand(lmms::ControlRegistry&) ()
#21 0x00005cae41997754 in std::_Function_handler<lmms::ControlResult (QJsonObject const&), lmms::(anonymous namespace)::registerUndoCommand(lmms::ControlRegistry&)::{lambda(QJsonObject const&)#1}>::_M_invoke(std::_Any_data const&, QJsonObject const&) ()
#22 0x00005cae42544b10 in lmms::ControlRegistry::runHandler(std::function<lmms::ControlResult (QJsonObject const&)> const&, QString const&, bool, QJsonObject const&) ()
#23 0x00005cae425452a3 in std::_Function_handler<lmms::ControlResult (), lmms::ControlRegistry::invoke(QString const&, QJsonObject const&)::{lambda()#1}>::_M_invoke(std::_Any_data const&) ()
#24 0x00005cae42542e80 in lmms::ControlRegistry::runOnUiThread(std::function<lmms::ControlResult ()> const&) ()
#25 0x00005cae4254364c in lmms::ControlRegistry::invoke(QString const&, QJsonObject const&) ()
#26 0x00005cae41858827 in DeviceMovesUndoTest::aWrappedChannelCanDisappearBeforeUndo() [clone .isra.0] ()
#27 0x0000795dbb54700d in QMetaMethod::invoke(QObject*, Qt::ConnectionType, QGenericReturnArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument, QGenericArgument) const () at /lib/x86_64-linux-gnu/libQt6Core.so.6
#28 0x0000795dbd3a75b0 in ??? () at /lib/x86_64-linux-gnu/libQt6Test.so.6
#29 0x0000795dbd3b4d1c in QTest::qRun() () at /lib/x86_64-linux-gnu/libQt6Test.so.6
#30 0x0000795dbd3aa397 in QTest::qExec(QObject*, int, char**) () at /lib/x86_64-linux-gnu/libQt6Test.so.6
#31 0x00005cae4183cf5b in main ()
Warning: the current language does not match this frame.
[Inferior 1 (process 27563) detached]
=== End of stack trace ===

```
