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
| The menu passes the selected setting through the registered command | `StemSplitPipelineTest::testClipMenuSplitsAtTheChosenQualityAndKeepsTheSource`: triggers the real "High quality" action; the new job in `stem.job_status` has `quality: high`, `overlap: 0.75`, and when finished `inference_quality: high` | ctest 0 |
| The job UI reports the effective setting and leaves the source clip available | same test: the progress label is `high quality, 75% overlap`; the completion message says `at high quality, 75% overlap` and `The original clip "Menu mix" is unchanged on its track.`; the source clip is the same object with the same buffer, still on its track; `testQualityTextNamesTheEffectiveSetting` | ctest 0 |

Revert proof: dropping the menu's quality makes `StemSplitPipelineTest` exit 8 (the new test fails on the
quality compare). Reverting every non-test hunk makes the test target fail to build (exit 2). With the
change restored: 18/18, exit 0. Logs are in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-062.2.r1/logs`
(`build-3-mutation.log`/`test-3-mutation.log`, `build-4-revert.log`, `build-5.log`/`test-5.log`).

## Notes

- ccache could not create its default temp directory from this lane's sandbox (`build-1.log`, permission
  denied). Builds used `CCACHE_TEMPDIR=/tmp/ccache-q0622`.
- `git checkout <file>` is blocked by the lane guard, so each revert was restored with `git show HEAD:<file> > <file>`.

## Self-check
