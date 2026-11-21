INCLUDE(BuildPlugin)

# Modular Synth (Q050-059): a playable graph of oscillator, envelope, filter, LFO and output modules.
BUILD_PLUGIN(modularsynth
	ModularGraph.cpp
	ModularProgram.cpp
	ModularSynth.cpp
	ModularSynthView.cpp
	MOCFILES
		ModularSynth.h
		ModularSynthView.h
	EMBEDDED_RESOURCES logo.png
)
