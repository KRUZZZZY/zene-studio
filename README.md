<div align="center">
	<h1>
	<img src="https://raw.githubusercontent.com/LMMS/artwork/master/Icon%20%26%20Mimetypes/lmms-64x64.svg" alt="LMMS Logo"><br>Zene Studio
	</h1>
	<p>Development fork — an LMMS-based digital audio workstation</p>
	<p>
		<b>Upstream:</b> <a href="https://github.com/LMMS/lmms">LMMS/lmms</a>
		⦁︎
		<b>Product repo:</b> <a href="https://github.com/KRUZZZZY/lmms-complete">KRUZZZZY/lmms-complete</a>
	</p>
</div>

> [!NOTE]
> **This is a development fork, not the official LMMS.** It is a working fork of [LMMS/lmms](https://github.com/LMMS/lmms) used to develop and verify new features, and to submit them upstream. The product built from this work — **Zene Studio**, the complete DAW — lives in [KRUZZZZY/lmms-complete](https://github.com/KRUZZZZY/lmms-complete). Pull requests opened from this fork target LMMS/lmms and keep upstream branding for review; Zene Studio branding applies to the product repo only.
>
> **What this fork carries:**
> - **12 feature branches** (`feat/*`): two-track recording, engine integration, plugin migration (90/93 plugins), sidechain/dynamic routing, VST3 hosting, CLAP hosting, Lua scripting, WASM sandbox, Patcher MVP, RNNoise denoiser, neural amp modelling, stem separation, slide notes, HiDPI scaling, git-friendly `.mmpz` — plus the merged integration branches (`integration/all-verified`).
> - **`standards/quality-gates`** — an 8-gate QA suite (static checks, build+test gates, coverage **85.24% on fork sources**, mutation harness) that also runs on GitHub Actions, and the workflow fixes it produced.
> - **Upstream PRs**: #8548 (HiDPI display scaling) and the rebase lane for #7459.
>
> Everything below is upstream LMMS documentation, retained unchanged.

<div align="center">
	<p>Cross-platform music production software</p>
	<p>
		<a href="https://lmms.io/">Website</a>
		⦁︎
		<a href="https://github.com/LMMS/lmms/releases">Releases</a>
		⦁︎
		<a href="https://lmms.io/documentation">User manual</a>
		⦁︎
		<a href="https://lmms.io/showcase/">Showcase</a>
		⦁︎
		<a href="https://lmms.io/lsp/">Sharing platform</a>
		⦁︎
		<a href="https://github.com/LMMS/lmms/wiki">Developer wiki</a>
		⦁︎
		<a href="https://lmms.github.io/lmms">Internal documentation</a>
	</p>
	<p>
		<a href="https://github.com/LMMS/lmms/actions/workflows/build.yml"><img src="https://github.com/LMMS/lmms/actions/workflows/build.yml/badge.svg" alt="Build status"></a>
		<a href="https://lmms.io/download"><img src="https://img.shields.io/github/release/LMMS/lmms.svg?maxAge=3600" 	alt="Latest stable release"></a>
		<a href="https://github.com/LMMS/lmms/releases"><img src="https://img.shields.io/github/downloads/LMMS/lmms/total.svg?maxAge=3600" alt="Overall downloads on Github"></a>
		<a href="https://discord.gg/3sc5su7"><img src="https://www.shields.io/badge/chat-on%20discord-7289DA.svg" alt="Join the chat at Discord"></a>
		<a href="https://www.transifex.com/lmms/lmms/"><img src="https://img.shields.io/badge/localise-on%20transifex-green.svg"></a>
	</p>
</div>


What is LMMS?
--------------

LMMS is an open-source cross-platform digital audio workstation designed for music production. It includes an advanced Piano Roll, Beat Sequencer, Song Editor, and Mixer for composing, arranging, and mixing music. It comes with 15+ synthesizer plugins by default, along with VST2 and SoundFont2 support.

Features
---------

* Song-Editor for arranging melodies, samples, patterns, and automation
* Pattern-Editor for creating beats and patterns
* An easy-to-use Piano-Roll for editing patterns and melodies
* A Mixer with unlimited mixer channels and arbitrary number of effects
* Many powerful instrument and effect-plugins out of the box
* Full user-defined track-based automation and computer-controlled automation sources
* Compatible with many standards such as SoundFont2, VST2 (instruments and effects), LADSPA, LV2, GUS Patches, and full MIDI support
* MIDI file importing and exporting

Building
---------

See [Compiling LMMS](https://github.com/LMMS/lmms/wiki/Compiling)

Join LMMS-development
----------------------

If you are interested in LMMS, its programming, artwork, testing, writing demo songs, (and improving this README...) or something like that, you're welcome to participate in the development of LMMS!

Information about what you can do and how can be found in the [wiki](https://github.com/LMMS/lmms/wiki).

Before coding a new big feature, please _always_ [file an issue](https://github.com/LMMS/lmms/issues/new) for your idea and suggestions about your feature and about the intended implementation on GitHub, or ask in one of the tech channels on Discord and wait for replies! Maybe there are different ideas, improvements, or hints, or maybe your feature is not welcome/needed at the moment.
