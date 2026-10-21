/*
 * ModulationAudioViewTest.cpp - the audio thread's modulation snapshot carries no QPointer
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

//! ModulationLayerPublisher::snapshot() is called by the audio thread once per
//! block inside a seqlock. It used to copy ModulationRuntime, whose entries hold
//! QPointer<AutomatableModel>: copying a QPointer increments a shared weak
//! reference count (a write the seqlock cannot retry away, into a block the
//! control thread may be freeing), and destroying the copy can free on the audio
//! thread. The view the audio thread copies is now plain bytes, and a destroyed
//! target is found through a per-slot liveness token instead.
//!
//!  * the snapshot type is trivially copyable (the pre-fix type fails this);
//!  * a destroyed target is skipped by a view taken before it died;
//!  * a republish retires the tokens of every older view;
//!  * the view path writes exactly what the control-side runtime path writes;
//!  * hasLayer() follows the authored layer, so the audio thread's
//!    pay-nothing test no longer reads the control-only layer;
//!  * a block through the view allocates nothing.

#include <QtTest>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <semaphore>
#include <thread>
#include <type_traits>
#include <utility>

#include "AllocationProbe.h"

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Engine.h"
#include "ModulationLayer.h"
#include "RealtimeScope.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

//! QCOMPARE cannot take `model->value<float>()`: the macro parses the angle
//! brackets as relational operators (ModulationTestSupport.h says the same).
float valueOf(const AutomatableModel& model) { return model.value<float>(); }

ModulatorSource sine()
{
	ModulatorSource source;
	source.shape = ModulationShape::Sine;
	source.rateHz = 1.0f;
	source.phase = 0.0f;
	source.unipolar = false;
	return source;
}

//! Publishes one entry driving @a model from source 0.
void publishOne(ModulationLayerPublisher& publisher, FloatModel* model)
{
	publisher.edit([model](ModulationLayer&, ModulationRuntime& runtime) {
		runtime = ModulationRuntime{};
		ModulationRuntime::Entry entry;
		entry.model = model;
		entry.modulator = 0;
		entry.depth = 0.25f;
		entry.base = 0.0f;
		entry.minimum = -100.0f;
		entry.maximum = 100.0f;
		runtime.entries[0] = entry;
		runtime.entryCount = 1;
		runtime.sources[0] = sine();
		runtime.sourceCount = 1;
		return true;
	});
}

} // namespace

namespace
{
class TestInstrument : public Instrument
{
public:
	TestInstrument() : Instrument(nullptr, nullptr) {}
	gui::PluginView* instantiateView(QWidget*) override { return nullptr; }
	QString nodeName() const override { return QStringLiteral("test"); }
	void saveSettings(QDomDocument&, QDomElement&) override {}
	void loadSettings(const QDomElement&) override {}
};
}

//! cluster 5186 (new-p12-159): what the probe instrument saw when its OWN teardown began.
namespace
{
ModulationLayerPublisher* g_probePublisher = nullptr;
std::uint32_t g_probeToken = 0;
bool g_probeToreDown = false;
bool g_probeGateWasClosed = false;

/*! An instrument a test can watch from its own destructor: it reports whether the modulation target
 *  that pointed at it was ALREADY closed when the teardown began.
 *
 *  A derived destructor runs BEFORE the base one, so a notification that comes from
 *  Instrument::~Instrument() arrives too late to be seen here - and too late to stop the audio
 *  thread from calling into the half-destroyed instrument. Only the owner, which knows that the
 *  teardown is about to start, can close the target in time. */
class ProbeInstrument : public Instrument
{
public:
	explicit ProbeInstrument(InstrumentTrack* track) : Instrument(track, nullptr) {}
	~ProbeInstrument() override
	{
		if (g_probeToreDown) { return; }  // only the probe under test reports
		g_probeToreDown = true;
		g_probeGateWasClosed = g_probePublisher != nullptr
			&& !g_probePublisher->targetAlive(0, g_probeToken);
	}
	gui::PluginView* instantiateView(QWidget*) override { return nullptr; }
	QString nodeName() const override { return QStringLiteral("cluster5186probe"); }
	void saveSettings(QDomDocument&, QDomElement&) override {}
	void loadSettings(const QDomElement&) override {}
};
}

