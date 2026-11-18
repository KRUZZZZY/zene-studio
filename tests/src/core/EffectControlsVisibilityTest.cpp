/*
 * EffectControlsVisibilityTest.cpp - EffectControls::m_viewVisible must be atomic
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

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>

#include "EffectControls.h"

using namespace lmms;

namespace
{

class StubEffectControls : public EffectControls
{
public:
	StubEffectControls() : EffectControls(nullptr) {}
	int controlCount() override { return 0; }
	gui::EffectControlDialog* createView() override { return nullptr; }
	QString nodeName() const override { return QStringLiteral("stub"); }
	void saveSettings(QDomDocument&, QDomElement&) override {}
	void loadSettings(const QDomElement&) override {}
};

} // namespace

class EffectControlsVisibilityTest : public QObject
{
	Q_OBJECT

private slots:
	void viewVisibleStorageMustBeAtomic()
	{
		StubEffectControls controls;
		QVERIFY2(controls.viewVisibleStorageIsAtomic(),
			"EffectControls::m_viewVisible must be std::atomic<bool> "
			"because processImpl() reads it on the audio thread while "
			"the GUI thread writes it via setViewVisible()");
	}

	void setAndGetViewVisibleRoundTrips()
	{
		StubEffectControls controls;
		QVERIFY(!controls.isViewVisible());
		controls.setViewVisible(true);
		QVERIFY(controls.isViewVisible());
		controls.setViewVisible(false);
		QVERIFY(!controls.isViewVisible());
	}
};

QTEST_MAIN(EffectControlsVisibilityTest)
#include "EffectControlsVisibilityTest.moc"
