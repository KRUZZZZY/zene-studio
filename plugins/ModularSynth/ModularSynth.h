/*
 * ModularSynth.h - Q050-059: the modular instrument - a playable graph of oscillator, envelope, filter, LFO and
 *                  output modules on an instrument track
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_MODULAR_SYNTH_H
#define LMMS_MODULAR_SYNTH_H

#include <QTimer>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "AutomatableModel.h"
#include "Instrument.h"
#include "ModularGraph.h"
#include "ModularProgram.h"

namespace lmms
{

/*! The graph a note plays is a compiled modular::Program. It is built on the control thread whenever the graph
 *  is replaced (a load, a state restore) and published by swapping one pointer while the audio engine's change
 *  guard holds the render period off - the same publication the oscillator instruments use - so a voice always
 *  reads one whole program, and the program it replaced is freed on the control thread afterwards. */
class ModularSynth : public Instrument
{
	Q_OBJECT
public:
	explicit ModularSynth(InstrumentTrack* track);
	~ModularSynth() override;

	void deleteNotePluginData(NotePlayHandle* note) override;
	float desiredReleaseTimeMs() const override;

	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;
	QString nodeName() const override;

	gui::PluginView* instantiateView(QWidget* parent) override;

	const modular::Graph& graph() const { return m_graph; }
	//! The parameter models of graph module @a index, in its kind's parameter order.
	std::vector<FloatModel*> moduleParameters(int index) const;

	//! The graph's wiring as "from -> to.port" lines (what the window shows; a test reads it).
	Q_INVOKABLE QString wiringText() const { return m_graph.describeWiring(); }
	//! Why the last load fell back to the starter graph, or empty.
	Q_INVOKABLE QString loadProblem() const { return m_loadProblem; }

signals:
	//! The modules or wiring were replaced; their parameter models may be new objects.
	void graphChanged();

private slots:
	void flushDeferredSignals();

private:
	void playNoteImpl(NotePlayHandle* note, std::span<SampleFrame> out) override;

	struct ModuleParams
	{
		std::array<std::unique_ptr<FloatModel>, modular::MaxParams> models;
	};

	//! New parameter models for every module of @a graph, at their defaults, children of this instrument.
	std::vector<ModuleParams> makeParameters(const modular::Graph& graph);
	//! Makes @a graph (valid) with @a params the one the voices play.
	void install(modular::Graph graph, std::vector<ModuleParams> params);

	modular::Graph m_graph;
	std::vector<ModuleParams> m_params;
	std::unique_ptr<modular::Program> m_programOwner;
	std::atomic<const modular::Program*> m_program{nullptr};
	QString m_loadProblem;

	QTimer m_deferredSignalTimer;
	static constexpr int DeferredSignalPollMs = 100;
};

} // namespace lmms

#endif // LMMS_MODULAR_SYNTH_H
