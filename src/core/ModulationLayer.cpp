/*
 * ModulationLayer.cpp - the #602 modulation layer's engine half: the authored
 *                       layer, its persistence, the LFO arithmetic and the
 *                       per-block application (SPEC-zene-studio A11-A16;
 *                       docs/MODULATION.md).
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include "ModulationLayer.h"

#include "AutomationRamp.h"

#include <algorithm>
#include <cmath>

#include <QDomDocument>

#include "AutomatableModel.h"
#include "ControlRegistry.h"

namespace lmms
{

namespace
{

constexpr double kTwoPi = 6.283185307179586;
//! The project-file element and its version, so a future shape can be told apart.
constexpr int kModulationLayerVersion = 1;

//! A modulator name is bounded so a project file cannot make one arbitrary.
QString trimmedName(const QString& name, int maximum)
{
	QString trimmed = name.trimmed();
	if (trimmed.size() > maximum) { trimmed.truncate(maximum); }
	return trimmed;
}

void saveSource(QDomDocument& doc, QDomElement& modulatorElement, const ModulatorSource& source)
{
	modulatorElement.setAttribute(QStringLiteral("shape"), modulationShapeName(source.shape));
	modulatorElement.setAttribute(QStringLiteral("rate"), static_cast<double>(source.rateHz));
	modulatorElement.setAttribute(QStringLiteral("phase"), static_cast<double>(source.phase));
	modulatorElement.setAttribute(QStringLiteral("unipolar"), source.unipolar ? 1 : 0);
	modulatorElement.setAttribute(QStringLiteral("active"), source.active ? 1 : 0);
	// MX-117: the step values and the envelope's attack, only for the shapes that read them.
	if (source.shape == ModulationShape::Steps)
	{
		QStringList values;
		for (int i = 0; i < source.stepCount; ++i) { values << QString::number(source.steps[static_cast<std::size_t>(i)], 'g', 6); }
		modulatorElement.setAttribute(QStringLiteral("steps"), values.join(QLatin1Char(',')));
	}
	if (source.shape == ModulationShape::Envelope) { modulatorElement.setAttribute(QStringLiteral("attack"), static_cast<double>(source.attack)); }
}

void saveRoute(QDomDocument& doc, QDomElement& modulatorElement, const ModulationRoute& route)
{
	QDomElement routeElement = doc.createElement(QStringLiteral("route"));
	routeElement.setAttribute(QStringLiteral("channel"), route.channel);
	routeElement.setAttribute(QStringLiteral("chain"), route.chain);
	routeElement.setAttribute(QStringLiteral("effect"), route.effect);
	routeElement.setAttribute(QStringLiteral("parameter"), route.parameter);
	routeElement.setAttribute(QStringLiteral("depth"), static_cast<double>(route.depth));
	// R1.3: written only when on, so every existing route keeps its bytes.
	if (route.perSample) { routeElement.setAttribute(QStringLiteral("persample"), 1); }
	modulatorElement.appendChild(routeElement);
}

ModulatorSource readSource(const QDomElement& element)
{
	ModulatorSource source;
	modulationShapeFromName(element.attribute(QStringLiteral("shape"), QStringLiteral("sine")),
		&source.shape);
	source.rateHz = static_cast<float>(element.attribute(QStringLiteral("rate")).toDouble());
	source.phase = static_cast<float>(element.attribute(QStringLiteral("phase")).toDouble());
	source.unipolar = element.attribute(QStringLiteral("unipolar"), QStringLiteral("0")).toInt() != 0;
	source.active = element.attribute(QStringLiteral("active"), QStringLiteral("1")).toInt() != 0;
	const QStringList values = element.attribute(QStringLiteral("steps")).split(QLatin1Char(','), Qt::SkipEmptyParts);
	source.stepCount = static_cast<int>(std::min<qsizetype>(values.size(), ModulatorSource::MaxSteps));
	for (int i = 0; i < source.stepCount; ++i) { source.steps[static_cast<std::size_t>(i)] = std::clamp(values[i].toFloat(), -1.f, 1.f); }
	source.attack = std::clamp(element.attribute(QStringLiteral("attack"), QStringLiteral("0.1")).toFloat(), 0.f, 1.f);
	return source;
}

ModulationRoute readRoute(const QDomElement& element)
{
	ModulationRoute route;
	route.channel = element.attribute(QStringLiteral("channel")).toInt();
	route.chain = element.attribute(QStringLiteral("chain")).toInt();
	route.effect = element.attribute(QStringLiteral("effect")).toInt();
	route.parameter = element.attribute(QStringLiteral("parameter"));
	route.depth = static_cast<float>(element.attribute(QStringLiteral("depth")).toDouble());
	route.perSample = element.attribute(QStringLiteral("persample"), QStringLiteral("0")).toInt() != 0;
	return route;
}

/*! One <modulator> child. False when its own source does not validate, in which
 *  case nothing is appended: a rate of 0 or a phase of 3 is a corrupt block, and
 *  silently repairing it would run something nobody authored.
 *
 *  A route is appended only when it names a parameter; a route element with no
 *  'parameter' describes nothing, so it is dropped rather than stored as a route
 *  that can never resolve.
 */
