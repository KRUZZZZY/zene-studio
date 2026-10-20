/*
 * NeuralAmpModelLoadTest.cpp - selecting a Neural Amp model never waits behind a load (Q050-030)
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

// The effect is compiled into this executable with ZENE_NAM_TEST_SEAM, which adds one hook that runs on
// the worker before each file is parsed. The tests use it to hold a load open; the shipped module has no
// such hook. The models are tiny real .nam files the real loader parses, and the model that ends up
// published is read back from the effect's own status text (it carries the file's metadata name).

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

#include "../core/AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "NeuralAmpEffect.h"
#include "SampleFrame.h"
#include "nam/NamModel.h"

using namespace lmms;
using Clock = std::chrono::steady_clock;

namespace
{

//! Holds every load whose path contains "hold" until release(); records the order loads began in.
struct Gate
{
	std::mutex mutex;
	std::condition_variable cv;
	bool open = true;
	std::vector<QString> started;
	std::atomic<int> holding{0};

	void reset()
	{
		std::lock_guard<std::mutex> lock(mutex);
		open = true;
		started.clear();
		holding = 0;
	}
	void close() { std::lock_guard<std::mutex> lock(mutex); open = false; }
	void release()
	{
		{ std::lock_guard<std::mutex> lock(mutex); open = true; }
		cv.notify_all();
	}
	void onLoad(const QString& path)
	{
		std::unique_lock<std::mutex> lock(mutex);
		started.push_back(QFileInfo(path).baseName());
		if (path.contains("hold"))
		{
			++holding;
			cv.wait(lock, [this] { return open; });
			--holding;
		}
	}
	int startedCount()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return static_cast<int>(started.size());
	}
};

Gate g_gate;

void beforeLoad(const QString& path) { g_gate.onLoad(path); }

//! A one-layer WaveNet with 8 weights, named \a name so the status text identifies it.
void writeModel(const QString& path, const QString& name)
{
	QFile file(path);
	QVERIFY2(file.open(QIODevice::WriteOnly), "cannot write the fixture model");
	file.write(QStringLiteral(
		"{\"version\":\"0.5.0\",\"architecture\":\"WaveNet\",\"metadata\":{\"name\":\"%1\"},"
		"\"config\":{\"layers\":[{\"input_size\":1,\"condition_size\":1,\"head_size\":1,\"head_bias\":false,"
		"\"channels\":1,\"kernel_size\":1,\"dilations\":[1],\"activation\":\"Tanh\"}],\"head\":null},"
		"\"weights\":[0.5,0.1,0.2,0.3,0.4,0.1,0.5,0.9]}").arg(name).toUtf8());
}

template<typename Pred>
bool waitFor(Pred pred, int ms = 10000)
{
	const auto end = Clock::now() + std::chrono::milliseconds(ms);
	while (Clock::now() < end)
	{
		if (pred()) { return true; }
		QTest::qWait(2);
	}
	return pred();
}

double msSince(Clock::time_point t)
{
	return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

} // namespace

class NeuralAmpModelLoadTest : public QObject
{
	Q_OBJECT
private:
	QTemporaryDir m_dir;
	QString path(const QString& base) const { return m_dir.filePath(base + ".nam"); }

	bool loaded(NeuralAmpEffect& e, const QString& name)
	{
		return e.modelStatus().startsWith("loaded " + name + " ");
	}

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		for (const char* n : {"A_hold", "B", "C", "D", "E"})
		{
			writeModel(path(n), n);
			if (QTest::currentTestFailed()) { return; }
		}
		NeuralAmpEffect::s_testBeforeLoad = &beforeLoad;
	}

	void cleanupTestCase()
	{
		NeuralAmpEffect::s_testBeforeLoad = nullptr;
		Engine::destroy();
	}

	void init() { g_gate.reset(); }

	// Acceptance 1: the selecting thread returns long before the slow load is done.
	void selectingWhileASlowLoadRunsReturnsPromptly()
	{
		constexpr double kSlowLoadMs = 1500.0;
		NeuralAmpEffect effect(nullptr, nullptr);
		g_gate.close();
		effect.setModelPath(path("A_hold"));
		QVERIFY(waitFor([] { return g_gate.holding.load() == 1; }));

		const auto t0 = Clock::now();
		effect.setModelPath(path("B"));
		const double firstMs = msSince(t0);
		const auto t1 = Clock::now();
		effect.setModelPath(path("C"));
		const double secondMs = msSince(t1);

		// The slow load is still held; the selections returned without waiting for it.
		QCOMPARE(g_gate.holding.load(), 1);
		QVERIFY2(firstMs < kSlowLoadMs / 10.0, qPrintable(QString::number(firstMs)));
		QVERIFY2(secondMs < kSlowLoadMs / 10.0, qPrintable(QString::number(secondMs)));

		// Let the slow load take its (modelled) time, then finish.
		QTest::qWait(static_cast<int>(kSlowLoadMs));
		QCOMPARE(g_gate.holding.load(), 1);
		g_gate.release();
		QVERIFY2(waitFor([&] { return loaded(effect, "C"); }), qPrintable(effect.modelStatus()));
	}

	// Acceptance 2a: A then B then C quickly ends on C; the superseded B is never parsed and A's
	// result is never published over C.
	void theFinalRequestedModelWins()
	{
		NeuralAmpEffect effect(nullptr, nullptr);
		g_gate.close();
		effect.setModelPath(path("A_hold"));
		QVERIFY(waitFor([] { return g_gate.holding.load() == 1; }));
		effect.setModelPath(path("B"));
		effect.setModelPath(path("C"));
		g_gate.release();
		QVERIFY2(waitFor([&] { return loaded(effect, "C"); }), qPrintable(effect.modelStatus()));
		QTest::qWait(100);
		QVERIFY(loaded(effect, "C"));
		std::lock_guard<std::mutex> lock(g_gate.mutex);
		QCOMPARE(g_gate.started, (std::vector<QString>{"A_hold", "C"}));
	}

	// Acceptance 2b: many rapid selections, then the effect is deleted while a load is held open. The
	// destructor returns once the held load ends, with no publication afterwards and no worker left.
	void rapidSelectionsAndDeletionMidLoadLeaveNoWorker()
	{
		for (int round = 0; round < 20; ++round)
		{
			auto effect = std::make_unique<NeuralAmpEffect>(nullptr, nullptr);
			const char* names[] = {"B", "C", "D", "E"};
			for (int i = 0; i < 50; ++i) { effect->setModelPath(path(names[i % 4])); }
			if (round % 2 == 0)
			{
				QVERIFY(waitFor([&] { return loaded(*effect, "C"); }));
			}
			// the 50th request (i = 49) is "C"; odd rounds delete while loads are still in flight
			effect.reset();
		}

		// Deleted while a load is held: the destructor waits for that one parse, then returns.
		g_gate.reset();
		g_gate.close();
		auto effect = std::make_unique<NeuralAmpEffect>(nullptr, nullptr);
		effect->setModelPath(path("A_hold"));
		QVERIFY(waitFor([] { return g_gate.holding.load() == 1; }));
		effect->setModelPath(path("B"));
		std::atomic<bool> destroyed{false};
		std::thread deleter([&] { effect.reset(); destroyed = true; });
		QTest::qWait(100);
		QVERIFY(!destroyed.load());
		g_gate.release();
		deleter.join();
		QVERIFY(destroyed.load());
		// B was dropped with the effect, never parsed.
		std::lock_guard<std::mutex> lock(g_gate.mutex);
		QCOMPARE(g_gate.started, (std::vector<QString>{"A_hold"}));
	}

	// The audio thread allocates nothing, locks nothing and never sees a half-built model while models
	// are swapped under it, and the output stays finite.
	void audioThreadAllocatesNothingWhileModelsSwap()
	{
		NeuralAmpEffect effect(nullptr, nullptr);
		effect.setModelPath(path("B"));
		QVERIFY(waitFor([&] { return loaded(effect, "B"); }));

		std::atomic<bool> stop{false};
		std::thread selector([&] {
			const char* names[] = {"B", "C", "D", "E"};
			int i = 0;
			while (!stop.load())
			{
				effect.setModelPath(path(names[i++ % 4]));
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		});

		std::vector<SampleFrame> buf(256, SampleFrame(0.25f, 0.25f));
		std::uint64_t allocations = 0;
		bool finite = true;
		for (int block = 0; block < 4000; ++block)
		{
			test::resetAllocationCount();
			test::tlCountAllocations = true;
			effect.processImpl(buf.data(), static_cast<f_cnt_t>(buf.size()));
			test::tlCountAllocations = false;
			allocations += test::tlAllocationCount;
			finite = finite && std::isfinite(buf[0][0]) && std::isfinite(buf[255][1]);
			for (auto& f : buf) { f = SampleFrame(0.25f, 0.25f); }
		}
		stop = true;
		selector.join();
		QCOMPARE(allocations, std::uint64_t{0});
		QVERIFY(finite);
	}

	// new-p12-163-port: a layer with a non-positive dilation is rejected at parse time, so it can
	// never reach the convolution history (the patrol's repro: kernel 3, dilation -1). The same
	// fixture with a positive dilation is the control that the weights themselves are well-formed.
	void nonPositiveDilationIsRejectedByTheLoader()
	{
		const auto writeDilated = [this](const QString& base, const QString& dilations) {
			QFile file(path(base));
			if (!file.open(QIODevice::WriteOnly)) { return false; }
			file.write(QStringLiteral(
				"{\"version\":\"0.5.0\",\"architecture\":\"WaveNet\",\"metadata\":{\"name\":\"%1\"},"
				"\"config\":{\"layers\":[{\"input_size\":1,\"condition_size\":1,\"head_size\":1,\"head_bias\":false,"
				"\"channels\":1,\"kernel_size\":3,\"dilations\":[%2],\"activation\":\"Tanh\"}],\"head\":null},"
				"\"weights\":[0.5,0.1,0.2,0.3,0.4,0.1,0.5,0.9,0.7,0.6]}").arg(base, dilations).toUtf8());
			return true;
		};

		QVERIFY(writeDilated("dilation_pos", QStringLiteral("1")));
		std::string posError;
		auto posModel = nam::NamModel::loadFromFile(path("dilation_pos").toStdString(), &posError);
		QVERIFY2(posModel != nullptr, posError.c_str());
		QVERIFY(posError.empty());

		QVERIFY(writeDilated("dilation_neg", QStringLiteral("-1")));
		std::string negError;
		auto negModel = nam::NamModel::loadFromFile(path("dilation_neg").toStdString(), &negError);
		QVERIFY(negModel == nullptr);
		QVERIFY(!negError.empty());

		QVERIFY(writeDilated("dilation_zero", QStringLiteral("0")));
		std::string zeroError;
		auto zeroModel = nam::NamModel::loadFromFile(path("dilation_zero").toStdString(), &zeroError);
		QVERIFY(zeroModel == nullptr);
		QVERIFY(!zeroError.empty());
	}
};

QTEST_GUILESS_MAIN(NeuralAmpModelLoadTest)
#include "NeuralAmpModelLoadTest.moc"
