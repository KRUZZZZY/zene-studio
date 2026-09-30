/*
 * ExportQualityControlsTest.cpp - the export dialog sets dither and resampling through the registry
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

/*! The export dialog's Render quality group, in the dialog itself: its Dither and Resampling
 *  combos start at the current selection, and choosing sets it through export.set_dither /
 *  export.set_src_quality - what export.get_settings reports and what the next render's
 *  OutputSettings inherits (ExportRenderSettings). */

#include <QtTest>

#include <QComboBox>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ExportProjectDialog.h"
#include "ExportRenderSettings.h"
#include "OutputSettings.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QComboBox* comboNamed(QWidget& root, const QString& name)
{
	for (QComboBox* box : root.findChildren<QComboBox*>())
	{
		if (box->accessibleName() == name) { return box; }
	}
	return nullptr;
}

} // namespace

class ExportQualityControlsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ExportRenderSettings::reset();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theDialogSetsDitherAndResampling()
	{
		ExportRenderSettings::reset();
		ExportProjectDialog dialog(QStringLiteral("/tmp/unused.wav"), ExportProjectDialog::Mode::ExportProject);
		QComboBox* dither = comboNamed(dialog, QStringLiteral("Dither"));
		QComboBox* resampling = comboNamed(dialog, QStringLiteral("Resampling"));
		QVERIFY(dither != nullptr && resampling != nullptr);
		QCOMPARE(dither->currentData().toString(), QStringLiteral("off"));
		QCOMPARE(resampling->currentData().toString(), QStringLiteral("linear"));
		QVERIFY(dither->count() >= 3);
		QVERIFY(resampling->count() >= 4);

		dither->setCurrentIndex(dither->findData(QStringLiteral("noise_shaped")));
		resampling->setCurrentIndex(resampling->findData(QStringLiteral("sinc_best")));
		const QJsonObject settings = ControlRegistry::instance()->invoke(QStringLiteral("export.get_settings"), QJsonObject{}).result;
		QCOMPARE(settings.value(QStringLiteral("dither_mode")).toString(), QStringLiteral("noise_shaped"));
		QCOMPARE(settings.value(QStringLiteral("src_quality")).toString(), QStringLiteral("sinc_best"));

		// The next render's settings, built the way the dialog builds them, inherit the choice.
		const OutputSettings next(44100, 160, OutputSettings::BitDepth::Depth16Bit);
		QCOMPARE(next.ditherMode(), ExportRenderSettings::ditherMode());
		QVERIFY(next.dither());

		// A second dialog opens on the selection, not on the defaults.
		ExportProjectDialog again(QStringLiteral("/tmp/unused.wav"), ExportProjectDialog::Mode::ExportProject);
		QCOMPARE(comboNamed(again, QStringLiteral("Resampling"))->currentData().toString(), QStringLiteral("sinc_best"));
	}
};

QTEST_MAIN(ExportQualityControlsTest)
#include "ExportQualityControlsTest.moc"
