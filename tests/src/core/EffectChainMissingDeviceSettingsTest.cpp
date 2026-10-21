/*
 * EffectChainMissingDeviceSettingsTest.cpp - what a chain saves for a device
 * whose plug-in is missing.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! new-p14-97: a device whose plug-in binary is missing is written back as
 *  the FILE's own XML, and that copy carried none of the placeholder's
 *  generic settings: a bypass the user set on it (or its auto-quit knob, or
 *  its device options) was dropped by the next save, and the file kept the
 *  value it had.
 *
 *  The devices are the test's own (an Effect subclass and the chain's own
 *  DummyEffect), so every case is deterministic and needs no plug-in
 *  directory: the file's XML is a literal, and the settings are read back
 *  from the element the chain wrote.
 */

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QString>

#include "DummyEffect.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Plugin.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

//! A device whose plug-in binary is missing: the wrapper a hosted plug-in
//! leaves behind, with the file's own XML kept beside it.
class UnavailableTestEffect : public Effect
{
public:
	UnavailableTestEffect(Model* parent) :
		Effect{&s_descriptor, parent, nullptr},
		m_controls(this)
	{
	}

	EffectControls* controls() override { return &m_controls; }

	QString unavailableReason() const override
	{
		return QStringLiteral("the plug-in binary is not available");
	}

protected:
	ProcessStatus processImpl(SampleFrame*, const f_cnt_t) override { return ProcessStatus::Sleep; }

private:
	DummyEffectControls m_controls;

	static const Plugin::Descriptor s_descriptor;
};

const Plugin::Descriptor UnavailableTestEffect::s_descriptor
{
	"unavailabletesteffect",
	"Unavailable test effect",
	"A device whose plug-in binary is missing",
	"LMMS",
	0x0100,
	Plugin::Type::Effect,
	nullptr,
	nullptr,
	nullptr
};

//! The XML a file held for a device whose plug-in is missing.
QDomElement preservedDeviceXml(QDomDocument& file)
{
	if (!file.setContent(QStringLiteral(
			"<effect name=\"ghost\" wet=\"1\" on=\"1\"><ghost><state>QUJD</state></ghost></effect>")))
	{
		return QDomElement();
	}
	return file.documentElement();
}

//! The effect element the chain wrote for the one device it holds.
QDomElement savedEffectElement(const QDomElement& chainElement)
{
	return chainElement.firstChildElement(QStringLiteral("effect"));
}

} // namespace

class EffectChainMissingDeviceSettingsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! new-p14-97: the bypass the user set on a device whose plug-in binary
	//! is missing is saved with the file's own XML, not dropped by it.
	void theBypassOfAnUnavailableDeviceIsSaved()
	{
		EffectChain chain{nullptr};
		auto* device = new UnavailableTestEffect(&chain);
		chain.appendEffect(device);

		QDomDocument file;
		device->setPreservedPluginData(preservedDeviceXml(file));

		// the user bypasses the placeholder while the plug-in is missing
		device->setEnabled(false);

		QDomDocument doc;
		QDomElement chainElement = doc.createElement(QStringLiteral("fxchain"));
		chain.saveSettings(doc, chainElement);

		const QDomElement saved = savedEffectElement(chainElement);
		QVERIFY2(!saved.isNull(), "the chain saved no effect element");
		// the device's own XML is kept verbatim ...
		QCOMPARE(saved.firstChildElement(QStringLiteral("ghost"))
				.firstChildElement(QStringLiteral("state")).text(),
			QStringLiteral("QUJD"));
		// ... and the generic settings the user changed are written with it
		QCOMPARE(saved.attribute(QStringLiteral("on")), QStringLiteral("0"));
	}

	//! The same for the placeholder the chain makes for a device whose module
	//! is missing: the file's XML is kept as it was, and the bypass the user
	//! set on the placeholder is saved with it.
	void theBypassOfAMissingDeviceIsSaved()
	{
		EffectChain chain{nullptr};
		QDomDocument file;
		auto* placeholder = new DummyEffect(&chain, preservedDeviceXml(file));
		chain.appendEffect(placeholder);

		// the user bypasses the placeholder
		placeholder->setEnabled(false);

		QDomDocument doc;
		QDomElement chainElement = doc.createElement(QStringLiteral("fxchain"));
		chain.saveSettings(doc, chainElement);

		const QDomElement saved = savedEffectElement(chainElement);
		QVERIFY2(!saved.isNull(), "the chain saved no effect element");
		// the device's own XML is kept ...
		QCOMPARE(saved.firstChildElement(QStringLiteral("ghost"))
				.firstChildElement(QStringLiteral("state")).text(),
			QStringLiteral("QUJD"));
		// ... and the generic settings the user changed are written with it
		QCOMPARE(saved.attribute(QStringLiteral("on")), QStringLiteral("0"));
	}

	//! A generic setting that is not the bypass is written the same way: the
	//! auto-quit knob the user turned on the placeholder survives the save.
	void theAutoQuitOfAMissingDeviceIsSaved()
	{
		EffectChain chain{nullptr};
		QDomDocument file;
		auto* placeholder = new DummyEffect(&chain, preservedDeviceXml(file));
		chain.appendEffect(placeholder);

		// the user sets the placeholder's auto-quit knob
		placeholder->autoQuitModel()->setValue(42);

		QDomDocument doc;
		QDomElement chainElement = doc.createElement(QStringLiteral("fxchain"));
		chain.saveSettings(doc, chainElement);

		const QDomElement saved = savedEffectElement(chainElement);
		QVERIFY2(!saved.isNull(), "the chain saved no effect element");
		QCOMPARE(saved.attribute(QStringLiteral("autoquit")), QStringLiteral("42"));
		// the device's own XML is still kept
		QCOMPARE(saved.firstChildElement(QStringLiteral("ghost"))
				.firstChildElement(QStringLiteral("state")).text(),
			QStringLiteral("QUJD"));
	}

	//! A placeholder nobody touched keeps the file's bytes exactly: the generic
	//! settings it holds are the ones the file said, so nothing is written over
	//! them.
	void anUntouchedPlaceholderKeepsTheFilesXml()
	{
		EffectChain chain{nullptr};
		QDomDocument file;
		auto* placeholder = new DummyEffect(&chain, preservedDeviceXml(file));
		chain.appendEffect(placeholder);

		QDomDocument doc;
		QDomElement chainElement = doc.createElement(QStringLiteral("fxchain"));
		chain.saveSettings(doc, chainElement);

		const QDomElement saved = savedEffectElement(chainElement);
		QVERIFY(!saved.isNull());
		QCOMPARE(saved.attribute(QStringLiteral("on")), QStringLiteral("1"));
		QCOMPARE(saved.attribute(QStringLiteral("wet")), QStringLiteral("1"));
		QCOMPARE(saved.firstChildElement(QStringLiteral("ghost"))
				.firstChildElement(QStringLiteral("state")).text(),
			QStringLiteral("QUJD"));
	}
};

QTEST_GUILESS_MAIN(EffectChainMissingDeviceSettingsTest)
#include "EffectChainMissingDeviceSettingsTest.moc"
