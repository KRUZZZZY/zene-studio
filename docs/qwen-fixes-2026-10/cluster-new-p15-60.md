ALREADY-FIXED

# Cluster new-p15-60 — EqAnalyser shares audio data with the GUI without synchronisation

The finding points at `plugins/Eq/EqSpectrumView.h:45` and `:63-67` on a snapshot taken
before commit `a7ab89bfe7` ("fix(eq): cluster new-p9-123 - publish the EQ spectrum as a
snapshot for the GUI"). That commit is an ancestor of this branch and it replaces exactly
the fields the finding quotes: a plain `m_bands` array, a plain `m_energy`, `m_sampleRate`,
`m_active` and `m_inProgress`, and a GUI that read them directly. On this branch the audio
thread publishes a whole snapshot, the GUI copies it, and every cross-thread flag is
atomic. No product change was needed in this job; the work is the evidence below and a
re-run of the existing regression both ways (RED on the pre-fix product code, GREEN here).

## Evidence

| Item | Evidence |
| --- | --- |
| The code the finding describes | `git show a7ab89bfe7^:plugins/Eq/EqSpectrumView.h` line 45 is `float m_bands[MAX_BANDS];` and lines 66-69 are `float m_energy; int m_sampleRate; bool m_active; bool m_inProgress;` — plain fields, exactly what the finding calls out. |
| Fix present on this branch | `plugins/Eq/EqSpectrumView.h:86-89` declares `std::atomic<float> m_energy`, `std::atomic<int> m_sampleRate`, `std::atomic<bool> m_active`, `std::atomic<bool> m_inProgress`; `plugins/Eq/EqSpectrumView.h:94-96` declares `std::atomic<float> m_publishedBands[MAX_BANDS]`, `std::atomic<float> m_publishedEnergy`, `std::atomic<unsigned int> m_publishSequence`. |
| Publish from the audio thread | `plugins/Eq/EqSpectrumView.cpp:154-169` `EqAnalyser::publish()` bumps the sequence to odd, stores the bands into the atomic slot, stores the energy, bumps the sequence to even (release). Atomic stores and two `fetch_add` calls only: no allocation, no lock, no logging. |
| Race-free reader | `plugins/Eq/EqSpectrumView.h:56-61` declares `copySpectrum()`; `plugins/Eq/EqSpectrumView.cpp:173-199` copies the published slots into the caller's array, re-checks the sequence, and when a publication is in flight hands out the last whole spectrum (`m_lastSpectrum`) instead of a half written one. |
| Flags the GUI touches | `plugins/Eq/EqSpectrumView.cpp:212-239`: `getEnergy()`, `getSampleRate()`, `getActive()` load and `setActive()` stores atomically, so the GUI timer calling `setActive()` no longer races `analyze()`. |
| The GUI reader uses the snapshot | `plugins/Eq/EqSpectrumView.cpp:291-297` `paintEvent()` copies `copySpectrum( m_spectrumBands.data(), energy )` before painting; `periodicalUpdate()` (`plugins/Eq/EqSpectrumView.cpp:383-388`) only calls the atomic `setActive()`. |
| No other cross-thread reader of `m_bands` | fff grep for `m_bands` in `plugins/Eq/`: the only analyser reader is `plugins/Eq/EqEffect.cpp:324` (`linearPeakBand`), reached from `setBandPeaks()` at `plugins/Eq/EqEffect.cpp:297`, inside the same `processImpl` call that ran `analyze()` at `plugins/Eq/EqEffect.cpp:296` — audio thread, same thread as the writer, no race. `EqParameterWidget::m_bands` is a different class. |
| Every caller of `analyze()`/`clear()` | `plugins/Eq/EqEffect.cpp:186`, `:190`, `:296`, `:301`: all inside `EqEffect::processImpl` on the audio thread. The GUI never calls `analyze()` or `clear()`. |
| Fix commit on the branch | `git merge-base --is-ancestor a7ab89bfe7 HEAD` printed `ANCESTOR_EXIT=0`; `git show --stat --oneline a7ab89bfe7` lists `plugins/Eq/EqSpectrumView.cpp`, `plugins/Eq/EqSpectrumView.h`, `tests/src/core/AudioAnalysisTest.cpp`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p14-99.md`. |
| Existing regression | `tests/src/core/AudioAnalysisTest.cpp:391-421` `theSpectrumIsPublishedAsAWholeSnapshot()` — reads the spectrum through the reader API, then installs the publish seam (`setPublishHook`, `tests/src/core/AudioAnalysisTest.cpp:410`) and takes a copy in the middle of the next publication: the copy must be a whole spectrum (`tests/src/core/AudioAnalysisTest.cpp:417`), the reader must be told it is stale (`:416`), and the spectrum after the publication must be the new one (`:420`). |
| Determinism of the test | One `EqAnalyser` on the test's stack, no threads, no sleeps, no wall clock. The interleaving is injected by `PublishHook`, called synchronously inside `publish()` at `plugins/Eq/EqSpectrumView.cpp:162-165` after half the bands were stored. |

## Commands and exit codes (all run in this session)

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-60.r1/logs`.

| Step | Exact command | EXIT | Observed |
| --- | --- | ---: | --- |
| 1 | `git log --oneline -8 -- plugins/Eq/EqSpectrumView.h plugins/Eq/EqSpectrumView.cpp` (worktree) | 0 | `a7ab89bfe7 fix(eq): cluster new-p9-123 - publish the EQ spectrum as a snapshot for the GUI` is in the history. |
| 2 | `git merge-base --is-ancestor a7ab89bfe7 HEAD; echo ANCESTOR_EXIT=$?` | 0 | `ANCESTOR_EXIT=0` — the fix commit is an ancestor of HEAD. |
| 3 | `cmake --build build --target AudioAnalysisTest -j4 > .../logs/build-01.log 2>&1` | 0 | `[100%] Built target AudioAnalysisTest`. |
| 4 | from `build/tests`: `ctest -R '^AudioAnalysisTest$' --output-on-failure > .../logs/test-01.log 2>&1; echo EXIT=$?` | 0 | `1/1 Test #361: AudioAnalysisTest ... Passed`, `100% tests passed out of 1`. |
| 5 | from `build/tests`: `./AudioAnalysisTest theSpectrumIsPublishedAsAWholeSnapshot > .../logs/test-02.log 2>&1; echo EXIT=$?` | 0 | `PASS : AudioAnalysisTest::theSpectrumIsPublishedAsAWholeSnapshot()`, `Totals: 3 passed, 0 failed`. |
| 6 | `git show a7ab89bfe7^:plugins/Eq/EqSpectrumView.h > plugins/Eq/EqSpectrumView.h && git show a7ab89bfe7^:plugins/Eq/EqSpectrumView.cpp > plugins/Eq/EqSpectrumView.cpp; echo RESTORE_FILES_EXIT=$?` | 0 | Printed `RESTORE_FILES_EXIT=0` (the label the shell used); `git status --short` then showed ` M plugins/Eq/EqSpectrumView.cpp` and ` M plugins/Eq/EqSpectrumView.h` — the pre-fix product code from `512199fe47`. |
| 7 | `cmake --build build --target AudioAnalysisTest -j4 > .../logs/build-02.log 2>&1` | 0 | `[100%] Built target AudioAnalysisTest` with the pre-fix sources. |
| 8 | from `build/tests`: `./AudioAnalysisTest theSpectrumIsPublishedAsAWholeSnapshot > .../logs/test-03.log 2>&1; echo EXIT=$?` | **1** | `FAIL! : AudioAnalysisTest::theSpectrumIsPublishedAsAWholeSnapshot() 'readPublishedSpectrum(analyser, bands, energy, &current)' returned FALSE. (the analyser publishes no snapshot, so the GUI reads the band array the audio thread writes)` |
| 9 | `git checkout -- plugins/Eq/EqSpectrumView.h plugins/Eq/EqSpectrumView.cpp; echo CHECKOUT_EXIT=$?` | 0 | `CHECKOUT_EXIT=0`; `git status --porcelain` printed nothing (clean). |
| 10 | `cmake --build build --target AudioAnalysisTest -j4 > .../logs/build-03.log 2>&1` | 0 | `[100%] Built target AudioAnalysisTest`. |
| 11 | from `build/tests`: `./AudioAnalysisTest theSpectrumIsPublishedAsAWholeSnapshot > .../logs/test-04.log 2>&1; echo EXIT=$?` | 0 | `PASS : AudioAnalysisTest::theSpectrumIsPublishedAsAWholeSnapshot()`, `Totals: 3 passed, 0 failed`. |
| 12 | from `build/tests`: `ctest -R '^AudioAnalysisTest$' --output-on-failure > .../logs/test-05.log 2>&1; echo CTEST_EXIT=$?` | 0 | `100% tests passed out of 1`; the whole `AudioAnalysisTest` CTest entry (22 QtTest cases, `-functions`) passes. |
| 13 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-01.log 2>&1; echo EXIT=$?` | 0 | `RESULT: PASS`; every gate row `PASS` (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity "no new item in your files", file-length "no new item in your files"). Logged `EXIT=0`. |

Step 6 note: a plain `git apply -R` of the fix commit's product diff was tried first and
refused — `git apply --check -R .../logs/fix-commit.patch` printed
`error: patch failed: plugins/Eq/EqSpectrumView.h:26` / `patch does not apply`, `CHECK_EXIT=1`,
because a later commit (`03e5a509b7`, hosted portability) touched the same header. The
pre-fix files were therefore taken straight from the fix commit's parent `512199fe47`,
which is the exact "without the fix" state and compiles (step 7).

## Regression proof (test fails without the fix, passes with it)

For an ALREADY-FIXED verdict there is no product change of my own to reverse, so the
"product change reverted" state is the fix commit's own parent, `512199fe47`. The existing
regression was run against both states in this session:

- Without the fix (steps 6-8): `EXIT=1`, failing assertion at
  `tests/src/core/AudioAnalysisTest.cpp:401` — the analyser publishes no snapshot.
- With the fix (steps 9-12): `EXIT=0` for the single case and for the whole
  `AudioAnalysisTest` CTest entry.

The test is discriminating: it checks the reader API exists and returns a *current*
spectrum, that a copy taken in the middle of a publication is stale-but-whole, and that it
equals the previous spectrum, never a mixture.

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit
  codes).** Yes. No product change belongs to this job; the equivalent state is the fix
  commit's parent. Red: step 8, `EXIT=1`, `FAIL! : ... returned FALSE. (the analyser
  publishes no snapshot, so the GUI reads the band array the audio thread writes)` in
  `logs/test-03.log`. Green: step 11, `EXIT=0` (`logs/test-04.log`), and step 12,
  `EXIT=0` for the whole CTest entry (`logs/test-05.log`).
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or
  injected values it uses).** The seam is `EqAnalyser::setPublishHook()`
  (`plugins/Eq/EqSpectrumView.h:69-72`), installed by the test at
  `tests/src/core/AudioAnalysisTest.cpp:410`. `publish()` calls it synchronously at
  `plugins/Eq/EqSpectrumView.cpp:164` after `MAX_BANDS / 2` bands were stored, so the
  reader in the test runs inside the publication on the same thread. One analyser on the
  test stack, two fixed input blocks (`0.5f` and silence), no threads, no sleeps, no clock.
  (Local TSan does not run on this box; the race argument is the design one above: every
  field the GUI reads is atomic, and the GUI never reads the array the audio thread writes.)
- **The fix is at the cause and complete: every caller and sibling of what you changed was
  checked and stays consistent.** The cause was unsynchronised cross-thread access, and
  the fix covers all five items of the finding: bands (`m_publishedBands` +
  `copySpectrum()`), energy (`m_publishedEnergy`, `std::atomic<float> m_energy`), sample
  rate (`std::atomic<int> m_sampleRate`), active state (`std::atomic<bool> m_active`) and
  progress state (`std::atomic<bool> m_inProgress`). The only GUI reader of the bands is
  `EqSpectrumView::paintEvent()` at `plugins/Eq/EqSpectrumView.cpp:296`; the analyser's
  remaining readers are on the writer's own thread (`plugins/Eq/EqEffect.cpp:296-297`,
  `:324`); every `analyze()`/`clear()` call site is inside `EqEffect::processImpl`
  (`plugins/Eq/EqEffect.cpp:186`, `:190`, `:296`, `:301`). The audio thread's work in
  `publish()` is 2048 relaxed atomic stores plus two `fetch_add`s — no allocation, no
  mutex, no I/O, no signal — the same order of work as the plain array writes it replaced.
- **No collateral damage: old projects and saved data still load as before, and any limit
  or check you added does not hit other callers that legitimately exceed it.** This job
  changed no product file (`git status --porcelain` empty after step 9 and before the
  report commit). `EqAnalyser` is runtime analysis state that is never saved: `EqControls`
  has no `saveSettings`/`loadSettings` entry for it, and no project serialisation
  references it, so project files load and render exactly as before. No limit or check was
  added.
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the
  intended contract that shows it.** `plugins/Eq/EqSpectrumView.h:86-96` (atomic flags and
  atomic published spectrum), `plugins/Eq/EqSpectrumView.cpp:154-199` (`publish()` /
  `copySpectrum()`), `plugins/Eq/EqSpectrumView.cpp:236-239` (atomic `setActive()`),
  `plugins/Eq/EqSpectrumView.cpp:291-297` (GUI copies the snapshot). Fix commit
  `a7ab89bfe7`, ancestor check `ANCESTOR_EXIT=0` (step 2). The pre-fix code the finding
  quotes is `a7ab89bfe7^:plugins/Eq/EqSpectrumView.h:45,66-69` (step 6).
- **Sibling commands / undo / audio thread.** This fix adds no command and no undo step,
  so the `ControlCommandsSnapshot` canary is untouched and no sibling command is affected.
  The audio-thread constraint is satisfied by the atomic publish path described above.

## Other findings

- `EqAnalyser::copySpectrum()` is `const` but records the last whole spectrum in the
  `mutable` members `m_lastSpectrum` / `m_lastEnergy`
  (`plugins/Eq/EqSpectrumView.h:97-100`). That is safe for the single GUI reader that
  exists today, but two concurrent readers would race each other. Not changed here: it is
  outside this finding (which is about the audio thread and the GUI), and fixing it needs a
  second buffer or a lock-free handoff.
- Nothing else observed in this area.