class ModulationAudioViewTest : public QObject
{
	Q_OBJECT

private:
	//! A track that OWNS a probe instrument, with a published per-voice entry pointing at it.
	InstrumentTrack* trackWithProbe(ModulationLayerPublisher& publisher, AutomatableModel* model)
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		if (track == nullptr) { return nullptr; }
		auto* probe = new ProbeInstrument(track);
		track->m_instrument = probe;  // the track owns it now, as it owns a loaded instrument
		publisher.edit([model, probe](ModulationLayer&, ModulationRuntime& runtime) {
			runtime = ModulationRuntime{};
			ModulationRuntime::Entry entry;
			entry.model = model;
			entry.modulator = 0;
			entry.depth = 0.25f;
			entry.base = 0.0f;
			entry.minimum = -100.0f;
			entry.maximum = 100.0f;
			entry.instrument = probe;
			entry.noteTarget = 0;
			runtime.entries[0] = entry;
			runtime.entryCount = 1;
			runtime.sources[0] = sine();
			runtime.sources[0].perVoice = true;
			runtime.sources[0].triggerTrack = 11;
			runtime.sourceCount = 1;
			return true;
		});
		const ModulationAudioView view = publisher.snapshot();
		g_probePublisher = &publisher;
		g_probeToken = view.entries[0].token;
		return track;
	}

	//! What a project file carries: a track element with a child that names an instrument.
	static QDomElement trackElementWithInstrumentNode(const QString& nodeName)
	{
		QDomDocument doc;
		QDomElement root = doc.createElement("track");
		doc.appendChild(root);
		QDomElement node = doc.createElement(nodeName);
		node.setAttribute("name", "cluster5186probe");
		root.appendChild(node);
		return root;
	}

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase() { Engine::destroy(); }

	void theAudioSnapshotIsPlainBytes()
	{
		using Snapshot = decltype(std::declval<const ModulationLayerPublisher&>().snapshot());
		QVERIFY2(std::is_trivially_copyable_v<Snapshot>,
			"the audio thread's seqlock copy holds a non-trivial type (a QPointer)");
	}

	void targetDestructionWaitsForAnInFlightAudioWrite()
	{
		ModulationLayerPublisher publisher;
		auto* model = new FloatModel(0.0f, -100.0f, 100.0f, 0.01f);
		publishOne(publisher, model);
		const ModulationAudioView view = publisher.snapshot();
		std::binary_semaphore writeEntered{0};
		std::binary_semaphore resumeWrite{0};
		QObject::connect(model, &Model::dataChanged, this, [&] {
			writeEntered.release();
			resumeWrite.acquire();
		}, Qt::DirectConnection);

		std::thread audio([&]
		{
			const RealtimeScope realtime;
			applyModulationBlock(publisher, view, 0.25);
		});
		const bool writeReachedModel = writeEntered.try_acquire_for(std::chrono::seconds(2));
		if (!writeReachedModel)
		{
			resumeWrite.release();
			audio.join();
			delete model;
			QVERIFY2(writeReachedModel, "the modulation write did not reach the target");
		}

		std::atomic<bool> destructionFinished{false};
		std::thread destroyer([&] {
			delete model;
			destructionFinished.store(true, std::memory_order_release);
		});
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (publisher.targetAlive(0, view.entries[0].token)
			&& std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::yield();
		}
		const bool teardownEntered = !publisher.targetAlive(0, view.entries[0].token);
		const bool waitedForWriter = teardownEntered
			&& !destructionFinished.load(std::memory_order_acquire);
		resumeWrite.release();
		audio.join();
		destroyer.join();

		QVERIFY2(teardownEntered, "the destroyer did not close the target gate before teardown");
		QVERIFY2(waitedForWriter,
			"the target was destroyed while the audio thread still held its raw pointer");
		QVERIFY(!publisher.targetAlive(0, view.entries[0].token));
	}

	void rebindDuringDestructionIgnoresAStaleTargetClosure()
	{
		ModulationLayerPublisher publisher;
		auto* oldModel = new FloatModel(0.0f, -100.0f, 100.0f, 0.01f);
		std::binary_semaphore teardownEntered{0};
		std::binary_semaphore resumeTeardown{0};
		QObject::connect(oldModel, &AutomatableModel::aboutToBeDestroyed, this, [&] {
			teardownEntered.release();
			resumeTeardown.acquire();
		}, Qt::DirectConnection);
		publishOne(publisher, oldModel);
		const auto oldView = publisher.snapshot();
		const std::uint32_t oldToken = oldView.entries[0].token;

		std::thread destroyer([&] { delete oldModel; });
		if (!teardownEntered.try_acquire_for(std::chrono::seconds(2)))
		{
			resumeTeardown.release();
			destroyer.join();
			QVERIFY2(false, "the destroyer did not enter model teardown");
		}

		FloatModel replacement(0.0f, -100.0f, 100.0f, 0.01f);
		publishOne(publisher, &replacement);
		const auto newView = publisher.snapshot();
		QVERIFY(publisher.targetAlive(0, newView.entries[0].token));

		// Exercise the old callback after the new generation has been published,
		// while the old model's destructor is still held at its teardown signal.
		ModulationLayerPublisher::retireTarget(publisher.m_alive[0], publisher.m_targetWriters[0], oldToken);
		const bool reboundTargetStillOpen = publisher.targetAlive(0, newView.entries[0].token)
			&& publisher.beginTargetWrite(0, newView.entries[0].token);
		if (reboundTargetStillOpen) { publisher.endTargetWrite(0); }
		resumeTeardown.release();
		destroyer.join();
		QVERIFY2(reboundTargetStillOpen, "a stale destruction callback muted the rebound target");
	}

	void aDestroyedTargetIsSkippedByAnOlderView()
	{
		ModulationLayerPublisher publisher;
		auto* model = new FloatModel(0.0f, -100.0f, 100.0f, 0.01f);
		publishOne(publisher, model);
		const ModulationAudioView view = publisher.snapshot();
		QCOMPARE(view.entryCount, 1);
		QVERIFY(publisher.targetAlive(0, view.entries[0].token));

		applyModulationBlock(publisher, view, 0.25);
		QCOMPARE(valueOf(*model), 0.25f * 200.0f);

		delete model;
		QVERIFY2(!publisher.targetAlive(0, view.entries[0].token),
			"a destroyed target is still reported alive");
		// Must skip the entry: the raw pointer in the view is dangling now.
		applyModulationBlock(publisher, view, 0.75);
	}

	void anInstrumentTargetIsRetiredWhenDestroyed()
	{
		ModulationLayerPublisher publisher;
		auto* model = new FloatModel(0.0f, -100.0f, 100.0f, 0.01f);
		auto* instrument = new TestInstrument();
		publisher.edit([model, instrument](ModulationLayer&, ModulationRuntime& runtime) {
			runtime = ModulationRuntime{};
			ModulationRuntime::Entry entry;
			entry.model = model;
			entry.modulator = 0;
			entry.depth = 0.25f;
			entry.base = 0.0f;
			entry.minimum = -100.0f;
			entry.maximum = 100.0f;
			entry.instrument = instrument;
			entry.noteTarget = 0;
			runtime.entries[0] = entry;
			runtime.entryCount = 1;
			runtime.sources[0] = sine();
			runtime.sources[0].perVoice = true;
			runtime.sources[0].triggerTrack = 11;
			runtime.sourceCount = 1;
			return true;
		});
		publisher.voiceChanged(11, true, 0, 60, 0);
		const ModulationAudioView view = publisher.snapshot();
		QVERIFY(publisher.targetAlive(0, view.entries[0].token));

		delete instrument;
		QVERIFY2(!publisher.targetAlive(0, view.entries[0].token),
			"an instrument's destruction did not retire its modulation target");
		delete model;
	}

	//! cluster 5186 (new-p12-159): the track's own load path replaces the instrument - the target
	//! that pointed at the old one must be closed BEFORE the old instrument tears down.
	void aLoadedInstrumentReplacementClosesTheTargetBeforeTheInstrumentTearsDown()
	{
		ModulationLayerPublisher publisher;
		auto* model = new FloatModel(0.0f, -100.0f, 100.0f, 0.01f);
		InstrumentTrack* track = trackWithProbe(publisher, model);
		QVERIFY(track != nullptr);
		QVERIFY2(publisher.targetAlive(0, g_probeToken), "the published target was not alive");

		// The file names an instrument: the track replaces the one it holds.
		track->loadTrackSpecificSettings(trackElementWithInstrumentNode("instrument"));

		QVERIFY2(g_probeToreDown, "the track did not replace the instrument it held");
		QVERIFY2(g_probeGateWasClosed,
			"the modulation target was still open when the instrument's own teardown began");
		delete model;
	}

	//! The same path, through the legacy plug-in node the loader still accepts.
	void aLegacyInstrumentReplacementClosesTheTargetBeforeTheInstrumentTearsDown()
	{
		ModulationLayerPublisher publisher;
		auto* model = new FloatModel(0.0f, -100.0f, 100.0f, 0.01f);
		InstrumentTrack* track = trackWithProbe(publisher, model);
		QVERIFY(track != nullptr);
		QVERIFY2(publisher.targetAlive(0, g_probeToken), "the published target was not alive");

		// A node that is not one of the known ones: the loader reads it as an instrument plug-in.
		track->loadTrackSpecificSettings(trackElementWithInstrumentNode("cluster5186legacy"));

		QVERIFY2(g_probeToreDown, "the track did not replace the instrument it held");
		QVERIFY2(g_probeGateWasClosed,
			"the modulation target was still open when the instrument's own teardown began");
		delete model;
	}

	void aRepublishRetiresEveryOlderView()
	{
		ModulationLayerPublisher publisher;
		FloatModel model(0.0f, -100.0f, 100.0f, 0.01f);
		publishOne(publisher, &model);
		const ModulationAudioView before = publisher.snapshot();
		publishOne(publisher, &model);
		const ModulationAudioView after = publisher.snapshot();
		QVERIFY(!publisher.targetAlive(0, before.entries[0].token));
		QVERIFY(publisher.targetAlive(0, after.entries[0].token));

		model.setValue(7.0f);
		applyModulationBlock(publisher, before, 0.25);
		QCOMPARE(valueOf(model), 7.0f);
	}

	/*! An edit that rebinds a target closes that slot and waits for the audio
	 *  writes that pinned it. That wait belongs to the writer and the edit, not
	 *  to the audio thread: while the reader-visible version is odd, every
	 *  snapshot() retries, so an edit that waits for a writer must not hold the
	 *  version odd while it waits - the audio thread cannot bound how long an
	 *  edit waits for a writer. */
	void anEditWaitingForAnActiveWriterKeepsTheReaderVisibleVersionEven()
	{
		ModulationLayerPublisher publisher;
		FloatModel model(0.0f, -100.0f, 100.0f, 0.01f);
		publishOne(publisher, &model);
		const ModulationAudioView view = publisher.snapshot();
		const std::uint32_t token = view.entries[0].token;

		std::binary_semaphore pinned{0};
		std::atomic<bool> pinTaken{false};
		std::atomic<unsigned> versionWhileWaiting{2};
		std::atomic<int> entriesWhileWaiting{-1};

		std::thread audio([&] {
			if (!publisher.beginTargetWrite(0, token)) { return; }
			pinTaken.store(true, std::memory_order_release);
			pinned.release();
			// The control thread closes the slot before it waits for this pin,
			// and it cannot finish that wait until this thread releases the
			// pin. So the moment the slot reads closed is inside the wait, and
			// that is where the reader-visible version is read.
			while (publisher.targetAlive(0, token)) { std::this_thread::yield(); }
			const unsigned version = publisher.m_version.load(std::memory_order_acquire);
			versionWhileWaiting.store(version, std::memory_order_release);
			if ((version & 1u) == 0u)
			{
				// The version is even: the snapshot returns the last published
				// view at once instead of retrying.
				entriesWhileWaiting.store(publisher.snapshot().entryCount, std::memory_order_release);
			}
			publisher.endTargetWrite(0);
		});

		pinned.acquire();
		QVERIFY(pinTaken.load(std::memory_order_acquire));
		// The edit rebinds the target: publishView() closes the slot and waits
		// for the writer to release its pin.
		publisher.edit([&model](ModulationLayer&, ModulationRuntime& runtime) {
			runtime = ModulationRuntime{};
			ModulationRuntime::Entry entry;
			entry.model = &model;
			entry.modulator = 0;
			entry.depth = 0.25f;
			entry.base = 0.0f;
			entry.minimum = -100.0f;
			entry.maximum = 100.0f;
			runtime.entries[0] = entry;
			runtime.entryCount = 1;
			runtime.sources[0] = sine();
			runtime.sourceCount = 1;
			return true;
		});
		audio.join();

		QVERIFY2((versionWhileWaiting.load(std::memory_order_acquire) & 1u) == 0u,
			"the reader-visible version was odd while the edit waited for an active "
			"writer: the audio thread's snapshot() spins for as long as the edit takes");
		QCOMPARE(entriesWhileWaiting.load(std::memory_order_acquire), 1);
	}

	void theViewWritesWhatTheRuntimeWrites()
	{
		ModulationLayerPublisher publisher;
		FloatModel viaView(0.0f, -100.0f, 100.0f, 0.01f);
		FloatModel viaRuntime(0.0f, -100.0f, 100.0f, 0.01f);
		publishOne(publisher, &viaView);
		const ModulationAudioView view = publisher.snapshot();
		ModulationRuntime runtime = publisher.runtime();
		runtime.entries[0].model = &viaRuntime;
		for (double seconds : {0.0, 0.1, 0.25, 0.5, 0.6, 0.75, 0.9})
		{
			applyModulationBlock(publisher, view, seconds);
			applyModulationBlock(runtime, seconds);
			QCOMPARE(valueOf(viaView), valueOf(viaRuntime));
		}
	}

	void hasLayerFollowsTheAuthoredLayer()
	{
		ModulationLayerPublisher publisher;
		QVERIFY(!publisher.hasLayer());
		publisher.edit([](ModulationLayer& layer, ModulationRuntime&) {
			return layer.addModulator(QStringLiteral("lfo"), sine()) >= 0;
		});
		QVERIFY(publisher.hasLayer());
		publisher.edit([](ModulationLayer& layer, ModulationRuntime&) {
			layer.clear();
			return true;
		});
		QVERIFY(!publisher.hasLayer());
	}

	void voicesFromTracksNoLongerFollowedAreDiscarded()
	{
		ModulationLayerPublisher publisher;
		auto publishTriggers = [&publisher](int firstTrack) {
			publisher.edit([firstTrack](ModulationLayer&, ModulationRuntime& runtime) {
				runtime = ModulationRuntime{};
				runtime.sourceCount = 2;
				runtime.sources[0].triggerTrack = firstTrack;
				runtime.sources[0].perVoice = true;
				runtime.sources[1].triggerTrack = 22;
				runtime.sources[1].perVoice = true;
				return true;
			});
		};

		publishTriggers(11);
		publisher.voiceChanged(11, true, 0, 60, 0);
		publisher.voiceChanged(22, true, 0, 62, 0);
		QCOMPARE(publisher.voices().activeCount(), 2);

		publishTriggers(33);
		publisher.voiceChanged(11, false, 0, 60, 0);  // No longer followed, so this note-off is ignored.
		publishTriggers(11);

		bool staleVoice = false;
		for (int slot = 0; slot < ModulationVoices::VoiceCount; ++slot)
		{
			ModulationVoice voice;
			staleVoice = staleVoice || (publisher.voices().voice(slot, &voice) && voice.track == 11);
		}
		QVERIFY(!staleVoice);
		QCOMPARE(publisher.voices().activeCount(), 1);
	}

	void aBlockThroughTheViewAllocatesNothing()
	{
		ModulationLayerPublisher publisher;
		FloatModel model(0.0f, -100.0f, 100.0f, 0.01f);
		publishOne(publisher, &model);

		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		for (int block = 0; block < 64; ++block)
		{
			const ModulationAudioView view = publisher.snapshot();
			applyModulationBlock(publisher, view, 0.001 * block);
		}
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		QCOMPARE(allocations, std::uint64_t{0});
	}
};

QTEST_GUILESS_MAIN(ModulationAudioViewTest)
#include "ModulationAudioViewTest.moc"
