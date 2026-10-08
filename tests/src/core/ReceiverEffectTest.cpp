/*
 * ReceiverEffectTest.cpp - the Receiver device mixes a sidechain input into a chain, and InsertRecorder passes its input
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

/*! The Receiver module (plugins/Receiver) is the device chain.receive places: it adds the chain's sidechain input
 *  to the signal at 'level', keeping the chain's own signal or ('replace') standing in for it. Driven here through
 *  the product's own EffectChain with a sidechain buffer handed in, as the mixer hands one in - the arithmetic is
 *  measured on real buffers, the controls round-trip through the project's save/load, and a chain with no sidechain
 *  leaves the signal as it was. InsertRecorder (plugins/InsertRecorder) is loaded the same way and must pass its
 *  input through unchanged while exposing its recorder. */

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>

#include "AudioBuffer.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectControls.h"
#include "Engine.h"

using namespace lmms;

namespace
{

constexpr f_cnt_t Frames = 64;

//! A stereo block (interleaved, as the device reads it) holding @a left / @a right in every frame.
AudioBuffer stereoBlock(float left, float right)
{
	AudioBuffer buffer{Frames, DEFAULT_CHANNELS};
	buffer.allocateInterleavedBuffer();
	for (f_cnt_t f = 0; f < Frames; ++f)
	{
		buffer.interleavedBuffer()[f][0] = left;
		buffer.interleavedBuffer()[f][1] = right;
	}
	buffer.assumeNonSilent(0);
	buffer.assumeNonSilent(1);
	return buffer;
}

//! A planar sidechain block holding @a left / @a right on its two channels.
AudioBuffer sidechainBlock(float left, float right)
{
	AudioBuffer buffer{Frames, DEFAULT_CHANNELS};
	for (f_cnt_t f = 0; f < Frames; ++f)
	{
		buffer.buffer(0)[f] = left;
		buffer.buffer(1)[f] = right;
	}
	buffer.assumeNonSilent(0);
	buffer.assumeNonSilent(1);
	return buffer;
}

//! The settings element a Receiver's controls read: its level and replace switch.
QDomElement settings(QDomDocument& doc, float level, bool replace)
{
	QDomElement element = doc.createElement(QStringLiteral("ReceiverControls"));
	element.setAttribute(QStringLiteral("level"), static_cast<double>(level));
	element.setAttribute(QStringLiteral("replace"), replace ? 1 : 0);
	return element;
}

} // namespace

class ReceiverEffectTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
	}

	void cleanupTestCase() { Engine::destroy(); }

	void aChainMixesItsSidechainInAtTheLevel()
	{
		EffectChain chain{nullptr};
		Effect* receiver = Effect::instantiate(QStringLiteral("receiver"), &chain, nullptr);
		if (receiver == nullptr) { QSKIP("this build has no Receiver module"); }
		chain.appendEffect(receiver);

		QDomDocument doc;
		receiver->controls()->restoreState(settings(doc, 0.5f, false));
		AudioBuffer block = stereoBlock(0.25f, -0.25f);
		const AudioBuffer received = sidechainBlock(0.5f, 0.25f);
		chain.processAudioBuffer(block, &received);
		for (f_cnt_t f = 0; f < Frames; ++f)
		{
			QCOMPARE(block.interleavedBuffer()[f][0], 0.5f);    // 0.25 + 0.5 * 0.5
			QCOMPARE(block.interleavedBuffer()[f][1], -0.125f); // -0.25 + 0.25 * 0.5
		}
		chain.clear();
	}

	void replaceStandsInForTheChainsOwnSignal()
	{
		EffectChain chain{nullptr};
		Effect* receiver = Effect::instantiate(QStringLiteral("receiver"), &chain, nullptr);
		if (receiver == nullptr) { QSKIP("this build has no Receiver module"); }
		chain.appendEffect(receiver);

		QDomDocument doc;
		receiver->controls()->restoreState(settings(doc, 1.f, true));
		AudioBuffer block = stereoBlock(0.9f, 0.9f);
		const AudioBuffer received = sidechainBlock(0.5f, 0.25f);
		chain.processAudioBuffer(block, &received);
		QCOMPARE(block.interleavedBuffer()[0][0], 0.5f);
		QCOMPARE(block.interleavedBuffer()[Frames - 1][1], 0.25f);
		chain.clear();
	}

	//! With no sidechain input the device hears nothing from outside: the own signal passes, or silence on replace.
	void withoutASidechainTheOwnSignalPassesOrSilenceOnReplace()
	{
		EffectChain chain{nullptr};
		Effect* receiver = Effect::instantiate(QStringLiteral("receiver"), &chain, nullptr);
		if (receiver == nullptr) { QSKIP("this build has no Receiver module"); }
		chain.appendEffect(receiver);

		QDomDocument doc;
		receiver->controls()->restoreState(settings(doc, 1.f, false));
		AudioBuffer kept = stereoBlock(0.25f, -0.5f);
		chain.processAudioBuffer(kept, nullptr);
		QCOMPARE(kept.interleavedBuffer()[3][0], 0.25f);
		QCOMPARE(kept.interleavedBuffer()[3][1], -0.5f);

		receiver->controls()->restoreState(settings(doc, 1.f, true));
		AudioBuffer replaced = stereoBlock(0.25f, -0.5f);
		chain.processAudioBuffer(replaced, nullptr);
		QCOMPARE(replaced.interleavedBuffer()[3][0], 0.f);
		QCOMPARE(replaced.interleavedBuffer()[3][1], 0.f);
		chain.clear();
	}

	void theControlsSaveAndLoadTheirTwoSettings()
	{
		EffectChain chain{nullptr};
		Effect* receiver = Effect::instantiate(QStringLiteral("receiver"), &chain, nullptr);
		if (receiver == nullptr) { QSKIP("this build has no Receiver module"); }
		chain.appendEffect(receiver);
		QCOMPARE(receiver->controls()->controlCount(), 2);
		QCOMPARE(receiver->controls()->nodeName(), QStringLiteral("ReceiverControls"));

		QDomDocument doc;
		receiver->controls()->restoreState(settings(doc, 0.75f, true));
		QDomElement parent = doc.createElement(QStringLiteral("root"));
		doc.appendChild(parent);
		const QDomElement saved = receiver->controls()->saveState(doc, parent);
		QCOMPARE(saved.attribute(QStringLiteral("level")).toFloat(), 0.75f);
		QCOMPARE(saved.attribute(QStringLiteral("replace")).toInt(), 1);

		// A second Receiver given the saved element behaves as the first: replace at 0.75.
		Effect* second = Effect::instantiate(QStringLiteral("receiver"), &chain, nullptr);
		QVERIFY(second != nullptr);
		second->controls()->restoreState(saved);
		QDomElement again = doc.createElement(QStringLiteral("again"));
		doc.appendChild(again);
		const QDomElement resaved = second->controls()->saveState(doc, again);
		QCOMPARE(resaved.attribute(QStringLiteral("level")).toFloat(), 0.75f);
		QCOMPARE(resaved.attribute(QStringLiteral("replace")).toInt(), 1);
		delete second;
		chain.clear();
	}

	//! InsertRecorder records the signal passing it and passes it on unchanged.
	void theInsertRecorderPassesItsInputThrough()
	{
		EffectChain chain{nullptr};
		Effect* recorder = Effect::instantiate(QStringLiteral("insertrecorder"), &chain, nullptr);
		if (recorder == nullptr) { QSKIP("this build has no InsertRecorder module"); }
		chain.appendEffect(recorder);
		QVERIFY(recorder->controls() != nullptr);

		AudioBuffer block = stereoBlock(0.125f, -0.375f);
		chain.processAudioBuffer(block, nullptr);
		QCOMPARE(block.interleavedBuffer()[0][0], 0.125f);
		QCOMPARE(block.interleavedBuffer()[Frames - 1][1], -0.375f);

		// Its state is the last take: nothing recorded, so nothing saved.
		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("root"));
		doc.appendChild(parent);
		const QDomElement saved = recorder->controls()->saveState(doc, parent);
		QVERIFY(saved.firstChildElement(QStringLiteral("last-take")).isNull());
		chain.clear();
	}
};

QTEST_GUILESS_MAIN(ReceiverEffectTest)
#include "ReceiverEffectTest.moc"
