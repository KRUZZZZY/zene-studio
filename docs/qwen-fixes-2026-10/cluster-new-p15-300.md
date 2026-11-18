# Cluster new-p15-300: the compressor's visualizer shared state with the audio thread

Verdict: **FIXED**

## The finding

`plugins/Compressor/CompressorControlDialog.cpp:395` (the finding's line; the code moved
with this fix) - the control dialog's timer and the audio thread wrote and read the same
plain-float fields:

- the dialog sampled `m_displayPeak` / `m_displayGain` and reset them
  (`plugins/Compressor/CompressorControlDialog.cpp:398-404` before the fix);
- the audio thread raised the same two fields to the block's peak and gain
  (`plugins/Compressor/Compressor.cpp:403` and `:489-490` before the fix);
- the dialog read the audio thread's own working state (`m_yL`, `m_gainResult`) to compute
  what to reset them to (`plugins/Compressor/CompressorControlDialog.cpp:401-404` before the
  fix) while the audio thread overwrote those two fields for every frame it processed
  (`plugins/Compressor/Compressor.cpp:367`, `:384`, `:411-426`).

Two threads writing and reading the same non-atomic objects is a data race: undefined
behaviour, and a graph that could show values from two different blocks at once. The
`isEnabled() / isAwake()` check in the dialog does not synchronise anything - it only
decides whether the dialog shows the resting values.

**This is a real defect on this branch, not a false positive**: the fields were plain
`float` (`plugins/Compressor/Compressor.h:147-148` before the fix) and both threads
touched them.

## The fix

The state is published, not shared:

- `plugins/Compressor/Compressor.h:151-152` - the two holds the two threads write
  (`m_displayPeak`, `m_displayGain`) are `std::atomic<float>` now. The audio thread's
  raise and the dialog's reset are two well-defined stores instead of a race.
- `plugins/Compressor/Compressor.h:157-158` - what the dialog resets the holds to is
  published by the audio thread: `m_publishedEnvelope` (the detector's envelope, `m_yL`)
  and `m_publishedGain` (the gain the block applied, `m_gainResult`).
- `plugins/Compressor/Compressor.cpp:409-412` - the audio thread raises the peak hold with
  an atomic load and store.
- `plugins/Compressor/Compressor.cpp:497-509` - the audio thread raises the gain hold and
  then publishes the envelope and the applied gain for the dialog.
- `plugins/Compressor/Compressor.cpp:74-80` and `:597-600` - the visualizer starts at rest
  and returns to rest when the effect is bypassed, as before.
- `plugins/Compressor/CompressorControlDialog.cpp:390-409` - the dialog loads and stores the
  holds atomically and resets them to the published envelope and gain. It no longer reads
  `m_yL` or `m_gainResult` at all.

The audio thread's own working state (`m_yL`, `m_gainResult`,
`plugins/Compressor/Compressor.h:143-144`) stays a plain float and stays private to the
audio thread - nothing outside `processImpl()` reads or writes it now.

Nothing else in the tree touches these fields: the only other copy of the plugin is the
frozen reference under `tests/reference/Compressor/`, which is verbatim upstream evidence
and is not touched.

## Evidence table

