# Cluster 5371 — Remote controls can keep following the previous selection

**Verdict: ALREADY-FIXED** (on this branch, before this run).

## Finding

`src/gui/shell/ShellSelection.cpp:61` — when a selected mixer channel has no
effects, `followRemoteControls()` did not update or clear the remote-control
target, so the knobs kept driving the previously selected track.

## Evidence

The fix is already present in the current sources and in this branch's history:

- `src/gui/shell/ShellSelection.cpp:67-70` — after the track and channel-with-device
  cases, `followRemoteControls()` now falls through to
  `remote->followSelection(QString(), QString());` ("Nothing here to follow ...
  The knobs have to stop driving whatever the last selection named").
- `src/core/RemoteControls.cpp:112-126` — `followSelection()` treats an empty
  target as "drive nothing": it clears `m_target`/`m_plugin`, resets `m_page`,
  and sends nothing back (there is no device whose page that would be).
- Commit `8c47953f9` ("fix(gui): the remote knobs kept driving the selection
  that had gone", Luna cluster 3517) changed exactly these two files and added
  the regression test `DevicePanelTest::followingStopsWhenTheNewSelectionHasNothingToFollow`
  (`tests/src/gui/DevicePanelTest.cpp:427-444`), which selects a track, then a
  mixer channel with no effects, and asserts the remote target becomes empty.

This finding is a re-report (4th time) of that same bug; no code change was
needed or made.

## Verification on the current (already-fixed) code

Exact commands run in this session (logs in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5371.r1/logs`):

```
cmake --build build --target zene DevicePanelTest > LOGS/build-1.log 2>&1   EXIT=0
ctest -R '^DevicePanelTest$' --output-on-failure > LOGS/test-1.log 2>&1     EXIT=0
```

`test-1.log` line 5: `100% tests passed, 0 tests failed out of 1` — the
`DevicePanelTest` executable ran (1 test executed, not zero) and all its cases,
including `followingStopsWhenTheNewSelectionHasNothingToFollow`, passed.

Gates (no code changed, run for completeness):

```
bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1                EXIT=0
bash tests/file-length-gate.sh --check >> LOGS/gate-1.log 2>&1              EXIT=0
```

No production files were modified in this run; only this report was committed.
