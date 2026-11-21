/*
 * MeterSpeech.cpp - spoken meter readings and the input/master peak watch
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "MeterSpeech.h"

#include <algorithm>
#include <cmath>

#include <QCoreApplication>
#include <QTimer>

#include "AccessibleWidgets.h"
#include "AudioEngine.h"
#include "ControlMeterSupport.h"
#include "Engine.h"
#include "InputMeter.h"
#include "MasterLoudnessTap.h"

namespace lmms::gui::meterspeech
{

namespace
{

constexpr int PollMs = 100;

struct Latch
{
	bool high = false;
	QString message;
};

Latch& inputLatch()
{
	static Latch latch;
	return latch;
}

Latch& masterLatch()
{
	static Latch latch;
	return latch;
}

double watchedThreshold = -6.0;
bool watchedEnabled = false;
bool configRead = false;
QString last;

void refreshConfig()
{
	const double threshold = control::meterPeakWatchThresholdDbfs();
	const bool enabled = control::meterPeakWatchEnabled();
	if (!configRead || threshold != watchedThreshold || enabled != watchedEnabled)
	{
		configRead = true;
		watchedThreshold = threshold;
		watchedEnabled = enabled;
		reset();
	}
}

double thresholdLinear()
{
	return std::pow(10.0, watchedThreshold / 20.0);
}

void crossing(const char* source, Latch& latch, float peak)
{
	const bool nowHigh = peak >= static_cast<float>(thresholdLinear());
	if (nowHigh && !latch.high)
	{
		const double db = peak > 0.f ? 20.0 * std::log10(static_cast<double>(peak)) : -120.0;
		latch.message = QStringLiteral("%1 peak %2 dBFS crossed the %3 dBFS threshold.")
			.arg(QString::fromLatin1(source), QString::number(db, 'f', 1), QString::number(watchedThreshold, 'f', 1));
		last = latch.message;
		// Every new crossing is a new event: two crossings that round to the same text still each get spoken.
		a11y::announceStatus(QCoreApplication::instance(), latch.message, a11y::Politeness::Polite, a11y::Repeat::Allow);
	}
	latch.high = nowHigh;
}

class Poller : public QObject
{
public:
	explicit Poller(QObject* parent) : QObject(parent)
	{
		setObjectName(QStringLiteral("zeneMeterSpeechPoller"));
		m_timer.setInterval(PollMs);
		QObject::connect(&m_timer, &QTimer::timeout, this, [] { poll(); });
		m_timer.start();
	}

private:
	QTimer m_timer;
};

} // namespace

void start(QObject* parent)
{
	if (parent == nullptr) { return; }
	for (QObject* child : parent->children())
	{
		if (child->objectName() == QLatin1String("zeneMeterSpeechPoller")) { return; }
	}
	new Poller(parent);
	refreshConfig();
}

void observe(float inputPeak, float masterPeak)
{
	refreshConfig();
	if (!watchedEnabled) { return; }
	crossing("Input", inputLatch(), std::max(0.f, inputPeak));
	crossing("Master", masterLatch(), std::max(0.f, masterPeak));
}

void poll()
{
	refreshConfig();
	// Drain both sources even while the watch is off: otherwise a peak from before it was
	// enabled would still be waiting and would announce a crossing that is not happening now.
	float left = 0.f;
	float right = 0.f;
	inputmeter::watch(&left, &right);
	AudioEngine* engine = Engine::audioEngine();
	MasterLoudnessTap* tap = engine != nullptr ? engine->masterLoudness() : nullptr;
	const float master = tap != nullptr ? tap->takeSamplePeak() : 0.f;
	if (!watchedEnabled) { return; }
	observe(std::max(left, right), master);
}

void reset()
{
	inputLatch() = {};
	masterLatch() = {};
	last.clear();
}

QString lastMessage()
{
	return last;
}

} // namespace lmms::gui::meterspeech