Every command ran from the worktree root except `ctest`, which ran from `build/tests`.
All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-300.r1/logs`.

| Phase | Exact command | Log | EXIT |
|---|---|---|---:|
| Test target before the tree was regenerated | `cmake --build build --target CompressorVisualizerTest` | `build-1.log` | 2 |
| Regenerate the lane build tree (the guard blocks `cmake -S . -B build`) | `cmake --build build --target zene` | `build-2.log` | 0 |
| Build the regression (pre-fix) | `cmake --build build --target CompressorVisualizerTest` | `build-3.log` | 0 |
| **RED, pre-fix** | `ctest -R '^CompressorVisualizerTest$' --output-on-failure` | `test-1-red.log` | **8** |
| Build product + test with the fix | `cmake --build build --target zene compressor CompressorVisualizerTest` | `build-4.log` | 0 |
| **GREEN, with the fix** | `ctest -R '^CompressorVisualizerTest$' --output-on-failure` | `test-2-green.log` | **0** |
| Reverse the product patch | `git diff -- plugins/Compressor/Compressor.h plugins/Compressor/Compressor.cpp plugins/Compressor/CompressorControlDialog.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` | `fix.patch` | 0 |
| Rebuild with the product change reverted | `cmake --build build --target zene compressor CompressorVisualizerTest` | `build-5-reversed.log` | 0 |
| **RED, product change reverted** | `ctest -R '^CompressorVisualizerTest$' --output-on-failure` | `test-3-red-reversed.log` | **8** |
| Reapply the product patch | `git apply .../logs/fix.patch` | - | 0 |
| Rebuild with the product change restored | `cmake --build build --target zene compressor CompressorVisualizerTest` | `build-6-restored.log` | 0 |
| **GREEN, restored** | `ctest -R '^CompressorVisualizerTest$' --output-on-failure` | `test-4-green-restored.log` | **0** |
| Build the affected family | `cmake --build build --target PluginPortsMigrationTest RealtimeSnapshotTest` | `build-7-family.log` | 0 |
| Affected family | `ctest -R 'Compressor\|PluginPortsMigration\|RealtimeSnapshot' --output-on-failure` | `test-5-family.log` | 8 (one pre-existing failure, see below) |
| The same family with the product change reverted | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | `test-6-reverted-family.log` | 8 (the same one failure) |
| Rebuild the product with the fix restored | `cmake --build build --target zene compressor CompressorVisualizerTest PluginPortsMigrationTest` | `build-9-restored.log` | 0 |

Test counts:

| Run | Command | Result |
|---|---|---|
| RED (`test-1-red.log`) | `ctest -R '^CompressorVisualizerTest$'` | 1 CTest target; Qt cases: **2 passed, 2 failed** |
| GREEN (`test-2-green.log`) | `ctest -R '^CompressorVisualizerTest$'` | 1 CTest target, **100% passed out of 1** |
| RED, reverted (`test-3-red-reversed.log`) | `ctest -R '^CompressorVisualizerTest$'` | 1 CTest target; Qt cases: **2 passed, 2 failed** |
| GREEN, restored (`test-4-green-restored.log`) | `ctest -R '^CompressorVisualizerTest$'` | 1 CTest target, **100% passed out of 1** |
| Family (`test-5-family.log`) | `ctest -R 'Compressor\|PluginPortsMigration\|RealtimeSnapshot'` | 3 CTest targets, 2 passed, 1 failed (`PluginPortsMigrationTest`: 66 passed, 1 failed, 2 skipped) |

The failing assertion is identical in both RED logs:

```
FAIL!  : CompressorVisualizerTest::theVisualizerHoldsAreAtomic() '(std::is_same_v<HoldElement<lmms::CompressorEffect>, std::atomic<float>>)' returned FALSE. (m_displayPeak is written by the audio thread and by the control dialog: it must be atomic)
FAIL!  : CompressorVisualizerTest::theResetValuesArePublished() 'PublishesTheResetValues<lmms::CompressorEffect>::value' returned FALSE. (the control dialog must reset the holds to values the audio thread published, not to the audio thread's own working state)
```

### The one failure in the family is pre-existing and unrelated

`PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid()` fails with
`SWH Decimator test fixture was not loaded`. It is a LADSPA fixture that this tree does
not build (`plugins/LadspaEffect/swh/plugins/decimator` does not exist), and it fails
identically with the product change reverted (`test-6-reverted-family.log`, EXIT 8, the
same single case, 66 passed / 1 failed / 2 skipped).

The compressor's own case in that test **passes and is sample-exact against the frozen
reference**:

```
QINFO  : PluginPortsMigrationTest::migratedPluginsPreserveBehaviour() compressor: 32768 samples | ... sha256 migrated=74705cb7068f8d727c52a86c785988712fdca568dfb2cc257ed067c9bcce00c5 reference=74705cb7068f8d727c52a86c785988712fdca568dfb2cc257ed067c9bcce00c5 | max|delta|=0
```

so the fix does not change a single sample of the compressor's output.

## The regression

`tests/src/plugins/CompressorVisualizerTest.cpp` (new, fork-NEW; registered in
`tests/fork-sources.txt` and `tests/all-sources.txt`).

It is deterministic: it asserts on the *type* of the state the two threads share and on
the existence of the published values - no clock, no sleep, no thread, no seeded race. The
detection of the published values is written so that it compiles against the old header as
well (a substitution failure on the missing member), which is what makes the RED a
runtime failure instead of a build failure.

The plugin is built as a module, so the test reads the plugin's header directly and opens
the access specifiers for its own translation unit only (the `SlicerTZoomTest` precedent);
that affects neither the layout nor the name mangling.

## Registration

- `plugins/Compressor/Compressor.h`, `plugins/Compressor/Compressor.cpp`,
  `plugins/Compressor/CompressorControlDialog.cpp` - inherited files, changed here, their
  reasons appended to `tests/upstream-modifications.txt`.
- `tests/CMakeLists.txt` - inherited file, changed here, reason appended to
  `tests/upstream-modifications.txt`.
- `tests/src/plugins/CompressorVisualizerTest.cpp` - fork-NEW test source, registered in
  `tests/fork-sources.txt` and derived into `tests/all-sources.txt` by that manifest's own
  recipe (`bash tests/all-sources-reproduce.sh` prints `REPRODUCES`, EXIT 0).
- `docs/qwen-fixes-2026-10/cluster-new-p15-300.md` - this report, registered in
  `tests/fork-sources.txt`.

No new control command, no new project file, no saved data touched.

## Self-check

**The regression test FAILS with the product change reverted and PASSES with it (exit
codes).** `test-1-red.log` EXIT 8 and `test-3-red-reversed.log` EXIT 8 with the two
assertions quoted above; `test-2-green.log` EXIT 0 and `test-4-green-restored.log` EXIT 0.
The reverse-patch step (`git apply -R .../logs/fix.patch`, then `git apply`) is recorded
with its exit codes in the evidence table.

**The test is deterministic: no wall-clock timing, sleeps or races.** It reads two types
and one trait out of the plugin's header; there is no clock, no thread and no injected
value at all. The seam is the header itself: the test compiles the plugin's header with
the access specifiers opened (`#define private public`, the `SlicerTZoomTest` precedent),
and the trait that looks for the published members is written so that it compiles against
the old header too - which is why the RED is a runtime failure and not a build failure.

**The fix is at the cause and complete: every caller and sibling of what you changed was
checked and stays consistent.** Every access to the four fields was enumerated before the
change (`grep -rn 'm_displayPeak|m_displayGain|m_publishedEnvelope|m_publishedGain'`):
the only writers and readers are `plugins/Compressor/Compressor.cpp` (the constructor, the
per-frame peak hold, the per-block gain hold, the bypass reset) and
`plugins/Compressor/CompressorControlDialog.cpp` (`updateDisplay()`, the only user of the
visualizer). All of them are updated; no other caller exists. `m_yL` and `m_gainResult`
stay the audio thread's private working state and are no longer read by the dialog. The
frozen copy under `tests/reference/Compressor/` is deliberately untouched (it is the
upstream evidence the migration test compares against) and keeps the old plain floats.

**No collateral damage: old projects and saved data still load as before, and any limit or
check you added does not hit other callers that legitimately exceed it.** No saved data,
no project format, no parameter and no model changed - the fix touches the visualizer
state only. The audio path's arithmetic is untouched: the compressor's render is
sample-exact against the frozen reference (`sha256 ... | max|delta|=0`, above). The
dialog's visible behaviour is unchanged: it still samples the same two holds and resets
them to the detector's envelope and the applied gain, only from the audio thread's
published copies instead of from its live working state (at most one block old, which is
invisible on a display that refreshes every few tens of milliseconds). The two values it
reads when the effect is disabled or asleep are the same two it wrote before (the noise
floor and 1.0). No new limit or check was added anywhere.

**Realtime, undo and commands.** The audio thread's new work is four relaxed atomic
stores per block (`m_publishedEnvelope`, `m_publishedGain`) and one relaxed atomic
load/store pair per frame and channel (`m_displayPeak`): no allocation, no free, no
mutex, no wait, no logging, no allocating Qt call, no signal - `std::atomic<float>` is
lock-free on every platform this product builds for, and the existing
`std::atomic<float> m_deepestGain` in the same class is the precedent. The dialog's
stores are on the GUI thread and were already there. Nothing about undo or the command
surface changed: no control command was added, no command's arguments changed, and the
fix touches no command file, so the `ControlCommandsSnapshot` canary does not move. The
product's save format is untouched (no `saveSettings`/`loadSettings` change), so old
projects and saved data load exactly as before.

**Portability.** The change uses `std::atomic<float>` (C++20, already used in this very
class) and nothing Qt-6-only, no `windows.h` macro names, no `std::filesystem`. The
test uses `std::void_t`/`std::is_same_v`/`std::remove_cvref_t` (C++17) and `QVERIFY2`.

**Sibling commands in the command files you changed** - none: this fix changes no command
file (`tools/mcp-zene-control/zene_control/commands_snapshot.json` is untouched), and
the product's agent surface is unchanged.

## Gates

| Exact command | Log | EXIT |
|---|---|---:|
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-300` | `gate-1-feature-check.log` | **0** |

```
feature-check zene-qwen-new-p15-300 (base b61e5f965, 9 changed files; logs in .../zene-qwen-new-p15-300-feature-check)
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

The two ratchets (complexity, file-length) are the ones that only fail on items NEW in the
files this change touches: the fix adds no function and no new file over 500 lines, and
the new test source is 113 lines.

## Files committed

- `plugins/Compressor/Compressor.h`
- `plugins/Compressor/Compressor.cpp`
- `plugins/Compressor/CompressorControlDialog.cpp`
- `tests/src/plugins/CompressorVisualizerTest.cpp` (new)
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p15-300.md` (new)
