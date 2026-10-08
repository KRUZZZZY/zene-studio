# Cluster 3887 — RED transcript

The pinned worktree was at `9e77a0188`. Before the product edit, `handleArm` called
`tap->setEnabled(requested)`, `control::addUndoStep`, and inserted `__transaction` for every
successful request. The regression was added to the existing socket test first.

From the worktree root:

```text
$ cmake --build build --parallel 12 --target zene MeterTapTest audiofileprocessor
EXIT=0
```

From `build/tests`:

```text
$ ctest -R '^(ControlMeterCommands|MeterTapTest)$' --output-on-failure
EXIT=8
1/2 Test #68: MeterTapTest ................. Passed
2/2 Test #377: ControlMeterCommands ........ Failed
  a no-op disarm adds no undo step .......... FAILED
  a no-op disarm adds no transaction record . FAILED
50% tests passed, 1 tests failed out of 2
```

Two tests ran (nonzero count). The state assertion showed that one undo after the repeated
disarm left the tap disarmed; the transaction assertion showed that the no-op was also
reported as a reversible transaction.
