# Cluster 4908 regression — RED

Product code was still unfixed for this run. The regression was added first.

## Build

Working directory: repository root.

```sh
cmake --build build --target zene ControlVcaCommandsTest
```

```text
[100%] Built target zene
[100%] Building CXX object tests/CMakeFiles/ControlVcaCommandsTest.dir/src/core/ControlVcaCommandsTest.cpp.o
[100%] Linking CXX executable ControlVcaCommandsTest
[100%] Built target ControlVcaCommandsTest
EXIT=0
```

## Targeted test

Working directory: `build/tests`.

```sh
ctest -R '^ControlVcaCommandsTest$' --output-on-failure
```

```text
1/1 Test #148: ControlVcaCommandsTest ...........***Failed
FAIL!  : ControlVcaCommandsTest::redoingCreateRestoresTheRequestedName() Compared values are not the same
   Actual   (restored.result.value(QStringLiteral("name")).toString()): "VCA 0"
   Expected (name)                                                    : "Redo preserves this name"
Totals: 9 passed, 1 failed, 0 skipped, 0 blacklisted
0% tests passed, 1 tests failed out of 1
EXIT=8
```
