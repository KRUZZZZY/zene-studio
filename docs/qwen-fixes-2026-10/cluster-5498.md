# Cluster 5498 — malformed lane follow ID loads as track 0

**Verdict: ALREADY-FIXED** (on this branch, before this session).

## Finding

Luna patrol, cluster 5498: `src/tracks/AutomationTrack.cpp` — a project with
`follows="bad"` loaded the lane as following track 0, because `QString::toInt()`
returns 0 for unparseable text and 0 is a valid track id (ids start at 0).

## Evidence

The fix is already present in the working tree and committed on this branch:

- `src/tracks/AutomationTrack.cpp:79-84` — `loadTrackSpecificSettings` now reads
  the attribute with the two-argument conversion and maps failure to the
  no-track sentinel:

  ```cpp
  bool followsOk = false;
  const int followsId = _this.attribute("follows", "-1").toInt(&followsOk);
  m_followsTrack = followsOk ? followsId : -1;
  ```

- Regression test already exists: `tests/src/tracks/AutomationTrackTest.cpp:215-242`
  (`testMalformedFollowsIdIsNoTrack`): `follows="bad"` -> `-1`, `follows="3"` -> `3`,
  missing attribute -> `-1`.

- Fix commit on this branch: `c89febfe3 fix(automation): a malformed lane follow id loaded as track 0`
  (ancestor of HEAD `4218c1e30`), touching `src/tracks/AutomationTrack.cpp` and
  `tests/src/tracks/AutomationTrackTest.cpp`. The commit message records the
  original RED (`ctest -R '^AutomationTrackTest$'` -> EXIT=8, "Actual 0, Expected -1")
  and GREEN (EXIT=0) from that session.

- Registration already in place: `tests/upstream-modifications.txt:1009` lists
  `src/tracks/AutomationTrack.cpp` (follows attribute, load default -1).

No production code was changed this session, so nothing new needed registering.

## Commands run this session (fresh verification)

All logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5498.r1/logs`:

```
cmake --build build --target AutomationTrackTest > LOGS/build-1.log 2>&1   EXIT=0
QT_QPA_PLATFORM=offscreen ctest -R '^AutomationTrackTest$' --output-on-failure > LOGS/test-1.log 2>&1   EXIT=0
```

`test-1.log` shows `100% tests passed, 0 tests failed out of 1` (1 test executed — not zero).

Ratchet gates (unpiped exit codes):

```
bash tests/complexity-gate.sh --check   > LOGS/gate-1.log 2>&1   EXIT=0
bash tests/file-length-gate.sh --check  >> LOGS/gate-1.log 2>&1  EXIT=0
```

## Evidence table

| Item | Command | Exit |
| --- | --- | --- |
| RED (historical, from fix commit c89febfe3) | `ctest -R '^AutomationTrackTest$'` | 8 (Actual 0, Expected -1) |
| GREEN (this session) | `QT_QPA_PLATFORM=offscreen ctest -R '^AutomationTrackTest$' --output-on-failure` | 0 |
| Affected-family tests run | 1 (`AutomationTrackTest`) | — |
| complexity-gate | `bash tests/complexity-gate.sh --check` | 0 |
| file-length-gate | `bash tests/file-length-gate.sh --check` | 0 |

## Files committed

- `docs/qwen-fixes-2026-10/cluster-5498.md` (this report; no code change needed).
