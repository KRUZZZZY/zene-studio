# Cluster new-p9-625: Carla parameter group ID

**Verdict: FIXED**

`CarlaParamFloatModel::m_groupId` was omitted from the constructor initialization and had no member initializer in [Carla.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-625/plugins/CarlaBase/Carla.h:162). A parameter queried before `setGroupId()` therefore had an indeterminate value. It now has an in-class zero initializer.

The regression lives in [CarlaParamFloatModelTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-625/tests/src/plugins/CarlaParamFloatModelTest.cpp:36), registered for Carla builds in [tests/CMakeLists.txt](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-625/tests/CMakeLists.txt:2435). It checks the runtime default and requires the explicit zero initializer. On this ABI the base constructor happens to zero the bytes used by the uninitialized derived member, so the runtime assertion alone passed before the fix. The source contract makes the regression deterministic; the same test failed when the production patch was reversed.

## Evidence

| Check | Exact command | Exit |
|---|---|---:|
| RED, original header | `ctest -R CarlaParamFloatModelTest --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-625.r1/logs/test-4.log 2>&1; echo EXIT=$?` (cwd: `build/tests`) | 8 |
| GREEN, fixed header | `ctest -R CarlaParamFloatModelTest --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-625.r1/logs/test-5.log 2>&1; echo EXIT=$?` (cwd: `build/tests`) | 0 |
| Mutation RED, reversed header patch | `ctest -R CarlaParamFloatModelTest --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-625.r1/logs/test-6.log 2>&1; echo EXIT=$?` (cwd: `build/tests`) | 8 |
| Restored GREEN | `ctest -R CarlaParamFloatModelTest --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-625.r1/logs/test-7.log 2>&1; echo EXIT=$?` (cwd: `build/tests`) | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 |
| File length gate | `bash tests/file-length-gate.sh --check` | 0 |

The affected ctest family contains **1 test**. The passing QtTest run reports 4 passed checks, 0 failed, 0 skipped. The final green run rebuilt `zene`, `carlabase`, and `CarlaParamFloatModelTest` first (build exit 0). The reversed-patch run rebuilt those same targets first (build exit 0) and failed on `groupIdHasZeroDefaultInitializer`, as required.

## Patch proof commands

```text
git diff -- plugins/CarlaBase/Carla.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-625.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-625.r1/logs/fix.patch; echo EXIT=$?  -> 0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-625.r1/logs/fix.patch; echo EXIT=$?  -> 0
```

Build commands for both patch states:

```text
cmake --build build --target zene carlabase CarlaParamFloatModelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-625.r1/logs/build-8.log 2>&1; echo EXIT=$?  -> 0
cmake --build build --target zene carlabase CarlaParamFloatModelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-625.r1/logs/build-9.log 2>&1; echo EXIT=$?  -> 0
cmake --build build --target zene carlabase CarlaParamFloatModelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-625.r1/logs/build-10.log 2>&1; echo EXIT=$?  -> 0
```

The files included in the cluster commit are:

- `plugins/CarlaBase/Carla.h`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/src/plugins/CarlaParamFloatModelTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-625.md`
