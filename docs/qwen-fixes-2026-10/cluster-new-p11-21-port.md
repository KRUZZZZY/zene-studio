# Cluster new-p11-21-port: FIXED

## Verdict

`smf_read` reported an out-of-range tempo map as importable because it checked
only event capacity. It now validates the events through the same
`mapFromEvents` helper used by `smf_import` and returns the validator's message
as `importability_error` when validation fails.

## File evidence

- [ControlCommandsInterchange.cpp](../../src/core/ControlCommandsInterchange.cpp:336)
  uses `mapFromEvents` for the reported status and returns its error at line 354.
- [SmfInterchangeTest.cpp](../../tests/src/core/SmfInterchangeTest.cpp:326)
  checks that the 4 BPM file is not importable and has a validation reason.
- [commands_snapshot.json](../../tools/mcp-zene-control/zene_control/commands_snapshot.json)
  regenerated from the built binary with the updated result schema.
- [upstream-modifications.txt](../../tests/upstream-modifications.txt)
  records the regression change to the inherited test file.

## Verification evidence

Commands ran from the worktree unless `build/tests` is stated. Build, test and
gate output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-21-port.r1/logs/`.

| Phase | Exact command | Exit | Log / result |
|---|---|---:|---|
| RED build | `cmake --build build --target zene SmfInterchangeTest` | 0 | `02-reversed-build.log` |
| RED test (`build/tests`) | `ctest -R '^SmfInterchangeTest$' --output-on-failure` | 8 | `14-reversed-test.log`; assertion failed |
| GREEN build | `cmake --build build --target zene SmfInterchangeTest SmfInterchangeRoundTripTest` | 0 | `15-restored-build.log` |
| GREEN test (`build/tests`) | `ctest -R '^SmfInterchange(Test\|RoundTripTest)$' --output-on-failure` | 0 | `16-restored.log`; 2/2 passed |
| Reverse production patch | `git diff -- src/core/ControlCommandsInterchange.cpp > .../fix.patch && git apply -R .../fix.patch` | 0 | `fix.patch` |
| Reversed build | `cmake --build build --target zene SmfInterchangeTest` | 0 | `13-reversed-build.log` |
| Reversed test (`build/tests`) | `ctest -R '^SmfInterchangeTest$' --output-on-failure` | 8 | `14-reversed-test.log` |
| Reapply production patch | `git apply .../fix.patch` | 0 | production fix restored |
| Restored build | `cmake --build build --target zene SmfInterchangeTest SmfInterchangeRoundTripTest` | 0 | `15-restored-build.log` |
| Restored test (`build/tests`) | `ctest -R '^SmfInterchange(Test\|RoundTripTest)$' --output-on-failure` | 0 | `16-restored.log`; 2/2 passed |
| Snapshot test (`build/tests`) | `ctest -R '^ControlCommandsSnapshot$' --output-on-failure` | 0 | `17-snapshot.log`; 1/1 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 1 | `09-complexity.log`; pre-existing 6 regressions |
| File length gate | `bash tests/file-length-gate.sh --check` | 1 | `10-filelen.log`; pre-existing 14 regressions |

## Files committed

- `src/core/ControlCommandsInterchange.cpp`
- `tests/src/core/SmfInterchangeTest.cpp`
- `tests/upstream-modifications.txt`
- `tools/mcp-zene-control/zene_control/commands_snapshot.json`
- `docs/qwen-fixes-2026-10/cluster-new-p11-21-port.md`
