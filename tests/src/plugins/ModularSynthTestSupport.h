/*
 * ModularSynthTestSupport.h - Q050-059.1: one engine, one track carrying the modular instrument, and the helpers
 *                             its tests share (states written by hand, notes played through the real play path)
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_MODULAR_SYNTH_TEST_SUPPORT_H
#define LMMS_MODULAR_SYNTH_TEST_SUPPORT_H

#include <QtTest>

#include <QDomDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <vector>

#include "../core/ReversibilityTestSupport.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"

namespace lmms::modular_test
{

inline const QString StarterWiring = QStringLiteral(
	"osc1 -> filter1.in\nfilter1 -> out.in\nenv1 -> out.amp\nlfo1 -> filter1.cutoff");

//! A <modularsynth> state holding @a body inside its <graph> (or no graph at all when @a withGraph is false).
inline QDomElement stateElement(QDomDocument& doc, const QString& body, bool withGraph = true)
{
	const QString xml = withGraph
		? QStringLiteral("<modularsynth version=\"1\"><graph>%1</graph></modularsynth>").arg(body)
		: QStringLiteral("<modularsynth/>");
	doc.setContent(xml);
	return doc.documentElement();
}

inline float peakOf(const std::vector<float>& v, std::size_t from = 0)
{
	float peak = 0.f;
	for (std::size_t i = from; i < v.size(); ++i) { peak = std::max(peak, std::abs(v[i])); }
	return peak;
}

//! How much of a signal's energy is in its fast changes: the energy of its first difference over its own.
inline float brightness(const std::vector<float>& v, std::size_t from)
{
	double diff = 0.0;
	double level = 0.0;
	for (std::size_t i = from + 1; i < v.size(); ++i)
	{
		diff += (v[i] - v[i - 1]) * (v[i] - v[i - 1]);
		level += v[i] * v[i];
	}
	return level > 0.0 ? static_cast<float>(diff / level) : 0.f;
}

inline int zeroCrossings(const std::vector<float>& v, std::size_t from)
{
	int n = 0;
	for (std::size_t i = from + 1; i < v.size(); ++i) { n += (v[i - 1] < 0.f) != (v[i] < 0.f) ? 1 : 0; }
	return n;
}


//! One engine and one track carrying the modular instrument at a time; the helpers every modular test uses.
class ModularSynthFixture : public QObject
{
public:
	InstrumentTrack* m_track = nullptr;
	Instrument* m_instrument = nullptr;
	f_cnt_t m_fpp = 0;

	static ControlResult run(const QString& id, const QJsonObject& args = {}) { return revtest::run(id, args); }

	//! A fresh project with one track carrying the modular instrument.
	void fresh()
	{
		Engine::getSong()->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(m_track != nullptr);
		m_instrument = m_track->loadInstrument(QStringLiteral("modularsynth"));
		QVERIFY2(m_instrument != nullptr, "the modularsynth module did not load");
	}

	AutomatableModel* model(const QString& name) const
	{
		for (int i = 0; i < m_instrument->parameterCount(); ++i)
		{
			if (m_instrument->parameterName(i) == name) { return m_instrument->parameterModel(i); }
		}
		return nullptr;
	}

	void setValue(const QString& name, float value)
	{
		AutomatableModel* m = model(name);
		QVERIFY2(m != nullptr, qPrintable(name));
		m->setValue(value);
	}

	QString wiring() const
	{
		QString text;
		QMetaObject::invokeMethod(m_instrument, "wiringText", Q_RETURN_ARG(QString, text));
		return text;
	}

	QString loadProblem() const
	{
		QString text;
		QMetaObject::invokeMethod(m_instrument, "loadProblem", Q_RETURN_ARG(QString, text));
		return text;
	}

	/*! The left channel of a note of @a key held for @a heldPeriods periods and then left to finish (or @a maxPeriods
	 *  in all), rendered through the note's own play path - InstrumentTrack, the instrument, its release. */
	std::vector<float> playNote(int key = 57, int heldPeriods = 8, int maxPeriods = 400)
	{
		const Note note(TimePos(384), TimePos(0), key);
		NotePlayHandle* nph = NotePlayHandleManager::acquire(m_track, 0, m_fpp * heldPeriods, note);
		std::vector<float> left;
		std::vector<SampleFrame> buffer(static_cast<std::size_t>(m_fpp));
		for (int i = 0; i < maxPeriods && !nph->isFinished(); ++i)
		{
			std::fill(buffer.begin(), buffer.end(), SampleFrame{});
			nph->play(std::span<SampleFrame>{buffer});
			for (const SampleFrame& frame : buffer) { left.push_back(frame[0]); }
		}
		NotePlayHandleManager::release(nph);
		return left;
	}

	void restore(const QDomElement& state) { m_instrument->restoreState(state); }

	//! Makes the project's track carrying the modular instrument the one the test plays; false if there is none.
	bool adoptModularTrack()
	{
		m_track = nullptr;
		for (Track* t : Engine::getSong()->tracks())
		{
			auto* it = dynamic_cast<InstrumentTrack*>(t);
			if (it != nullptr && it->instrument() != nullptr && it->instrument()->nodeName() == QLatin1String("modularsynth")) { m_track = it; }
		}
		m_instrument = m_track != nullptr ? m_track->instrument() : nullptr;
		return m_track != nullptr;
	}

	void initEngine()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
		m_fpp = Engine::audioEngine()->framesPerPeriod();
	}

	void cleanupEngine()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}
};

} // namespace lmms::modular_test

#endif // LMMS_MODULAR_SYNTH_TEST_SUPPORT_H
