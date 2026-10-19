/*
 * ExportProjectDialog.cpp - implementation of dialog for exporting project
 *
 * Copyright (c) 2004-2013 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "ExportProjectDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include "ControlExportPresetSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "ExportQualityControls.h"
#include "ProjectRenderer.h"
#include "Song.h"
#include "SurroundExport.h"

namespace lmms::gui {

namespace {
constexpr auto maxCompressionLevel = 8;
constexpr auto defaultCompressionLevel = 5;
constexpr auto defaultBitRate = SUPPORTED_BITRATES[2];
constexpr auto defaultBitDepth = OutputSettings::BitDepth::Depth24Bit;
constexpr auto defaultStereoMode = OutputSettings::StereoMode::Stereo;
constexpr auto maxLoopRepeat = std::numeric_limits<int>::max();

//! 0.5.0 L12 (Q050-009): the name "Save Preset..." asks for. A test replaces it, so a preset can be
//! created from the dialog without a modal; empty means the default QInputDialog.
std::function<QString(const QString& suggested)> s_namePrompt;
} // namespace

void setExportPresetNamePrompt(std::function<QString(const QString& suggested)> prompt)
{
	s_namePrompt = std::move(prompt);
}

ExportProjectDialog::ExportProjectDialog(const QString& path, Mode mode, QWidget* parent)
	: QDialog(parent)
	, m_fileFormatLabel(new QLabel(tr("File format:")))
	, m_fileFormatComboBox(new QComboBox())
	, m_presetLabel(new QLabel(tr("Render preset:")))
	, m_presetComboBox(new QComboBox())
	, m_savePresetButton(new QPushButton(tr("Save Preset...")))
	, m_sampleRateLabel(new QLabel(tr("Sampling rate:")))
	, m_sampleRateComboBox(new QComboBox())
	, m_bitRateLabel(new QLabel(tr("Bit rate:")))
	, m_bitRateComboBox(new QComboBox())
	, m_bitDepthLabel(new QLabel(tr("Bit depth:")))
	, m_bitDepthComboBox(new QComboBox())
	, m_stereoModeLabel(new QLabel(("Stereo mode:")))
	, m_stereoModeComboBox(new QComboBox())
	, m_compressionLevelLabel(new QLabel(tr("Compression level:")))
	, m_compressionLevelComboBox(new QComboBox())
	, m_channelModeLabel(new QLabel(tr("Channels:")))
	, m_channelModeComboBox(new QComboBox())
	, m_fileFormatSettingsGroupBox(new QGroupBox(tr("File format settings")))
	, m_fileFormatSettingsLayout(new QFormLayout(m_fileFormatSettingsGroupBox))
	, m_exportAsLoopBox(new QCheckBox(tr("Export as loop (remove extra bar)")))
	, m_exportBetweenLoopMarkersBox(new QCheckBox(tr("Export between loop markers")))
	, m_loopRepeatLabel(new QLabel(tr("Render looped section:")))
	, m_loopRepeatBox(new QSpinBox())
	, m_startButton(new QPushButton(tr("Start")))
	, m_cancelButton(new QPushButton(tr("Cancel")))
	, m_progressBar(new QProgressBar())
	, m_path(path)
	, m_mode(mode)
{
	setWindowTitle(tr("Export project"));

	for (const auto& device : ProjectRenderer::fileEncodeDevices)
	{
		if (!device.isAvailable()) { continue; }
		m_fileFormatComboBox->addItem(tr(device.m_description), static_cast<int>(device.m_fileFormat));
	}

	for (const auto& sampleRate : SUPPORTED_SAMPLERATES)
	{
		const auto str = tr("%1 %2").arg(QString::number(sampleRate), "Hz");
		m_sampleRateComboBox->addItem(str, sampleRate);
	}

	for (const auto& bitRate : SUPPORTED_BITRATES)
	{
		const auto str = tr("%1 %2").arg(QString::number(bitRate), "KBit/s");
		m_bitRateComboBox->addItem(str, bitRate);
	}

	for (auto i = 0; i < static_cast<int>(OutputSettings::BitDepth::Count); ++i)
	{
		switch (static_cast<OutputSettings::BitDepth>(i))
		{
		case OutputSettings::BitDepth::Depth16Bit:
			m_bitDepthComboBox->addItem(tr("16 Bit integer"), i);
			break;
		case OutputSettings::BitDepth::Depth24Bit:
			m_bitDepthComboBox->addItem(tr("24 Bit integer"), i);
			break;
		case OutputSettings::BitDepth::Depth32Bit:
			m_bitDepthComboBox->addItem(tr("32 Bit float"), i);
			break;
		default:
			assert(false && "invalid or unsupported bit depth");
			break;
		}
	}

	for (auto i = 0; i < static_cast<int>(OutputSettings::StereoMode::Count); ++i)
	{
		switch (static_cast<OutputSettings::StereoMode>(i))
		{
		case OutputSettings::StereoMode::Mono:
			m_stereoModeComboBox->addItem(tr("Mono"), i);
			break;
		case OutputSettings::StereoMode::Stereo:
			m_stereoModeComboBox->addItem(tr("Stereo"), i);
			break;
		case OutputSettings::StereoMode::JointStereo:
			m_stereoModeComboBox->addItem(tr("Joint stereo"), i);
			break;
		default:
			assert(false && "invalid or unsupported stereo mode");
			break;
		}
	}

	for (auto i = 0; i <= maxCompressionLevel; ++i)
	{
		const auto compressionValue = static_cast<float>(i) / maxCompressionLevel;
		switch (i)
		{
		case 0:
			m_compressionLevelComboBox->addItem(tr("%1 (Fastest, biggest)").arg(i), compressionValue);
			break;
		case maxCompressionLevel:
			m_compressionLevelComboBox->addItem(tr("%1 (Slowest, smallest)").arg(i), compressionValue);
			break;
		default:
			m_compressionLevelComboBox->addItem(QString::number(i), compressionValue);
			break;
		}
	}


	fillChannelModes();
	connect(m_channelModeComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
		&ExportProjectDialog::onChannelModeChanged);

	auto loopRepeatLayout = new QHBoxLayout{};
	loopRepeatLayout->addWidget(m_loopRepeatLabel);
	loopRepeatLayout->addWidget(m_loopRepeatBox);

	// The loudness report is offered on by default in the GUI: it is measured
	// from the blocks that are written, so the render itself is unchanged, and
	// the report is the one thing a user cannot get from the file afterwards.
	m_loudnessReportBox = new QCheckBox(tr("Loudness report (EBU R128)"));
	m_loudnessReportBox->setObjectName(QStringLiteral("exportLoudnessReport"));
	m_loudnessReportBox->setToolTip(tr("Measure the render and write the EBU R128 report - integrated LUFS "
									   "short-term maximum and true peak - beside the output as "
									   "\"<file>.loudness.txt\". Measure-only: the exported audio is identical "
									   "either way."));
	m_loudnessReportBox->setChecked(true);

	m_loudnessResultLabel = new QLabel();
	m_loudnessResultLabel->setWordWrap(true);
	m_loudnessResultLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_loudnessResultLabel->hide();

	auto exportSettingsGroupBox = new QGroupBox(tr("Export settings"));
	auto exportSettingsLayout = new QVBoxLayout{exportSettingsGroupBox};
	exportSettingsLayout->addWidget(m_exportAsLoopBox);
	exportSettingsLayout->addWidget(m_exportBetweenLoopMarkersBox);
	exportSettingsLayout->addWidget(m_loudnessReportBox);
	exportSettingsLayout->addLayout(loopRepeatLayout);

	m_fileFormatSettingsLayout->addRow(m_fileFormatLabel, m_fileFormatComboBox);

	auto startCancelButtonsLayout = new QHBoxLayout{};
	startCancelButtonsLayout->addStretch();
	startCancelButtonsLayout->addWidget(m_startButton);
	startCancelButtonsLayout->addWidget(m_cancelButton);

	auto mainLayout = new QVBoxLayout(this);
	// 0.5.0 L12 (Q050-009): the render-preset picker leads the dialog, so choosing "the 24/96
	// master" is the first thing in reach; choosing one applies it (export.preset_apply) and moves
	// the settings rows below, and the next render is started with those settings.
	auto presetLayout = new QHBoxLayout{};
	m_presetComboBox->setObjectName(QStringLiteral("exportPresetCombo"));
	m_savePresetButton->setObjectName(QStringLiteral("exportSavePresetButton"));
	m_presetComboBox->setAccessibleName(tr("Render preset"));
	m_presetLabel->setBuddy(m_presetComboBox);
	presetLayout->addWidget(m_presetLabel);
	presetLayout->addWidget(m_presetComboBox, 1);
	presetLayout->addWidget(m_savePresetButton);
	mainLayout->addLayout(presetLayout);
	mainLayout->addWidget(exportSettingsGroupBox);
	mainLayout->addWidget(m_fileFormatSettingsGroupBox);
	// Dither and resampling, through export.set_dither / export.set_src_quality.
	mainLayout->addWidget(makeExportQualityControls(this));
	mainLayout->addStretch();
	mainLayout->addWidget(m_loudnessResultLabel);
	mainLayout->addLayout(startCancelButtonsLayout);
	mainLayout->addWidget(m_progressBar);

	m_progressBar->setValue(0);
	m_loopRepeatBox->setRange(1, maxLoopRepeat);
	m_loopRepeatBox->setValue(1);
	m_loopRepeatBox->setSuffix(tr(" time(s)"));
	// R8.5: the label beside it names it; the editable field inside is what Tab reaches.
	m_loopRepeatBox->setAccessibleName(tr("Render looped section"));
	if (auto* field = m_loopRepeatBox->findChild<QLineEdit*>()) { field->setAccessibleName(tr("Render looped section")); }

	m_fileFormatComboBox->setCurrentIndex(-1);
	connect(m_fileFormatComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
		&ExportProjectDialog::onFileFormatChanged);

	const auto pathExtension = QFileInfo{path}.completeSuffix().prepend(".");
	const auto pathFormat = ProjectRenderer::getFileFormatFromExtension(pathExtension);

	m_fileFormatComboBox->setCurrentIndex(std::max(0, m_fileFormatComboBox->findData(static_cast<int>(pathFormat))));
	m_bitRateComboBox->setCurrentIndex(std::max(0, m_bitRateComboBox->findData(defaultBitRate)));
	m_bitDepthComboBox->setCurrentIndex(std::max(0, m_bitDepthComboBox->findData(static_cast<int>(defaultBitDepth))));
	m_stereoModeComboBox->setCurrentIndex(
		std::max(0, m_stereoModeComboBox->findData(static_cast<int>(defaultStereoMode))));
	m_compressionLevelComboBox->setCurrentIndex(defaultCompressionLevel);

	// Object names for the settings a preset drives, so a test can address them and a screen
	// reader (and the UI test) can find them by name rather than by position.
	m_fileFormatComboBox->setObjectName(QStringLiteral("exportFileFormatCombo"));
	m_sampleRateComboBox->setObjectName(QStringLiteral("exportSampleRateCombo"));
	m_bitDepthComboBox->setObjectName(QStringLiteral("exportBitDepthCombo"));
	m_stereoModeComboBox->setObjectName(QStringLiteral("exportStereoModeCombo"));
	m_channelModeComboBox->setObjectName(QStringLiteral("exportChannelsCombo"));

	// 0.5.0 L12 (Q050-009): the preset picker follows export.preset_list, applies a choice with
	// export.preset_apply and saves the dialog's own settings with export.preset_add.
	rebuildPresetList();
	connect(m_presetComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
		&ExportProjectDialog::onPresetChanged);
	connect(m_savePresetButton, &QPushButton::clicked, this, &ExportProjectDialog::onSavePresetClicked);

	connect(m_startButton, &QPushButton::clicked, this, &ExportProjectDialog::onStartButtonClicked);
	connect(m_cancelButton, &QPushButton::clicked, this, &ExportProjectDialog::reject);
}

void ExportProjectDialog::onFileFormatChanged(int index)
{
	if (m_mode == Mode::ExportProject)
	{
		const auto fileInfo = QFileInfo{m_path};
		const auto extension
			= ProjectRenderer::getFileExtensionFromFormat(static_cast<ProjectRenderer::ExportFileFormat>(index));
		m_path = fileInfo.path() + QDir::separator() + fileInfo.completeBaseName() + extension;
	}

	// Remove all rows after the file format row and release the returned layout items.
	while (m_fileFormatSettingsLayout->rowCount() > 1)
	{
		const auto row = m_fileFormatSettingsLayout->takeRow(1);
		delete row.labelItem;
		delete row.fieldItem;
	}

	switch (static_cast<ProjectRenderer::ExportFileFormat>(index))
	{
	case ProjectRenderer::ExportFileFormat::Wave:
		m_fileFormatSettingsLayout->addRow(m_sampleRateLabel, m_sampleRateComboBox);
		m_fileFormatSettingsLayout->addRow(m_bitDepthLabel, m_bitDepthComboBox);
		m_fileFormatSettingsLayout->addRow(m_channelModeLabel, m_channelModeComboBox);
		break;
	case ProjectRenderer::ExportFileFormat::Flac:
		m_fileFormatSettingsLayout->addRow(m_sampleRateLabel, m_sampleRateComboBox);
		m_fileFormatSettingsLayout->addRow(m_bitDepthLabel, m_bitDepthComboBox);
		m_fileFormatSettingsLayout->addRow(m_compressionLevelLabel, m_compressionLevelComboBox);
		break;
	case ProjectRenderer::ExportFileFormat::Ogg:
		m_fileFormatSettingsLayout->addRow(m_sampleRateLabel, m_sampleRateComboBox);
		m_fileFormatSettingsLayout->addRow(m_bitRateLabel, m_bitRateComboBox);
		break;
	case ProjectRenderer::ExportFileFormat::MP3:
		m_fileFormatSettingsLayout->addRow(m_stereoModeLabel, m_stereoModeComboBox);
		m_fileFormatSettingsLayout->addRow(m_bitRateLabel, m_bitRateComboBox);
	default:
		break;
	}
}

void ExportProjectDialog::fillChannelModes()
{
	m_channelModeComboBox->addItem(tr("Stereo (surround folded with the ITU-R BS.775 downmix)"),
		static_cast<int>(OutputSettings::ChannelMode::Stereo));
	const surround::Format master = surroundexport::masterFormat();
	if (master != surround::Format::Stereo && m_mode == Mode::ExportProject)
	{
		m_channelModeComboBox->addItem(tr("Surround %1 (%2 channels)").arg(surround::formatName(master))
			.arg(surround::channelCount(master)), static_cast<int>(OutputSettings::ChannelMode::Surround));
	}
	m_channelModeComboBox->setEnabled(m_channelModeComboBox->count() > 1);
	m_channelModeComboBox->setToolTip(m_channelModeComboBox->isEnabled()
		? tr("Surround writes one WAV channel per speaker of the master, in the standard "
			 "WAVE_FORMAT_EXTENSIBLE order with its channel mask.")
		: tr("The master is stereo (or several tracks are exported), so the export is stereo."));
	m_channelModeComboBox->setAccessibleName(tr("Channels"));
}

void ExportProjectDialog::onChannelModeChanged(int index)
{
	const bool surround = static_cast<OutputSettings::ChannelMode>(m_channelModeComboBox->itemData(index).toInt())
		== OutputSettings::ChannelMode::Surround;
	if (surround) { m_loudnessReportBox->setChecked(false); }
	m_loudnessReportBox->setEnabled(!surround);
}

ProjectRenderer::ExportFileFormat ExportProjectDialog::exportFormat() const
{
	return static_cast<ProjectRenderer::ExportFileFormat>(m_fileFormatComboBox->currentData().toInt());
}

OutputSettings ExportProjectDialog::exportSettings() const
{
	const auto sampleRate = static_cast<sample_rate_t>(m_sampleRateComboBox->currentData().toInt());
	const auto bitRate = static_cast<bitrate_t>(m_bitRateComboBox->currentData().toInt());
	const auto bitDepth = static_cast<OutputSettings::BitDepth>(m_bitDepthComboBox->currentData().toInt());
	const auto stereoMode = static_cast<OutputSettings::StereoMode>(m_stereoModeComboBox->currentData().toInt());
	OutputSettings outputSettings{sampleRate, bitRate, bitDepth, stereoMode};
	outputSettings.setCompressionLevel(m_compressionLevelComboBox->currentData().toDouble());
	outputSettings.setLoudnessReport(m_loudnessReportBox->isChecked());
	// The channels row is only on the WAV page; every other format exports stereo, as before.
	if (exportFormat() == ProjectRenderer::ExportFileFormat::Wave)
	{
		outputSettings.setChannelMode(
			static_cast<OutputSettings::ChannelMode>(m_channelModeComboBox->currentData().toInt()));
	}
	return outputSettings;
}

QString ExportProjectDialog::currentBitDepthName() const
{
	return QString::fromLatin1(controlExportPresetBitDepthName(
		static_cast<OutputSettings::BitDepth>(m_bitDepthComboBox->currentData().toInt())));
}

QString ExportProjectDialog::currentFormatName() const
{
	return ProjectRenderer::getFileExtensionFromFormat(exportFormat()).mid(1);
}

QString ExportProjectDialog::currentStereoModeName() const
{
	return QString::fromLatin1(controlExportPresetStereoModeName(
		static_cast<OutputSettings::StereoMode>(m_stereoModeComboBox->currentData().toInt())));
}

void ExportProjectDialog::rebuildPresetList()
{
	const ControlResult listed = ControlRegistry::instance()->invoke(QStringLiteral("export.preset_list"));
	const QSignalBlocker blocker(m_presetComboBox);
	m_presetComboBox->clear();
	m_presetComboBox->addItem(tr("Default Settings (44100 Hz, 16-bit, joint stereo)"), QString());
	if (listed.ok)
	{
		for (const QJsonValue& value : listed.result.value(QStringLiteral("presets")).toArray())
		{
			const QString name = value.toObject().value(QStringLiteral("name")).toString();
			m_presetComboBox->addItem(name, name);
		}
		const QString applied = listed.result.value(QStringLiteral("applied_preset")).toString();
		const int index = applied.isEmpty() ? 0 : m_presetComboBox->findData(applied);
		m_presetComboBox->setCurrentIndex(index >= 0 ? index : 0);
		// Opening the dialog shows the settings the render path is on, so a user (and a test)
		// reads the applied preset's rate/depth/mode without having to re-select it.
		showPresetSettings(listed.result.value(QStringLiteral("settings")).toObject());
	}
	m_savePresetButton->setEnabled(ControlRegistry::instance()->hasCommand(QStringLiteral("export.preset_add")));
}

void ExportProjectDialog::showPresetSettings(const QJsonObject& settings)
{
	// The file format first: it decides which rows the page shows (and, for an export, the
	// destination's extension). A preset that does not state one ("") leaves the format alone.
	const QString formatName = settings.value(QStringLiteral("format")).toString();
	if (!formatName.isEmpty())
	{
		const auto format = ProjectRenderer::getFileFormatFromExtension(QLatin1Char('.') + formatName);
		const int formatIndex = m_fileFormatComboBox->findData(static_cast<int>(format));
		if (formatIndex >= 0) { m_fileFormatComboBox->setCurrentIndex(formatIndex); }
	}

	const int rateIndex = m_sampleRateComboBox->findData(settings.value(QStringLiteral("sample_rate")).toInt());
	if (rateIndex >= 0) { m_sampleRateComboBox->setCurrentIndex(rateIndex); }

	OutputSettings::BitDepth depth = OutputSettings::BitDepth::Depth16Bit;
	if (controlExportPresetBitDepthFromName(settings.value(QStringLiteral("bit_depth")).toString(), &depth))
	{
		const int depthIndex = m_bitDepthComboBox->findData(static_cast<int>(depth));
		if (depthIndex >= 0) { m_bitDepthComboBox->setCurrentIndex(depthIndex); }
	}

	OutputSettings::StereoMode mode = OutputSettings::StereoMode::JointStereo;
	if (controlExportPresetStereoModeFromName(settings.value(QStringLiteral("stereo_mode")).toString(), &mode))
	{
		const int modeIndex = m_stereoModeComboBox->findData(static_cast<int>(mode));
		if (modeIndex >= 0) { m_stereoModeComboBox->setCurrentIndex(modeIndex); }
	}

	// The channel mode ("" = not stated). "surround" is only on offer when the master is a
	// surround one, so a preset naming it on a stereo master leaves the combo where it is.
	OutputSettings::ChannelMode channels = OutputSettings::ChannelMode::Stereo;
	if (surroundexport::channelModeFromName(settings.value(QStringLiteral("channels")).toString(), &channels))
	{
		const int channelsIndex = m_channelModeComboBox->findData(static_cast<int>(channels));
		if (channelsIndex >= 0) { m_channelModeComboBox->setCurrentIndex(channelsIndex); }
	}
}

void ExportProjectDialog::onPresetChanged(int index)
{
	const QString name = m_presetComboBox->itemData(index).toString();
	const ControlResult applied = name.isEmpty()
		? ControlRegistry::instance()->invoke(QStringLiteral("export.preset_apply"))
		: ControlRegistry::instance()->invoke(QStringLiteral("export.preset_apply"),
			{{QStringLiteral("name"), name}});
	if (!applied.ok) { return; }
	showPresetSettings(applied.result);
}

void ExportProjectDialog::onSavePresetClicked()
{
	QString name = s_namePrompt ? s_namePrompt(m_presetComboBox->currentText())
		: QInputDialog::getText(this, tr("Save Render Preset"), tr("Preset name:"));
	name = name.trimmed();
	if (name.isEmpty()) { return; }

	bool exists = false;
	const ControlResult listed = ControlRegistry::instance()->invoke(QStringLiteral("export.preset_list"),
		{{QStringLiteral("name"), name}});
	if (listed.ok && !listed.result.value(QStringLiteral("presets")).toArray().isEmpty()) { exists = true; }
	if (exists)
	{
		const auto answer = QMessageBox::question(this, tr("Save Render Preset"),
			tr("A render preset named \"%1\" exists. Replace it?").arg(name),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		if (answer != QMessageBox::Yes) { return; }
	}

	const ControlResult added = ControlRegistry::instance()->invoke(QStringLiteral("export.preset_add"),
		{{QStringLiteral("name"), name},
			{QStringLiteral("sample_rate"), m_sampleRateComboBox->currentData().toInt()},
			{QStringLiteral("bit_depth"), currentBitDepthName()},
			{QStringLiteral("stereo_mode"), currentStereoModeName()},
			{QStringLiteral("format"), currentFormatName()},
			{QStringLiteral("channels"), surroundexport::channelModeName(
				static_cast<OutputSettings::ChannelMode>(m_channelModeComboBox->currentData().toInt()))},
			{QStringLiteral("overwrite"), exists}});
	if (!added.ok)
	{
		QMessageBox::warning(this, tr("Save Render Preset"), added.errorMessage);
		return;
	}
	// Saving writes ONE document and does NOT put the render path on the new
	// preset: the applied selection, and the settings rows below, stay where the
	// user left them. The picker gains the name so it can be selected (which is
	// what applies it), but selecting it here would dispatch export.preset_apply
	// as a SECOND command, i.e. two undo checkpoints for one click. Adding the
	// item leaves the current index untouched (the list is never empty), so no
	// currentIndexChanged fires and no apply is recorded.
	if (m_presetComboBox->findData(name) < 0) { m_presetComboBox->addItem(name, name); }
}

void ExportProjectDialog::onStartButtonClicked()
{
	const auto outputSettings = exportSettings();
	const auto format = exportFormat();
	const QString refused = surroundexport::refusal(outputSettings, format == ProjectRenderer::ExportFileFormat::Wave);
	if (!refused.isEmpty())
	{
		QMessageBox::warning(this, tr("Cannot export"), refused);
		return;
	}
	m_renderManager = std::make_unique<RenderManager>(outputSettings, format, m_path);
	m_startButton->setEnabled(false);

	Engine::getSong()->setExportLoop(m_exportAsLoopBox->isChecked());
	Engine::getSong()->setRenderBetweenMarkers(m_exportBetweenLoopMarkersBox->isChecked());
	Engine::getSong()->setLoopRenderCount(m_loopRepeatBox->value());

	connect(m_renderManager.get(), &RenderManager::progressChanged, m_progressBar, &QProgressBar::setValue);
	connect(m_renderManager.get(), &RenderManager::progressChanged, this, &ExportProjectDialog::updateTitleBar);
	connect(m_renderManager.get(), &RenderManager::finished, this, &ExportProjectDialog::onRenderFinished);

	switch (m_mode)
	{
	case Mode::ExportProject:
		m_renderManager->renderProject();
		break;
	case Mode::ExportTracks:
		m_renderManager->renderTracks();
		break;
	}
}

void ExportProjectDialog::accept()
{
	m_renderManager.reset(nullptr);
	QDialog::accept();
}

void ExportProjectDialog::reject()
{
	if (m_renderManager) { m_renderManager->abortProcessing(); }
	m_renderManager.reset(nullptr);
	QDialog::reject();
}

void ExportProjectDialog::updateTitleBar(int prog)
{
	setWindowTitle(tr("Rendering: %1%").arg(prog));
}

void ExportProjectDialog::onRenderFinished()
{
	const QString report = m_renderManager ? m_renderManager->loudnessReportText() : QString();
	if (report.isEmpty())
	{
		// No report was asked for (or the render was silent and produced none):
		// behave exactly as before.
		accept();
		return;
	}

	// Keep the dialog open so the measurement is actually readable, and offer
	// the sidecar next to the render as the copy that outlives the dialog.
	QString text = report;
	const QString sidecar = m_renderManager->loudnessReportPath();
	if (!sidecar.isEmpty())
	{
		text += tr("Written to %1").arg(sidecar);
	}
	else if (!m_renderManager->loudnessReportError().isEmpty())
	{
		text += tr("The report could not be written: %1").arg(m_renderManager->loudnessReportError());
	}
	m_loudnessResultLabel->setText(text);
	m_loudnessResultLabel->show();
	m_startButton->setEnabled(false);
	m_cancelButton->setText(tr("Close"));
	setWindowTitle(tr("Export finished - EBU R128 loudness report"));
	adjustSize();
}

} // namespace lmms::gui
