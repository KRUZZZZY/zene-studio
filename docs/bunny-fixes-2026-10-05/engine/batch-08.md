# Engine batch-08 — mixer findings

Worktree `zene-bunny-engine`, branch `bugs/bunny-engine-2026-10-05`, base commit `480fd367e`.
Six findings, three fixed (one commit each), three false positives.

### Cluster 4277 — A saved deferred sidechain can reload as a gating cycle
Verdict: FIXED
Evidence: `src/core/Mixer.cpp:1528` — `createSidechainSend()` sets `deferred = checkInfiniteLoop(from, to)`, i.e. deferred status is INFERRED from the routing graph at creation time. `saveSettings` wrote only `channel`, `mode` and `amount` on `<sidechain-send>`, and `loadSettings` created the route while reading the sender's element (`:2639`), before the receiver's regular send (loaded later, `:2607`) existed, so the inference ran against a half-built graph and returned false. The route then came back gating and the regular send closed a scheduling cycle in which neither channel is queued. The flag is now written (`deferred="1"`, only for deferred routes, so projects without one save byte-identically) and honored on load through the new `MixerSidechainRoute::setDeferred()` (`include/Mixer.h:494`), applied on the control thread before playback exactly as `setPreFader()` is.
Commit: 60ae1c0b1
Test: `MixerRoutingBackwardCompatTest::aDeferredSidechainRouteSurvivesSaveLoadAsDeferred`
Red/green: `cd build/tests && ctest -R '^MixerRoutingBackwardCompatTest$' --output-on-failure > build/bunny-t.log 2>&1` — red `EXIT=8` (`'after->deferred()' returned FALSE. (a saved deferred sidechain reloaded as a gating route (4277))`), green `EXIT=0`.

### Cluster 1523 — Disabling group solo links or suspending a group can leave solo mutes active
Verdict: FIXED
Evidence: `Mixer::applyGroupSolo` returned early for `!group->linksSolo() || group->isSuspended()` BEFORE the `!soloed` branch that calls `deactivateSolo()`. Only the group's solo model is connected to that handler (`src/core/VcaGroup.cpp:64-72`) — `setLinks()`/`setSuspended()` are plain setters (`include/VcaGroup.h:160-161`) — so nothing re-applied it, and clearing the flag afterwards skipped the restore. The `!soloed` path now runs before the link check, guarded by a new `Mixer::m_soloingGroup` (`include/Mixer.h:768`) so only the group that actually muted may restore; clearing a flag of a group that never soloed must not unmute a channel solo in progress. The channel-solo paths take ownership (`src/core/Mixer.cpp:931`, `src/core/MixerStrip.cpp:402`).
Commit: 4ca34db15
Test: `VcaGroupTest::clearingSoloRestoresMutesAfterTheSoloLinkIsTurnedOff`
Red/green: `cd build/tests && ctest -R '^VcaGroupTest$' --output-on-failure > build/bunny-t2.log 2>&1` — red `EXIT=8` (`'mixer->mixerChannel(ch)->m_muteModel.value() == false' returned FALSE. (channel 1 stayed muted after the solo was cleared (1523))`), green `EXIT=0`.

### Cluster 3295 — Malformed VCA members can group the master or allocate many channels
Verdict: FIXED
Evidence: the loader read `memberDom.attribute("channel").toInt()` with no ok flag and passed the result to `allocateChannelsTo()` before `addMember()`. The "groups the master" half of the claim is unreachable: a missing/non-numeric value becomes 0 and `VcaGroup::addMember` already refuses `channel == 0` (`src/core/VcaGroup.cpp:111`); but `mix_ch_t` is `std::uint16_t` (`include/LmmsTypes.h:47`), so a negative value wraps to 65535, and a large positive one is handed to `allocateChannelsTo()`, which loops `createChannel()` — a malformed file could allocate an entire mixer on the control thread. All `<mixerchannel>` elements load before the group pass, so a legitimate member always names an EXISTING non-master channel: the attribute is now parsed with an ok flag and skipped unless it is a positive index below `m_mixerChannels.size()` (`src/core/Mixer.cpp:2718-2726`); `allocateChannelsTo()` is no longer used here.
Commit: c7cff42ed
Test: `MixerRoutingBackwardCompatTest::aMalformedVcaMemberChannelIsSkipped`
Red/green: `cd build/tests && ctest -R '^MixerRoutingBackwardCompatTest$' --output-on-failure > build/bunny-t.log 2>&1` — red `EXIT=8` (`'mixer->numChannels() < 100' returned FALSE. (a member channel="500" allocated 501 channels (3295))`), green `EXIT=0`. The fixture uses `channel="500"` rather than `100000` so the red run stays fast while still proving the allocation.

### Cluster 2353 — Send-pan automation is ignored
Verdict: FALSE-POSITIVE
Evidence: `src/core/MixerSendPan.cpp:47` — `sendIsPanned()` keys off `route.pan()->valueBuffer() != nullptr`, not off the scalar `value()`, so an automated send pan takes the panned path. `addPannedSend()` then reads `panBuf->values()[f]` per frame at `src/core/MixerSendPan.cpp:83`, i.e. the per-sample buffer, not one fixed pan. The sample-exact fix the finding asks for is already present.
Commit: none
Test: none
Red/green: none

### Cluster 4271 — Pre-fader sends are mixed twice
Verdict: FALSE-POSITIVE
Evidence: `src/core/Mixer.cpp:635` — the pre-fader send is added in an `if` block that closes at `:649`, and the ordinary-send mixer that follows starts with `else if( ! volBuf && ! sendBuf )` at `:651`. The branches are mutually exclusive, so a pre-fader send is never added a second time and the sender's fader never touches it.
Commit: none
Test: none
Red/green: none

### Cluster 4273 — Concurrent senders race while waking a receiver
Verdict: FALSE-POSITIVE
Evidence: `include/Mixer.h:268` declares `m_dependenciesMet` as `std::atomic_size_t`, so the increment at `src/core/Mixer.cpp:355` is atomic and no update is lost. Each gating sender increments exactly once, so only the final increment reaches `i >= gatingReceives() + gatingSidechainReceives()` and exactly one worker takes the queue branch — the one-time decision `m_queued` is therefore made by a single winner, not raced. `m_queued` itself is a plain `bool` (`include/Mixer.h:155`) but is written only by that winner plus the setup/reset paths at `src/core/Mixer.cpp:2135` and `:2214`, which run before/after the workers.
Commit: none
Test: none
Red/green: none

Leftovers: none — every finding in this batch is either fixed with a regression test or argued FALSE-POSITIVE against the current source; the five Mixer/VCA regression runs and both gates (`bash tests/complexity-gate.sh --check` EXIT=0, `bash tests/file-length-gate.sh --check` EXIT=0) pass on the final tree.