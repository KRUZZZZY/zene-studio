FIXED

# Cluster new-p9-411 — analyzer control flags had unsynchronized cross-thread access

Verdict: **REAL** for `m_terminate`, `m_spectrumActive`, `m_waterfallActive` and `m_reallocating`; the rest of the
finding (`m_flipRequest`, `m_inBlockSize`, `m_waterfallHeight`, `m_waterfallNotEmpty`) is **ALREADY-FIXED** on this
branch — those members were already `std::atomic` when I started, so only the four plain `bool`s were still racing.

## Why it is real

`SaProcessor::analyze()` runs on its own thread: `Analyzer` owns a `DataprocLauncher`, and `m_processorThread.start()`
(`plugins/SpectrumAnalyzer/Analyzer.cpp:71`) reaches `DataprocLauncher::run()` → `m_processor->analyze(...)`
(`plugins/SpectrumAnalyzer/DataprocLauncher.h:48-51`). The GUI thread stores the same flags:

| flag | GUI-thread store | analyzer-thread read (pre-fix, plain `bool`) |
|---|---|---|
| `m_terminate` | `terminate()` at `SaProcessor.h:58`, called from `Analyzer::~Analyzer()` (`Analyzer.cpp:82`) | `while (!m_terminate)` at `SaProcessor.cpp:105` |
| `m_spectrumActive` | `SaProcessor::setSpectrumActive()` at `SaProcessor.cpp:353`, called by `SaSpectrumView::periodicUpdate()` (`SaSpectrumView.cpp:818`) | `SaProcessor.cpp:119` |
| `m_waterfallActive` | `SaProcessor::setWaterfallActive()` at `SaProcessor.cpp:358`, called by `SaWaterfallView::periodicUpdate()` (`SaWaterfallView.cpp:244`) | `SaProcessor.cpp:119` and `:201` |
| `m_reallocating` | `SaProcessor::reallocateBuffers()` at `SaProcessor.cpp:383` and `:434`, called by the controls dialog's block-size combo (`SaControlsDialog.cpp:206-207`) | `SaProcessor.cpp:119` |
| `m_flipRequest` | `flipRequest()` at `SaProcessor.h:63`, called by `SaWaterfallView` (`SaWaterfallView.cpp:154`) | `SaProcessor.cpp:285` — **already `std::atomic<bool>`** (`SaProcessor.h:150`) |

Concurrent unsynchronized reads and writes of a plain `bool` are a data race, i.e. undefined behaviour: the compiler
may cache `m_terminate` in the analyzer loop (the thread then never stops), and it may reorder the `m_spectrumActive`
/ `m_reallocating` reads (analysis running when it must not, or the loop starving the reallocation lock).

The three remaining members the finding lists as "also shared this way" are already atomic on this branch
(`SaProcessor.h:150-151`, `:158`), so that part needs no change and is recorded as ALREADY-FIXED rather than touched.

## The fix

`plugins/SpectrumAnalyzer/SaProcessor.h` — the four flags are published, not shared:

- `bool m_terminate;` → `std::atomic<bool> m_terminate;` (`:123`)
- `bool m_spectrumActive;` → `std::atomic<bool> m_spectrumActive;` (`:156`)
- `bool m_waterfallActive;` → `std::atomic<bool> m_waterfallActive;` (`:157`)
- `bool m_reallocating;` → `std::atomic<bool> m_reallocating;` (`:159`)

Nothing else changed in the product: the setters keep their existing wording (`m_terminate = true;`,
`m_spectrumActive = active;`, `m_reallocating = true/false;`), which are atomic stores now, and every read site
converts to an atomic load. All four are still initialised to `false` by the constructor's init list
(`SaProcessor.cpp:52-60`), so first-run behaviour is unchanged.

Memory order: the default (sequentially consistent) store/load the implicit conversions produce is used. `relaxed`
would also be sufficient, and here is why nothing stronger is needed: no buffer state is published through these
flags. Every consumer takes `m_reallocationAccess` and/or `m_dataAccess` before it touches the buffers
(`SaProcessor.cpp:388-389` in `reallocateBuffers()`, the `QMutexLocker` in the `analyze()` loop, and
`clear()`/`clearHistory()`/`spectrumNotEmpty()`/`historySnapshot()`), so the flags only say *whether* to look, never
*what* is there. Stop/start ordering is covered by `QThread::wait()` in `~Analyzer` (`Analyzer.cpp:84`).

## Test matrix (written before the edit)

