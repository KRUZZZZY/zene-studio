FIXED

# Cluster 5039-port-port-port-port-port-port: alternate signal stacks are per-thread

This is the manual port of accepted fix `d51f0e653` from the older line. The
fresh branch did not already contain it. The reported conflict in
`src/core/AudioEngineWorkerThread.cpp` was resolved by adding registration at
the start of the current `run()` body, leaving the newer worker/drain code
intact. No merge, cherry-pick or rebase was used.

## Decision: REAL

Before this port, `src/core/CrashReporter.cpp` held one process-global
`s_altStack`, and `install()` called `sigaltstack()` only on the installing
thread before arming process-wide dispositions with `SA_ONSTACK`. POSIX
`sigaltstack()` is per-thread. A later audio thread therefore had no alternate
stack and would handle stack exhaustion on its ordinary, exhausted stack.

The regression recorded that state before the product change:
`CrashReporterTest::a_thread_can_register_the_alternate_signal_stack_for_itself`
failed because the registration API was absent, and
`a_repeated_install_registers_the_calling_thread` showed that a repeated
`install()` on another thread left it without a stack.

## Fix and current-tree evidence

- [`src/core/CrashReporter.cpp`](../../src/core/CrashReporter.cpp:100) now has
  thread-local 32 KiB stack storage. The helper at `:266-278` registers the
  calling thread once; `install()` calls it before its already-installed
  early return at `:280-287`. Signal dispositions still use `SA_ONSTACK` at
  `:292`.
- [`include/CrashReporter.h`](../../include/CrashReporter.h:147) declares the
  per-thread API. [`src/core/CrashReporterWindows.cpp`](../../src/core/CrashReporterWindows.cpp:51)
  provides the matching Windows no-op.
- The new thread registration is at the current thread's entry point or
  callback in `include/AudioDummy.h:105`,
  `src/core/AudioEngineWorkerThread.cpp:307`, `src/core/AudioEngine.cpp:976`,
  `src/core/ProjectRenderer.cpp:281`, `src/core/audio/AudioAlsa.cpp:297,608`,
  `src/core/audio/AudioJack.cpp:413`, `src/core/audio/AudioOss.cpp:227`,
  `src/core/audio/AudioPortAudio.cpp:214`,
  `src/core/audio/AudioPulseAudio.cpp:185`,
  `src/core/audio/AudioSdl.cpp:174,192`,
  `src/core/audio/AudioSndio.cpp:134`, and
  `src/core/audio/AudioSoundIo.cpp:256`. The
  `AudioEngine::renderNextPeriod()` registration is the backstop for every
  render path. ALSA's capture thread and SDL's separate input callback register
  as well.
- [`tests/src/core/CrashReporterTest.cpp`](../../tests/src/core/CrashReporterTest.cpp:537)
  synchronously raises `SIGUSR1` on an unregistered thread and a registered
  worker thread. It verifies the first has no alternate stack and that the
  second receives `SA_ONSTACK` delivery inside its own kernel-reported stack
  range. It also tests repeated installation on another thread.
- [`docs/CRASH-REPORTER.md`](../../docs/CRASH-REPORTER.md:445) now states the
  coverage limit: a private plugin thread that never reaches a registration
  point still has no alternate stack.

## Evidence table

Logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r1/logs`.
Test commands below ran from `build/tests`; build and gate commands ran from
the worktree root.

The exact command strings and exit codes follow. Each ctest ran with working
directory `build/tests`; other commands ran from the worktree root. Build and
test output is in the listed new log file. The shell `EXIT` marker was printed
after redirection and is recorded here.

| Step | Command | Exit | Result |
| --- | --- | ---: | --- |
| RED build | `mkdir -p /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r1/logs && cmake --build build --target zene CrashReporterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Built test against unchanged product. |
| RED test | `ctest -R '^CrashReporterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 | 13 passed, 2 failed: missing helper and repeated-install assertions. |
| GREEN build | `cmake --build build --target zene CrashReporterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Built the per-thread implementation. |
| GREEN test | `ctest -R '^CrashReporterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | CrashReporterTest passed. |
| Family build | `cmake --build build --target zene CrashReporterTest CrashReporterArmTest ControlCrashAcknowledgeTest DummyPacingTest RenderJobQueueTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | All five related targets built. |
| Family test | `ctest -R '^(CrashReporterTest|CrashReporterArmTest|ControlCrashAcknowledgeTest|DummyPacingTest|RenderJobQueueTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | 5/5 ctest targets passed. |

The exact 4b product patch commands were:

```sh
git diff -- include/AudioDummy.h include/CrashReporter.h src/core/AudioEngine.cpp src/core/AudioEngineWorkerThread.cpp src/core/CrashReporter.cpp src/core/CrashReporterWindows.cpp src/core/ProjectRenderer.cpp src/core/audio/AudioAlsa.cpp src/core/audio/AudioJack.cpp src/core/audio/AudioOss.cpp src/core/audio/AudioPortAudio.cpp src/core/audio/AudioPulseAudio.cpp src/core/audio/AudioSdl.cpp src/core/audio/AudioSndio.cpp src/core/audio/AudioSoundIo.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result
EXIT=0
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result
EXIT=0
cmake --build build --target zene CrashReporterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit $result
EXIT=0
ctest -R '^CrashReporterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit $result
EXIT=8
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result
EXIT=0
cmake --build build --target zene CrashReporterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result; exit $result
EXIT=0
ctest -R '^CrashReporterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r1/logs/test-5.log 2>&1; result=$?; echo EXIT=$result; exit $result
EXIT=0
```

The source-manifest correction used this Python command and exited 0:

```sh
python3 - <<'PY'
from pathlib import Path
import subprocess

