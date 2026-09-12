/*
 * MidiLearnGui.cpp - GUI side of global MIDI learn.
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
 *
 */

#include "MidiLearnGui.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QWidget>

#include "AutomatableModel.h"
#include "MidiLearn.h"
#include "ModelView.h"

namespace lmms::gui
{

namespace
{

//! A learn is a one-shot user gesture: a few tens of milliseconds of latency is
//! invisible, and this is the whole cost of the hand-off on the GUI side.
constexpr int BindPollIntervalMs = 10;

} // namespace



MidiLearnGui* MidiLearnGui::instance()
{
	static MidiLearnGui s_instance;
	return &s_instance;
}



MidiLearnGui::MidiLearnGui()
{
	// The instance() static is first built on the GUI thread (MainWindow's
	// constructor calls it), so this timer runs on the GUI thread - which is the
	// point: the timer is how a learn reaches the thread that owns the models.
	m_bindTimer.setInterval(BindPollIntervalMs);
	QObject::connect(&m_bindTimer, &QTimer::timeout, this, &MidiLearnGui::onBindTimer);
}



void MidiLearnGui::onBindTimer()
{
	// Runs on the GUI thread. Apply whatever the MIDI input thread queued: the
	// MidiController, the ControllerConnection and the model's connection
	// pointer are all written from here, never from the MIDI input thread.
	MidiLearn::instance()->applyPendingBinding();

	// A learn can end without the menu being touched - the control-change binds
	// it, or the control the user focused went away. Reconcile now so the tick
	// does not wait for the Edit menu to be reopened.
	syncArmedState();
}



void MidiLearnGui::syncArmedState()
{
	if (MidiLearn::instance()->isEnabled())
	{
		return;
	}

	if (m_action != nullptr && m_action->isChecked())
	{
		m_action->setChecked(false);
	}

	if (m_bindTimer.isActive())
	{
		m_bindTimer.stop();
	}

	MidiLearn::instance()->setFocusTarget(nullptr);

	if (qApp != nullptr)
	{
		qApp->removeEventFilter(this);
	}
}



void MidiLearnGui::setArmed(bool armed)
{
	MidiLearn::instance()->setEnabled(armed);

	if (armed)
	{
		if (qApp != nullptr)
		{
			qApp->installEventFilter(this);
		}
		// Start delivering learns left by the MIDI input thread.
		if (!m_bindTimer.isActive())
		{
			m_bindTimer.start();
		}
	}
	else
	{
		syncArmedState();
	}

	if (m_action != nullptr && m_action->isChecked() != armed)
	{
		m_action->setChecked(armed);
	}
}



bool MidiLearnGui::isArmed() const
{
	return MidiLearn::instance()->isEnabled();
}



void MidiLearnGui::setAction(QAction* action)
{
	m_action = action;

	if (m_action != nullptr && m_action->isChecked() != isArmed())
	{
		m_action->setChecked(isArmed());
	}
}



bool MidiLearnGui::eventFilter(QObject* watched, QEvent* event)
{
	if (!MidiLearn::instance()->isEnabled())
	{
		return QObject::eventFilter(watched, event);
	}

	const auto type = event->type();
	if (type == QEvent::MouseButtonPress || type == QEvent::FocusIn)
	{
		// "Focus a control": whichever model-backed widget the user just pressed
		// or keyboard-focused becomes the learn target. Controls inherit QWidget
		// and ModelView together (Knob, Fader, LcdSpinBox, ...), so a single
		// cross-cast covers every control type without per-widget code.
		auto* widget = qobject_cast<QWidget*>(watched);
		auto* view = widget != nullptr ? dynamic_cast<ModelView*>(widget) : nullptr;
		if (view != nullptr)
		{
			if (auto* model = dynamic_cast<AutomatableModel*>(view->model()))
			{
				MidiLearn::instance()->setFocusTarget(model);
			}
		}
	}

	return QObject::eventFilter(watched, event);
}




} // namespace lmms::gui
