/*
 * AuParameterHandoffTest.cpp - an Audio Unit parameter's automation reaches the unit without a signal on the
 *                              render thread (Q050-491, review round 1)
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

/*! The AU hosts' parameter models (plugins/AuEffect/AuParameter.cpp), compiled into this test on every build with a
 *  stand-in for the unit (au::ParamTarget). An automation clip on an AU parameter model plays, and one real render
 *  period runs on its own thread: Song::processAutomations sets the model there, on the render thread, and a play
 *  handle then does what the AU device's processImpl does before rendering - pushChangedValues(). The value must
 *  reach the unit in that same period, and no dataChanged() may be emitted while the period renders; the GUI thread's
 *  flushDeferred() emits it afterwards. A GUI-thread change (plugin.param_set) reaches the unit with the
 *  next block, and the GUI poll does not pull the unit's stale value over it in between. */

#include <QtTest>

#include <array>
#include <atomic>
#include <thread>
#include <vector>

#include "AudioBusHandle.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "AuParameter.h"
#include "Engine.h"
#include "Model.h"
#include "PlayHandle.h"
#include "SampleFrame.h"
#include "Song.h"

using namespace lmms;

namespace
{

//! The unit: two parameters; what reached it, recorded without allocating.
class FakeUnit : public au::ParamTarget
{
public:
	FakeUnit()
	{
		au::ParamInfo cutoff;
		cutoff.id = 7;
		cutoff.name = QStringLiteral("Cutoff");
		cutoff.minimum = 0.f;
		cutoff.maximum = 10000.f;  // the model's step is 0.1 % of the range: 10, so every value below is exact
		cutoff.defaultValue = 1000.f;
		au::ParamInfo locked = cutoff;
		locked.id = 8;
		locked.name = QStringLiteral("Meter");
		locked.writable = false;
		m_params = {cutoff, locked};
		for (std::size_t i = 0; i < m_values.size(); ++i) { m_values[i].store(m_params[i].defaultValue); }
	}

	bool isLoaded() const override { return true; }
	const std::vector<au::ParamInfo>& parameters() const override { return m_params; }
	float paramValue(int index) const override { return m_values[static_cast<std::size_t>(index)].load(); }
	void setParam(int index, float value) override
	{
		m_values[static_cast<std::size_t>(index)].store(value);
		m_sets.fetch_add(1);
	}

	//! The unit's own editor moved a parameter.
	void moveFromItsEditor(int index, float value) { m_values[static_cast<std::size_t>(index)].store(value); }
	int sets() const { return m_sets.load(); }

private:
	std::vector<au::ParamInfo> m_params;
	std::array<std::atomic<float>, 2> m_values;
	std::atomic<int> m_sets{0};
};

//! A play handle runs inside every render period, after the song's automation (stage 1): it hands the block's values
//! to the unit as the AU device's processImpl does.
class AutomatingHandle : public PlayHandle
{
public:
	explicit AutomatingHandle(AuParamModelList* list) :
		PlayHandle{PlayHandle::Type::InstrumentPlayHandle, 0},
		m_list(list)
	{
	}

	void play(std::span<SampleFrame>) override
	{
		m_list->pushChangedValues();
		m_blocks.fetch_add(1);
	}
	bool isFinished() const override { return false; }
	bool isFromTrack(const Track*) const override { return false; }

	int blocks() const { return m_blocks.load(); }

private:
	AuParamModelList* m_list;
	std::atomic<int> m_blocks{0};
};

void renderOnePeriodOnItsOwnThread()
{
	std::thread renderer([] { Engine::audioEngine()->renderNextPeriod(); });
	renderer.join();
}

} // namespace

class AuParameterHandoffTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// This test renders the periods itself.
		Engine::audioEngine()->audioDev()->stopProcessing();
		m_owner = std::make_unique<Model>(nullptr);
		m_list = std::make_unique<AuParamModelList>(&m_unit, m_owner.get());
		m_list->build();
		QCOMPARE(static_cast<int>(m_list->models().size()), 2);
		QCOMPARE(m_list->models().front()->value(), 1000.f);  // the unit's value
		m_bus = std::make_unique<AudioBusHandle>(QStringLiteral("AU handoff"), false);
		m_effect = new AutomatingHandle(m_list.get());
		m_effect->setAudioBusHandle(m_bus.get());
		QVERIFY(Engine::audioEngine()->addPlayHandle(m_effect));
		AuParamModel* cutoff = m_list->models().front();
		QObject::connect(cutoff, &Model::dataChanged, cutoff, [this] { m_signals.fetch_add(1); }, Qt::DirectConnection);
	}

	void cleanupTestCase()
	{
		Engine::audioEngine()->removePlayHandle(m_effect);
		m_bus.reset();
		m_list.reset();
		m_owner.reset();
		Engine::destroy();
	}

	void automationReachesTheUnitWithNoSignalOnTheRenderThread()
	{
		AuParamModel* cutoff = m_list->models().front();
		Song* song = Engine::getSong();
		AutomationTrack track(song);
		AutomationClip clip(&track);
		clip.setProgressionType(AutomationClip::ProgressionType::Discrete);
		clip.putValue(0, 5000.0, false);
		clip.putValue(4000, 5000.0, false);
		clip.movePosition(0);
		clip.addObject(cutoff);

		const int setsBefore = m_unit.sets();
		const int blocksBefore = m_effect->blocks();
		const int signalsBefore = m_signals.load();
		song->playSong();
		renderOnePeriodOnItsOwnThread();
		const int signalsDuringRender = m_signals.load() - signalsBefore;
		song->stop();
		QVERIFY2(m_effect->blocks() > blocksBefore, "the play handle did not run in the render period");
		QCOMPARE(cutoff->value(), 5000.f);  // the song's automation set it
		QCOMPARE(m_unit.paramValue(0), 5000.f);  // and it reached the unit in the same period
		QCOMPARE(m_unit.sets(), setsBefore + 1);
		QCOMPARE(signalsDuringRender, 0);  // no dataChanged() while the period rendered

		// The GUI thread emits the held-back signal, once.
		const int signalsAfterRender = m_signals.load();
		m_list->flushDeferred();
		QCOMPARE(m_signals.load(), signalsAfterRender + 1);
		m_list->flushDeferred();
		QCOMPARE(m_signals.load(), signalsAfterRender + 1);

		// An unchanged value is not sent again, and the read-only parameter is never sent.
		renderOnePeriodOnItsOwnThread();
		QCOMPARE(m_unit.sets(), setsBefore + 1);
	}

	void aGuiChangeReachesTheUnitWithTheNextBlockAndIsNotPulledBack()
	{
		AuParamModel* cutoff = m_list->models().front();
		cutoff->setValue(250.f);  // plugin.param_set's path, on this thread
		QCOMPARE(m_unit.paramValue(0), 5000.f);  // not yet: the audio path hands it over
		m_list->pullValues(0.f);
		QCOMPARE(cutoff->value(), 250.f);  // the poll did not pull the unit's stale value over it
		renderOnePeriodOnItsOwnThread();
		QCOMPARE(m_unit.paramValue(0), 250.f);

		// The unit's own editor moves it: the poll brings the model along.
		m_unit.moveFromItsEditor(0, 640.f);
		m_list->pullValues(0.f);
		QCOMPARE(cutoff->value(), 640.f);
	}

private:
	FakeUnit m_unit;
	std::unique_ptr<Model> m_owner;
	std::unique_ptr<AuParamModelList> m_list;
	std::unique_ptr<AudioBusHandle> m_bus;
	AutomatingHandle* m_effect = nullptr;
	std::atomic<int> m_signals{0};
};

QTEST_GUILESS_MAIN(AuParameterHandoffTest)
#include "AuParameterHandoffTest.moc"
