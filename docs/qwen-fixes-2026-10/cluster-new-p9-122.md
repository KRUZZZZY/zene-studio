# cluster new-p9-122 — audio/GUI peak-meter float handoff races through EqFader

Verdict: FIXED (four bug reports, one root cause — one shared fix, one line per report below)

- new-p9-122: **FIXED** — `EqFader` (the peak-meter fader shared by the Eq, Delay
  and Compressor dialogs) took raw `float*` to plugin fields; its GUI-timer slot
  read them and then cleared them (`*m_lPeak = 0`) while the audio thread wrote
  the same floats. It now takes `std::atomic<float>*` and reads with a single
  `exchange(0.0f, relaxed)` — read-and-clear as one atomic step.
- new-p15-58: **FIXED** — `EqControls.h:65-76` declared all fourteen peak fields
  (`m_inPeakL`..`m_highShelfPeakR`) as plain `float`; they are now lock-free
  `std::atomic<float>` (with a `static_assert` on `is_always_lock_free`).
- new-p16-57: **FIXED** — `DelayEffect.cpp:138-139` wrote
  `m_delayControls.m_outPeakL/R` (plain floats) on the audio thread while the
  Delay dialog's EqFaders read them on the GUI timer; the fields are now atomic
  and the writes are relaxed stores. The sibling `CompressorEffect`
  (`Compressor.cpp:561-564`, same widget, same defect) is fixed the same way.
- new-p17-52: **FIXED** — `EqEffect.cpp:180-181` and `277-278` raised the
  in/out peaks with a plain read-modify-write (`m = m < v ? v : m`) on the audio
  thread while the UI timer read and cleared them; the max-hold is now the
  lock-free `raisePeak` CAS helper, and the band peaks
  (`EqEffect.cpp:330-347`, chained plain assignments) are relaxed stores.

## Finding (shared root cause)

`EqFader` (plugins/Eq/EqFader.h) is the peak-meter widget of three effect
dialogs (EqControlsDialog.cpp:77/83/93, DelayControlsDialog.cpp:70,
CompressorControlDialog.cpp:186/192). It is connected to
`MainWindow::periodicUpdate()` (GUI timer) and its private slot
`updateVuMeters()` read the peak floats and then wrote zero back, while the
audio thread's `processImpl` wrote the same plain floats (`EqEffect.cpp:180`,
`DelayEffect.cpp:138`, `Compressor.cpp:561`). Unsynchronized concurrent
read/modify/write of the same plain float is a data race (undefined behaviour
under the C++ memory model), and the GUI's read-then-clear also loses any peak
the audio thread writes between the read and the clear. The four bug reports
name the four faces of this one handoff: the widget (new-p9-122), the Eq storage
(new-p15-58), the Delay writes (new-p16-57) and the Eq audio-side writes
(new-p17-52).

## Evidence (file:line)

| What | Where |
|---|---|
| Buggy widget ctor + read-then-clear | `plugins/Eq/EqFader.h:45` (`float* lPeak`), `:71-74` (`*m_lPeak` then `*m_lPeak = 0`), `:81-84` (same for right) |
| Buggy Eq storage | `plugins/Eq/EqControls.h:65-76` — fourteen `float` peak fields |
| Buggy Eq audio writes | `plugins/Eq/EqEffect.cpp:180-181,277-278` (read-modify-write max-hold), `:330-347` (chained band assignments) |
| Buggy Delay storage/writes | `plugins/Delay/DelayControls.h:58-59`, `plugins/Delay/DelayEffect.cpp:138-139` |
| Buggy Compressor storage/writes (sibling) | `plugins/Compressor/CompressorControls.h:96-99`, `plugins/Compressor/Compressor.cpp:561-564` |
| Widget takes atomic pointers, exchanges | `plugins/Eq/EqFader.h:51` (`std::atomic<float>* lPeak`), `:80,90` (`exchange(0.0f, relaxed)`) |
| Eq storage is atomic (lock-free asserted) | `plugins/Eq/EqControls.h:70-83`, `:145-147` (static_assert) |
| Eq audio writes | `plugins/Eq/EqEffect.cpp:74-79` (`raisePeak` CAS), `:194-195,291-292` (in/out), `:346-347` (band relaxed stores) |
| Delay atomic storage/writes | `plugins/Delay/DelayControls.h:63-64`, `plugins/Delay/DelayEffect.cpp:145-146` |
| Compressor atomic storage/writes | `plugins/Compressor/CompressorControls.h:101-104`, `plugins/Compressor/Compressor.cpp:564-567` |
| Dialog plumbing passes atomic pointers | `plugins/Eq/EqParameterWidget.h:71-72` (EqBand), `plugins/Eq/EqControlsDialog.h:60`, `plugins/Eq/EqControlsDialog.cpp:224` |

