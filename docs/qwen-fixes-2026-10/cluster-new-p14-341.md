FIXED
new-p14-341: FIXED
new-p10-423: FIXED
new-p12-273: FIXED

new-p14-341 (src/core/UndoHistory.cpp:374): loadSteps now validates the declared `steps` count against the collected step list before pushing any snapshots.
new-p10-423 (src/core/UndoHistory.cpp:62): stripMetadataNodes now prunes a section root carrying metadata="1" (returns false) and fingerprint skips it.
new-p12-273 (src/core/UndoHistory.cpp:225): fingerprint now includes root version and type attributes.

Evidence/commands:
- RED: ctest -R '^UndoHistoryPersistenceTest$' EXIT=8 (anIncompleteSavedHistoryIsDiscarded loaded_steps=1; fingerprint mismatches)
- GREEN: same ctest EXIT=0 (13/13 tests passed)
- feature_check.py EXIT=0
- git apply -R .../fix.patch then rebuild/test RED EXIT=8; git apply ... GREEN EXIT=0

## Self-check
- RED/GREEN: reversed product hunks via fix.patch, rebuilt UndoHistoryPersistenceTest, ctest EXIT=8 with the new tests failing; restored, ctest EXIT=0.
- Deterministic: tests use injected DOM attribute counts and direct UndoHistory::fingerprint calls; no sleeps/timing/races.
- Cause/complete: loadSteps count check before push; stripMetadataNodes root handling mirrored in fingerprint; root version/type hashed. Callers (beginLoad/seal) unchanged.
- No collateral: missing `steps` attribute skips validation; old projects without undohistory unaffected; fingerprint change only affects new saves.
