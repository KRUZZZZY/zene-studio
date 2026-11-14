# Launch priorities: Rune, musician workflows and release evidence

Read-only assessment requested by the owner on 2026-11-14, using three independent
GPT-6 Luna reviews of UI migration, feature inventory and launch readiness.
This is a proposed ordering, not a change to live queues, model limits or release
gates. Existing Terra priority and the owner's Rune Phase 0 decisions stand.

## Recommendation

Make **a Rune-powered, dependable first music-making session** the next major
product checkpoint within milestone 0.4. Put Rune feasibility ahead of additional
long-tail feature breadth, while required Terra repairs, data safety and current
train/CI validation keep running first. Do not spend a train applying a Rune-look
Qt skin: the owner already rejected that approach.

1. **Restore a trustworthy baseline.** Finish the current train and hosted CI on
   its exact SHA; reconcile required Terra findings. Existing-baseline failures
   are not a green result. Prioritize crashes, project/take loss, undo/redo and
   audio correctness over feature count. Do not interrupt the current validation.
2. **Finish Rune Phase 0.** The approved brief requires a real-engine connection
   with rendered audio, measured interaction latency, Linux/Windows/macOS proof,
   native plug-in editor behavior and accessibility. Its written go/no-go report
   is the next concrete UI deliverable. Use available permitted feature/downtime
   capacity; this assessment does not grant new implementation slots.
3. **Implement one complete musician journey in Rune after the feasibility gate.**
   Add an instrument or audio, hear it, edit notes/clips, use tempo/loop, mix and
   apply an effect, undo/redo, save, reopen and render. Recording adds real device
   input, arming/monitoring and a preserved take. Keep the existing engine; do not
   combine the UI migration with an audio-engine rewrite. Keep existing workflows
   available until their replacement has equivalent behavior and tests.
4. **Polish the first session and establish parity.** Readable defaults, clear
   selection/focus, discoverable controls, useful errors, keyboard behavior,
   accessibility and plug-in windows must work consistently. Resolve uncertain
   and skipped usage checks, not just the easy clicks. This is interaction work,
   not only styling. A private feedback round is useful; it does not introduce
   the separate preview binary the owner rejected.
5. **Continue feature breadth from a reconciled inventory.** Rank recording,
   projects/recovery, editing, plug-in use and export dependencies before niche
   additions. Do not remove the approved beta scope silently. Freeze, stabilise
   and satisfy the current SPEC-050 requirements before broad advertising.

## What we have evidence for

The promoted baseline remains `050/m0-train-30` at
`40b4322860e9c2133f412a4b921b63b057dd710c`, development identity
`0.4.21.1-alpha`. The Rune prototype is a separate side project with demo-data
playback, six surfaces and 26 headless checks. It is not the current product UI
and its checks do not prove audio-engine integration.

The promoted source contains composing/note editing, arrangement and imported
audio, tempo/loop control, automation, project save/reopen, mixing/routing/effect
chains and plug-in hosting. It also contains recording/monitor/punch and take/comp
work, clip launching, racks/VCAs, recovery, DAWproject interchange, opt-in v2
containers, MIDI export and local agent control. Presence in source is not a
certificate that all of these have polished GUI behavior on all platforms.
Command-line/headless tools must not be advertised as turnkey GUI workflows.

The train30 musician session exercised notes, transport, loops, tempo, importing
and moving/copying/deleting audio clips, adding an effect and save/reopen. It also
observed redo disabled after undo. Its solo check was inconclusive because only
one channel was sounding; this is not proof of a mixer defect. Its audio backend
was Dummy, so it is not evidence of real device playback/recording.

## Evidence gaps that change priorities

- The new local train31 bar reported **974 passes and nine failures out of 983**.
  All failures were in the existing baseline; one was an AbletonLinkSessionTest
  segmentation fault. This is progress, not a clean release result. Gates were
  still running at inspection.
- Hosted build `43601236832`, on earlier repair commit `109b6b59`, had five failed
  jobs while MinGW/MSVC were still running. Checks, quality gates and Doxygen
  passed. This is not hosted validation of candidate `c20ef591` or a green matrix.
- The train30 control sweep recorded 246 pass, 18 fail, 60 unsure and 12 skipped
  out of 336. Some failures are harness/targeting mistakes. Neither the raw failure
  count nor zero newly queued bugs establishes product quality; uncertainty needs
  a valid retest, and genuine findings need independent reproduction.
- The feature queue now has **503 rows**, whereas SPEC-050 describes 476. Runtime
  acceptance/queue status also differs from reachable merged history. For example,
  Q050-001/007 are merged in train3 and Q050-008/020 in train6; a pending queue row
  does not make those features absent. Reconcile source, aliases and acceptance
  evidence before assigning completion percentages or reordering individual IDs.
- The dated product README/status/limitations contain old version and capability
  statements. They need reconciliation against the chosen frozen commit before
  anyone turns them into advertising copy.

## Advertising judgment

There is substantial implementation, but the evidence supports an engineering
alpha rather than a polished musician-facing beta. The next one or two train
numbers are not a launch schedule. Rune is a significant remaining integration
program; Phase 0 measurements should determine its implementation scope and time.

Before broad advertising: demonstrate reliable installation/first launch on each
claimed platform, the complete musician journey with real audio, safe
save/reopen/recovery, dependable undo/redo, the claimed plug-in/editor paths,
and repeatable clean validation on one frozen commit. Preserve SPEC-050's actual
audio soak, project round trips, blocker policy and hosted-CI requirements. A
limited private musician feedback round may happen earlier with clear alpha
expectations; it is not public-launch sign-off.

Sources: `plans/SPEC-050.md`, `plans/VERSIONING-AND-MILESTONES.md`,
`side-projects/zene-rune-prototype/{README,PHASE0-BRIEF,SPEC-UI-REWRITE}.md`,
`~/.local/share/zene-bunny-fix/qwen/{TRAIN-VERSIONS,TRAIN-CI-STATUS}.json`,
`qwen/merge/train-31-bar.log`, and the train30 Terra `buttons` / `session` run
summaries, results, verdicts and logs under the runtime's `terra-pilot/` directory.