| path | what it is | covered by |
|---|---|---|
| normal | the flags start `false` and the setters drive the analyzer's decision to process | existing `AudioPluginTest::waterfallFlipRequestStartsClear` (m_flipRequest init) plus the constructor init list at `SaProcessor.cpp:52-60`; type regression below |
| failure (the race) | GUI thread stores / analyzer thread reads the same flag with no lock | `SaSpectrumViewTest::controlFlagsAreAtomic` — asserts the declared type of each flag is `std::atomic<bool>`; fails on the pre-fix header |
| exhaustion | the flag path allocates, frees, locks and waits for nothing, so it cannot exhaust or block | by inspection: the four setters are single stores; no `new`, no container, no lock |
| reconfiguration | `reallocateBuffers()` sets `m_reallocating` around the buffer swap while `analyze()` reads it | `m_reallocating` type assertion + `SaProcessor.cpp:383`/`:434`; the swap itself stays under both locks |
| audio thread | `Analyzer::processImpl()` (audio callback) touches none of these flags — it only writes the lock-free ring buffer | `Analyzer.cpp:88-112` unchanged; no line was added to any render path |
| platforms | Qt 6 / Qt 5 and MSVC build: the test uses only `<atomic>`, `<type_traits>` and the header | test target sets `cxx_std_20`; FFTW's include directory is added only `if(FFTW3F_INCLUDE_DIRS)`, so a default-path FFTW is unaffected |

Local TSan does not run on this box, so the race is reasoned from the code above and pinned by the type assertion
instead of by a detector. The regression uses no thread, no clock and no sleep.

## Evidence

Worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-411` (branch
`bugs/qwen-new-p9-411`). Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-411.r1/logs/`. Build and patch
commands ran from the worktree root, `ctest` ran from `build/tests`.

| Proof | Exact command | Exit |
|---|---|---:|
| Baseline gates (before any change, 0 changed files) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gate-baseline-1.log 2>&1; echo EXIT=$?` | 0 (`RESULT: PASS`) |
| Build the test before the product change | `cmake --build build --target SaSpectrumViewTest -j4 > logs/build-1-red.log 2>&1; echo EXIT=$?` | 0 |
| RED: new test fails on the pre-fix header | `cd build/tests && ctest -R SaSpectrumView --output-on-failure > logs/test-1-red.log 2>&1; echo EXIT=$?` | 8 (`FAIL!  : SaSpectrumViewTest::controlFlagsAreAtomic() 'isAtomicBool<decltype(std::declval<Processor&>().m_terminate)>' returned FALSE`) |
| Build with the fix | `cmake --build build --target analyzer SaSpectrumViewTest AudioPluginTest -j4 > logs/build-2-green.log 2>&1; echo EXIT=$?` | 0 |
| GREEN: test and the header's other test target | `cd build/tests && ctest -R "SaSpectrumView|AudioPluginTest" --output-on-failure > logs/test-2-green.log 2>&1; echo EXIT=$?` | 0 (`100% tests passed out of 2`) |
| GREEN slot detail | `cd build/tests && ./SaSpectrumViewTest -v1 > logs/test-3-green-slots.log 2>&1; echo EXIT=$?` | 0 (`PASS: SaSpectrumViewTest::controlFlagsAreAtomic()`; `Totals: 6 passed, 0 failed`) |
| Step 4b reverse the product patch | `git diff -- plugins/SpectrumAnalyzer/SaProcessor.h > logs/fix.patch && git apply -R logs/fix.patch; echo REVERT_EXIT=$?` | 0 |
| Step 4b rebuild without the fix | `cmake --build build --target SaSpectrumViewTest -j4 > logs/build-3-red-reverted.log 2>&1; echo BUILD_EXIT=$?` | 0 |
| Step 4b run without the fix | `cd build/tests && ctest -R SaSpectrumView --output-on-failure > logs/test-4-red-reverted.log 2>&1; echo TEST_EXIT=$?` | 8 (`0% tests passed, 1 tests failed out of 1`) |
| Step 4b restore the product patch | `git apply logs/fix.patch; echo REAPPLY_EXIT=$?` | 0 |
| Step 4b rebuild with the fix | `cmake --build build --target SaSpectrumViewTest analyzer -j4 > logs/build-4-green-reapplied.log 2>&1; echo BUILD_EXIT=$?` | 0 |
| Step 4b run with the fix restored | `cd build/tests && ctest -R SaSpectrumView --output-on-failure > logs/test-5-green-reapplied.log 2>&1; echo TEST_EXIT=$?` | 0 (`100% tests passed out of 1`) |
| Final build of every target that includes the header | `cmake --build build --target zene analyzer AudioPluginTest SaSpectrumViewTest -j4 > logs/build-5-final.log 2>&1; echo BUILD_EXIT=$?` | 0 |
| Final run | `cd build/tests && ctest -R "SaSpectrumView|AudioPluginTest" --output-on-failure > logs/test-6-final.log 2>&1; echo TEST_EXIT=$?` | 0 (`100% tests passed out of 2`) |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gate-1.log 2>&1; echo GATE_EXIT=$?` | 0 (`RESULT: PASS`, 8 rows PASS) |

The gate table as printed (`logs/gate-1.log`):

```
feature-check kads-new-p9-411 (base 3436a808b, 4 changed files; logs in .../build/feature-check)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
GATE_EXIT=0
```

## Registration

- `tests/upstream-modifications.txt`: two new entries — `plugins/SpectrumAnalyzer/SaProcessor.h` (the four atomic
  flags, with the reason and the regression name) and `tests/CMakeLists.txt` (the include path / FFTW directory the
  existing `SaSpectrumViewTest` target needs to compile the header). The file still ends with a newline.
