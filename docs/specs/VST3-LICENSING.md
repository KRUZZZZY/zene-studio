<!--
  In-repo copy of the specification this repository's code and tests cite (DOC-5, 2026-09-15).
  This is a PINNED SNAPSHOT, not a live document:
    source   : the program workspace, plugin-hosting/VST3-LICENSING.md
    sha256   : 631a72f01a684e8bdbf3f01f00e4f60647346576c9a3c5e3011dd44c82a7b1b6
    bytes    : 5001
    why this file: the VST3 SDK licensing finding; cited by cmake/modules/Vst3Sdk.cmake as the reason the >= 3.8 requirement is GPLv2-clean
  The workspace copy remains the working copy and is where the document is edited; refreshing
  this snapshot is a deliberate act, recorded in the commit that does it. If a citation in this
  repository and this file ever disagree, reconcile them in one commit - the citations name the
  file by bare name (e.g. `SPEC-zene-studio.md`), and this directory is where that name resolves.
  See docs/specs/README.md for the convention and for the cited documents NOT yet committed.
-->
# VST3 SDK licensing decision for GPLv2 LMMS (task #560)

> **Verdict: the licensing blocker is GONE.** As of **VST 3.8 (October 2025)** the Steinberg VST3
> SDK is licensed under **MIT**, replacing the old dual GPLv3/proprietary model. MIT is
> GPL-compatible, so **native VST3 hosting in LMMS needs no GPL exception, no clean-room
> workaround, and no out-of-process requirement for licensing reasons.**
> Written 2026-09-08 · local analysis (no push) · maintainer sign-off still outstanding → BACKLOG B2.

---

## 1. Primary evidence

| Evidence | Detail |
|---|---|
| SDK licence text (fetched 2026-09-08) | `steinbergmedia/vst3sdk@master/LICENSE.txt` and `steinbergmedia/vst3_public_sdk@master/LICENSE.txt` are **"MIT License — Copyright (c) 2026, Steinberg Media Technologies GmbH"** (1,094 bytes each, identical) |
| Official VST3 developer portal | *"Since version 3.8, VST 3 is licensed under MIT license. Developers can adopt the MIT license for full open-source integration."* — `steinbergmedia.github.io/vst3_dev_portal/pages/VST+3+Licensing/VST3+License.html` |
| Independent coverage | Sonicstate 2025-10-30 ("VST 3 Now Available Under MIT License"); CDM 2025-11-04 ("previously available under a dual license (proprietary and GPLv3). Now that Steinberg has re-released it under the MIT License, it's much simpler and more permissive"); Sound on Sound 2025-10-31; KVR 2025-10-29 |

**Compatibility argument:** MIT is a permissive, GPL-compatible licence (notice retention only).
LMMS is **GPL-2.0-or-later** (`LICENSE.txt:297-298`: *"either version 2 of the License, or (at your
option) any later version"*). MIT code can therefore be linked into LMMS with no licence conflict,
and the "or later" clause would even permit GPLv3-only dependencies if one were ever needed.

## 2. What this changes vs the program's earlier analysis

| Claim (REPORT.md, pre-3.8 research) | Status now |
|---|---|
| "Requires Steinberg VST3 SDK (GPL exception — compatible with LMMS GPLv2)" (REPORT §3 P2) | **Superseded** — no exception is needed; the licence is MIT |
| "VST3 SDK has a GPL exception but the license is specific. Must confirm compatibility" (REPORT P2 risk) | **Resolved** — confirmed MIT since 3.8 |
| Risk register **R4**: "GPLv2 vs VST3 SDK licensing incompatible … unresolved" | **CLOSED** — no incompatibility exists for SDK ≥ 3.8 |
| Task #560 gate: "before any VST3 hosting code is merged" | **Satisfied in substance**; only the on-record maintainer reply remains (B2) |

## 3. The three candidate paths — re-evaluated

| Path | Legal status now | When to use |
|---|---|---|
| **Native in-process VST3 hosting** using the SDK interfaces | **Clean** (MIT) | The default. This is what the mission wants. |
| **Clean-room subset** (Vestige pattern) | Still required for **VST2** only — VST2 was NOT opened | Keep Vestige as the VST2 path; do not extend it to VST3 |
| **Out-of-process bridge** (RemotePlugin shared memory) | No longer needed for licensing | Optional, for **crash isolation** and 32-bit/64-bit bridging — a robustness choice, not a legal one |

## 4. Conditions for proceeding

1. **Pin the SDK at ≥ 3.8.** Pre-3.8 snapshots remain dual GPLv3/proprietary; vendoring an old copy
   would reintroduce the problem. Record the exact SDK version in the dependency file.
2. **Retain the MIT notice** in any vendored SDK headers/subset.
3. **VST2 stays Vestige-only** — no Steinberg VST2 headers, ever (AGENTS.md rule unchanged).
4. **ASIO is GPLv3** (Windows-only, irrelevant to LMMS today). LMMS being "GPL-2.0-or-later" makes
   it *compatible* if ever wanted, but it is out of scope and must not be bundled.
5. VST3 hosting still rides on the multi-channel work (PR #7459 / Part B): the licensing gate is
   clear, the **engineering** dependency is not.

## 5. Recommendation

**Proceed with native VST3 hosting built on SDK ≥ 3.8 under MIT.** No GPL exception, no clean-room
for VST3, no out-of-process requirement. Keep the RemotePlugin path for crash isolation if desired,
and keep Vestige for VST2.

## 6. What remains (owner-gated)

- **B2 (backlog):** post this analysis upstream (e.g. on LMMS issue #4715) and capture a
  maintainer's on-record reply, which is the literal DoD wording of task #560. The technical
  question is answered; only the acknowledgement is outstanding.
- Downstream: the VST3 implementation task should be created **after** Part B lands (it needs the
  multi-channel transport), and must cite this document.

## 7. Sources

- `https://raw.githubusercontent.com/steinbergmedia/vst3sdk/master/LICENSE.txt` (fetched 2026-09-08)
- `https://raw.githubusercontent.com/steinbergmedia/vst3_public_sdk/master/LICENSE.txt` (fetched 2026-09-08)
- `https://steinbergmedia.github.io/vst3_dev_portal/pages/VST+3+Licensing/VST3+License.html`
- Sonicstate 2025-10-30; CDM 2025-11-04; Sound on Sound 2025-10-31; KVR 2025-10-29
- LMMS `LICENSE.txt:297-298` (GPL-2.0-or-later), `plugins/Vestige/` (VST2 clean-room precedent)