Realtime constraints kept: no locks, no allocation, no logging on the audio
thread — relaxed stores and a lock-free CAS loop only
(`static_assert(std::atomic<float>::is_always_lock_free)` in all three controls
headers, per the `DspLoadProbe.h:42` precedent). Peak fields are runtime-only
(never saved/loaded in `EqControls::saveSettings/loadSettings`), so no project
format impact. `tests/reference/**` untouched (frozen migration evidence).

## Regression test

New `tests/src/plugins/EqPeakHandoffTest.cpp` (registered in
`tests/CMakeLists.txt`, manifested in `tests/all-sources.txt`), one slot per bug
id: `newP9_122_eqFaderReadsPeaksWithAtomicExchange`,
`newP15_58_eqControlsPeakFieldsAreAtomic`,
`newP16_57_delayAndCompressorPeaksAreAtomic`,
`newP17_52_eqEffectWritesPeaksThroughAtomics`. A concurrent plain-float race
has no deterministic failure without a race detector, so the slots fail
deterministically on the original code through what can be checked without
racing (the cluster-new-p8-266 / cluster-new-p9-370 pattern): the declared type
of the handoff fields (`std::is_same_v<decltype(...), std::atomic<float>>`),
the widget/audio-side access patterns in the sources (source-text checks, the
Vst3HostTest `readSource` pattern), and — on a real constructed `DelayEffect` —
the exchange handoff itself through a template that compiles against both the
plain-float and the atomic version and fails at runtime on the plain-float one.

## Commands run (EXIT codes)

RED — test fails on unfixed code:

```
cmake --build build --target EqPeakHandoffTest > LOGS/build-red.log 2>&1        # EXIT=0 (compiles on plain floats)
ctest -R '^EqPeakHandoffTest$' --output-on-failure > LOGS/test-red-unfixed.log 2>&1  # EXIT=8 (4/4 slots FAIL)
```

GREEN — test passes with the fix (EqFader.h comment reworded after the first
green attempt so the source check no longer matched its own comment):

```
cmake --build build --target EqPeakHandoffTest > LOGS/build-green2.log 2>&1     # EXIT=0
ctest -R '^EqPeakHandoffTest$' --output-on-failure > LOGS/test-green.log 2>&1   # EXIT=0
```

Reverse-patch proof — the test catches the bug, not the build:

```
git diff -- plugins/ > LOGS/fix.patch && git apply -R LOGS/fix.patch            # APPLY_R_EXIT=0
cmake --build build --target EqPeakHandoffTest > LOGS/build-rev.log 2>&1        # EXIT=0
ctest -R '^EqPeakHandoffTest$' --output-on-failure > LOGS/test-rev-red.log 2>&1 # EXIT=8 (4/4 slots FAIL)
git apply LOGS/fix.patch                                                        # APPLY_EXIT=0
cmake --build build --target EqPeakHandoffTest > LOGS/build-final-green.log 2>&1  # EXIT=0
ctest -R '^EqPeakHandoffTest$' --output-on-failure > LOGS/test-final-green.log 2>&1  # EXIT=0
```

Plugin targets rebuilt after the product change:

```
cmake --build build --target eq > LOGS/build-eq2.log 2>&1           # EXIT=0
cmake --build build --target delay > LOGS/build-delay.log 2>&1      # EXIT=0
cmake --build build --target compressor > LOGS/build-compressor.log 2>&1  # EXIT=0
```

Affected-family tests (the test compiles the changed Delay sources and reads
the changed Eq/Compressor sources; PluginPortsMigrationTest renders the eq,
delay and compressor ports against the frozen references):

```
cmake --build build --target DelayControlsTest DelayLfoAutomationTest EqFilterTest EqCurveTest PluginPortsMigrationTest \
  > LOGS/build-family.log 2>&1                                              # EXIT=0
ctest -R '^(EqPeakHandoffTest|DelayControlsTest|DelayLfoAutomationTest|EqFilterTest|EqCurveTest|PluginPortsMigrationTest)$' \
  --output-on-failure > LOGS/test-family2.log 2>&1                          # EXIT=0 (6/6)
```

(The first family run failed only `PluginPortsMigrationTest` on the SWH
Decimator LADSPA fixture — the `decimator_1202` module had never been built in
this targeted-build lane tree; `cmake --build build --target decimator_1202`
(EXIT=0) then made the suite pass. Unrelated to this cluster.)

## Evidence table

