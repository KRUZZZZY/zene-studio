/*
 * ModularGraph.h - Q050-059: the modular instrument's graph as data - its module kinds, their ports and
 *                  parameters, the wiring between them, and the rules a graph has to keep to be played
 *
 * Nothing here runs on the audio thread: a graph is built, checked and serialised on the control thread, and
 * ModularProgram.h compiles a valid one into the fixed-size form the voices read.
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_MODULAR_GRAPH_H
#define LMMS_MODULAR_GRAPH_H

#include <QString>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

class QDomDocument;
class QDomElement;

namespace lmms::modular
{

//! The most modules one instrument holds; the voice state is sized for it.
constexpr int MaxModules = 16;
//! The most wires one instrument holds.
constexpr int MaxWires = 64;
//! The most input ports and parameters any module kind has.
constexpr int MaxPorts = 2;
constexpr int MaxParams = 4;

enum class ModuleKind : std::uint8_t
{
	Oscillator,
	Envelope,
	Filter,
	Lfo,
	Output,
};

//! What a port carries: an audio signal, or a control signal (envelope 0..1, LFO -1..1).
enum class Signal : std::uint8_t
{
	Audio,
	Control,
};

struct PortSpec
{
	const char* key;
	Signal signal;
};

struct ParamSpec
{
	const char* key;	//!< the attribute it is saved under
	const char* label;	//!< what the window and plugin.param_get call it, after the module's own label
	float lowest;
	float highest;
	float init;
	float step;
	bool logarithmic;
	const char* unit;	//!< what the window's hint shows after the value
};

struct KindSpec
{
	ModuleKind kind;
	const char* key;	//!< the type saved in a project ("oscillator", ...)
	const char* label;	//!< the module's label before its number ("Osc", ...)
	bool hasOutput;
	Signal output;
	std::span<const PortSpec> ports;
	std::span<const ParamSpec> params;
};

const KindSpec& kindSpec(ModuleKind kind);
std::optional<ModuleKind> kindFromKey(const QString& key);
//! The index of @a port among @a kind's ports, or -1.
int portIndex(ModuleKind kind, const QString& port);

struct Module
{
	QString id;		//!< unique within its graph, e.g. "osc1"
	ModuleKind kind;
};

//! One wire: the output of @a from feeds input port @a port of @a to. Several wires into one port add up.
struct Wire
{
	QString from;
	QString to;
	QString port;

	bool operator==(const Wire&) const = default;
};

class Graph
{
public:
	std::vector<Module> modules;
	std::vector<Wire> wires;

	/*! The graph a new instrument starts with: an oscillator through a low-pass filter to the output, an envelope
	 *  on the output's level and an LFO on the filter's cutoff. */
	static Graph starter();

	//! The module with @a id, or -1.
	int indexOf(const QString& id) const;

	/*! True for a graph the voices can play: unique non-empty ids, known kinds, at most MaxModules modules and
	 *  MaxWires wires, exactly one output module, every wire between two existing modules into a port its source's
	 *  signal fits, no wire twice and no cycle. Otherwise false, with the first problem in @a why. */
	bool isValid(QString* why = nullptr) const;

	//! The module indices in an order where every module comes after the modules wired into it (valid graphs only).
	std::vector<int> processingOrder() const;

	//! "osc1 -> filter1.in" lines, one per wire, for the window and the logs.
	QString describeWiring() const;

	/*! Writes the modules and wires under @a parent as <module id type/> and <wire from to port/> children; the
	 *  modules' parameters are not part of the graph (the instrument adds them to each <module>). */
	void save(QDomDocument& doc, QDomElement& parent) const;
	/*! Reads what save() wrote. A module of an unknown kind or without an id is refused as a whole (nullopt, the
	 *  reason in @a why) rather than dropped, so a damaged graph is never played half-wired; isValid() checks the rest. */
	static std::optional<Graph> load(const QDomElement& parent, QString* why = nullptr);

	//! The label a window shows for module @a index: its kind's label and its number among modules of that kind.
	QString moduleLabel(int index) const;

private:
	//! What is wrong with the module list, or empty.
	QString modulesProblem() const;
	//! What is wrong with wire @a index, or empty.
	QString wireProblem(std::size_t index) const;
	//! Adds @a delta to the pending input count of @a wire's target, if both its ends exist.
	void countInto(const Wire& wire, std::vector<int>& pending, int delta) const;
	//! The lowest module index with no pending input that is not done yet, or -1.
	static int nextReady(const std::vector<int>& pending, const std::vector<bool>& done);
};

} // namespace lmms::modular

#endif // LMMS_MODULAR_GRAPH_H
