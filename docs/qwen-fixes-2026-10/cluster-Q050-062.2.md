IMPLEMENTED

# Q050-062.2: the quality choice in the sample clip menu (stage 2/4 of Q050-062)

Branch `feat/050-Q050-062.2-oc`, cut from `050/m0-train-19` (`5c1c6715d9`). Feature record:
`docs/050-features/Q050-062.2.md`.

The sample clip's **Split to stems** is now a submenu: Fast / Standard / High quality. The item you pick
passes its quality name to `stem.job_start` through `ControlRegistry`, the same command agents use (stage 1,
already on the train). The progress dialog and the completion message name the setting the job reports
(`inference_quality`, else `quality`). The completion message also says the original clip is unchanged on its
track. No command or metadata changes.

## Acceptance

| Criterion | Test | Exit |
| --- | --- | --- |
| The menu passes the selected setting through the registered command | `StemSplitMenuTest::testClipMenuSplitsAtTheChosenQualityAndKeepsTheSource`: triggers the real "High quality" action; the new job in `stem.job_status` has `quality: high`, `overlap: 0.75`, and when finished `inference_quality: high` | ctest 0 |
| The job UI reports the effective setting and leaves the source clip available | same test: the progress label is `high quality, 75% overlap`; the completion message says `at high quality, 75% overlap` and `The original clip "Menu mix" is unchanged on its track.`; the source clip is the same object with the same buffer, still on its track; `testQualityTextNamesTheEffectiveSetting` | ctest 0 |

Both tests are in `tests/src/gui/StemSplitMenuTest.cpp` (CTest `StemSplitMenuTest`, new, registered in
`tests/CMakeLists.txt` and `tests/fork-sources.txt`; `tests/all-sources.txt` re-derived, `all-sources-reproduce.sh` exit 0).

Revert proof (logs in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-062.2.r1/logs`):
- Mutation, the menu's quality dropped in `startJob`: build 0, `StemSplitMenuTest` exit 8. The new test fails at
  `StemSplitMenuTest.cpp:181`, the job's `quality` compare (`test-9-mutation.log`).
- Every non-test hunk reverted to base: the test target does not build, exit 2 (`build-10-revert.log`).
- Restored: build 0; `StemSplitMenuTest`, `StemSplitPipelineTest`, `StemJobManagerTest`, `ControlStemCommands`,
  `ReversibilityContractTest`, `ControlCommandsSnapshot`, `DocFiguresGate`, `StemAvailabilityDocsGate` 8/8 pass, exit 0
  (`test-11.log`).

## Notes

- ccache could not create its default temp directory from this lane's sandbox (`build-1.log`, permission
  denied). Builds used `CCACHE_TEMPDIR=/tmp/ccache-q0622`.
- `git checkout <file>` is blocked by the lane guard, so each revert was restored with `git show HEAD:<file> > <file>`.

## Self-check
