FIXED

# Cluster new-p16-236: String-pair drags cannot preserve colons in keys

**Verdict: FIXED**

## The defect

A StringPair drag publishes its pair as the raw text `key:value` under the MIME type
`application/x-zene-stringpair` (`include/Clipboard.h:62`). Every reader splits that text at the
**first** colon, so a key that itself carries a colon is cut in half: the reader sees a truncated
key, and the value it gets is the rest of the key plus the value.

Before this fix:

- `src/gui/StringPairDrag.cpp:94` — the drag producer wrote `( _key + ":" + _value ).toUtf8()`, with no escaping.
- `src/core/Clipboard.cpp:78` (pre-fix) — the clipboard producer wrote `key + ":" + value`, the same format, second producer.
- `src/core/Clipboard.cpp:90` (pre-fix) — `decodeKey()` was `section( ':', 0, 0 )`, the key truncated at the first colon.
- `src/core/Clipboard.cpp:98` — `decodeValue()` was `section( ':', 1, -1 )`, the value keeping every colon it had.
- `src/gui/StringPairDrag.cpp:131` (pre-fix) — `processDragEnterEvent()` compared `txt.section( ':', 0, 0 )` against its allowed-key list: a second, hand-rolled key parse with the same defect.

Proof of corruption, measured by the regression test (`tests/src/gui/BrowserDragUrlTest.cpp:297`):
a drag of key `preset:factory` with value `v` produced the payload `preset:factory:v`, from which
the receiver read key `preset` and value `factory:v`. **Both halves of the pair came out wrong.**

The key namespace is open - `browserresults::dragKey()`, the `FloatModelEditorBase` allowed-key
list, the browser file keys - and nothing in the contract forbids a colon in a key, so the format
was genuinely ambiguous, not merely unused.

## The fix

One shared codec in `Clipboard`, used by both producers and both decoders:

- `include/Clipboard.h:63` — declares `encodeStringPairKey()`.
- `src/core/Clipboard.cpp:115` — `encodeStringPairKey()` escapes the key half in one left-to-right
  pass: a colon becomes `%3A`, a percent sign becomes `%25`. Two characters is enough, because the
  only two characters that can make the format ambiguous are the separator itself and the escape
  marker.
- `src/core/Clipboard.cpp:41` — `decodeStringPairKey()` unescapes in one left-to-right pass, so the
  escape is an exact inverse. A percent sign that is not followed by `3A` or `25` stays a literal
  percent sign, so a payload that was never escaped decodes exactly as it was read before.
- `src/core/Clipboard.cpp:105` — `copyStringPair()` (the clipboard producer) encodes the key.
- `src/core/Clipboard.cpp:135` — `decodeKey()` (the shared decoder) unescapes the key half.
- `src/gui/StringPairDrag.cpp:94` — `createMimeData()` (the drag producer) encodes the key.
- `src/gui/StringPairDrag.cpp:132` — `processDragEnterEvent()` reads the key through the shared
  decoder instead of its own `section( ':', 0, 0 )`, so it compares the whole key.

The **value** half is deliberately left alone: values legitimately carry colons and percent signs
(paths, base64 bodies, serialized clips), `section( ':', 1, -1 )` already keeps them, and escaping
them would change the bytes on the wire for no gain.

The **body shape** is deliberately unchanged (`key:value`, no JSON, no length prefix): the six
in-tree plug-ins that parse the raw payload themselves, the two frozen reference copies under
`tests/reference/`, and `tests/src/gui/SampleEditorDialogTest.cpp:454` all read the raw
`key:value` text, and a new shape would have broken them.

### Why every reader stays consistent

| Reader | Key it compares / decodes | Behaviour after the fix |
| --- | --- | --- |
| `Clipboard::decodeKey()` (`src/core/Clipboard.cpp:131`) | the shared decoder | unescapes the key half |
| `StringPairDrag::processDragEnterEvent()` (`src/gui/StringPairDrag.cpp:132`) | the shared decoder now | same key as every other reader |
| `StringPairDrag::decodeKey()` (`src/gui/StringPairDrag.cpp:147`) | the shared decoder | unchanged, now unescapes |
| `TrackContentWidget` (uses `Clipboard::decodeKey`/`decodeValue`) | the shared decoder | unchanged, now unescapes |
| `plugins/Patman/Patman.cpp:640`, `plugins/SlicerT/SlicerTView.cpp:183,187`, `plugins/AudioFileProcessor/AudioFileProcessorView.cpp:167,172`, `plugins/ZynAddSubFx/ZynAddSubFx.cpp:694`, `plugins/Lv2Instrument/Lv2Instrument.cpp:343`, `plugins/Vestige/Vestige.cpp:868,1177` | raw `section( ':', 0, 0 )` against one **plain** key (`samplefile`, `pluginpresetfile`, `vstplugin`, ...) | the payload for those plain keys is byte-identical to before, so these plug-ins behave exactly as they did |

