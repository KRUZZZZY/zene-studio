/*
 * ModularGraph.cpp - Q050-059: the modular instrument's module kinds, wiring rules and graph serialisation
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "ModularGraph.h"

#include <QDomDocument>
#include <QDomElement>
#include <QStringList>

#include <algorithm>
#include <array>

namespace lmms::modular
{

namespace
{

constexpr std::array OscillatorPorts{PortSpec{"pitch", Signal::Control}, PortSpec{"amp", Signal::Control}};
constexpr std::array FilterPorts{PortSpec{"in", Signal::Audio}, PortSpec{"cutoff", Signal::Control}};
constexpr std::array OutputPorts{PortSpec{"in", Signal::Audio}, PortSpec{"amp", Signal::Control}};

// Wave and mode parameters are whole numbers: the index of the shape or mode named in the label's hint.
constexpr std::array OscillatorParams{
	ParamSpec{"wave", "Wave", 0.f, 3.f, 1.f, 1.f, false, ""},		// sine, saw, square, triangle
	ParamSpec{"coarse", "Coarse", -24.f, 24.f, 0.f, 1.f, false, " st"},	// semitones
	ParamSpec{"fine", "Fine", -100.f, 100.f, 0.f, 1.f, false, " ct"},	// cents
	ParamSpec{"level", "Level", 0.f, 1.f, 0.8f, 0.01f, false, ""},
};
constexpr std::array EnvelopeParams{
	ParamSpec{"attack", "Attack", 0.f, 5000.f, 5.f, 1.f, false, " ms"},	// milliseconds
	ParamSpec{"decay", "Decay", 0.f, 5000.f, 300.f, 1.f, false, " ms"},
	ParamSpec{"sustain", "Sustain", 0.f, 1.f, 0.6f, 0.01f, false, ""},
	ParamSpec{"release", "Release", 0.f, 5000.f, 200.f, 1.f, false, " ms"},
};
constexpr std::array FilterParams{
	ParamSpec{"mode", "Mode", 0.f, 2.f, 0.f, 1.f, false, ""},			// low-pass, high-pass, band-pass
	ParamSpec{"cutoff", "Cutoff", 20.f, 20000.f, 1200.f, 1.f, true, " Hz"},	// Hz
	ParamSpec{"resonance", "Resonance", 0.f, 0.95f, 0.3f, 0.01f, false, ""},
	ParamSpec{"depth", "Mod depth", 0.f, 6.f, 1.f, 0.01f, false, " oct"},		// octaves per unit of the cutoff input
};
constexpr std::array LfoParams{
	ParamSpec{"wave", "Wave", 0.f, 3.f, 0.f, 1.f, false, ""},			// sine, triangle, saw, square
	ParamSpec{"rate", "Rate", 0.05f, 40.f, 3.f, 0.01f, true, " Hz"},		// Hz
	ParamSpec{"depth", "Depth", 0.f, 1.f, 0.5f, 0.01f, false, ""},
};
constexpr std::array OutputParams{
	ParamSpec{"gain", "Gain", 0.f, 2.f, 0.8f, 0.01f, false, ""},
};

const std::array<KindSpec, 5> Kinds{
	KindSpec{ModuleKind::Oscillator, "oscillator", "Osc", true, Signal::Audio, OscillatorPorts, OscillatorParams},
	KindSpec{ModuleKind::Envelope, "envelope", "Env", true, Signal::Control, {}, EnvelopeParams},
	KindSpec{ModuleKind::Filter, "filter", "Filter", true, Signal::Audio, FilterPorts, FilterParams},
	KindSpec{ModuleKind::Lfo, "lfo", "LFO", true, Signal::Control, {}, LfoParams},
	KindSpec{ModuleKind::Output, "output", "Output", false, Signal::Audio, OutputPorts, OutputParams},
};

} // namespace


const KindSpec& kindSpec(ModuleKind kind)
{
	return Kinds[static_cast<std::size_t>(kind)];
}

std::optional<ModuleKind> kindFromKey(const QString& key)
{
	for (const KindSpec& spec : Kinds)
	{
		if (key == QLatin1String(spec.key)) { return spec.kind; }
	}
	return std::nullopt;
}

int portIndex(ModuleKind kind, const QString& port)
{
	const auto ports = kindSpec(kind).ports;
	for (std::size_t i = 0; i < ports.size(); ++i)
	{
		if (port == QLatin1String(ports[i].key)) { return static_cast<int>(i); }
	}
	return -1;
}


Graph Graph::starter()
{
	Graph g;
	g.modules = {
		{QStringLiteral("osc1"), ModuleKind::Oscillator},
		{QStringLiteral("env1"), ModuleKind::Envelope},
		{QStringLiteral("filter1"), ModuleKind::Filter},
		{QStringLiteral("lfo1"), ModuleKind::Lfo},
		{QStringLiteral("out"), ModuleKind::Output},
	};
	g.wires = {
		{QStringLiteral("osc1"), QStringLiteral("filter1"), QStringLiteral("in")},
		{QStringLiteral("filter1"), QStringLiteral("out"), QStringLiteral("in")},
		{QStringLiteral("env1"), QStringLiteral("out"), QStringLiteral("amp")},
		{QStringLiteral("lfo1"), QStringLiteral("filter1"), QStringLiteral("cutoff")},
	};
	return g;
}

int Graph::indexOf(const QString& id) const
{
	for (std::size_t i = 0; i < modules.size(); ++i)
	{
		if (modules[i].id == id) { return static_cast<int>(i); }
	}
	return -1;
}

bool Graph::isValid(QString* why) const
{
	QString problem = modulesProblem();
	for (std::size_t w = 0; problem.isEmpty() && w < wires.size(); ++w) { problem = wireProblem(w); }
	if (problem.isEmpty() && processingOrder().size() != modules.size()) { problem = QStringLiteral("the wiring has a cycle"); }
	if (why != nullptr) { *why = problem; }
	return problem.isEmpty();
}

QString Graph::modulesProblem() const
{
	if (modules.empty()) { return QStringLiteral("the graph has no modules"); }
	if (modules.size() > static_cast<std::size_t>(MaxModules))
	{
		return QStringLiteral("the graph has %1 modules, more than %2").arg(modules.size()).arg(MaxModules);
	}
	if (wires.size() > static_cast<std::size_t>(MaxWires))
	{
		return QStringLiteral("the graph has %1 wires, more than %2").arg(wires.size()).arg(MaxWires);
	}
	for (std::size_t i = 0; i < modules.size(); ++i)
	{
		if (modules[i].id.isEmpty()) { return QStringLiteral("a module has no id"); }
		if (indexOf(modules[i].id) != static_cast<int>(i))
		{
			return QStringLiteral("the module id '%1' is used twice").arg(modules[i].id);
		}
	}
	const auto outputs = std::count_if(modules.begin(), modules.end(),
		[](const Module& m) { return m.kind == ModuleKind::Output; });
	if (outputs != 1) { return QStringLiteral("the graph has %1 output modules, not exactly one").arg(outputs); }
	return {};
}

QString Graph::wireProblem(std::size_t index) const
{
	const Wire& wire = wires[index];
	const int from = indexOf(wire.from);
	const int to = indexOf(wire.to);
	if (from < 0) { return QStringLiteral("a wire starts at the unknown module '%1'").arg(wire.from); }
	if (to < 0) { return QStringLiteral("a wire ends at the unknown module '%1'").arg(wire.to); }
	if (from == to) { return QStringLiteral("the module '%1' is wired into itself").arg(wire.from); }
	const KindSpec& source = kindSpec(modules[static_cast<std::size_t>(from)].kind);
	const KindSpec& target = kindSpec(modules[static_cast<std::size_t>(to)].kind);
	if (!source.hasOutput) { return QStringLiteral("the module '%1' has no output to wire").arg(wire.from); }
	const int port = portIndex(target.kind, wire.port);
	if (port < 0) { return QStringLiteral("the module '%1' has no input '%2'").arg(wire.to, wire.port); }
	if (target.ports[static_cast<std::size_t>(port)].signal != source.output)
	{
		const bool audio = source.output == Signal::Audio;
		return QStringLiteral("'%1' cannot feed '%2.%3': %4 signal into %5 input")
			.arg(wire.from, wire.to, wire.port, audio ? QStringLiteral("an audio") : QStringLiteral("a control"),
				audio ? QStringLiteral("a control") : QStringLiteral("an audio"));
	}
	if (std::find(wires.begin(), wires.begin() + static_cast<std::ptrdiff_t>(index), wire) != wires.begin() + static_cast<std::ptrdiff_t>(index))
	{
		return QStringLiteral("the wire %1 -> %2.%3 is there twice").arg(wire.from, wire.to, wire.port);
	}
	return {};
}

std::vector<int> Graph::processingOrder() const
{
	// Kahn's algorithm, taking the lowest ready index first so the order is stable for a given graph.
	std::vector<int> pending(modules.size(), 0);
	for (const Wire& wire : wires) { countInto(wire, pending, 1); }
	std::vector<int> order;
	std::vector<bool> done(modules.size(), false);
	for (int next = nextReady(pending, done); next >= 0; next = nextReady(pending, done)) // stops short on a cycle
	{
		done[static_cast<std::size_t>(next)] = true;
		order.push_back(next);
		for (const Wire& wire : wires)
		{
			if (indexOf(wire.from) == next) { countInto(wire, pending, -1); }
		}
	}
	return order;
}

void Graph::countInto(const Wire& wire, std::vector<int>& pending, int delta) const
{
	const int to = indexOf(wire.to);
	if (to >= 0 && indexOf(wire.from) >= 0) { pending[static_cast<std::size_t>(to)] += delta; }
}

int Graph::nextReady(const std::vector<int>& pending, const std::vector<bool>& done)
{
	for (std::size_t i = 0; i < pending.size(); ++i)
	{
		if (!done[i] && pending[i] == 0) { return static_cast<int>(i); }
	}
	return -1;
}

QString Graph::describeWiring() const
{
	QStringList lines;
	for (const Wire& wire : wires) { lines << QStringLiteral("%1 -> %2.%3").arg(wire.from, wire.to, wire.port); }
	return lines.join(QLatin1Char('\n'));
}

void Graph::save(QDomDocument& doc, QDomElement& parent) const
{
	for (const Module& module : modules)
	{
		QDomElement e = doc.createElement(QStringLiteral("module"));
		e.setAttribute(QStringLiteral("id"), module.id);
		e.setAttribute(QStringLiteral("type"), QString::fromLatin1(kindSpec(module.kind).key));
		parent.appendChild(e);
	}
	for (const Wire& wire : wires)
	{
		QDomElement e = doc.createElement(QStringLiteral("wire"));
		e.setAttribute(QStringLiteral("from"), wire.from);
		e.setAttribute(QStringLiteral("to"), wire.to);
		e.setAttribute(QStringLiteral("port"), wire.port);
		parent.appendChild(e);
	}
}

std::optional<Graph> Graph::load(const QDomElement& parent, QString* why)
{
	Graph g;
	for (QDomElement e = parent.firstChildElement(QStringLiteral("module")); !e.isNull();
		e = e.nextSiblingElement(QStringLiteral("module")))
	{
		const QString type = e.attribute(QStringLiteral("type"));
		const auto kind = kindFromKey(type);
		if (!kind)
		{
			if (why != nullptr) { *why = QStringLiteral("unknown module type '%1'").arg(type); }
			return std::nullopt;
		}
		g.modules.push_back({e.attribute(QStringLiteral("id")), *kind});
	}
	for (QDomElement e = parent.firstChildElement(QStringLiteral("wire")); !e.isNull();
		e = e.nextSiblingElement(QStringLiteral("wire")))
	{
		g.wires.push_back({e.attribute(QStringLiteral("from")), e.attribute(QStringLiteral("to")),
			e.attribute(QStringLiteral("port"))});
	}
	return g;
}

QString Graph::moduleLabel(int index) const
{
	const ModuleKind kind = modules[static_cast<std::size_t>(index)].kind;
	const KindSpec& spec = kindSpec(kind);
	if (kind == ModuleKind::Output) { return QString::fromLatin1(spec.label); }
	int number = 0;
	for (int i = 0; i <= index; ++i)
	{
		if (modules[static_cast<std::size_t>(i)].kind == kind) { ++number; }
	}
	return QStringLiteral("%1 %2").arg(QString::fromLatin1(spec.label)).arg(number);
}

} // namespace lmms::modular
