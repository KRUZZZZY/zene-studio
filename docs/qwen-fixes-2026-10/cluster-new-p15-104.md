FALSE-POSITIVE

# Cluster new-p15-104: LV2 reload does not leave auxiliary output pointers dangling

The finding says `addExtraPort()` fills `m_extraOutPorts` with raw pointers and that a reload
"rebuilds the ports" without clearing the vector, leaving freed `Lv2Ports::Audio` objects behind.
On this branch the precondition is false: `Lv2Proc::reload()` never rebuilds the ports. The port
objects live in `m_ports`, which is resized and filled only once, from the constructor. A reload
only destroys and re-instantiates the LV2 instance; it reuses the same port objects. `m_extraOutPorts`
therefore keeps pointing at live objects for the whole lifetime of the `Lv2Proc`, and no dangling
read is possible from this path.

No product code was changed. The suggested `m_extraOutPorts.clear()` in the reload path would actually
break behaviour: after a sample-rate change the vector would still be empty while the already-published
output-bus layout still asks for those buses, so every auxiliary bus would go silent (or read stale
buffers).

## Evidence (file:line on this branch)

1. **`createPorts()` is called only from the constructor.**
   `src/core/lv2/Lv2Proc.cpp:215-224` is the constructor, with `createPorts();` at line 222 and
   `initPlugin();` at line 223. A repository-wide search for the symbol finds only the definition and the
   constructor call:
   `logs/002-createPorts-callsites.log` (EXIT 0) lists `src/core/lv2/Lv2Proc.cpp:222` (call),
   `src/core/lv2/Lv2Proc.cpp:1051` (definition), `include/Lv2Proc.h:243` (declaration), and no other
   source-tree call site.

2. **`m_ports` is resized and filled only inside `createPorts()`.**
   `src/core/lv2/Lv2Proc.cpp:1125-1126` computes the port count and calls `m_ports.resize(maxPorts)`;
   `src/core/lv2/Lv2Proc.cpp:790` resets each slot while `createPorts()` fills them. No other source
   file touches `Lv2Proc::m_ports` (`rg -n "m_ports" src include plugins tests`, no build output).

3. **`reload()` only suspends and restarts the instance.**
   `src/core/lv2/Lv2Proc.cpp:234` is `void Lv2Proc::reload() { Lv2ProcSuspender(this); }`.
   `src/core/lv2/Lv2Proc.cpp:194-213` is the suspender: its constructor calls `shutdownPlugin()` when
   an instance is active, and its destructor calls `initPlugin()`. Neither calls `createPorts()`, and
   neither destroys or replaces any `Lv2Ports::Audio`.

4. **`shutdownPlugin()` does not touch the port objects.**
   `src/core/lv2/Lv2Proc.cpp:540-550` deactivates and frees the Lilv instance, clears features and
   options, and leaves `m_ports` alone.

5. **`initPlugin()` reconnects the existing port objects.**
   `src/core/lv2/Lv2Proc.cpp:490-538` instantiates the plug-in and loops over the existing
   `m_ports.size()` at line 520, calling `connectPort(portNum)` for each. `connectPort` (`:1175`)
   visits the same port object, so the addresses in `m_extraOutPorts` remain the buffer owners.

6. **The extra list is built only from those same port objects.**
   `src/core/lv2/Lv2Proc.cpp:393-395` (`addExtraPort`) pushes `&audio`; its only caller is
   `src/core/lv2/Lv2Proc.cpp:1093`, inside `createPorts()`'s `RegisterPort` visitor. The readers at
   `:401-404` (`extraOutputCount`) and `:409-412` (`copyExtraOutputToCore`) dereference entries that
   were never invalidated. Destructor order is safe too: `m_extraOutPorts` is declared after
   `m_ports` at `include/Lv2Proc.h:204` and `:210`, so it is destroyed first, and a vector of raw
   pointers frees nothing.

7. **The finding's contract does not exist in this code.**
   `include/Lv2Proc.h:241-243` describes `createPorts()` as the place that allocates `m_ports` and
   assigns meaning to ports. There is no reload-time port rebuild to clear the vector before.

## History: the port-rebuilding reload predates the vector

`git show 5c37aa2e2e` (2023-10-21, "Fixes #6786: Lv2Proc: Fix losing automation on export") is the
change that removed port rebuilding from reload. Its diff shows the old `reload()` doing a placement
new over the whole `Lv2Proc` (`this->~Lv2Proc(); new (this) Lv2Proc(...)`), and the same diff moves
`createPorts()` out of `initPlugin()` into the constructor and replaces `reload()` with
`Lv2ProcSuspender` (`logs/010-history.log`, EXIT 0). That old reload could indeed have invalidated any
cached port pointer.

The `m_extraOutPorts` cache was introduced later, by `009f591b27` (2026-10-04, the multi-output
catch-up), which adds `addExtraPort` inside `createPorts()` and the constructor-time `createPorts()`
call. `git merge-base --is-ancestor` confirms `5c37aa2e2e` is an ancestor of `009f591b27`, and
`009f591b27` is an ancestor of HEAD (`logs/010-history.log`, both EXIT 0). No revision of this branch
had both a reload that rebuilds ports and the `m_extraOutPorts` vector, so the reported mechanism never
existed in this code.