The six plug-ins were left untouched on purpose: each accepts one fixed key that carries neither a
colon nor a percent sign, so its payload bytes do not change. Where a plug-in *did* mis-read a
colon key before, it now sees the escaped key and refuses the drag instead of accepting it and
decoding the wrong pair - the behaviour the finding describes as the harm.

The two frozen reference copies (`tests/reference/Lv2Instrument/Lv2Instrument.cpp:269`,
`tests/reference/AudioFileProcessor/AudioFileProcessorView.cpp:158`) are blob-pinned upstream
copies and were **not** edited; they parse plain keys, whose payload bytes are unchanged.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before the fix | `cmake --build build --target BrowserDragUrlTest > .../logs/build-1.log 2>&1; echo EXIT=$?` -> **EXIT=0**; `ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?` from `build/tests` -> **EXIT=8**. Failing assertions: `aKeyWithAColonSurvivesTheStringPairPayload` (actual key `preset`, expected `preset:factory`), `aKeyWithAPercentSignSurvivesTheStringPairPayload` (actual `%`, expected `%:`), `aDragEnterAcceptsTheWholeColonKey` (drag-enter refused the allowed colon key), `theClipboardKeepsAColonKeyIntact` (actual key `preset`, expected `preset:factory`). |
| GREEN after the fix | `cmake --build build --target zene BrowserDragUrlTest > .../logs/build-5.log 2>&1; echo EXIT=$?` -> **EXIT=0**; `ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../logs/test-4.log 2>&1; echo EXIT=$?` from `build/tests` -> **EXIT=0**, 1/1 CTest target passed. |
| Reversed-fix proof (4b) | `git diff -- include/Clipboard.h src/core/Clipboard.cpp src/gui/StringPairDrag.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` -> **EXIT=0**; `cmake --build build --target zene BrowserDragUrlTest > .../logs/build-6.log 2>&1` -> **EXIT=0**; `ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../logs/test-5.log 2>&1` -> **EXIT=8**, the same four assertions fail again. |
| Restored-fix proof (4b) | `git apply .../logs/fix.patch` -> **EXIT=0**; `cmake --build build --target zene BrowserDragUrlTest > .../logs/build-7.log 2>&1` -> **EXIT=0**; `ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../logs/test-6.log 2>&1` -> **EXIT=0**, 1/1 passed. |
| Affected family | `ctest -R 'BrowserDragUrlTest\|Lv2InstrumentKeyRegressionTest\|AudioFileProcessorPlayheadTest\|AudioFileProcessorWaveViewTest\|SampleEditorDialogTest' --output-on-failure > .../logs/test-3.log 2>&1` from `build/tests` -> **EXIT=0**, **5 CTest test targets executed, 5/5 passed**. |
| Gates (`feature_check.py`) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gate-1.log 2>&1` -> **EXIT=0**; all eight rows PASS (`fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity`, `file-length`). |
| Gates (`submit_check.py`) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p16-236` -> **EXIT=0** (see the Self-check below). |

