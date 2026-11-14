# Zene Studio — Program Workspace (agent rules)

<!-- State sections rewritten 2026-09-25 after the merge/cleanup campaign. The RULES and the
     KNOWN API TRAPS are unchanged and still binding. -->

## What this is
Workspace for the **Zene Studio** program (AI-KOS program `lmms-complete-daw-program`; the older
`lmms-fl-replacement-program` epic was absorbed into it on 2026-09-11 and is kept as superseded
history).

**Read in this order:** `START-HERE.md` (orientation) → `REPO-MAP.md` (the live layout — two
worktrees) → `BUGS_FOUND.md` (the campaign record) → `plans/QA-SWEEP-PROTOCOL.md` (the active
hunt process, v6) → `plans/DELEGATION-RULES.md` (how delegated work is run) →
`research/ableton-gap/SPEC-zene-studio.md` (the product spec) → `plans/HANDOVER-*` / `history/`
(dated history). `CLAUDE.md` is a pointer to this file for Claude Code; the rules are identical.

## Verified facts — do not re-litigate without new evidence
- **The product's one live line is `0.4.0/train-w1` in the `zene-040` worktree.** On 2026-09-28 the
  owner merged `040/feat-597`, `040/feat-capture-limits` and `040/survey-fixes` into it (the merge
  train's repairs are in the merge commits). The clone `zene-studio/` is unshallowed and is the git
  authority; it has `docs/fork-readme` checked out with the owner's uncommitted work — leave it
  alone. Nothing is pushed; `product/main` moves only on the owner's word (rule 11).
- **Suite:** **316/316** on the merged tip `5d627f0ca` (0.4.0-alpha, 2026-10-01; BUGS 11.27-11.30) (2026-09-30: M3 menus, clip recording, the RTSan job, BUGS 11.17-11.19) after the 2026-09-29 lanes (M1, TSan, dither, JACK, Rubber Band, stem-default, then M3 registry actions, M2 edit groups/comp playback, consolidate, scene Follow Actions, input monitoring and the clip-launch grid) (`ctest` from `zene-survey/build/tests` on the same tree - `zene-040/build` was deleted for disk; a full rebuild with `-DWANT_STEM_SPLIT=ON`) —
  `ControlGoldenAudio` included (`BUGS_FOUND.md` §8). `ControlCommandsSnapshot` canary:
  **<!-- canary -->822 ids / 76 groups<!-- /canary -->**. Gates: every executed gate passes (gate 13 since
  the vendored `thirdparty/mts-esp` exclusion); gate 2 needs `--with-coverage`. Gate 7 counts
  CODE lines since 2026-09-28.
- **`MAX_MIXER_CHANNELS` does not exist.** Mixer channels are an unbounded `std::vector`;
  `DEFAULT_CHANNELS = ch_cnt_t{2}` (`lmms_constants.h:38`). Any "fixed 64 channels" claim is WRONG.
- **VST3 SDK is MIT since 3.8** (Oct 2025) → GPLv2-clean. LMMS is GPL-2.0-or-later. Pin SDK ≥ 3.8.
  VST2 stays Vestige-only. AI deps (RNNoise, RTNeural, ONNX Runtime, HTDemucs, NAM core) are
  MIT/BSD — GPLv2-clean.
- **Real-time stem separation is impossible** (HTDemucs needs 7.8 s lookahead) — offline job only.
- **ALSA capture EXISTS — the 2026-09-19 "no capture path" audit is SUPERSEDED.** `AudioAlsa.cpp` has a
  capture thread (`snd_pcm_readi`, `publishCaptured`), `AudioInputPath` (1..128 channels + the stereo bus's
  `left`/`right` pins), and `record.*`/`track.set_arm` routes. Bounds: **128 capture channels**,
  **64 record routes** (raised from 32/16 on 2026-09-28). Since `040/jack-capture` (2026-09-29) **JACK** also feeds
  the `AudioInputPath` machinery (the N-channel path, the capture state `record.input_get_state` reports,
  and its frame counter — proven against a live `jackd -d dummy` by `ControlJackCapture`). PortAudio and
  SDL still do not. Note the earlier wording "the recorder is blind on JACK" was overstated: record routes
  were fed through the stereo engine input all along; what JACK lacked was the capture path and state.
- **The 0.3.0-era crash-campaign fixes were LOST with the `zene-030` worktree** (never committed;
  not in any branch or tag). `v0.3.0-alpha` therefore ships the deep-nesting crash (D-1 class).
  The fix exists only on `0.4.0/train-w1` (`68ac6ee9f`, rewritten with a regression test).
  `verification/DEFECTS-2026-09-19.md`'s "fixed and verified" wording is true only of the deleted
  worktree. See `BUGS_FOUND.md` "Lost-work events".
- **Known product contracts (by design — never "fix" these):** the append-only `<z:provenance>`
  save journal (saves differ by it; strip list in `docs/SAVE-CANONICAL-STABILITY.md`); undo
  coalescing with the `undone_commands` span reply (`docs/UNDO-BOUNDS.md`);
  `mastering.run`/`render` async ACK pattern (`mastering.run` acks and completes async); fractional
  values on boolean parameters are refused `invalid_args`; the FIRST save to a new path reports
  `reversible:false` with an empty inverse; empty-active tempo maps persist
  (`docs/TEMPO-MAP.md`); fx-id renumbering after unload/undo; a malformed-wire line gets an
  `id:-1` reply.

## Where the work is tracked
- **Defect register + campaign record:** `BUGS_FOUND.md` (~52 verified defects found and fixed
  across audits, sweeps, the full suite, and open find-and-fix passes; fix commits land on
  `0.4.0/train-w1`).
- **The hunt process:** `plans/QA-SWEEP-PROTOCOL.md` (v6 deep open passes — finder fixes in its
  own disposable copy with a failing-then-passing test per fix; verifier reproduces every claim on
  a clean pinned build and grades a frozen severity list; canary catch-rate **p** is always quoted
  beside capture-recapture estimates; the 299-run certificate is optional and off by default).
- **Evidence of record:** `verification/` (verifier reports, the lane patches + SHA256SUMS);
  per-fix evidence under `zene-040/docs/*-logs/` and `tests/integration-logs-*`.
- **Instruments:** fixes = DeepSeek V4.1 Flash (`opencode-go`); hunt passes + certification =
  Codex CLI on the ChatGPT seat; verification = a second model; orchestration = the parent agent.

## Known API traps (all verified against the clone or a real build failure)
- **`SampleBuffer` has NO `operator[]`.** Use `data()` (→ `const SampleFrame*`) with `size()`, or
  `begin()/end()`. `buf[i]` does not compile.
- **`AudioEngine::sampleRate` does not exist** → `baseSampleRate()`.
- **`Effect::processImpl(SampleFrame*, const f_cnt_t)`** is the protected virtual;
  `processAudioBuffer(AudioBuffer&)` is the public entry. There is no
  `processBuffer(SampleFrame*, f_cnt_t)`.
- **`PlanarBufferView::buffer()` returns a view, not a raw pointer** — `auto* p = view.buffer(i)` fails.
- **Qt6 removed `QString::midRef`** → use `mid`.
- **Upstream removed `MixHelpers::sanitize`/`useNaNHandler` and the `addSanitized*` prefix** (#7323),
  and `AudioEngine::currentQualitySettings` (#7858 → use the resampler).
- **Types collapsed upstream**: `proc_ch_t` and `track_ch_t` are gone → `ch_cnt_t` (uint16).
- **`lua_pushfstring` has no `%l`** → use `%I` for a lua_Integer.
- **`MixerChannel`'s buffer is sized once in the constructor** with `framesPerPeriod()`; nothing
  resizes it at runtime (pre-existing upstream behaviour, not a regression).
- **`Q_ASSERT` compiles to nothing** under RelWithDebInfo/QT_NO_DEBUG — a side effect inside it
  silently vanishes; use `QVERIFY` in tests.

## Rules for agents working here
Generated Zene/KADS coding-agent prompts must include the exact sentence: **"you are the owner and long-term maintainer of this codebase, and its design is up to you"**. Runtime prompt dispatchers add it idempotently; new prompt paths must preserve it at the final delivery boundary.

1. **Never push upstream.** Nothing goes to `LMMS/lmms` or `messmerd/lmms` — no PRs, no comments, no
   branches there. Pushing to the product repo (`KRUZZZZY/zene-studio`) is *not* what this rule
   forbids, but an agent does it only on the owner's explicit word (rule 11) — the project
   `.claude/settings.json` allows exactly two pushes and denies the dangerous ones (owner, 2026-10-27).
   **Standing word for hosted CI (owner, 2026-10-08; tags 2026-10-27):** an agent may push the most
   up-to-date line's tip to a `ci/*` branch on `product` (`git -C <tree> push product HEAD:refs/heads/ci/<line>`)
   and a test tag named `v*-test*` (`git -C <tree> push product refs/tags/<tag>`, the tag made locally first),
   each after a secret scan of the pushed range — never `product/main`, never any other tag, never
   `origin`, never a force push or a deletion.
   Owner-gated items live in `plans/BACKLOG.md`.
2. **Work only in your own worktree.** The live inventory is tiny (`zene-040` + disposable `/tmp`
   copies for hunt passes); one build dir per copy and one verifier per build dir. **Build at
   `-j12`** (owner decision 2026-09-29; it was `-j2`, which made a full relink of the ~250 test
   executables take about an hour on this 20-core box) — and up to two builds at once
   (e.g. `-j12` + `-j8`) now that the disk has room (~80 GB free after the 2026-09-29 clean-up);
   keep the total near the 20 cores, and stop below 12 GB free.
2a. **Test targeted while coding, fully at merge points** (owner decision 2026-09-29). Each change
   still carries its failing-then-passing test and runs its own affected tests (`ctest -R`) on a
   rebuilt binary; the full `ctest` bar and `run-all-gates.sh` run once per lane, before it merges,
   and once on the merged tip. Never hard-stop a gate run (onboarding trap 4b).
3. **Verify every file/class/line claim against the clone** before citing it.
4. **Realtime rule:** no allocation, no locking, no unbounded growth on audio-thread paths.
   Tests that prove it (allocation counters) are the gold standard — copy that pattern.
5. **Behaviour-preserving by default:** a migration that changes output is a FAIL even if it compiles.
6. **Measure exit codes unpiped:** `cmd > log 2>&1; echo EXIT=$?`. `cmd | tail; echo $?` reports
   *tail's* exit code and will launder a failed build into a green one.
7. **ctest must run from `<build>/tests`.** The top-level build dir has no `CTestTestfile.cmake` and
   reports 0 tests — treat 0 tests as an ERROR, never a pass.
8. **GPLv2-clean:** never vendor Steinberg VST2 headers (Vestige is the only VST2 path); AI deps
   MIT/BSD only; retain upstream notices.
9. **'Compiles' is not 'works'.** A precise blocker with the exact command output is an acceptable
   deliverable; a fabricated success is not.
10. **If you hit an iteration cap,** the worktree is the deliverable: the parent commits a labelled
    WIP and hands the exact failure to a fresh lane. Leave the tree in a describable state.
11. **`product/main` is protected (2026-09-11).** Non-fast-forward pushes and branch deletion are blocked
    for *everyone*, with no bypass, so `git fetch product && git rebase product/main` comes **before**
    `git push product HEAD:main`; a rejected push means main moved under you, not that the rules changed.
    `pull_request` is required for non-admins; `v*` tags cannot be moved or deleted without the owner
    bypass. Write it down here or in `zene-studio-standards`, not in a shell history.
12. **Never weaken a test, gate, ratchet or assertion** to get to green; a declared behavioural
    change to inherited code must carry a regression test in the same commit. Register new files in
    `tests/fork-sources.txt` (product-new) or `tests/upstream-modifications.txt` (inherited, with a
    reason) in that commit. Stage explicit paths (`git add <paths>`), never `git add -A`.

13. **Terra priority-one rule, after train 26 (owner, 2026-11-05).** Activate only once
    `050/m0-train-26` has been promoted. From then on, every outstanding Terra button-sweep
    or human-session finding is priority one, including older outstanding Terra findings.
    Pause new ordinary feature/bug implementations, integrations and reviews while the Terra
    campaign owns the queue; preserve their state and let already-running sessions finish.
    Assign the enabled Claude lanes and DeepSeek slots to Terra repairs, retaining model
    selections, resource/usage limits, review requirements and shared issue budgets.
    Before starting or promoting the following train, both Terra sweeps of the current
    promoted build must finish and every finding must have an independently accepted fix
    (or independent confirmation that it is already fixed or not a bug). Include all required
    accepted Terra fixes ahead of ordinary candidates; the ordinary batch cutoff must not
    leave a required fix behind. Missing, reverted, excluded, blocked, exhausted or merely
    claimed fixes do not satisfy this rule. Force/cohort-wait flags cannot bypass it.
    Ordinary work resumes once the Terra findings are resolved on the promoted base and
    its sweeps are complete. Runtime policy: `qwen/TERRA-PRIORITY.json` and
    `qwen/terra_priority.py` under `~/.local/share/zene-bunny-fix/`.

14. **Required-fix integration deadlock recovery (owner, 2026-11-12).** An independently
    accepted Terra repair that remains excluded after exhausting its integration attempts
    must enter a bounded recovery path rather than silently stall the program. Diagnose the
    conflict, pin the latest independently accepted source and an independently reviewed
    cumulative target, then integrate or minimally remake the fix while preserving both
    sides' behavioral regression coverage. The runtime may grant **one additional integration
    attempt per original issue**, tied to that exact source/target pair. Keep all attempt
    history and the overall shared issue budget; aliases, moving refs and retries cannot renew
    the grant. Use the configured DeepSeek recovery route and existing resource/usage limits.
    A fresh independent review is required before acceptance; exclusions stay until a newly
    accepted candidate supersedes them, and the merged suite, gates and Terra promotion rule
    still apply. If the bounded attempt fails, preserve its tree and exact blocker for an
    explicit next decision; never reset budgets or bypass a release gate. Runtime policy:
    `WORKFLOW.json` → `priority_recovery`; implementation: `priority_recovery.py` and
    `issue_ledger.py` in `~/.local/share/zene-bunny-fix/qwen/`.

## Environment
Primary working directory: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program`.
Platform linux; model landscape for delegation: DeepSeek V4.1 Flash via `opencode-go` (fixes),
Codex CLI on a ChatGPT Plus seat (hunting/certification), `commandcode` GPT-5.6 Luna (checks —
credits exhausted as of 2026-09-25). Helper scripts are mode 644 (`bash <script>`); recoverable
deletion via `/home/kruzzzzy/.minimax/bin/mavis-trash` (same filesystem — trash does not free
space until emptied).
