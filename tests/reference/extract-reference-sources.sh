#!/usr/bin/env bash
# Part C behaviour-preservation harness: extract the pre-migration plugin
# sources used as the reference renderer. Every file is written verbatim from
# the git object named in tests/reference/ORIGIN.md, so `git hash-object` on
# the extracted file equals the recorded blob id.
#
# Usage: tests/reference/extract-reference-sources.sh [<commit>]
set -euo pipefail

BASE_COMMIT="${1:-4ac5c3e38}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"

extract() { # extract <plugin-dir> <file>
	local src="plugins/$1/$2"
	local dst="$HERE/$1/$2"
	mkdir -p "$(dirname "$dst")"
	git -C "$REPO" show "$BASE_COMMIT:$src" > "$dst"
	printf '%s\t%s\t%s\n' "$BASE_COMMIT" "$src" "$(git -C "$REPO" hash-object "$dst")"
}

: > "$HERE/ORIGIN.tsv"
{
	for p in Amplifier BassBooster Bitcrush DualFilter WaveShaper; do
		for f in "$p.cpp" "$p.h" "${p}ControlDialog.cpp" "${p}ControlDialog.h" "${p}Controls.cpp" "${p}Controls.h"; do
			extract "$p" "$f"
		done
	done
	for f in FlangerEffect.cpp FlangerEffect.h FlangerControls.cpp FlangerControls.h \
	         FlangerControlsDialog.cpp FlangerControlsDialog.h MonoDelay.cpp MonoDelay.h; do
		extract Flanger "$f"
	done
	for f in DelayEffect.cpp DelayEffect.h DelayControls.cpp DelayControls.h \
	         DelayControlsDialog.cpp DelayControlsDialog.h Lfo.cpp Lfo.h StereoDelay.cpp StereoDelay.h; do
		extract Delay "$f"
	done
	extract Eq EqFader.h
	# Slice 2 (task #589): next batch of migrated plugins.
	for p in Compressor CrossoverEQ DynamicsProcessor LOMM MultitapEcho PeakControllerEffect ReverbSC StereoEnhancer StereoMatrix; do
		for f in "$p.cpp" "$p.h" "${p}ControlDialog.cpp" "${p}ControlDialog.h" "${p}Controls.cpp" "${p}Controls.h"; do
			extract "$p" "$f"
		done
	done
	# ReverbSC also builds the Sean Costello reverb C sources.
	for f in base.c base.h dcblock.c dcblock.h revsc.c revsc.h; do
		extract ReverbSC "$f"
	done
	# Slice 3 (task #589): analysers, Dispersion, GranularPitchShifter, Eq.
	for f in Dispersion.cpp Dispersion.h DispersionControls.cpp DispersionControls.h \
	         DispersionControlDialog.cpp DispersionControlDialog.h; do
		extract Dispersion "$f"
	done
	for f in Vectorscope.cpp Vectorscope.h VecControls.cpp VecControls.h \
	         VecControlsDialog.cpp VecControlsDialog.h VectorView.cpp VectorView.h; do
		extract Vectorscope "$f"
	done
	for f in Analyzer.cpp Analyzer.h SaControls.cpp SaControls.h \
	         SaControlsDialog.cpp SaControlsDialog.h SaProcessor.cpp SaProcessor.h \
	         SaSpectrumView.cpp SaSpectrumView.h SaWaterfallView.cpp SaWaterfallView.h \
	         DataprocLauncher.h; do
		extract SpectrumAnalyzer "$f"
	done
	for f in GranularPitchShifterEffect.cpp GranularPitchShifterEffect.h \
	         GranularPitchShifterControls.cpp GranularPitchShifterControls.h \
	         GranularPitchShifterControlDialog.cpp GranularPitchShifterControlDialog.h; do
		extract GranularPitchShifter "$f"
	done
	for f in EqEffect.cpp EqEffect.h EqControls.cpp EqControls.h \
	         EqControlsDialog.cpp EqControlsDialog.h EqCurve.cpp EqCurve.h \
	         EqFilter.h EqParameterWidget.cpp EqParameterWidget.h \
	         EqSpectrumView.cpp EqSpectrumView.h; do
		extract Eq "$f"
	done
	# Slice 4 (task #589): software-synth instruments.
	for f in FreeBoy.cpp FreeBoy.h GbApuWrapper.cpp GbApuWrapper.h; do
		extract FreeBoy "$f"
	done
	for f in Nes.cpp Nes.h; do
		extract Nes "$f"
	done
	for f in SidInstrument.cpp SidInstrument.h; do
		extract Sid "$f"
	done
	for f in OpulenZ.cpp OpulenZ.h; do
		extract OpulenZ "$f"
	done
	for f in Sfxr.cpp Sfxr.h; do
		extract Sfxr "$f"
	done
	for f in BitInvader.cpp BitInvader.h; do
		extract BitInvader "$f"
	done
	for f in Watsyn.cpp Watsyn.h; do
		extract Watsyn "$f"
	done
	for f in Xpressive.cpp Xpressive.h ExprSynth.cpp ExprSynth.h; do
		extract Xpressive "$f"
	done
	for f in Vibed.cpp Vibed.h NineButtonSelector.cpp NineButtonSelector.h \
	         VibratingString.cpp VibratingString.h; do
		extract Vibed "$f"
	done
	for f in Kicker.cpp Kicker.h KickerOsc.h; do
		extract Kicker "$f"
	done
	# Slice 5 (task #589): multi-oscillator and sample-playback instruments.
	for f in TripleOscillator.cpp TripleOscillator.h; do
		extract TripleOscillator "$f"
	done
	for f in Monstro.cpp Monstro.h; do
		extract Monstro "$f"
	done
	for f in Organic.cpp Organic.h; do
		extract Organic "$f"
	done
	for f in AudioFileProcessor.cpp AudioFileProcessor.h AudioFileProcessorView.cpp \
	         AudioFileProcessorView.h AudioFileProcessorWaveView.cpp AudioFileProcessorWaveView.h; do
		extract AudioFileProcessor "$f"
	done
	# Slice 6 (task #589): LADSPA host and the Lb302 bass synth.
	for f in Lb302.cpp Lb302.h; do
		extract Lb302 "$f"
	done
	for f in LadspaEffect.cpp LadspaEffect.h LadspaControls.cpp LadspaControls.h \
	         LadspaControlDialog.cpp LadspaControlDialog.h LadspaMatrixControlDialog.cpp \
	         LadspaMatrixControlDialog.h LadspaSubPluginFeatures.cpp LadspaSubPluginFeatures.h \
	         LadspaWidgetFactory.cpp LadspaWidgetFactory.h; do
		extract LadspaEffect "$f"
	done
} >> "$HERE/ORIGIN.tsv"

echo "extracted $(wc -l < "$HERE/ORIGIN.tsv") files from $BASE_COMMIT"
