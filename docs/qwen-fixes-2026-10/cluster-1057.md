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
reads). Gate and completion-check runs are listed in the Self-check below.

## Self-check

See the section appended after the gates ran.

## Other findings

- The file header comments at `src/core/ControlReversibilityTableDawProject.cpp:13-17` and
  `src/core/ControlCommandsDawProject.cpp:24-27` say `not_mutating` is "the class
  project.save ... carr[ies]". That is stale: `project.save` is `RC::Snapshot`
  (`src/core/ControlReversibilityTableSnapshot.cpp:73`). This is comment-only and not fixed here.

Delegation: see the Self-check section.