manifest = Path('tests/all-sources.txt')
current = manifest.read_text()
first_entry = current.index('include/AafImport.h')
header = current[:first_entry]
recipe = """{ git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' \\
    | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)' ; \\
  git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } | LC_ALL=C sort -u"""
result = subprocess.run(['bash', '-c', recipe], check=True, text=True, capture_output=True)
manifest.write_text(header + result.stdout)
PY
EXIT=0
```

The two exact feature-gate
commands and results were:

```sh
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5039-port-port-port-port-port-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result; exit $result
EXIT=1
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5039-port-port-port-port-port-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5039-port-port-port-port-port-port.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result; exit $result
EXIT=0
git diff --check; result=$?; echo EXIT=$result; exit $result
EXIT=0
```

The first feature gate found the pre-existing `tests/all-sources.txt` mismatch:
1,148 tracked vendored sfizz paths contradicted its exclusion recipe. After
re-deriving the manifest from that recipe, all 8/8 gates passed, including
`fork-sources` and `all-sources-reproduce`.

## Files committed

- `docs/CRASH-REPORTER.md`
- `include/AudioDummy.h`
- `include/CrashReporter.h`
- `src/core/AudioEngine.cpp`
- `src/core/AudioEngineWorkerThread.cpp`
- `src/core/CrashReporter.cpp`
- `src/core/CrashReporterWindows.cpp`
- `src/core/ProjectRenderer.cpp`
- `src/core/audio/AudioAlsa.cpp`
- `src/core/audio/AudioJack.cpp`
- `src/core/audio/AudioOss.cpp`
- `src/core/audio/AudioPortAudio.cpp`
- `src/core/audio/AudioPulseAudio.cpp`
- `src/core/audio/AudioSdl.cpp`
- `src/core/audio/AudioSndio.cpp`
- `src/core/audio/AudioSoundIo.cpp`
- `tests/all-sources.txt` (manifest regeneration described above)
- `tests/src/core/CrashReporterTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-5039-port-port-port-port-port-port.md` (this report)

The reporter's header and implementation were already registered as fork
sources. The modified inherited files and the changed regression test are
registered in `tests/upstream-modifications.txt`; no new source file was
introduced.

## Self-check

- **RED/GREEN proof:** `CrashReporterTest` failed on the original product
  (`test-1.log`, ctest exit 8; 13 passed and the two alternate-stack cases
  failed), passed after the fix (`test-2.log`, exit 0), failed again with the
  product-only diff reversed (`test-4.log`, exit 8), and passed after restoring
  it (`test-5.log`, exit 0). The `fix.patch` reverse/apply commands both exited
  0. The related family passed 5/5 ctest targets (`test-3.log`, exit 0).
- **Determinism:** the new test uses synchronous `raise(SIGUSR1)` and
  `std::thread::join()` for completion. The handler records a stack-local
  marker address; the worker compares it to the range returned by its own
  `sigaltstack()` query. There are no sleeps, clocks, timeout assertions or
  races in the new cases.
- **Completeness:** `install()` registers its caller even when the process-wide
  handlers are already armed. App-owned audio render/capture threads,
  library-owned audio callbacks, the export thread and the shared engine render
  entry point were checked and registered. The conflict site only gained the
  registration at `run()` entry; its newer worker/drain handshake is intact.
  Windows has the matching stub.
- **Realtime and collateral behavior:** registration uses a thread-local
  32 KiB buffer and a thread-local boolean. The first call makes one
  `sigaltstack()` system call; later calls are a TLS check. The path adds no
  allocation, free, lock, wait, logging, Qt call or emitted signal. There is no
  project format, save/load, report format, limit or command change. A
  non-audio private thread that never registers remains outside the coverage,
  and that limitation is documented.
- **Undo/commands and verdict:** no undoable behavior or control command was
  changed or added. Verdict is FIXED because the original defect reproduced in
  the RED runs and the same assertions passed only with the product change.