bool loadModulator(const QDomElement& node, ModulationLayer* layer)
{
	const ModulatorSource source = readSource(node);
	QString why;
	if (!layer->validSource(source, &why)) { return false; }
	const int index = layer->addModulator(node.attribute(QStringLiteral("name")), source);
	if (index < 0) { return false; }

	const QDomNodeList routes = node.elementsByTagName(QStringLiteral("route"));
	for (int r = 0; r < routes.count(); ++r)
	{
		const QDomElement routeElement = routes.at(r).toElement();
		if (routeElement.isNull()) { continue; }
		const ModulationRoute route = readRoute(routeElement);
		if (route.parameter.isEmpty()) { continue; }
		layer->addRoute(index, route);
	}
	return true;
}

//! One resolved entry, or false when the address no longer names a parameter.
bool resolveRoute(const ModulationRoute& route, ModulationRuntime::Entry* entry)
{
	entry->model = modulationTargetModel(route, nullptr);
	if (entry->model.isNull()) { return false; }
	entry->depth = std::clamp(route.depth, -1.0f, 1.0f);
	entry->base = entry->model->value<float>();
	entry->minimum = entry->model->minValue<float>();
	entry->maximum = entry->model->maxValue<float>();
	return true;
}

void copySource(const Modulator& modulator, int index, ModulationRuntime* runtime)
{
	runtime->sources[static_cast<std::size_t>(index)] = modulator.source;
	if (index >= runtime->sourceCount) { runtime->sourceCount = index + 1; }
}

//! One route of one modulator, appended to \a runtime when it resolves.
void appendEntry(const Modulator& modulator, int modulatorIndex, const ModulationRoute& route,
	ModulationRuntime* runtime)
{
	if (runtime->entryCount >= ModulationRuntime::MaxEntries) { return; }
	ModulationRuntime::Entry entry;
	entry.modulator = modulatorIndex;
	entry.perSample = route.perSample;
	if (!resolveRoute(route, &entry)) { return; }
	runtime->entries[static_cast<std::size_t>(runtime->entryCount)] = entry;
	++runtime->entryCount;
}

//! The shared body of both rebuilds: \a runtime is filled from \a layer.
void fillRuntime(const ModulationLayer& layer, ModulationRuntime* runtime)
{
	*runtime = ModulationRuntime{};
	for (int i = 0; i < layer.modulatorCount(); ++i)
	{
		const Modulator* modulator = layer.modulator(i);
		if (modulator == nullptr) { continue; }
		copySource(*modulator, i, runtime);
		for (const ModulationRoute& route : modulator->routes)
		{
			appendEntry(*modulator, i, route, runtime);
		}
	}
}

} // namespace

// ---------------------------------------------------------------------------
// the shape vocabulary
// ---------------------------------------------------------------------------

const QStringList& modulationShapeNames()
{
	static const QStringList names{QStringLiteral("sine"), QStringLiteral("triangle"), QStringLiteral("square"),
		QStringLiteral("saw"), QStringLiteral("random"), QStringLiteral("smooth_random"), QStringLiteral("steps"),
		QStringLiteral("envelope")};
	return names;
}

QString modulationShapeName(ModulationShape shape)
{
	return modulationShapeNames().value(static_cast<int>(shape));
}

bool modulationShapeFromName(const QString& name, ModulationShape* shape)
{
	const int index = modulationShapeNames().indexOf(name);
	if (index < 0) { return false; }
	*shape = static_cast<ModulationShape>(index);
	return true;
}