## What the suggested fix would do

`m_extraOutPorts` is what `plugins/Lv2Instrument/Lv2Instrument.cpp:246` counts after a reload.
`publishOutputBuses()` runs once from the constructor (`Lv2Instrument.cpp:115`) and keeps the bus
layout while `reload()` runs on a sample-rate change. Clearing the vector during `reload()` (or in
`shutdownPlugin()`) without rebuilding the ports would make `extraOutputCount()` return 0, so
`deliverOutputBuses()` (`Lv2Instrument.cpp:264-270`) would stop writing every auxiliary bus even
though the published layout still exposes them. That is a regression, not a fix. The claim was not
applied.

## Test matrix

No product change, so there is no RED/GREEN pair. The refuted path is the only one the finding names:

| Path | Evidence | Result |
| --- | --- | --- |
| Normal construction | `Lv2Proc.cpp:222`, `:1126`, `:1093` | Ports allocated once; extra pointers valid |
| Reload / sample-rate change | `Lv2Proc.cpp:234`, `:194-213`, `:540-550`, `:490-538` | Instance recreated only; port objects untouched |
| Buffer exhaustion / port count change | `:1126` is the only resize, so the count cannot change after construction | Not reachable |
| Audio thread | No product change; `copyExtraOutputToCore` unchanged | No new allocation, lock, wait or I/O |
| Platform builds | No header or source change | No platform-specific effect |

A test that passes both with and without the proposed change would prove nothing, so none was added.
Any real test would have to force a second port rebuild, which this code cannot perform.

## Commands run and EXIT codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-104.r1/logs/`.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Gate table | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p15-104 > logs/000-feature-check.log 2>&1` | 0 | `000-feature-check.log`: all eight rows PASS, `RESULT: PASS` |
| Line evidence | `rg -n "Lv2Proc::Lv2Proc\|createPorts\\(\\);\\|void Lv2Proc::reload\|Lv2ProcSuspender\|void Lv2Proc::initPlugin\|void Lv2Proc::shutdownPlugin\|m_ports.resize\|m_procs.push_back\|Lv2ControlBase::reload\|addExtraPort\|m_extraOutPorts" src/core/lv2/Lv2Proc.cpp src/core/lv2/Lv2ControlBase.cpp include/Lv2Proc.h include/Lv2ControlBase.h > logs/001-evidence-lines.log 2>&1` | 0 | `001-evidence-lines.log` |
| Call-site search | `rg -n "createPorts" src include plugins tests > logs/002-createPorts-callsites.log 2>&1` | 0 | `002-createPorts-callsites.log` |
| Reload/constructor code | `sed -n 190,236p src/core/lv2/Lv2Proc.cpp > logs/003-reload-code.log 2>&1` | 0 | `003-reload-code.log` |
| History | `git log -S "m_extraOutPorts" ...; git show 5c37aa2e2e ...; git merge-base --is-ancestor ...` | 0 | `010-history.log` |

No build or ctest was run because the verdict required no product or test change; the only change in
this session is this report.

## Other findings

- The same port-stability argument applies to the raw `m_latencyPort` and `m_midiIn`/`m_midiOut`
  pointers (`include/Lv2Proc.h:211`, `:213`): they are also assigned only in `createPorts()` and are not
  invalidated by `reload()`.
- `reload()` on the audio path is not the subject here, but it does not take `requestChangeInModel` around
  `shutdownPlugin()`/`initPlugin()`; that is a separate concurrency question, outside this finding.

## Self-check

1. **Regression test RED/GREEN.** Not applicable: verdict is FALSE-POSITIVE and no product change was
   made, so there is no product change to revert and no RED candidate. The finding's own suggested
   change would be a regression (see "What the suggested fix would do").
2. **Determinism.** Not applicable; no test was added. The verdict is a code-path fact, not a timing
   observation.
3. **Cause and completeness.** The finding names reload-time port rebuild as the cause. The evidence at
   `Lv2Proc.cpp:222`, `:234` and `:194-213` shows reload never rebuilds ports, and `:1126` is the only
   `m_ports` resize. `addExtraPort` has one caller (`:1093`) inside that one construction path.
4. **No collateral damage.** No code changed. Old projects and saved bus layouts load exactly as before.
   No limit or check was added.
5. **FALSE-POSITIVE evidence.** Code references are listed above at `Lv2Proc.cpp:222`, `:234`,
   `:194-213`, `:490-538`, `:540-550`, `:790`, `:1093`, `:1126`, and `include/Lv2Proc.h:204`, `:210`,
   `:243`; the only source-tree `createPorts` call site is the constructor.
6. **Registration and gates.** No product file or inherited file changed, so no list-file entry is
   needed. The gate run is `000-feature-check.log`, EXIT 0, `RESULT: PASS`.
7. **Sibling commands.** This session's commit `d61d44730` changes only this report; it touches no
   `ControlCommands*`, `ControlReversibilityTable` or `ControlRegistry` file, so no command sibling
   group is affected. The command ids the pre-submit check listed (`app.*`, `arrangement.*`) come from
   commits already on the branch before this job (the check diffs against the merge base), not from this
   change. No command was added, removed or altered here, so all of them keep their existing
   reversibility class and behaviour.