All log files are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-236.r2/logs/`.

## Build and test commands

All builds ran from the worktree root; all ctest runs ran from `build/tests`.

```text
cmake --build build --target BrowserDragUrlTest > .../logs/build-1.log 2>&1; echo EXIT=$?  # EXIT=0, test only, product still buggy
ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?  # EXIT=8, RED
cmake --build build --target zene BrowserDragUrlTest > .../logs/build-2.log 2>&1; echo EXIT=$?  # EXIT=2, one target name does not exist in this build tree; corrected in build-3
cmake --build build --target zene BrowserDragUrlTest SampleEditorDialogTest AudioFileProcessorPlayheadTest AudioFileProcessorWaveViewTest partc_ref_lv2instrument partc_ref_audiofileprocessor lv2instrument audiofileprocessor > .../logs/build-3.log 2>&1; echo EXIT=$?  # EXIT=0, fix in place
cmake --build build --target ControlDeviceCatalogueTest > .../logs/build-4.log 2>&1; echo EXIT=$?  # EXIT=0
cmake --build build --target zene BrowserDragUrlTest > .../logs/build-5.log 2>&1; echo EXIT=$?  # EXIT=0, public API left unchanged
ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../logs/test-2.log 2>&1; echo EXIT=$?  # EXIT=0, GREEN
ctest -R 'BrowserDragUrlTest|Lv2InstrumentKeyRegressionTest|AudioFileProcessorPlayheadTest|AudioFileProcessorWaveViewTest|SampleEditorDialogTest' --output-on-failure > .../logs/test-3.log 2>&1; echo EXIT=$?  # EXIT=0, 5/5 family
git diff -- include/Clipboard.h src/core/Clipboard.cpp src/gui/StringPairDrag.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch; echo EXIT=$?  # EXIT=0, product-only patch reversed
cmake --build build --target zene BrowserDragUrlTest > .../logs/build-6.log 2>&1; echo EXIT=$?  # EXIT=0, fix reversed
ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../logs/test-5.log 2>&1; echo EXIT=$?  # EXIT=8, reversed-fix RED
git apply .../logs/fix.patch; echo EXIT=$?  # EXIT=0, fix restored
cmake --build build --target zene BrowserDragUrlTest > .../logs/build-7.log 2>&1; echo EXIT=$?  # EXIT=0, fix restored
ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../logs/test-6.log 2>&1; echo EXIT=$?  # EXIT=0, restored-fix GREEN
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gate-1.log 2>&1; echo EXIT=$?  # EXIT=0, all eight gate rows PASS
```

## Files committed

- `include/Clipboard.h` - declares the StringPair key codec.
- `src/core/Clipboard.cpp` - the codec: encode in `copyStringPair()`, unescape in `decodeKey()`.
- `src/gui/StringPairDrag.cpp` - the drag producer encodes the key; drag-enter reads it through the shared decoder.
- `tests/src/gui/BrowserDragUrlTest.cpp` - the regression (five new cases).
- `tests/upstream-modifications.txt` - the three inherited files declared with their reasons.
- `docs/qwen-fixes-2026-10/cluster-new-p16-236.md` - this report.

No new product file and no new test file: `tests/src/gui/BrowserDragUrlTest.cpp` is already
registered in `tests/fork-sources.txt:2742` and already built by `tests/CMakeLists.txt:1126`, so
`tests/fork-sources.txt` needed no entry.

## Self-check

1. **"The regression test FAILS with your product change reverted and PASSES with it (exit codes)."**
   Reverted: `git diff -- include/Clipboard.h src/core/Clipboard.cpp src/gui/StringPairDrag.cpp >
   .../logs/fix.patch && git apply -R .../logs/fix.patch` (EXIT=0), rebuild (`build-6.log`,
   EXIT=0), `ctest -R '^BrowserDragUrlTest$'` (`test-5.log`) -> **EXIT=8** with the four failing
   assertions listed in the evidence table. Restored: `git apply .../logs/fix.patch` (EXIT=0),
   rebuild (`build-7.log`, EXIT=0), same ctest (`test-6.log`) -> **EXIT=0**, 1/1 passed. The
   reverse-applied patch contains product files only; the regression stayed in place and still
   compiled, so the failure is the assertion, not a broken build.

2. **"The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses)."**
   The five new cases in `tests/src/gui/BrowserDragUrlTest.cpp:297-357` use only injected values:
   a literal key, a literal value, and a `QMimeData` built in the test
   (`StringPairDrag::createMimeData()`), decoded through `QDropEvent`/`QDragEnterEvent` objects
   constructed in the test. No timer, no sleep, no clock, no thread, no polling - every assertion
   reads a value the test itself put in. The clipboard case
   (`tests/src/gui/BrowserDragUrlTest.cpp:351`) writes and reads the clipboard synchronously within
   the same process; it is a value round-trip, not a timing-dependent handover.

3. **"The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent."**
   The cause is the payload format, so the fix is one codec shared by both producers and both
   decoders, not a per-site patch. Checked and consistent:
   - Producers: `Clipboard::copyStringPair()` (`src/core/Clipboard.cpp:103`, used by
     `src/gui/clips/ClipView.cpp:1289` for the system clipboard) and
     `StringPairDrag::createMimeData()` (`src/gui/StringPairDrag.cpp:94`, used by the file
     browser, the search results, the automatable-model and instrument drags, the float editor,
     the track views, the envelope editor and the plugin browser) - both now encode.
   - Decoders: `Clipboard::decodeKey()`/`decodeValue()`, `StringPairDrag::decodeKey()`/`decodeValue()`,
     `TrackContentWidget` - all read through the shared decoder.
   - The second, hand-rolled key parse, `StringPairDrag::processDragEnterEvent()`
     (`src/gui/StringPairDrag.cpp:132`), now goes through the same decoder instead of parsing the
     raw text a second way.
   - The six in-tree plug-ins that parse the raw payload were checked one by one
     (table above); their accepted keys are plain, their payload bytes are unchanged, so they are
     consistent without being edited.
   - The two frozen reference copies under `tests/reference/` were checked and left untouched; they
     parse plain keys whose bytes did not change.
   - `tests/src/gui/SampleEditorDialogTest.cpp:454` writes the payload by hand
     (`"samplefile:/tmp/dropped.wav"`); it still decodes to key `samplefile` and value
     `/tmp/dropped.wav`, and the family run above passes.

4. **"No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it."**
   The payload shape is unchanged (`key:value`), and for every key in use today - `samplefile`,
   `sampledata`, `presetfile`, `pluginpresetfile`, `soundfontfile`, `patchfile`, `vstpluginfile`,
   `importedproject`, `projectfile`, `clip_<tracktype>`, `track_<tracktype>`, `float_value`,
   `automatable_model`, `instrument`, `vstplugin` - neither a colon nor a percent sign occurs, so
   the escaped payload is **byte-identical** to the old one. The regression
   `anOrdinaryKeyKeepsItsStringPairPayload` (`tests/src/gui/BrowserDragUrlTest.cpp:324`) pins that
   byte equality (`samplefile:/tmp/dropped.wav`) and passes. Nothing is stored in a saved project
   or in a session file, so no saved data changes; a payload written by an older Zene (or by hand)
   still decodes unchanged, because a percent sign that is not followed by `3A` or `25` stays a
   literal percent sign. No limit, threshold or check was added, so no caller that legitimately
   exceeds one can be hit. The one behaviour change outside the bug is that a drag whose key
   carries a colon is now *refused* by a target that does not allow that whole key, where before
   it was accepted and decoded into the wrong pair - that is the fix.

5. **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it."**
   Not applicable - the verdict is FIXED. The defect was confirmed empirically before the change:
   the pre-fix `ctest` run (`.../logs/test-1.log`, EXIT=8) shows the receiver reading key `preset`
   for the drag of key `preset:factory`.

Additional notes for the reviewers:

- **Realtime, undo and commands.** Nothing was added on the audio thread: the codec is a plain
  string function on the GUI/clipboard path, with no allocation on any per-period path, no lock,
  no wait, no logging. `encodeStringPairKey()` does allocate a `QString`, but only when a drag or
  a clipboard copy is created - a user action, not a per-period one. No undo step was added (the
  fix changes no state a command can change), and **no control command was added or changed** -
  `tools/mcp-zene-control/zene_control/commands_snapshot.json` is untouched, so the control
  snapshot canary does not move.
- **Portability.** The fix uses only `QString`, `QChar`, `QLatin1String` and `QLatin1Char` - all
  available in Qt 5.15 and Qt 6. No Qt-6-only API, no `windows.h` macro name, no
  `std::filesystem`. The `%3A`/`%25` escape is plain ASCII.
- **Public API.** `include/Clipboard.h` gained one declaration (`encodeStringPairKey()`); no
  existing symbol changed its signature, its export status or its behaviour. An earlier draft
  exported `copyStringPair()`/`decodeKey()`/`decodeValue()` so the test could link them; that was
  dropped, because the test binaries link the static `lmmsobjs` library and need no export
  (`tests/CMakeLists.txt:1607`).
