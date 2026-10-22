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
- `src/core/Clipboard.cpp:142` — `encodeStringPairKey()` escapes the key half in one left-to-right
  pass: a colon becomes `%3A`, a percent sign becomes `%25`. Two characters is enough, because the
  only two characters that can make the format ambiguous are the separator itself and the escape
  marker.
- `src/core/Clipboard.cpp:57` — `decodeStringPairKey()` unescapes in one left-to-right pass, so the
  escape is an exact inverse.
- `src/core/Clipboard.cpp:130` — `copyStringPair()` (the clipboard producer) encodes the key.
- `src/core/Clipboard.cpp:165` — `decodeKey()` (the shared decoder) unescapes the key half.
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

### The key half is versioned (round-2 rework)

The first version of this fix escaped the key half **unconditionally**, and the decoder translated
`%3A` and `%25` in every payload it read. Round 2 found that this corrupted valid *legacy*
payloads: a key written by an older Zene as `a%3A` was read as `a:`, and `a%25` as `a%`, while the
decoder before the fix (`b61e5f965:src/core/Clipboard.cpp:90`) returned the key text unchanged.

The escape is therefore **versioned**: a key that has to be escaped is *marked*, and only a marked
key half is unescaped.

- `src/core/Clipboard.cpp:44` — `markedKeyPrefix` (`"%1"`) opens the key half of a payload Zene
  wrote for a key that needed escaping.
- `src/core/Clipboard.cpp:142` — `encodeStringPairKey()` writes the key **as it always was** when
  it carries neither a colon nor a percent sign (no marker, byte-identical payload), and writes
  `markedKeyPrefix` + the escaped key when it does.
- `src/core/Clipboard.cpp:59` — a key half that does **not** open with the marker is returned
  **literally**: it is a payload an older Zene wrote, or one written by hand.
- `src/core/Clipboard.cpp:65` — a marked key half that carries no escape sequence was never marked
  by the encoder, so it is returned literally too. That covers a legacy key that merely starts with
  the marker's text.

So a legacy payload's key is read exactly as it was read before the fix, and a key that Zene had to
escape is distinguishable from every legacy payload of the same key - which is what
`tests/src/gui/BrowserDragUrlTest.cpp:390` asserts.

The one residual, stated plainly: a legacy key that *both* opens with the marker's text and
carries an escape sequence would still be read as a marked payload. The marker text is `%1`, the
key namespace is product-controlled (every key in use is a lower-case identifier such as
`samplefile`, `clip_0`, `track_1`, `float_value`), and no such key exists in the tree; a legacy key
would have to be crafted to collide.

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

All rows below describe the **current** tree (round 2). The round-1 rows are kept at the end for
the record; round 2 re-ran every proof against the reworked codec.

| Check | Exact command and result |
| --- | --- |
| RED: the whole product change reverted (base `b61e5f965`) | `git diff b61e5f965 -- include/Clipboard.h src/core/Clipboard.cpp src/gui/StringPairDrag.cpp > .../logs/fix-full.patch && git apply -R .../logs/fix-full.patch` -> **EXIT=0**; `cmake --build build --target zene BrowserDragUrlTest > .../logs/build-11.log 2>&1` -> **EXIT=0**; `ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../logs/test-11.log 2>&1` -> **EXIT=8**. Failing assertions: `aKeyWithAColonSurvivesTheStringPairPayload`, `aKeyWithAPercentSignSurvivesTheStringPairPayload`, `aDragEnterAcceptsTheWholeColonKey`, `theClipboardKeepsAColonKeyIntact`, `anEscapedKeyIsMarkedInThePayload`. |
| GREEN with the fix in place | `git apply .../logs/fix-full.patch` -> **EXIT=0**; `cmake --build build --target zene BrowserDragUrlTest > .../logs/build-12.log 2>&1` -> **EXIT=0**; `ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../logs/test-12.log 2>&1` -> **EXIT=0**, 1/1 CTest target passed, 16/16 cases in the binary. |
| 4b for the round-2 rework: the rework reverted | `git diff -- include/Clipboard.h src/core/Clipboard.cpp src/gui/StringPairDrag.cpp > .../logs/fix-round2.patch && git apply -R .../logs/fix-round2.patch` -> **EXIT=0**; `cmake --build build --target zene BrowserDragUrlTest > .../logs/build-9.log 2>&1` -> **EXIT=0**; `ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../logs/test-9.log 2>&1` -> **EXIT=8**: `aLegacyPayloadIsReadLiterally` fails with actual key `a:` and expected `a%3A` - exactly the compatibility regression round 2 reported. |
| 4b for the round-2 rework: restored | `git apply .../logs/fix-round2.patch` -> **EXIT=0**; `cmake --build build --target zene BrowserDragUrlTest > .../logs/build-10.log 2>&1` -> **EXIT=0**; `ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../logs/test-10.log 2>&1` -> **EXIT=0**, 1/1 passed. |
| Affected family (the five focused CTest cases) | `cmake --build build --target zene BrowserDragUrlTest SampleEditorDialogTest AudioFileProcessorPlayheadTest AudioFileProcessorWaveViewTest ControlDeviceCatalogueTest partc_ref_lv2instrument partc_ref_audiofileprocessor > .../logs/build-13.log 2>&1` -> **EXIT=0**; `ctest -R 'BrowserDragUrlTest\|Lv2InstrumentKeyRegressionTest\|AudioFileProcessorPlayheadTest\|AudioFileProcessorWaveViewTest\|SampleEditorDialogTest' --output-on-failure > .../logs/test-13.log 2>&1` from `build/tests` -> **EXIT=0**, **5 CTest test targets executed, 5/5 passed**. |
| Gates (`feature_check.py`) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gate-2.log 2>&1` -> **EXIT=0**; all eight rows PASS (`fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity`, `file-length`). |
| Gates (`submit_check.py`) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p16-236` -> **EXIT=0** (see the Self-check below). |
| Round-1 record | RED `.../logs/test-1.log` **EXIT=8**; GREEN `.../logs/test-4.log` **EXIT=0**; reversed fix `.../logs/test-5.log` **EXIT=8**; restored `.../logs/test-6.log` **EXIT=0**; family `.../logs/test-3.log` **EXIT=0**, 5/5; gates `.../logs/gate-1.log` **EXIT=0**. |