| Item | Result |
|---|---|
| RED command | `ctest -R '^EqPeakHandoffTest$'` on unfixed code → **EXIT=8** (4 slots FAIL: ctor is `float*`, `decltype` is `float`, stores are plain) |
| Reverse-patch RED | `git apply -R fix.patch` + rebuild + ctest → **EXIT=8** (same 4 failures) |
| GREEN command | `ctest -R '^EqPeakHandoffTest$'` → **EXIT=0** (100% tests passed, 1/1) |
| Affected-family tests executed | 6 (`EqPeakHandoffTest`, `EqFilterTest`, `EqCurveTest`, `PluginPortsMigrationTest`, `DelayControlsTest`, `DelayLfoAutomationTest`) — all **PASS** |
| Files committed | `plugins/Eq/EqFader.h`, `plugins/Eq/EqControls.h`, `plugins/Eq/EqEffect.cpp`, `plugins/Eq/EqParameterWidget.h`, `plugins/Eq/EqControlsDialog.h`, `plugins/Eq/EqControlsDialog.cpp`, `plugins/Delay/DelayControls.h`, `plugins/Delay/DelayEffect.cpp`, `plugins/Compressor/CompressorControls.h`, `plugins/Compressor/Compressor.cpp`, `tests/src/plugins/EqPeakHandoffTest.cpp`, `tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p9-122.md` |

## Notes

- One shared fix; the four reports cannot honestly be split into separate
  commits — each report is a face of the same handoff, and the widget fix
  (new-p9-122) only compiles once the fields (new-p15-58/new-p16-57) are
  atomic. One commit names the cluster and all four ids.
- The Compressor plugin is the sibling-in defect (same EqFader widget): the
  sibling rule brings its fields and writes into the same change.
- `EqControls.cpp` needed no edit: its constructor's `m_... = 0;` lines are
  single-threaded inits and compile as atomic stores.
- `tests/reference/**` is frozen migration evidence (ORIGIN.tsv blobs) — not
  touched; the reference modules carry their own plain-float EqFader copy.
- LOG FOLDER: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-122.r1/logs`

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with
  it (exit codes).** Yes. Unfixed code: `ctest -R '^EqPeakHandoffTest$'` →
  **EXIT=8**, all four slots FAIL (log `test-red-unfixed.log`). Reverse-patch
  proof on the committed fix: `git apply -R fix.patch`, rebuild →
  **EXIT=8**, same four failures (`test-rev-red.log`); `git apply fix.patch`,
  rebuild → **EXIT=0** (`test-final-green.log`). The test compiles on both
  versions (the template helper's plain-float branch is only reached at
  runtime), so the proof isolates the test from the build.
- **The test is deterministic: no wall-clock timing, sleeps or races.** It
  checks compile-time field types (`std::is_same_v<decltype(EqControls::
  m_inPeakL), std::atomic<float>>` and 15 more), source text of the three
  product files read from disk (`readSource("../../plugins/...")`, the
  Vst3HostTest pattern — seam: the on-disk sources the binary is built from),
  and the exchange handoff on a real `DelayEffect`'s fields driven by explicit
  values (store 0.5, exchange must return 0.5 and leave 0; store 0.125,
  exchange must return 0.125). No threads, no timers, no sleeps.
- **The fix is at the cause and complete: every caller and sibling was checked
  and stays consistent.** The one cause is the raw-float EqFader handoff.
  Callers: EqControlsDialog (in/out faders + per-band faders via
  `EqParameterWidget::EqBand` pointers), DelayControlsDialog,
  CompressorControlDialog — all now pass atomic pointers through the changed
  `EqFader`/`setBand` signatures (grep: no other construction site).
  Readers/writers: only `EqEffect::processImpl`/`setBandPeaks`,
  `DelayEffect::processImpl`, `CompressorEffect::processImpl` (writes) and
  `EqFader::updateVuMeters` (read+clear) touch the fields — all converted;
  the ctor inits (`EqControls.cpp`, `DelayControls.cpp`) compile as atomic
  stores. Sibling: Compressor had the identical defect (same widget) and is
  fixed in the same change. Peak fields are never serialized
  (`saveSettings`/`loadSettings` do not mention them), so nothing else reads
  them. Family tests (EqFilterTest, EqCurveTest, PluginPortsMigrationTest —
  renders eq/delay/compressor against the frozen references — DelayControlsTest,
  DelayLfoAutomationTest) all PASS, exit 0.
- **No collateral damage: old projects and saved data still load as before.**
  The peak fields are runtime meters only — `EqControls::saveSettings/
  loadSettings` never touch them (verified by grep over the save/load
  functions), so project files are unchanged and old projects load and render
  identically (PluginPortsMigrationTest renders the migrated eq/delay/
  compressor equal to the frozen references). No limit or check was added that
  any caller could exceed: the only additions are atomics, a lock-free CAS
  raise and relaxed stores — `static_assert(is_always_lock_free)` compiles on
  every platform this project supports or the build fails loudly, and no
  runtime bound was introduced anywhere.
- **If the verdict were ALREADY-FIXED or FALSE-POSITIVE:** not applicable —
  the verdict is FIXED; the original code's race is evidenced in the Finding
  table above (`EqFader.h:71-84` read-then-clear on plain floats written by
  `EqEffect.cpp:180` et al. on the audio thread).
