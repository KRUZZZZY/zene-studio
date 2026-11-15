# Security policy

Zene Studio is an LMMS-derived digital audio workstation. It loads and runs code that did not
come from this repository: native plugins, sandboxed Lua scripts, and sandboxed WebAssembly.
That is the security surface this policy covers.

## How to report a vulnerability

**Use GitHub's private vulnerability reporting.** It is enabled on this repository — open

<https://github.com/KRUZZZZY/zene-studio/security/advisories/new>

or go to the **Security** tab and click **Report a vulnerability**. The report stays private
between you and the maintainers until it is published as an advisory.

**There is no security mailbox, no separate security contact and no PGP key.** None is
configured today, and this page does not point at one. The private-report form above is the
only confidential channel that exists. Issues, pull requests and discussions are public the
moment they are sent, so do not describe an unreleased vulnerability there.

A report that can be acted on usually contains:

- the build string from `--version`, plus the release tag or commit you ran;
- your platform and its version (and, on Linux, the distribution);
- exact steps to reproduce, and whether a specific plugin, project file or script is needed
  to trigger it;
- what you observed versus what you expected;
- your assessment of impact, if you have one.

Please do not publish a working exploit before a fix has shipped or you have agreed a
disclosure date with the maintainers.

## Scope

**In scope** — the paths that load or interpret untrusted input:

- **Plugin hosting.** Loading, running and unloading native plugins in any supported format:
  VST2 through the built-in Vestige host, VST3, CLAP, LV2, LADSPA, SoundFont and GIG files.
- **The out-of-process plugin boundary.** Remote plugins (VST2/Vestige, ZynAddSubFx, the
  VstBase/VstEffect host) run in a child process and talk to the main process over a
  shared-memory protocol. Memory corruption, a crash that crosses that boundary, or an escape
  from it is in scope.
- **The sandboxes.** The Lua scripting sandbox (`--run-script`) and the WASM DSP sandbox
  (wasmtime). A sandbox escape — reaching the filesystem or network, or executing native code
  outside the sandbox — is a vulnerability.
- **File handling.** Project loaders (`.mmp`, `.mmpz`), preset and sample loaders, and the
  MIDI, Hydrogen and SoundFont import paths: path traversal or writes outside the expected
  directory, unsafe deserialisation, or memory corruption triggered by a crafted file.
- **Memory-safety defects in first-party code** reached from any of the above (heap or stack
  overflow, use-after-free, unbounded recursion) with a plausible trigger.

**Out of scope as a vulnerability report:**

- **The lack of code signing.** Every build of this alpha is unsigned. Windows SmartScreen and
  macOS Gatekeeper warn on first launch, which
  [docs/KNOWN-LIMITATIONS.md](docs/KNOWN-LIMITATIONS.md) documents as expected. "A security
  warning appeared" is not a vulnerability, and the answer is never to turn the protection off.
- **Defects already listed in [docs/KNOWN-LIMITATIONS.md](docs/KNOWN-LIMITATIONS.md).** A
  documented, known gap is not a security finding; those are tracked as product work.
- **Crashes with no security consequence.** A plugin that crashes the app, or a malformed
  project that is rejected with an error, is an ordinary bug — use the repository's issue
  templates.
- **Vulnerabilities entirely inside a third-party plugin or library binary** that this project
  merely loads: those belong to their vendor or upstream.

## What happens next

There is no bug-bounty programme, and this project makes **no response-time commitment**. Reports
are handled by the maintainers as time allows; the private advisory is the record of what was
reported and what was decided. Fixes ship in a normal release unless the report warrants an
out-of-band one.
