/*
 * ExportProjectDialogPresetTest.cpp - the export dialog's render-preset picker (0.5.0 L12 / Q050-009)
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

/*! The export dialog now lists the saved render presets and can SAVE the settings it holds:
 *
 *  1. Save from the dialog: the rate/depth/stereo the combos hold become a stored preset and the
 *     picker selects it;
 *  2. reopen the dialog, select the preset: the settings rows move to the stored values and
 *     export.preset_apply reports the preset applied (so render.render is started with it);
 *  3. render through the dialog's own Start button and parse the WAV the render wrote: its own
 *     header carries the preset's rate and bit depth, the chosen format's extension and stereo
 *     channels.
 *
 *  Over a throwaway preset store (the working directory); the render path is RenderManager's own,
 *  the same one the dialog always used. */

#include <QtTest>

#include <QComboBox>
#include <QCheckBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "ExportProjectDialog.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

const QString kPreset = QStringLiteral("Dialog 48k stereo");

QComboBox* combo(QWidget* dialog, const char* name)
{
	return dialog->findChild<QComboBox*>(QString::fromLatin1(name));
}

QPushButton* button(QWidget* dialog, const QString& objectName)
{
	return dialog->findChild<QPushButton*>(QString::fromLatin1(objectName.toUtf8()));
}

//! The preset store's view of one preset, or an empty object.
QJsonObject listed()
{
	const ControlResult listed = ControlRegistry::instance()->invoke(QStringLiteral("export.preset_list"),
		{{QStringLiteral("name"), kPreset}});
	const QJsonArray presets = listed.result.value(QStringLiteral("presets")).toArray();
	return presets.isEmpty() ? QJsonObject{} : presets.first().toObject();
}

//! What a RIFF/WAVE header says about the file: rate, bits and channels, or defaults on a miss.
struct WavHeader
{
	int rate = 0;
	int bits = 0;
	int channels = 0;
	int frames = 0;
};

WavHeader readWavHeader(const QString& path)
{
	WavHeader header;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return header; }
	const QByteArray blob = file.readAll();
	if (blob.size() < 44 || blob.mid(0, 4) != "RIFF" || blob.mid(8, 4) != "WAVE") { return header; }
	int offset = 12;
	while (offset + 8 <= blob.size())
	{
		const QByteArray id = blob.mid(offset, 4);
		const quint32 size = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(blob.constData() + offset + 4));
		const int body = offset + 8;
		if (id == "fmt " && body + 16 <= blob.size())
		{
			header.channels = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(blob.constData() + body + 2));
			header.rate = int(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(blob.constData() + body + 4)));
			header.bits = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(blob.constData() + body + 14));
		}
		else if (id == "data")
		{
			const int blockAlign = header.channels * (header.bits / 8);
			if (blockAlign > 0) { header.frames = int(size) / blockAlign; }
		}
		offset = body + int(size) + (size % 2);
	}
	return header;
}

} // namespace

class ExportProjectDialogPresetTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_world.isValid());
		ConfigManager::inst()->setWorkingDir(m_world.path());

		const ControlResult track = ControlRegistry::instance()->invoke(QStringLiteral("track.add"),
			{{QStringLiteral("type"), QStringLiteral("instrument")}, {QStringLiteral("name"), QStringLiteral("Dialog")}});
		QVERIFY2(track.ok, qPrintable(track.errorMessage));
		const QString trackId = track.result.value(QStringLiteral("track")).toString();
		const ControlResult clip = ControlRegistry::instance()->invoke(QStringLiteral("clip.add"),
			{{QStringLiteral("track"), trackId}, {QStringLiteral("position"), 0}, {QStringLiteral("length"), 768}});
		QVERIFY2(clip.ok, qPrintable(clip.errorMessage));
		ControlRegistry::instance()->invoke(QStringLiteral("note.add"),
			{{QStringLiteral("clip"), clip.result.value(QStringLiteral("clip")).toString()},
				{QStringLiteral("key"), 60}, {QStringLiteral("position"), 0},
				{QStringLiteral("length"), 384}, {QStringLiteral("velocity"), 100}});
	}

	void cleanupTestCase()
	{
		ControlRegistry::instance()->invoke(QStringLiteral("export.preset_apply"), QJsonObject{});
		ControlRegistry::instance()->invoke(QStringLiteral("export.preset_remove"),
			{{QStringLiteral("name"), kPreset}});
		setExportPresetNamePrompt({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aPresetSavedFromTheDialogReopensSelectsAndRendersAtItsSettings()
	{
		const QString outPath = m_world.path() + QStringLiteral("/dialog-preset.wav");

		// 1. Save the dialog's own settings as a preset. The stereo-mode row only exists on the
		//    MP3 page, so it is set there and the dialog switched back to the WAV page (the value
		//    is kept by the member; the row is off-page, not reset).
		setExportPresetNamePrompt([](const QString&) { return kPreset; });
		{
			ExportProjectDialog dialog(outPath, ExportProjectDialog::Mode::ExportProject);
			QComboBox* format = combo(&dialog, "exportFileFormatCombo");
			QVERIFY(format != nullptr);
			format->setCurrentIndex(format->findData(static_cast<int>(ProjectRenderer::ExportFileFormat::MP3)));
			QComboBox* stereo = combo(&dialog, "exportStereoModeCombo");
			QVERIFY(stereo != nullptr);
			stereo->setCurrentIndex(stereo->findData(static_cast<int>(OutputSettings::StereoMode::Stereo)));
			format->setCurrentIndex(format->findData(static_cast<int>(ProjectRenderer::ExportFileFormat::Wave)));

			QComboBox* rate = combo(&dialog, "exportSampleRateCombo");
			QComboBox* depth = combo(&dialog, "exportBitDepthCombo");
			QVERIFY(rate != nullptr && depth != nullptr);
			rate->setCurrentIndex(rate->findData(48000));
			depth->setCurrentIndex(depth->findData(static_cast<int>(OutputSettings::BitDepth::Depth24Bit)));
			button(&dialog, QStringLiteral("exportSavePresetButton"))->click();
		}
		const QJsonObject stored = listed();
		QCOMPARE(stored.value(QStringLiteral("sample_rate")).toInt(), 48000);
		QCOMPARE(stored.value(QStringLiteral("bit_depth")).toString(), QStringLiteral("24"));
		QCOMPARE(stored.value(QStringLiteral("stereo_mode")).toString(), QStringLiteral("stereo"));

		// 2. Reopen, select the preset: the settings rows move and the preset is applied.
		ExportProjectDialog dialog(outPath, ExportProjectDialog::Mode::ExportProject);
		QComboBox* presets = combo(&dialog, "exportPresetCombo");
		QVERIFY(presets != nullptr);
		const int index = presets->findData(kPreset);
		QVERIFY2(index >= 0, "the reopened dialog does not list the saved preset");
		presets->setCurrentIndex(index);

		QComboBox* format = combo(&dialog, "exportFileFormatCombo");
		format->setCurrentIndex(format->findData(static_cast<int>(ProjectRenderer::ExportFileFormat::Wave)));
		QCOMPARE(dialog.exportFormat(), ProjectRenderer::ExportFileFormat::Wave);
		const OutputSettings settings = dialog.exportSettings();
		QCOMPARE(int(settings.getSampleRate()), 48000);
		QCOMPARE(settings.getBitDepth(), OutputSettings::BitDepth::Depth24Bit);
		QCOMPARE(settings.getStereoMode(), OutputSettings::StereoMode::Stereo);
		QCOMPARE(settings.channelMode(), OutputSettings::ChannelMode::Stereo);
		QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("export.preset_list"), QJsonObject{})
				.result.value(QStringLiteral("applied_preset")).toString() == kPreset,
			"selecting the preset did not apply it, so render.render would not be started with it");

		// 3. Render through the dialog's own Start button and read the file's own header.
		if (QCheckBox* report = dialog.findChild<QCheckBox*>(QStringLiteral("exportLoudnessReport")))
		{
			report->setChecked(false);
		}
		QSignalSpy finished(&dialog, &QDialog::finished);
		QPushButton* start = nullptr;
		for (QPushButton* candidate : dialog.findChildren<QPushButton*>())
		{
			if (candidate->text() == QStringLiteral("Start")) { start = candidate; }
		}
		QVERIFY2(start != nullptr, "the dialog has no Start button");
		start->click();
		if (finished.isEmpty()) { finished.wait(180000); }
		QVERIFY2(!finished.isEmpty(), "the dialog's render did not finish");

		const WavHeader header = readWavHeader(outPath);
		QVERIFY2(header.frames > 0, "the dialog's render wrote no audio");
		QCOMPARE(header.rate, 48000);
		QCOMPARE(header.bits, 24);
		QCOMPARE(header.channels, 2);
	}

private:
	QTemporaryDir m_world;
};

QTEST_MAIN(ExportProjectDialogPresetTest)
#include "ExportProjectDialogPresetTest.moc"
