/*
 * ModularSynthView.h - Q050-059: the modular instrument's window - a knob for every module parameter and the
 *                      graph's wiring
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_GUI_MODULAR_SYNTH_VIEW_H
#define LMMS_GUI_MODULAR_SYNTH_VIEW_H

#include <memory>
#include <vector>

#include "InstrumentView.h"
#include "Knob.h"
#include "ModularGraph.h"

class QScrollArea;

namespace lmms
{

class FloatModel;
class ModularSynth;

namespace gui
{

//! A knob that shows a stand-in model: it offers no automation or controller link of its own (they would be made on the
//! stand-in, not on the parameter).
class CommandKnob : public Knob
{
	Q_OBJECT
public:
	using Knob::Knob;

protected:
	void contextMenuEvent(QContextMenuEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
};

/*! One row per module (its label and a knob for each of its parameters), then the wiring; rebuilt whenever the
 *  instrument's graph is replaced.
 *  Each knob shows a stand-in model of the view's own; what the knob does to it is sent as plugin.param_set - the
 *  command an agent runs, so a knob edit and an agent edit are the same edit with the same undo step - and the
 *  parameter's own changes (an agent, automation, an undo) are copied back to it. */
class ModularSynthView : public InstrumentViewFixedSize
{
	Q_OBJECT
public:
	ModularSynthView(Instrument* instrument, QWidget* parent);
	~ModularSynthView() override = default;

protected:
	void paintEvent(QPaintEvent* event) override;

private slots:
	void rebuild();

private:
	//! A knob's stand-in model and the parameter it stands for.
	struct Binding
	{
		std::unique_ptr<FloatModel> proxy;
		FloatModel* target = nullptr;
		bool following = false;	//!< the stand-in is being set from the parameter, not by the knob
	};

	void modelChanged() override;
	Knob* makeKnob(const modular::Module& module, const modular::ParamSpec& p, FloatModel* target, QWidget* parent,
		std::vector<std::unique_ptr<Binding>>& bindings);
	//! Sends what the knob set as plugin.param_set.
	void dispatch(Binding& binding);
	//! Copies the parameter's value into the stand-in.
	static void follow(Binding& binding);

	QScrollArea* m_scroll;
	std::vector<std::unique_ptr<Binding>> m_bindings;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_MODULAR_SYNTH_VIEW_H
