# Cluster 4908 regression — GREEN

## Targeted build after the fix

Working directory: repository root.

```sh
cmake --build build --target zene ControlVcaCommandsTest
```

```text
[100%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsVca.cpp.o
[100%] Linking CXX executable ../zene
[100%] Built target zene
[100%] Linking CXX executable ControlVcaCommandsTest
[100%] Built target ControlVcaCommandsTest
EXIT=0
```

## Targeted CTest

Working directory: `build/tests`.

```sh
ctest -R '^ControlVcaCommandsTest$' --output-on-failure
```

```text
1/1 Test #148: ControlVcaCommandsTest ...........   Passed
100% tests passed, 0 tests failed out of 1
EXIT=0
```

Verbose targeted run, also from `build/tests`:

```sh
ctest -V -R '^ControlVcaCommandsTest$'
```

```text
PASS   : ControlVcaCommandsTest::redoingCreateRestoresTheRequestedName()
Totals: 10 passed, 0 failed, 0 skipped, 0 blacklisted
1/1 Test #148: ControlVcaCommandsTest ...........   Passed
100% tests passed, 0 tests failed out of 1
EXIT=0
```
