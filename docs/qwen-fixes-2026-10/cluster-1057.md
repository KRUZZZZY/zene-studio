FALSE-POSITIVE

1057: FALSE-POSITIVE

# Cluster 1057: "DAWproject export overwrites files without a transaction"

## Verdict

The finding asks `dawproject.export` to record a transaction (a file revision, or an
`irreversible` mutation) when it replaces an existing file with `overwrite: true`. The
project's reversibility contract (SPEC A16) rules that out on purpose. A command that
writes an OUTPUT ARTEFACT at a path the caller chooses, and changes no session state, is
classed `not_mutating`. Overwriting that artefact is the caller's explicit decision, not
something the command layer undoes. `dawproject.export` follows that contract exactly,
in the same way as its siblings. No product code was changed.

## Evidence (file:line, read this session)

- Contract: `docs/A16-REVERSIBILITY.md:39-41`. The three mutating classes apply to
  commands that change project state. A command that changes no project state "is not
  'reversible', it is *not a mutation*", so it is classed `not_mutating`.
- Contract, output artefacts: `docs/A16-REVERSIBILITY.md:132-136` lists "`render.render`
  (an output artefact)" in the `not_mutating` section. `docs/A16-REVERSIBILITY.md:163`
  gives the rule: "it writes an OUTPUT ARTEFACT; the session it renders is not modified
  ... the rendered file is an output, not project state, and overwriting it is the
  caller's decision".
- The row for this command: `src/core/ControlReversibilityTableDawProject.cpp:88-94`,
  `R("dawproject.export", RC::NotMutating, false, ...)`. Its stated mechanism: "the file it
  wrote is the caller's own artefact, and the reply carries its path, size, hash and model
  digest so a caller can identify or delete it".
- The handler: `src/core/ControlCommandsDawProject.cpp:331-398`. It reads the song
  (`dawProjectModelFromSong`), writes only the caller's absolute path, and changes no
  session object. It refuses a relative path (`:367-371`). It refuses an existing file
  unless the caller passes `overwrite: true` (`:372-377`), so a file is only replaced
  when the caller explicitly asks for it. The description says so (`:342-343`). The reply
  carries `path`, `bytes` and `sha256` (`:391-393`).
- The same family, the same class: `interchange.smf_export` uses the same overwrite gate
  (`src/core/ControlCommandsInterchange.cpp:248`, same message) and is
  `RC::NotMutating` (`src/core/ControlReversibilityTableInterchange.cpp:82`). So are
  `shortcuts.export` (`src/core/ControlReversibilityTableCustomisation.cpp:87`) and
  `theme.export` / `settings.export` (`src/core/ControlReversibilityTableW3Custom.cpp:48`,
  `:110`).
- Contrast: `project.save` is `RC::Snapshot` with a file revision
  (`src/core/ControlReversibilityTableSnapshot.cpp:73`,
  `src/core/ControlCommandsProjectFiles.cpp:109-151`). The reason is that it overwrites
  the PROJECT's own file, which is project state. An export target is not project state.

Why I did not apply the finding's fix: either option would put one member of the
export family in a different class from its siblings, against the written contract.
Changing a `not_mutating` row to `irreversible` would also make `control.undo` refuse
across a pure export. That changes undo behaviour outside the bug (definition of done,
item 4). Changing the contract for every output-artefact writer is a design decision,
not a bug fix.

## Commands and exit codes

There is no product change, so there is no RED/GREEN regression run and no
`fix.patch` reversal (step 4b does not apply to a not-real verdict). Code evidence was
gathered with the fff search tools and `sed -n` line reads (no exit codes recorded for
reads). The gate and completion-check runs are in the Gates table below.

## Gates

| Command | Result |
| --- | --- |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-1057 > .../1057.r1/logs/01-feature-check.log 2>&1; echo EXIT=$?` | EXIT=0. Every row PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length), `RESULT: PASS` |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-1057 1057 > .../1057.r1/logs/02-submit-check.log 2>&1; echo EXIT=$?` | EXIT=0, `RESULT: PASS - submit` |

## Self-check

- **The regression test fails without the fix and passes with it:** not applicable. The
  verdict is FALSE-POSITIVE, so no product change was made and there is no patch to
  reverse. The submit check's "tests added or changed" row lists the branch's
  pre-existing test files (`tests/Qt5QStringListSizeConstructorCheck.py`,
  `tests/clang-template-keyword-check.py`, `tests/control-audio-test-tone.py`). None
  of them was added by this session; this session committed only this report.
- **Deterministic test:** not applicable. I added no test, for the reason above.
- **The fix is at the cause and complete:** there is no fix. I checked the siblings
  against the verdict. `interchange.smf_export` has the identical overwrite gate and the
  same `NotMutating` class (`ControlCommandsInterchange.cpp:248`,
  `ControlReversibilityTableInterchange.cpp:82`). So do `render.render`
  (`docs/A16-REVERSIBILITY.md:163`), `shortcuts.export`, `theme.export` and
  `settings.export`. The family is consistent with the contract.
- **No collateral damage:** no product, schema or test file changed (the commit touches
  only `docs/qwen-fixes-2026-10/cluster-1057.md`), so loading old projects and saved
  data is unaffected.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** the contract is
  `docs/A16-REVERSIBILITY.md:39-41, 132-136, 163`. The row is
  `src/core/ControlReversibilityTableDawProject.cpp:88-94`. The handler's explicit
  overwrite opt-in is `src/core/ControlCommandsDawProject.cpp:372-377`. All of these are
  listed under Evidence above.
- **Sibling commands in the command files you changed** (app.*, arrangement.*): I changed
  no command file. The listed ids come from the branch's existing diff against its
  base, not from this session, and they are unaffected.

Delegation: Haiku ran `feature_check.py` and `submit_check.py`, logged both and reported
the tables. I checked the `RESULT` lines in both logs myself. I did not use Sonnet: the
verdict is not-real, so there was no test to write and no fix to implement. The
diagnosis is lead work. DeepSeek was unavailable (rate-limited).

## Other findings

- The file header comments at `src/core/ControlReversibilityTableDawProject.cpp:13-17` and
  `src/core/ControlCommandsDawProject.cpp:24-27` say `not_mutating` is "the class
  project.save ... carr[ies]". That is stale: `project.save` is `RC::Snapshot`
  (`src/core/ControlReversibilityTableSnapshot.cpp:73`). This is comment-only and not fixed here.