namespace
{

//! A repeatable value in -1..1 for cycle @a n (a hash, so the audio thread keeps no state).
double randomAt(std::int64_t n)
{
	auto x = static_cast<std::uint64_t>(n) * 0x9E3779B97F4A7C15ULL;
	x ^= x >> 31;
	x *= 0xBF58476D1CE4E5B9ULL;
	x ^= x >> 29;
	return static_cast<double>(x >> 11) / static_cast<double>(1ULL << 53) * 2.0 - 1.0;
}

//! MX-117's shapes at @a cycles (the phase-shifted cycle count, cycles >= 0).
double extraShape(const ModulatorSource& source, double cycles)
{
	const auto n = static_cast<std::int64_t>(std::floor(cycles));
	const double phase = cycles - std::floor(cycles);
	switch (source.shape)
	{
		case ModulationShape::Random: return randomAt(n);
		case ModulationShape::SmoothRandom:
		{
			const double t = 0.5 - 0.5 * std::cos(phase * kTwoPi * 0.5);
			return randomAt(n) + (randomAt(n + 1) - randomAt(n)) * t;
		}
		case ModulationShape::Steps:
			return source.stepCount > 0 ? source.steps[static_cast<std::size_t>(n % source.stepCount)] : 0.0;
		case ModulationShape::Envelope:
		{
			const double attack = std::clamp(static_cast<double>(source.attack), 0.0, 1.0);
			const double level = phase < attack ? phase / std::max(attack, 1e-9) : std::exp(-5.0 * (phase - attack) / std::max(1.0 - attack, 1e-9));
			return 2.0 * level - 1.0;
		}
		default: break;
	}
	return 0.0;
}

} // namespace

// ---------------------------------------------------------------------------
// the LFO
// ---------------------------------------------------------------------------

float ModulationLayer::outputAt(const ModulatorSource& source, double seconds)
{
	const double rate = std::clamp(static_cast<double>(source.rateHz),
		static_cast<double>(MinRateHz), static_cast<double>(MaxRateHz));
	double phase = std::fmod(static_cast<double>(source.phase) + seconds * rate, 1.0);
	if (phase < 0.0) { phase += 1.0; }

	double value = 0.0;
	switch (source.shape)
	{
		// Sine and triangle start at 0 and rise; saw starts at -1 and rises;
		// square starts at +1 and drops at the half cycle. Stated because a
		// route whose depth is positive must move the SAME way on every shape
		// for the first half cycle, and that is a property of the offsets here.
		case ModulationShape::Sine:
			value = std::sin(phase * kTwoPi);
			break;
		case ModulationShape::Triangle:
		{
			// Shifted so the triangle STARTS at 0 and RISES - the same first
			// half cycle the sine has - which is what makes a positive depth
			// move the same way on every continuous shape.
			const double shifted = std::fmod(phase + 0.75, 1.0);
			value = 4.0 * std::fabs(shifted - 0.5) - 1.0;
			break;
		}
		case ModulationShape::Square:
			value = phase < 0.5 ? 1.0 : -1.0;
			break;
		case ModulationShape::Saw:
			value = 2.0 * phase - 1.0;
			break;
		default:
			value = extraShape(source, static_cast<double>(source.phase) + std::max(0.0, seconds) * rate);
			break;
	}
	if (source.unipolar) { value = (value + 1.0) * 0.5; }
	return static_cast<float>(value);
}

