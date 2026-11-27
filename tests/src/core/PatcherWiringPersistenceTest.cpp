/*
 * PatcherWiringPersistenceTest.cpp - an EffectChain's AUTHORED patch wiring
 * (EffectChain::setPatchWiring) survives saveSettings/loadSettings.
 *
 * WHAT IS PROVEN HERE:
 *   * a non-linear authored wiring is written by saveSettings and restored,
 *     edge for edge, by loadSettings into a fresh chain;
 *   * loading XML that carries no <patchwiring> puts the chain back on its
 *     derived wiring, even when it had an authored one before;
 *   * a chain on its derived wiring writes no <patchwiring> child, so old
 *     projects keep their format;
 *   * a malformed <patchwiring> loads without crashing and leaves the chain on
 *     its derived wiring.
 *
 * SETUP: the chains are built from a legacy test effect (no audio-ports model),
 * so the routing graph is live. Reloading goes through Effect::instantiate by
 * name, which does not know the test effect: loadSettings hands back a
 * DummyEffect per <effect> element. A DummyEffect has no audio-ports model
 * either, so the loaded chain's graph is live too and can host the wiring.
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

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QString>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectControls.h"
#include "Engine.h"
#include "PatchWiring.h"
#include "Plugin.h"

namespace lmms
{

namespace
{

//! Controls with nothing to save: Effect::saveSettings dereferences controls().
class WiringPersistenceControls final : public EffectControls
{
public:
	explicit WiringPersistenceControls(Effect* effect) : EffectControls{effect} {}

	int controlCount() override { return 0; }
	void saveSettings(QDomDocument&, QDomElement&) override {}
	void loadSettings(const QDomElement&) override {}
	QString nodeName() const override { return QStringLiteral("WiringPersistenceControls"); }
	gui::EffectControlDialog* createView() override { return nullptr; }
};

//! A legacy Effect (no audio-ports model), so a chain of these builds its graph.
class WiringPersistenceEffect : public Effect
{
public:
	explicit WiringPersistenceEffect(Model* parent)
		: Effect{&s_descriptor, parent, nullptr}, m_controls{this} {}

	EffectControls* controls() override { return &m_controls; }

protected:
	ProcessStatus processImpl(SampleFrame*, const f_cnt_t) override { return ProcessStatus::Continue; }

private:
	WiringPersistenceControls m_controls;
	static const Plugin::Descriptor s_descriptor;
};

const Plugin::Descriptor WiringPersistenceEffect::s_descriptor
{
	"wiringpersistencetest",
	"Wiring persistence test effect",
	"Silent legacy effect used by PatcherWiringPersistenceTest",
	"LMMS",
	0x0100,
	Plugin::Type::Effect,
	nullptr,
	nullptr,
	nullptr
};

PatchEdge edge(const PatchRef& from, const PatchRef& to)
{
	PatchEdge e;
	e.from = from;
	e.to = to;
	return e;
}

//! input -> effect:1 -> effect:0, leaving through effect:0 - NOT the linear wiring.
PatchWiring reversedWiring()
{
	PatchWiring wiring;
	wiring.addEdge(edge(PatchRef::input(), PatchRef::effect(1)));
	wiring.addEdge(edge(PatchRef::effect(1), PatchRef::effect(0)));
	wiring.setOutput(PatchRef::effect(0));
	return wiring;
}

//! Two effects on @a chain, graph live.
void fill(EffectChain& chain)
{
	chain.appendEffect(new WiringPersistenceEffect(&chain));
	chain.appendEffect(new WiringPersistenceEffect(&chain));
}

//! @a chain's settings as an <fxchain> element of @a doc.
QDomElement save(EffectChain& chain, QDomDocument& doc)
{
	QDomElement element = doc.createElement(QStringLiteral("fxchain"));
	doc.appendChild(element);
	chain.saveSettings(doc, element);
	return element;
}

} // namespace

} // namespace lmms

using namespace lmms;

class PatcherWiringPersistenceTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The dummy device thread renders in the background; nothing here needs it.
		Engine::audioEngine()->audioDev()->stopProcessing();
		QVERIFY(Engine::audioEngine()->framesPerPeriod() != 0);
	}

	void cleanupTestCase() { Engine::destroy(); }

	void authoredWiringSurvivesSaveAndLoad()
	{
		EffectChain chain{nullptr};
		fill(chain);
		const PatchWiring wiring = reversedWiring();
		QString error;
		QVERIFY2(chain.setPatchWiring(wiring, &error), qPrintable(error));

		QDomDocument doc;
		const QDomElement element = save(chain, doc);

		EffectChain loaded{nullptr};
		fill(loaded);
		loaded.loadSettings(element);

		// DummyEffects stand in for the test effect, and the graph is still live.
		QCOMPARE(loaded.effects().size(), std::size_t{2});
		QVERIFY(loaded.routesThroughGraph());
		QVERIFY2(loaded.patchActive(), "the authored wiring was not restored by loadSettings");
		QVERIFY2(loaded.patchWiring().equals(wiring), "the restored wiring differs from the authored one");
		QVERIFY(!loaded.patchDropped());
	}

	void loadingXmlWithoutWiringRestoresDerivation()
	{
		EffectChain derived{nullptr};
		fill(derived);
		QDomDocument doc;
		const QDomElement element = save(derived, doc);

		EffectChain chain{nullptr};
		fill(chain);
		QString error;
		QVERIFY2(chain.setPatchWiring(reversedWiring(), &error), qPrintable(error));
		QVERIFY(chain.patchActive());

		chain.loadSettings(element);

		QVERIFY(chain.routesThroughGraph());
		QVERIFY2(!chain.patchActive(), "XML without a <patchwiring> left the old authored wiring in force");
	}

	void derivedChainSavesNoWiringElement()
	{
		EffectChain chain{nullptr};
		fill(chain);
		QVERIFY(!chain.patchActive());

		QDomDocument doc;
		const QDomElement element = save(chain, doc);

		QVERIFY(element.elementsByTagName(QStringLiteral("patchwiring")).isEmpty());
	}

	void malformedWiringFallsBackToDerivation()
	{
		EffectChain source{nullptr};
		fill(source);
		QDomDocument doc;
		QDomElement element = save(source, doc);

		QDomElement wiring = doc.createElement(QStringLiteral("patchwiring"));
		wiring.setAttribute(QStringLiteral("output"), QStringLiteral("effect:0"));
		QDomElement bad = doc.createElement(QStringLiteral("edge"));
		bad.setAttribute(QStringLiteral("from"), QStringLiteral("bogus"));
		bad.setAttribute(QStringLiteral("fromport"), 0);
		bad.setAttribute(QStringLiteral("to"), QStringLiteral("effect:1"));
		bad.setAttribute(QStringLiteral("toport"), 0);
		wiring.appendChild(bad);
		element.appendChild(wiring);

		EffectChain loaded{nullptr};
		fill(loaded);
		loaded.loadSettings(element);

		QVERIFY(loaded.routesThroughGraph());
		QVERIFY2(!loaded.patchActive(), "a malformed <patchwiring> must leave the chain on its derived wiring");
	}
};

QTEST_GUILESS_MAIN(PatcherWiringPersistenceTest)
#include "PatcherWiringPersistenceTest.moc"
