/*
 * ExportProjectDialog.h - declaration of class ExportProjectDialog which is
 *                           responsible for exporting project
 *
 * Copyright (c) 2004-2012 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_GUI_EXPORT_PROJECT_DIALOG_H
#define LMMS_GUI_EXPORT_PROJECT_DIALOG_H

#include <QDialog>
#include <QJsonObject>

#include <functional>

#include "lmms_export.h"
#include "RenderManager.h"

class QString;
class QLabel;
class QProgressBar;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QFormLayout;
class QGroupBox;

namespace lmms::gui {

/*! 0.5.0 L12 (Q050-009): the name "Save Preset..." in the export dialog asks for. A test replaces
 *  it (the default opens a QInputDialog), so a preset can be created from the dialog without a
 *  modal. An empty return cancels the save. */
LMMS_EXPORT void setExportPresetNamePrompt(std::function<QString(const QString& suggested)> prompt);

class ExportProjectDialog : public QDialog
{
public:
	enum class Mode
	{
		ExportProject,
		ExportTracks,
	};

	ExportProjectDialog(const QString& path, Mode mode, QWidget* parent = nullptr);

	//! The settings the Start button would export with (rate, bit depth, stereo mode, and, on the
	//! WAV page, the channel mode), and the chosen file format. Exposed so a test can prove a
	//! selected preset reaches the render without driving the audio engine.
	OutputSettings exportSettings() const;
	ProjectRenderer::ExportFileFormat exportFormat() const;

private:
	void accept() override;
	void reject() override;
	void onFileFormatChanged(int index);
	//! 0.5.0 F7: a surround export carries no loudness report, so choosing it turns the report off.
	void onChannelModeChanged(int index);
	//! The channels row, for a WAV export: Stereo always, Surround when the master is surround.
	void fillChannelModes();
	void onStartButtonClicked();
	void updateTitleBar(int prog);
	//! Shows the loudness report when one was produced, otherwise closes.
	void onRenderFinished();
	//! 0.5.0 L12 (Q050-009): the preset picker - "Default Settings" and one entry per stored
	//! render preset, the applied one selected; selecting one applies it and moves the combos.
	void rebuildPresetList();
	void onPresetChanged(int index);
	//! Moves the rate/bit-depth/stereo rows onto the settings a preset (or the applied selection)
	//! carries, so the dialog shows what a render would use.
	void showPresetSettings(const QJsonObject& settings);
	//! Saves the dialog's current settings as a named preset (export.preset_add).
	void onSavePresetClicked();
	//! The stereo mode and bit depth the combos carry, mapped to the preset store's names.
	QString currentBitDepthName() const;
	QString currentStereoModeName() const;

	QLabel* m_fileFormatLabel = nullptr;
	QComboBox* m_fileFormatComboBox = nullptr;

	//! 0.5.0 L12 (Q050-009): the render-preset picker and its Save button.
	QLabel* m_presetLabel = nullptr;
	QComboBox* m_presetComboBox = nullptr;
	QPushButton* m_savePresetButton = nullptr;

	QLabel* m_sampleRateLabel = nullptr;
	QComboBox* m_sampleRateComboBox = nullptr;

	QLabel* m_bitRateLabel = nullptr;
	QComboBox* m_bitRateComboBox = nullptr;

	QLabel* m_bitDepthLabel = nullptr;
	QComboBox* m_bitDepthComboBox = nullptr;

	QLabel* m_stereoModeLabel = nullptr;
	QComboBox* m_stereoModeComboBox = nullptr;

	QLabel* m_compressionLevelLabel = nullptr;
	QComboBox* m_compressionLevelComboBox = nullptr;

	//! 0.5.0 F7 (MX-060): stereo (the BS.775 downmix of a surround master) or surround (one channel per speaker).
	QLabel* m_channelModeLabel = nullptr;
	QComboBox* m_channelModeComboBox = nullptr;

	QGroupBox* m_fileFormatSettingsGroupBox = nullptr;
	QFormLayout* m_fileFormatSettingsLayout = nullptr;

	QCheckBox* m_exportAsLoopBox = nullptr;
	QCheckBox* m_exportBetweenLoopMarkersBox = nullptr;
	//! Ask the render for an EBU R128 loudness report (OutputSettings).
	QCheckBox* m_loudnessReportBox = nullptr;
	//! The report itself, shown once the render finished.
	QLabel* m_loudnessResultLabel = nullptr;
	QLabel* m_loopRepeatLabel = nullptr;
	QSpinBox* m_loopRepeatBox = nullptr;
	QPushButton* m_startButton = nullptr;
	QPushButton* m_cancelButton = nullptr;
	QProgressBar* m_progressBar = nullptr;

	QString m_path;
	Mode m_mode;
	std::unique_ptr<RenderManager> m_renderManager;
};

} // namespace lmms::gui

#endif // LMMS_GUI_EXPORT_PROJECT_DIALOG_H