- No new source file was added: `tests/src/core/SaSpectrumViewTest.cpp` is already in `tests/all-sources.txt:3246`, so
  `fork-sources` and `all-sources-reproduce` stay green (both PASS above).

## Files committed

- `plugins/SpectrumAnalyzer/SaProcessor.h`
- `tests/src/core/SaSpectrumViewTest.cpp`
- `tests/CMakeLists.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-411.md`

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reverted: `git apply -R
  logs/fix.patch` exit 0, rebuild exit 0, `ctest -R SaSpectrumView` **exit 8** with
  `FAIL! : SaSpectrumViewTest::controlFlagsAreAtomic() ... returned FALSE` (`logs/test-4-red-reverted.log`). Restored:
  `git apply logs/fix.patch` exit 0, rebuild exit 0, `ctest -R SaSpectrumView` **exit 0**
  (`logs/test-5-green-reapplied.log`). The first RED run on the untouched product (`logs/test-1-red.log`) was exit 8
  too.
- **The test is deterministic: no wall-clock timing, sleeps or races.** The seam is the type of the member:
  `#define private public` around `SaProcessor.h` (the same translation-unit seam `CompressorVisualizerTest` and
  `AudioPluginTest` already use) and `std::is_same_v` on `decltype(...)` in `SaSpectrumViewTest.cpp:105-116`. No
  thread is started, no clock is read, no value is injected and nothing waits; the test's other three slots are
  source/initializer contracts.
- **The fix is at the cause and complete: every caller and sibling was checked.** Callers: `terminate()`
  (`Analyzer.cpp:82`), `flipRequest()` (`SaWaterfallView.cpp:154`), `setSpectrumActive()` (`SaSpectrumView.cpp:818`),
  `setWaterfallActive()` (`SaWaterfallView.cpp:244`), and `m_reallocating` in `reallocateBuffers()`
  (`SaProcessor.cpp:383`, `:434`, called from `SaControlsDialog.cpp:206-207`) — all on the GUI thread, all reading the
  same members in `analyze()`, and all covered by the type change. Siblings: the other flags of the same object
  (`m_flipRequest`, `m_inBlockSize`, `m_waterfallHeight`, `m_waterfallNotEmpty`) are already `std::atomic` and were
  left alone; the assertion also pins `m_flipRequest` so a revert of that earlier fix cannot pass unnoticed.
- **No collateral damage.** No limit or check was added or removed; no save format, model, command, undo step or
  plugin state changed. The only product change is four member declarations, so old projects load exactly as before
  (`AudioPluginTest` — the only other target that includes the header — still passes, `logs/test-6-final.log`, and
  `zene` plus the `analyzer` module rebuilt clean, `logs/build-5-final.log`). `m_terminate`/`m_spectrumActive`/
  `m_waterfallActive`/`m_reallocating` are still initialised to `false` by the constructor (`SaProcessor.cpp:52-60`),
  which the raw-storage construction in `AudioPluginTest::waterfallFlipRequestStartsClear` keeps exercising.
- **ALREADY-FIXED / FALSE-POSITIVE item:** not the verdict for these four flags (they were plain `bool`s and the new
  assertion failed on the pre-fix tree, `logs/test-1-red.log`). The finding's `m_flipRequest` part is already fixed at
  `plugins/SpectrumAnalyzer/SaProcessor.h:150` (`std::atomic<bool> m_flipRequest{false};`), fixed earlier by cluster
  new-p10-358.
- **Realtime:** the analyzer thread is not the audio thread. `Analyzer::processImpl()` (`Analyzer.cpp:88`) — the audio
  callback — was not touched and reads none of these flags; the analysis thread still allocates and locks exactly where
  it did before. A `std::atomic<bool>` store/load allocates nothing, takes no lock and logs nothing.
- **Undo and commands:** no command, no undoable action and no `ControlCommands` file was touched, so the
  `ControlCommandsSnapshot` canary does not move.

## Other findings (not fixed here)

- `SaProcessor::m_sampleRate` (`SaProcessor.h:129`) is written by the analyzer thread
  (`SaProcessor.cpp:170`, inside the data lock) and read by the GUI through `getSampleRate()` (`SaProcessor.cpp:487`)
  without a lock — the same class of race, a different member and a different finding.
- `SaProcessor::m_fftBlockSize` (`SaProcessor.h:128`) is written under the data lock in `reallocateBuffers()` but
  `binCount()`/`waterfallWidth()` read it from view code that does not always hold the lock.
- `makePixel()` reads `SaControls`' colour and gamma models (`SaProcessor.cpp:330-345`) on the analyzer thread while
  the GUI can change them — model access from a non-GUI thread, outside this finding's scope.
- `tests/src/plugins/PluginPortsMigrationTest.cpp` was left unbuilt and unrun in this session (it is not built in this
  tree and it only reads the source text of `SaProcessor.cpp`/`SaWaterfallView.cpp`, neither of which changed).
