ALREADY-FIXED

# cluster new-p14-299: project-file gain can produce infinite samples

## Evidence
- Restore path: `src/core/SessionClip.cpp:228` reads `gain` via `gainDbAttribute()`.
- `src/core/SessionModelPrivate.h:83-88`: returns the default for non-finite/unparsable text and otherwise `std::clamp(value, -60.0, 24.0)`. So `gain="10000"` loads as 24 dB, `pow(10, 24/20)` is about 15.8, finite. Added by commit 0d5aed2769 (already on this branch).
- Existing test `tests/src/core/SessionMalformedAttributeTest.cpp:416-427` (`nonFiniteAndOutOfRangeNumbersUseDocumentedPolicy`) restores `gain="1000"` and asserts `gainDb() == 24.0f`; `gain="nan"` gives 0.0 (lines 389-390); `-60` boundary accepted (441). `gain="10000"` takes the identical clamp branch.
- `SessionAudioCells.cpp:252` (`pow(10.0f, slot.gainDb()/20.0f)`) is therefore fed only a clamped value from project files.

## Commands (from logs)
- `cmake --build build --target SessionMalformedAttributeTest -j4 > logs/build-1.log 2>&1` EXIT=0
- `cd build/tests && ctest -R SessionMalformedAttribute --output-on-failure > logs/test-1.log 2>&1` EXIT=0 (1/1 passed)
- No product change, so no RED/GREEN or 4b cycle; the verdict is backed by the code references and the passing test above. Not run: feature_check.py (nothing changed).

## Other findings
- Sibling path NOT covered by the finding's trigger: `src/core/ControlCommandsSession.cpp:209` (`applySlotShape`) calls `slot.setGainDb(float(gain_db))` with only `numberProperty()` schema (line 499), no range check. A huge `gain_db` (e.g. 1e40 becomes float inf) reaches `SessionAudioCells.cpp:252` and gives an infinite multiplier. Unfixed here; it is a live-command input, not a project-file one. Suggest clamping to `MinGainPointDb..MaxGainPointDb` in the command handler or in `prepare()`.

## Self-check
- Regression test fails without / passes with the change: not applicable, I made no product change (ALREADY-FIXED). The existing test `SessionMalformedAttributeTest` passes (logs/test-1.log, EXIT=0) and asserts the clamp at `tests/src/core/SessionMalformedAttributeTest.cpp:427`. I did not revert the earlier fix commit 0d5aed2769 to see it fail; that is unverified.
- Deterministic: that test parses fixed XML strings via `parseElement()` and restores a `ClipSlot`; no timing, sleeps or threads.
- Fix complete / siblings: the project-file restore path is the only loader of `gain` (`SessionClip.cpp:228`); `restoreState` is the single entry. The live-command sibling `ControlCommandsSession.cpp:209` is not range-checked; it is listed under Other findings and left unchanged because the finding's trigger is a project file.
- Collateral damage: none, nothing changed in product code or tests. Only this report file was added.
- ALREADY-FIXED proof: `src/core/SessionModelPrivate.h:83-88` (`gainDbAttribute` clamps to [-60,24], non-finite gives default) used at `src/core/SessionClip.cpp:228`; test at `SessionMalformedAttributeTest.cpp:416-427` (`gain="1000"` gives 24.0f).
- Sibling commands in command files: I changed no command files; the app.* / arrangement.* commands listed by the check are unaffected.
