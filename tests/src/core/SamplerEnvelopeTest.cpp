/*
 * SamplerEnvelopeTest.cpp - Q050-103: the sampler's attack and release, their automation and persistence
 *
 * Renders real notes through the audiofileprocessor module and measures them: the attack ramp, the release tail,
 * the window's controls, the agent's parameter commands and automation of the values, the save/load round trip,
 * and that a project saved before these settings existed plays as it always did.
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <QApplication>
#include <QDomDocument>
#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <memory>

#include "ControlVocabulary.h"
#include "InstrumentView.h"
#include "Knob.h"
#include "LcdSpinBox.h"
#include "ReversibilityTestSupport.h"
#include "SamplerTestSupport.h"

using namespace lmms;
using namespace lmms::sampler_test;

class SamplerEnvelopeTest : public SamplerFixture
{
	Q_OBJECT

private slots:
	void initTestCase() { initEngine(); }
	void cleanupTestCase() { cleanupEngine(); }

	//! A sampler from before Q050-103 has no attack and a 3 ms release: the new models default to exactly that.
	void defaultsAreTheOldBehaviour()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		QCOMPARE(model(QStringLiteral("Attack"))->value<float>(), 0.f);
		QCOMPARE(model(QStringLiteral("Release"))->value<float>(), 3.f);
		QCOMPARE(model(QStringLiteral("Voice limit"))->value<float>(), 64.f);
		QCOMPARE(m_instrument->desiredReleaseTimeMs(), 3.f);
		// They are appended after the eight existing parameters, so those keep their agent-facing indices.
		QVERIFY(parameterIndex(QStringLiteral("Amplify")) == 0);
		QCOMPARE(parameterIndex(QStringLiteral("Interpolation mode")), 7);
		QCOMPARE(parameterIndex(QStringLiteral("Attack")), 8);
		QCOMPARE(parameterIndex(QStringLiteral("Release")), 9);
		QCOMPARE(parameterIndex(QStringLiteral("Voice limit")), 10);

		Played p = start();
		renderPeriods({&p}, 3);
		// No attack: full level from the first frame.
		QVERIFY2(std::abs(p.left[0]) > 0.4f, "with no attack the note starts at full level");
		NotePlayHandleManager::release(p.nph);
	}

	//! A note fades in from silence over the attack time, and a note that starts later has its own ramp.
	void attackRampsEachNoteFromSilence()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		Played reference = start();
		renderPeriods({&reference}, 4);
		const float full = reference.left[2 * m_fpp];
		QVERIFY(full > 0.4f);
		NotePlayHandleManager::release(reference.nph);

		setValue(QStringLiteral("Attack"), 10.f);
		const long attack = frames(10.f);
		QVERIFY(attack > static_cast<long>(m_fpp)); // spans several periods, so the ramp crosses period boundaries
		Played a = start();
		renderPeriods({&a}, 4);
		QVERIFY2(std::abs(a.left[0]) < 1e-4f, "the note starts from silence");
		for (const long at : {attack / 4, attack / 2, attack * 3 / 4})
		{
			const float expected = full * static_cast<float>(at) / static_cast<float>(attack);
			QVERIFY2(std::abs(a.left[at] - expected) < 0.02f * full,
				qPrintable(QStringLiteral("frame %1: %2 vs ramp %3").arg(at).arg(a.left[at]).arg(expected)));
		}
		QVERIFY2(std::abs(a.left[attack + 8] - full) < 0.01f * full, "after the attack the note is at full level");

		// A second note starting while the first is already at full level ramps on its own.
		Played b = start(69);
		renderPeriods({&a}, 1);
		renderPeriods({&b}, 1);
		QVERIFY2(std::abs(b.left[0]) < 1e-4f, "an overlapping note starts from silence too");
		QVERIFY2(std::abs(b.left[m_fpp / 2]) < full * 0.9f, "and is still rising while the earlier note is full");
		NotePlayHandleManager::release(a.nph);
		NotePlayHandleManager::release(b.nph);
	}

	//! The release time is the model's value: the note's audible tail follows it, and it is not the old fixed 3 ms.
	void releaseTailFollowsTheModel()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		const auto audibleFrames = [&](float releaseMs) {
			setValue(QStringLiteral("Release"), releaseMs);
			const Note note(TimePos(384), TimePos(0), 69);
			Played p;
			p.nph = NotePlayHandleManager::acquire(m_track, 0, m_fpp * 2, note);
			for (int i = 0; i < 400 && !p.nph->isFinished(); ++i) { renderPeriod(p); }
			if (!p.nph->isFinished()) { return -1L; }
			std::size_t audible = 0;
			for (std::size_t i = 0; i < p.left.size(); ++i) { if (std::abs(p.left[i]) > 1e-3f) { audible = i + 1; } }
			NotePlayHandleManager::release(p.nph);
			return static_cast<long>(audible);
		};
		const long noteFrames = static_cast<long>(m_fpp) * 2;
		for (const float ms : {3.f, 25.f, 80.f})
		{
			const long audible = audibleFrames(ms);
			QVERIFY2(audible > 0, "the released note ends");
			const long tail = audible - noteFrames;
			QVERIFY2(std::labs(tail - frames(ms)) <= std::max(4L, frames(ms) / 100), // the last frames sit under the audibility threshold
				qPrintable(QStringLiteral("release %1 ms: tail %2 frames, expected %3").arg(ms).arg(tail).arg(frames(ms))));
		}
	}

	//! The released note fades linearly: halfway through the tail it is at half level.
	void releaseFadesLinearly()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		setValue(QStringLiteral("Release"), 40.f);
		const Note note(TimePos(384), TimePos(0), 69);
		Played p;
		p.nph = NotePlayHandleManager::acquire(m_track, 0, m_fpp * 2, note);
		for (int i = 0; i < 400 && !p.nph->isFinished(); ++i) { renderPeriod(p); }
		const long release = frames(40.f);
		const float full = p.left[m_fpp / 2];
		const float half = p.left[static_cast<std::size_t>(static_cast<long>(m_fpp) * 2 + release / 2)];
		QVERIFY2(std::abs(half - 0.5f * full) < 0.05f * full, qPrintable(QStringLiteral("%1 vs %2").arg(half).arg(full)));
		NotePlayHandleManager::release(p.nph);
	}

	//! The envelope values and the voice limit save, load, and leave the loop controls as they were.
	void settingsRoundTripAndTheLoopControlsStayStable()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		setValue(QStringLiteral("Attack"), 12.5f);
		setValue(QStringLiteral("Release"), 40.f);
		setValue(QStringLiteral("Voice limit"), 7.f);
		setValue(QStringLiteral("Loop mode"), 2.f);
		setValue(QStringLiteral("Start of sample"), 0.1f);
		setValue(QStringLiteral("End of sample"), 0.8f);
		setValue(QStringLiteral("Loopback point"), 0.3f);
		setValue(QStringLiteral("Reverse sample"), 1.f);
		setValue(QStringLiteral("Stutter"), 1.f);

		QDomDocument doc;
		QDomElement saved = doc.createElement(QStringLiteral("state"));
		m_instrument->saveState(doc, saved);
		doc.appendChild(saved);
		const QDomElement settings = saved.firstChildElement(); // the plugin's own element
		QVERIFY(settings.hasAttribute(QStringLiteral("atk")));
		QVERIFY(settings.hasAttribute(QStringLiteral("rel")));
		QVERIFY(settings.hasAttribute(QStringLiteral("voices")));

		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		m_instrument->restoreState(settings);
		QCOMPARE(model(QStringLiteral("Attack"))->value<float>(), 12.5f);
		QCOMPARE(model(QStringLiteral("Release"))->value<float>(), 40.f);
		QCOMPARE(model(QStringLiteral("Voice limit"))->value<float>(), 7.f);
		QCOMPARE(m_instrument->desiredReleaseTimeMs(), 40.f);
		QCOMPARE(model(QStringLiteral("Loop mode"))->value<float>(), 2.f);
		QVERIFY(std::abs(model(QStringLiteral("Start of sample"))->value<float>() - 0.1f) < 1e-6f);
		QVERIFY(std::abs(model(QStringLiteral("End of sample"))->value<float>() - 0.8f) < 1e-6f);
		QVERIFY(std::abs(model(QStringLiteral("Loopback point"))->value<float>() - 0.3f) < 1e-6f);
		QCOMPARE(model(QStringLiteral("Reverse sample"))->value<float>(), 1.f);
		QCOMPARE(model(QStringLiteral("Stutter"))->value<float>(), 1.f);
	}

	//! A project saved before these settings existed has none of them: it loads with attack 0, release 3 ms and the
	//! full limit - the sampler it always was - and its loop-point compatibility rule still holds.
	void oldProjectsLoadWithTheOldBehaviour()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		setValue(QStringLiteral("Attack"), 30.f);
		setValue(QStringLiteral("Release"), 90.f);
		setValue(QStringLiteral("Voice limit"), 5.f);

		QDomDocument doc;
		QDomElement old = doc.createElement(QStringLiteral("audiofileprocessor"));
		old.setAttribute(QStringLiteral("src"), m_dir.filePath(QStringLiteral("dc.wav")));
		old.setAttribute(QStringLiteral("looped"), 1);
		old.setAttribute(QStringLiteral("amp"), 100);
		old.setAttribute(QStringLiteral("sframe"), 0.25);
		old.setAttribute(QStringLiteral("eframe"), 0.9);
		old.setAttribute(QStringLiteral("reversed"), 0);
		old.setAttribute(QStringLiteral("stutter"), 0);
		m_instrument->restoreState(old);

		QCOMPARE(model(QStringLiteral("Attack"))->value<float>(), 0.f);
		QCOMPARE(model(QStringLiteral("Release"))->value<float>(), 3.f);
		QCOMPARE(model(QStringLiteral("Voice limit"))->value<float>(), 64.f);
		QCOMPARE(m_instrument->desiredReleaseTimeMs(), 3.f);
		QCOMPARE(model(QStringLiteral("Loop mode"))->value<float>(), 1.f);
		QVERIFY(std::abs(model(QStringLiteral("Start of sample"))->value<float>() - 0.25f) < 1e-6f);
		QVERIFY(std::abs(model(QStringLiteral("End of sample"))->value<float>() - 0.9f) < 1e-6f);
		// no lframe in the old file: the loop point follows the start point, as it always did
		QVERIFY(std::abs(model(QStringLiteral("Loopback point"))->value<float>() - 0.25f) < 1e-6f);
	}

	//! The window has the controls: knobs for attack and release and a box for the voice limit, bound to the models the
	//! agent reads, and turning one in the GUI changes the model the render path reads.
	void windowControlsDriveTheModels()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		QWidget parent;
		std::unique_ptr<gui::PluginView> view(m_instrument->createView(&parent));
		QVERIFY(view != nullptr);
		view->resize(250, 250);
		view->show();
		QApplication::processEvents();

		const auto wheelUp = [](QWidget* w) {
			const QPointF at(w->width() / 2.0, w->height() / 2.0);
			QWheelEvent event(at, w->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
				Qt::NoScrollPhase, false);
			QApplication::sendEvent(w, &event);
		};

		for (const QString& name : {QStringLiteral("Attack"), QStringLiteral("Release")})
		{
			gui::Knob* knob = nullptr;
			for (gui::Knob* k : view->findChildren<gui::Knob*>())
			{
				if (k->model() == model(name)) { knob = k; }
			}
			QVERIFY2(knob != nullptr, qPrintable(name + " has a knob in the window"));
			const float before = model(name)->value<float>();
			wheelUp(knob);
			QVERIFY2(model(name)->value<float>() > before, qPrintable(name + ": the knob moved the model"));
		}
		QCOMPARE(m_instrument->desiredReleaseTimeMs(), model(QStringLiteral("Release"))->value<float>());

		gui::LcdSpinBox* box = nullptr;
		for (gui::LcdSpinBox* b : view->findChildren<gui::LcdSpinBox*>())
		{
			if (b->model() == model(QStringLiteral("Voice limit"))) { box = b; }
		}
		QVERIFY2(box != nullptr, "the voice limit has a box in the window");
		const float limit = model(QStringLiteral("Voice limit"))->value<float>();
		QVERIFY(limit > 1.f);
		QWheelEvent down(QPointF(4, 4), box->mapToGlobal(QPointF(4, 4)), QPoint(), QPoint(0, -120), Qt::NoButton,
			Qt::NoModifier, Qt::NoScrollPhase, false);
		QApplication::sendEvent(box, &down);
		QVERIFY2(model(QStringLiteral("Voice limit"))->value<float>() < limit, "the box lowered the voice limit");
	}

	//! Automation drives the attack: a clip on the parameter (recorded through automation.add_point) moves the model as the
	//! transport plays, and a note started then ramps for the automated time. The agent's plugin.param_set / param_get see
	//! the same models.
	void automationDrivesTheEnvelope()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		const QString trackId = control::trackIdOf(m_track);
		const int attackIndex = parameterIndex(QStringLiteral("Attack"));
		const int releaseIndex = parameterIndex(QStringLiteral("Release"));
		QVERIFY(attackIndex >= 0 && releaseIndex >= 0);
		const QString attackParam = QStringLiteral("inst/%1").arg(attackIndex);

		// The agent surface: param_set / param_get on the instrument.
		const auto setParam = [&](int index, double value) {
			return revtest::run(QStringLiteral("plugin.param_set"),
				{{QStringLiteral("target"), trackId}, {QStringLiteral("plugin"), QStringLiteral("inst")},
					{QStringLiteral("index"), index}, {QStringLiteral("value"), value}});
		};
		ControlResult set = setParam(releaseIndex, 55.0);
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(model(QStringLiteral("Release"))->value<float>(), 55.f);
		const ControlResult got = revtest::run(QStringLiteral("plugin.param_get"),
			{{QStringLiteral("target"), trackId}, {QStringLiteral("plugin"), QStringLiteral("inst")},
				{QStringLiteral("index"), releaseIndex}});
		QVERIFY2(got.ok, qPrintable(got.errorMessage));
		QCOMPARE(got.result.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("value")).toDouble(), 55.0);
		QCOMPARE(m_instrument->desiredReleaseTimeMs(), 55.f);
		QCOMPARE(revtest::run(QStringLiteral("control.undo")).ok, true);
		QCOMPARE(model(QStringLiteral("Release"))->value<float>(), 3.f);

		// Automation: attack 0 ms at tick 0, 60 ms from tick 48 (a new clip holds each value until the next point).
		for (const auto& [tick, value] : {std::pair{0, 0.0}, std::pair{48, 60.0}})
		{
			const ControlResult added = revtest::run(QStringLiteral("automation.add_point"),
				{{QStringLiteral("track"), trackId}, {QStringLiteral("parameter"), attackParam},
					{QStringLiteral("ticks"), tick}, {QStringLiteral("value"), value}});
			QVERIFY2(added.ok, qPrintable(added.errorMessage));
		}
		Song* song = Engine::getSong();
		song->playSong();
		song->getTimeline().setTicks(0);
		int buffers = 0;
		for (; buffers < 400 && song->getPlayPos().getTicks() < 96; ++buffers)
		{
			AutomatableModel::incrementPeriodCounter(); // one render period, as the engine counts them
			song->processNextBuffer();
		}
		const int reached = song->getPlayPos().getTicks();
		const float automated = model(QStringLiteral("Attack"))->value<float>(); // read while playing: stop() rewinds the models
		song->stop();
		QVERIFY2(reached >= 96, "the transport never passed the second automation point");
		model(QStringLiteral("Attack"))->setValue(automated); // hold the automated value, as a note started mid-clip sees it
		QVERIFY2(std::abs(automated - 60.f) < 0.5f, qPrintable(QStringLiteral("attack %1 ms").arg(automated)));

		// The automated value is what a note started now ramps over.
		Played p = start();
		renderPeriods({&p}, 16);
		const long attack = frames(automated);
		QVERIFY(attack > 2 * static_cast<long>(m_fpp) && attack + 100 < static_cast<long>(p.left.size()));
		const float full = p.left[static_cast<std::size_t>(attack + 100)];
		QVERIFY(full > 0.4f);
		QVERIFY2(std::abs(p.left[0]) < 1e-4f, "starts from silence");
		QVERIFY2(std::abs(p.left[static_cast<std::size_t>(m_fpp)] - full * static_cast<float>(m_fpp) / static_cast<float>(attack))
				< 0.03f * full,
			"the ramp length is the automated attack");
		NotePlayHandleManager::release(p.nph);
	}
};

QTEST_MAIN(SamplerEnvelopeTest)
#include "SamplerEnvelopeTest.moc"