All log files are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-236.r2/logs/`.

## Build and test commands

All builds ran from the worktree root; all ctest runs ran from `build/tests`.

```text
# round 1
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

# round 2 - the key half is versioned, so a legacy payload's key is read literally
cmake --build build --target zene BrowserDragUrlTest > .../logs/build-8.log 2>&1; echo EXIT=$?  # EXIT=0, versioned codec in place
ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../logs/test-8.log 2>&1; echo EXIT=$?  # EXIT=0, 16/16 cases in the binary
git diff -- include/Clipboard.h src/core/Clipboard.cpp src/gui/StringPairDrag.cpp > .../logs/fix-round2.patch && git apply -R .../logs/fix-round2.patch; echo EXIT=$?  # EXIT=0, the round-2 rework reversed
cmake --build build --target zene BrowserDragUrlTest > .../logs/build-9.log 2>&1; echo EXIT=$?  # EXIT=0, rework reversed
ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../logs/test-9.log 2>&1; echo EXIT=$?  # EXIT=8, aLegacyPayloadIsReadLiterally: actual "a:", expected "a%3A"
git apply .../logs/fix-round2.patch; echo EXIT=$?  # EXIT=0, rework restored
cmake --build build --target zene BrowserDragUrlTest > .../logs/build-10.log 2>&1; echo EXIT=$?  # EXIT=0, rework restored
ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../logs/test-10.log 2>&1; echo EXIT=$?  # EXIT=0, restored GREEN
git diff b61e5f965 -- include/Clipboard.h src/core/Clipboard.cpp src/gui/StringPairDrag.cpp > .../logs/fix-full.patch && git apply -R .../logs/fix-full.patch; echo EXIT=$?  # EXIT=0, the whole product change reversed
cmake --build build --target zene BrowserDragUrlTest > .../logs/build-11.log 2>&1; echo EXIT=$?  # EXIT=0, whole change reversed
ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../logs/test-11.log 2>&1; echo EXIT=$?  # EXIT=8, five failing cases
git apply .../logs/fix-full.patch; echo EXIT=$?  # EXIT=0, whole change restored
cmake --build build --target zene BrowserDragUrlTest > .../logs/build-12.log 2>&1; echo EXIT=$?  # EXIT=0, whole change restored
ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../logs/test-12.log 2>&1; echo EXIT=$?  # EXIT=0, GREEN
cmake --build build --target zene BrowserDragUrlTest SampleEditorDialogTest AudioFileProcessorPlayheadTest AudioFileProcessorWaveViewTest ControlDeviceCatalogueTest partc_ref_lv2instrument partc_ref_audiofileprocessor > .../logs/build-13.log 2>&1; echo EXIT=$?  # EXIT=0, the five focused targets rebuilt
ctest -R 'BrowserDragUrlTest|Lv2InstrumentKeyRegressionTest|AudioFileProcessorPlayheadTest|AudioFileProcessorWaveViewTest|SampleEditorDialogTest' --output-on-failure > .../logs/test-13.log 2>&1; echo EXIT=$?  # EXIT=0, 5/5 focused cases
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gate-2.log 2>&1; echo EXIT=$?  # EXIT=0, all eight gate rows PASS
```

## Files committed

Round 1 (commit `e22f0ec9d`) and round 2 (the versioned key half, this commit) together touch:

- `include/Clipboard.h` - declares the StringPair key codec.
- `src/core/Clipboard.cpp` - the codec: mark and escape the key in `copyStringPair()`, unescape a
  marked key in `decodeKey()`, read an unmarked (legacy) key literally.
- `src/gui/StringPairDrag.cpp` - the drag producer encodes the key; drag-enter reads it through the shared decoder.
- `tests/src/gui/BrowserDragUrlTest.cpp` - the regression (eight new cases: five for the colon-key
  fix, three for the legacy-payload compatibility round 2 asked for).
- `tests/upstream-modifications.txt` - the three inherited files declared with their reasons.
- `docs/qwen-fixes-2026-10/cluster-new-p16-236.md` - this report.

No new product file and no new test file: `tests/src/gui/BrowserDragUrlTest.cpp` is already
registered in `tests/fork-sources.txt:2742` and already built by `tests/CMakeLists.txt:1126`, so
`tests/fork-sources.txt` needed no entry.

## Self-check

1. **"The regression test FAILS with your product change reverted and PASSES with it (exit codes)."**
   Two reversals were run, both on product files only, with the regression left in place and still
   compiling (so the failure is an assertion, never a broken build).
   - The **whole** product change vs the base `b61e5f965`: `git diff b61e5f965 -- include/Clipboard.h
     src/core/Clipboard.cpp src/gui/StringPairDrag.cpp > .../logs/fix-full.patch && git apply -R
     .../logs/fix-full.patch` (EXIT=0), rebuild (`build-11.log`, EXIT=0), `ctest -R
     '^BrowserDragUrlTest$'` (`test-11.log`) -> **EXIT=8** with five failing cases (the four
     colon/percent cases and `anEscapedKeyIsMarkedInThePayload`). Restored: `git apply
     .../logs/fix-full.patch` (EXIT=0), rebuild (`build-12.log`, EXIT=0), same ctest (`test-12.log`)
     -> **EXIT=0**, 1/1 passed.
   - The **round-2 rework** on its own: `git diff -- include/Clipboard.h src/core/Clipboard.cpp
     src/gui/StringPairDrag.cpp > .../logs/fix-round2.patch && git apply -R .../logs/fix-round2.patch`
     (EXIT=0), rebuild (`build-9.log`, EXIT=0), `ctest -R '^BrowserDragUrlTest$'` (`test-9.log`)
     -> **EXIT=8**, `aLegacyPayloadIsReadLiterally` fails with actual key `a:` and expected `a%3A`
     - the compatibility regression round 2 reported, now guarded. Restored: `git apply
     .../logs/fix-round2.patch` (EXIT=0), rebuild (`build-10.log`, EXIT=0), same ctest
     (`test-10.log`) -> **EXIT=0**.

2. **"The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses)."**
   The eight new cases in `tests/src/gui/BrowserDragUrlTest.cpp:297-400` use only injected values:
   a literal key, a literal value, and a `QMimeData` built in the test - either through
   `StringPairDrag::createMimeData()` or, for the legacy cases, from raw bytes written straight into
   the MIME type with `QMimeData::setData()` - decoded through `QDropEvent`/`QDragEnterEvent` objects
   constructed in the test. No timer, no sleep, no clock, no thread, no polling - every assertion
   reads a value the test itself put in. The clipboard case
   (`tests/src/gui/BrowserDragUrlTest.cpp:351`) writes and reads the clipboard synchronously within
   the same process; it is a value round-trip, not a timing-dependent handover.

3. **"The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent."**
   The cause is the payload format, so the fix is one codec shared by both producers and both
   decoders, not a per-site patch. Checked and consistent:
   - Producers: `Clipboard::copyStringPair()` (`src/core/Clipboard.cpp:130`, used by
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
   - Round 2 changed only the codec, so the callers above needed no further change; the version
     marker is applied and read in exactly one place each (`src/core/Clipboard.cpp:142` and
     `src/core/Clipboard.cpp:57`).

4. **"No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it."**
   The payload shape is unchanged (`key:value`), and for every key in use today - `samplefile`,
   `sampledata`, `presetfile`, `pluginpresetfile`, `soundfontfile`, `patchfile`, `vstpluginfile`,
   `importedproject`, `projectfile`, `clip_<tracktype>`, `track_<tracktype>`, `float_value`,
   `automatable_model`, `instrument`, `vstplugin` - neither a colon nor a percent sign occurs, so
   the payload is **byte-identical** to the old one - no marker, no escape. The regression
   `anOrdinaryKeyKeepsItsPayloadBytes` (`tests/src/gui/BrowserDragUrlTest.cpp:324`) pins that byte
   equality (`samplefile:/tmp/dropped.wav`) and passes. Nothing is stored in a saved project or in a
   session file, so no saved data changes.
   **Legacy payloads are read exactly as the old decoder read them**: a key half that does not
   carry the version marker is returned literally (`src/core/Clipboard.cpp:59`), so a legacy key
   `a%3A` stays `a%3A` and `a%25` stays `a%25` instead of being decoded into `a:` and `a%` - the
   compatibility regression round 2 found, which the version marker removes. The regressions
   `aLegacyPayloadIsReadLiterally` (`tests/src/gui/BrowserDragUrlTest.cpp:361`) and
   `aLegacyPayloadWithAColonKeyIsReadAsBefore` (`tests/src/gui/BrowserDragUrlTest.cpp:379`) pin
   that, from raw legacy MIME bytes.
   No limit, threshold or check was added, so no caller that legitimately exceeds one can be hit.
   The one behaviour change outside the bug is that a drag whose key carries a colon is now
   *refused* by a target that does not allow that whole key, where before it was accepted and
   decoded into the wrong pair - that is the fix.

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
