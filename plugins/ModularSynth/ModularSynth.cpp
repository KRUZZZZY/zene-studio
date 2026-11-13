/*
 * ModularSynth.cpp - Q050-059: the modular instrument
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "ModularSynth.h"

#include <QDomElement>

#include <algorithm>
#include <limits>
#include <memory>

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "ModularSynthView.h"
#include "NotePlayHandle.h"
#include "Song.h"

#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT modularsynth_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Modular Synth",
	QT_TRANSLATE_NOOP("PluginBrowser",
		"A modular instrument: oscillator, envelope, filter, LFO and output modules wired into a playable graph"),
	"Zene Studio",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader("logo"),
	nullptr,
	nullptr,
};

PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* model, void*)
{
	return new ModularSynth(static_cast<InstrumentTrack*>(model));
}

}

static_assert(sizeof(modular::Voice) <= NotePlayHandle::PluginDataStorageSize);
static_assert(alignof(modular::Voice) <= alignof(std::max_align_t));

namespace
{

//! A release this short still keeps a note's end free of a click when nothing in the graph shapes it.
constexpr float MinimumReleaseMs = 3.f;

} // namespace


ModularSynth::ModularSynth(InstrumentTrack* track)
	: Instrument(track, &modularsynth_plugin_descriptor)
{
	modular::Graph starter = modular::Graph::starter();
	auto params = makeParameters(starter);
	install(std::move(starter), std::move(params));

	// An automation tick sets a parameter on the render thread, where nothing may emit a signal: every parameter
	// holds its change signal back there (makeParameters) and this timer, on the instrument's own thread, emits it.
	connect(&m_deferredSignalTimer, &QTimer::timeout, this, &ModularSynth::flushDeferredSignals);
	m_deferredSignalTimer.start(DeferredSignalPollMs);
}

ModularSynth::~ModularSynth() = default;

QString ModularSynth::nodeName() const
{
	return QString::fromLatin1(modularsynth_plugin_descriptor.name);
}

std::vector<FloatModel*> ModularSynth::moduleParameters(int index) const
{
	std::vector<FloatModel*> models;
	if (index < 0 || static_cast<std::size_t>(index) >= m_params.size()) { return models; }
	for (const auto& model : m_params[static_cast<std::size_t>(index)].models)
	{
		if (model) { models.push_back(model.get()); }
	}
	return models;
}

std::vector<ModularSynth::ModuleParams> ModularSynth::makeParameters(const modular::Graph& graph)
{
	std::vector<ModuleParams> all(graph.modules.size());
	for (std::size_t i = 0; i < graph.modules.size(); ++i)
	{
		const modular::KindSpec& spec = modular::kindSpec(graph.modules[i].kind);
		const QString label = graph.moduleLabel(static_cast<int>(i));
		for (std::size_t k = 0; k < spec.params.size(); ++k)
		{
			const modular::ParamSpec& p = spec.params[k];
			auto model = std::make_unique<FloatModel>(p.init, p.lowest, p.highest, p.step, this,
				QStringLiteral("%1 %2").arg(label, QString::fromLatin1(p.label)));
			if (p.logarithmic) { model->setScaleLogarithmic(true); }
			model->setDeferChangeSignalOnRenderThread();
			all[i].models[k] = std::move(model);
		}
	}
	return all;
}

void ModularSynth::install(modular::Graph graph, std::vector<ModuleParams> params)
{
	std::array<std::array<const FloatModel*, modular::MaxParams>, modular::MaxModules> pointers{};
	for (std::size_t i = 0; i < params.size(); ++i)
	{
		for (std::size_t k = 0; k < static_cast<std::size_t>(modular::MaxParams); ++k)
		{
			pointers[i][k] = params[i].models[k].get();
		}
	}
	std::unique_ptr<modular::Program> program = modular::compile(graph, pointers);
	{
		// No render period runs while the guard is held, so no voice is inside the program being replaced.
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		m_graph = std::move(graph);
		std::swap(m_params, params);
		m_program.store(program.get(), std::memory_order_release);
		std::swap(m_programOwner, program);
	}
	emit graphChanged();
	// The replaced program and parameter models go here, on the control thread, after the window let go of them.
}

void ModularSynth::flushDeferredSignals()
{
	for (const ModuleParams& module : m_params)
	{
		for (const auto& model : module.models)
		{
			if (model) { model->flushDeferredChange(); }
		}
	}
}

void ModularSynth::playNoteImpl(NotePlayHandle* note, std::span<SampleFrame> out)
{
	if (note->m_pluginData == nullptr)
	{
		note->m_pluginData = std::construct_at(static_cast<modular::Voice*>(note->pluginDataStorage()));
	}
	auto* voice = static_cast<modular::Voice*>(note->m_pluginData);
	const modular::Program* program = m_program.load(std::memory_order_acquire);

	const f_cnt_t frames = note->framesLeftForCurrentPeriod();
	const f_cnt_t offset = note->noteOffset();
	// A released note held by the sustain pedal keeps sounding until the pedal lets its release start.
	const bool releasing = note->isReleased()
		&& (note->isReleaseStarted() || !instrumentTrack()->isSustainPedalPressed());
	const f_cnt_t releaseAt = releasing ? note->framesBeforeRelease() : std::numeric_limits<f_cnt_t>::max();

	modular::render(*program, *voice, note->frequency(), static_cast<float>(getSampleRate()),
		out.data(), offset, offset + frames, releaseAt);

	applyFadeIn(out.data(), note);
	// An envelope on the output's level is the note's release; otherwise the note fades over the release time.
	if (!program->outputEnveloped) { applyRelease(out.data(), note); }
}

void ModularSynth::deleteNotePluginData(NotePlayHandle* note)
{
	std::destroy_at(static_cast<modular::Voice*>(note->m_pluginData));
}

float ModularSynth::desiredReleaseTimeMs() const
{
	const modular::Program* program = m_program.load(std::memory_order_acquire);
	return std::max(MinimumReleaseMs, program != nullptr ? modular::longestReleaseMs(*program) : 0.f);
}

void ModularSynth::saveSettings(QDomDocument& doc, QDomElement& element)
{
	element.setAttribute(QStringLiteral("version"), 1);
	QDomElement graphElement = doc.createElement(QStringLiteral("graph"));
	element.appendChild(graphElement);
	m_graph.save(doc, graphElement);

	std::size_t index = 0;
	for (QDomElement module = graphElement.firstChildElement(QStringLiteral("module")); !module.isNull();
		module = module.nextSiblingElement(QStringLiteral("module")), ++index)
	{
		const modular::KindSpec& spec = modular::kindSpec(m_graph.modules[index].kind);
		for (std::size_t k = 0; k < spec.params.size(); ++k)
		{
			m_params[index].models[k]->saveSettings(doc, module, QString::fromLatin1(spec.params[k].key));
		}
	}
}

void ModularSynth::loadSettings(const QDomElement& element)
{
	// A state with no <graph> (written before the graph existed, or by hand) is the starter graph at its defaults;
	// a graph that cannot be played is never installed half-wired: the starter graph is, and the reason is reported.
	QDomElement graphElement = element.firstChildElement(QStringLiteral("graph"));
	std::optional<modular::Graph> graph;
	QString problem;
	if (!graphElement.isNull())
	{
		graph = modular::Graph::load(graphElement, &problem);
		if (graph && !graph->isValid(&problem)) { graph.reset(); }
	}
	if (!graph)
	{
		if (!graphElement.isNull())
		{
			m_loadProblem = tr("The modular graph could not be loaded (%1); the starter graph was used instead.")
				.arg(problem);
			Engine::getSong()->collectError(m_loadProblem);
		}
		else { m_loadProblem.clear(); }
		graph = modular::Graph::starter();
		graphElement = QDomElement();
	}
	else { m_loadProblem.clear(); }

	std::vector<ModuleParams> params = makeParameters(*graph);
	std::size_t index = 0;
	for (QDomElement module = graphElement.firstChildElement(QStringLiteral("module")); !module.isNull();
		module = module.nextSiblingElement(QStringLiteral("module")), ++index)
	{
		const modular::KindSpec& spec = modular::kindSpec(graph->modules[index].kind);
		for (std::size_t k = 0; k < spec.params.size(); ++k)
		{
			FloatModel* model = params[index].models[k].get();
			model->loadSettings(module, QString::fromLatin1(spec.params[k].key));
			// A value saved as a plain attribute loads as a linear scale; the parameter's own scale wins.
			model->setScaleLogarithmic(spec.params[k].logarithmic);
		}
	}
	install(std::move(*graph), std::move(params));
}

gui::PluginView* ModularSynth::instantiateView(QWidget* parent)
{
	return new gui::ModularSynthView(this, parent);
}

} // namespace lmms