bool ModulationLayer::validSource(const ModulatorSource& source, QString* why) const
{
	if (!(source.rateHz >= MinRateHz) || !(source.rateHz <= MaxRateHz))
	{
		if (why != nullptr)
		{
			*why = QStringLiteral("'rate' %1 is outside %2..%3 Hz")
				.arg(static_cast<double>(source.rateHz))
				.arg(static_cast<double>(MinRateHz))
				.arg(static_cast<double>(MaxRateHz));
		}
		return false;
	}
	if (!(source.phase >= 0.0f) || !(source.phase < 1.0f))
	{
		if (why != nullptr)
		{
			*why = QStringLiteral("'phase' %1 is outside 0..1 (0.5 is half way round)")
				.arg(static_cast<double>(source.phase));
		}
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// the authored layer
// ---------------------------------------------------------------------------

const Modulator* ModulationLayer::modulator(int index) const
{
	if (index < 0 || index >= modulatorCount()) { return nullptr; }
	return &m_modulators[static_cast<std::size_t>(index)];
}

Modulator* ModulationLayer::modulator(int index)
{
	if (index < 0 || index >= modulatorCount()) { return nullptr; }
	return &m_modulators[static_cast<std::size_t>(index)];
}

int ModulationLayer::addModulator(const QString& name, const ModulatorSource& source)
{
	if (modulatorCount() >= MaxModulators) { return -1; }
	Modulator modulator;
	modulator.name = trimmedName(name, 64);
	modulator.source = source;
	m_modulators.push_back(modulator);
	return modulatorCount() - 1;
}

bool ModulationLayer::insertModulator(int index, const Modulator& modulator)
{
	if (index < 0 || index > modulatorCount() || modulatorCount() >= MaxModulators)
	{
		return false;
	}
	m_modulators.insert(m_modulators.begin() + index, modulator);
	return true;
}

bool ModulationLayer::removeModulator(int index)
{
	if (index < 0 || index >= modulatorCount()) { return false; }
	m_modulators.erase(m_modulators.begin() + index);
	return true;
}

bool ModulationLayer::setSource(int index, const ModulatorSource& source)
{
	Modulator* modulator = this->modulator(index);
	if (modulator == nullptr) { return false; }
	modulator->source = source;
	return true;
}

int ModulationLayer::addRoute(int modulator, const ModulationRoute& route)
{
	if (modulator < 0 || modulator >= modulatorCount()) { return -1; }
	Modulator* entry = this->modulator(modulator);
	if (entry->routeCount() >= MaxRoutesPerModulator) { return -1; }
	entry->routes.push_back(route);
	return entry->routeCount() - 1;
}

bool ModulationLayer::insertRoute(int modulator, int route, const ModulationRoute& incoming)
{
	Modulator* entry = this->modulator(modulator);
	if (entry == nullptr) { return false; }
	if (route < 0 || route > entry->routeCount() || entry->routeCount() >= MaxRoutesPerModulator)
	{
		return false;
	}
	entry->routes.insert(entry->routes.begin() + route, incoming);
	return true;
}

bool ModulationLayer::removeRoute(int modulator, int route)
{
	Modulator* entry = this->modulator(modulator);
	if (entry == nullptr) { return false; }
	if (route < 0 || route >= entry->routeCount()) { return false; }
	entry->routes.erase(entry->routes.begin() + route);
	return true;
}

bool ModulationLayer::setDepth(int modulator, int route, float depth)
{
	Modulator* entry = this->modulator(modulator);
	if (entry == nullptr) { return false; }
	if (route < 0 || route >= entry->routeCount()) { return false; }
	entry->routes[static_cast<std::size_t>(route)].depth = std::clamp(depth, -1.0f, 1.0f);
	return true;
}

void ModulationLayer::zeroDepths()
{
	for (Modulator& modulator : m_modulators)
	{
		for (ModulationRoute& route : modulator.routes) { route.depth = 0.0f; }
	}
}

const ModulationRoute* ModulationLayer::findRoute(const ModulationRoute& route) const
{
	for (const Modulator& modulator : m_modulators)
	{
		for (const ModulationRoute& candidate : modulator.routes)
		{
			const bool same = candidate.channel == route.channel
				&& candidate.chain == route.chain && candidate.effect == route.effect
				&& candidate.parameter == route.parameter;
			if (same) { return &candidate; }
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// persistence
// ---------------------------------------------------------------------------

bool ModulationLayer::saveSettings(QDomDocument& doc, QDomElement& parent) const
{
	if (!shouldPersist()) { return false; }
	QDomElement layerElement = doc.createElement(QStringLiteral("modulation-layer"));
	layerElement.setAttribute(QStringLiteral("version"), kModulationLayerVersion);
	layerElement.setAttribute(QStringLiteral("modulators"), modulatorCount());
	layerElement.setAttribute(QStringLiteral("max-modulators"), MaxModulators);
	for (const Modulator& modulator : m_modulators)
	{
		QDomElement modulatorElement = doc.createElement(QStringLiteral("modulator"));
		modulatorElement.setAttribute(QStringLiteral("name"), modulator.name);
		saveSource(doc, modulatorElement, modulator.source);
		for (const ModulationRoute& route : modulator.routes) { saveRoute(doc, modulatorElement, route); }
		layerElement.appendChild(modulatorElement);
	}
	parent.appendChild(layerElement);
	return true;
}

bool ModulationLayer::loadSettings(const QDomElement& element)
{
	clear();
	if (element.isNull()) { return false; }

	bool any = false;
	const QDomNodeList modulators = element.elementsByTagName(QStringLiteral("modulator"));
	for (int i = 0; i < modulators.count() && modulatorCount() < MaxModulators; ++i)
	{
		const QDomElement node = modulators.at(i).toElement();
		if (node.isNull()) { continue; }
		if (loadModulator(node, this)) { any = true; }
	}

	// An element holding nothing this build accepts loads as the EMPTY layer -
	// the state a project with no modulation layer is in - so a truncated or
	// future block degrades to "no modulation" and never to a wrong one.
	if (!any) { clear(); }
	return any;
}

void ModulationLayer::clear()
{
	m_modulators.clear();
}

// ---------------------------------------------------------------------------
// the runtime and the block
// ---------------------------------------------------------------------------

void writeModulationBase(AutomatableModel* model, float base)
{
	if (model == nullptr) { return; }
	// isAutomated: a modulation write is not a project edit and must not push a
	// journal checkpoint - the same flag Song::processAutomations writes with.
	model->setValue(base, true);
}

void rebuildModulationRuntime(const ModulationLayer& layer, ModulationRuntime* runtime)
{
	fillRuntime(layer, runtime);
}

void rebuildModulationRuntimeRestoring(const ModulationLayer& layer, ModulationRuntime* runtime)
{
	// Restoring first is what makes re-capturing idempotent: see the header.
	restoreModulationBases(*runtime);
	fillRuntime(layer, runtime);
}

void restoreModulationBases(const ModulationRuntime& runtime)
{
	for (int i = 0; i < runtime.entryCount; ++i)
	{
		const ModulationRuntime::Entry& entry = runtime.entries[static_cast<std::size_t>(i)];
		if (entry.model.isNull()) { continue; }
		writeModulationBase(entry.model.data(), entry.base);
	}
}

void writeModulatedEntry(AutomatableModel* model, const ModulatorSource& source, float base, float depth,
	float minimum, float maximum, double seconds, bool perSample, ModulationBlock block)
{
	if (!perSample || block.frames <= 1 || block.sampleRate == 0)
	{
		writeModulationBase(model, modulatedValue(source, base, depth, minimum, maximum, seconds));
		return;
	}
	AutomationRamp ramp;
	ramp.reset(block.frames);
	const f_cnt_t last = block.frames - 1;
	for (int k = 0; k < AutomationRamp::MaxKnots; ++k)
	{
		const auto frame = static_cast<f_cnt_t>(static_cast<long long>(last) * k / (AutomationRamp::MaxKnots - 1));
		ramp.addKnot(frame, modulatedValue(source, base, depth, minimum, maximum,
			seconds + static_cast<double>(frame) / static_cast<double>(block.sampleRate)));
	}
	writeModulationBase(model, ramp.knot(0).value);
	model->publishAutomationRamp(ramp);
}

void applyModulationBlock(const ModulationRuntime& runtime, double seconds, ModulationBlock block)
{
	if (!runtime.active()) { return; }
	for (int i = 0; i < runtime.entryCount; ++i)
	{
		const ModulationRuntime::Entry& entry = runtime.entries[static_cast<std::size_t>(i)];
		// A target destroyed under the audio thread reads null (QPointer) and
		// is skipped, never dereferenced.
		if (entry.model.isNull()) { continue; }
		// A zero depth is bookkeeping, not a modulation: writing the base back
		// every block would emit a dataChanged per period for no change.
		if (entry.depth == 0.0f) { continue; }
		if (entry.modulator < 0 || entry.modulator >= runtime.sourceCount) { continue; }
		const ModulatorSource& source = runtime.sources[static_cast<std::size_t>(entry.modulator)];
		if (!source.active) { continue; }
		writeModulatedEntry(entry.model.data(), source, entry.base, entry.depth, entry.minimum,
			entry.maximum, seconds, entry.perSample, block);
	}
}

} // namespace lmms
