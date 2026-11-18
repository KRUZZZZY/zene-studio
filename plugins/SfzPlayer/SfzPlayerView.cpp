/*
 * SfzPlayerView.cpp - the SFZ Player's window
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "SfzPlayerView.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>

#include "ComboBox.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "FileDialog.h"
#include "FontHelper.h"
#include "InstrumentTrack.h"
#include "Knob.h"
#include "LcdSpinBox.h"
#include "SfzPlayer.h"

namespace lmms::gui
{

SfzPlayerView::SfzPlayerView(Instrument* instrument, QWidget* parent)
	: InstrumentViewFixedSize(instrument, parent)
{
	setAutoFillBackground(true);
	QPalette palette;
	palette.setColor(backgroundRole(), QColor(38, 41, 46));
	setPalette(palette);

	m_openButton = new QPushButton(tr("Open SFZ..."), this);
	m_openButton->setObjectName(QStringLiteral("sfzOpenButton"));
	m_openButton->setCursor(Qt::PointingHandCursor);
	m_openButton->setGeometry(10, 112, 110, 24);
	connect(m_openButton, &QPushButton::clicked, this, &SfzPlayerView::chooseFile);

	m_volumeKnob = new Knob(KnobType::Bright26, this);
	m_volumeKnob->move(14, 176);
	m_volumeKnob->setHintText(tr("Volume:"), " dB");

	m_qualityBox = new LcdSpinBox(2, this, "Sample quality");
	m_qualityBox->move(70, 180);

	m_polyphonyBox = new LcdSpinBox(3, this, "Polyphony");
	m_polyphonyBox->move(124, 180);

	m_oversamplingBox = new ComboBox(this);
	m_oversamplingBox->setGeometry(184, 180, 56, ComboBox::DEFAULT_HEIGHT);
}

SfzPlayer* SfzPlayerView::player()
{
	return castModel<SfzPlayer>();
}

void SfzPlayerView::modelChanged()
{
	SfzPlayer* p = player();
	connect(p, &SfzPlayer::fileChanged, this, qOverload<>(&SfzPlayerView::update));
	m_volumeKnob->setModel(&p->volumeModel());
	m_qualityBox->setModel(&p->qualityModel());
	m_polyphonyBox->setModel(&p->polyphonyModel());
	m_oversamplingBox->setModel(&p->oversamplingModel());
	update();
}

void SfzPlayerView::chooseFile()
{
	SfzPlayer* p = player();
	// A test answers the dialog by setting this property on the window; nothing else does.
	QString chosen = property("chosenFile").toString();
	if (chosen.isEmpty())
	{
		FileDialog dialog(nullptr, tr("Open SFZ instrument"));
		dialog.setFileMode(FileDialog::ExistingFile);
		dialog.setNameFilters({tr("SFZ instruments (*.sfz)")});
		dialog.setDirectory(p->file().isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
												: QFileInfo(p->file()).absolutePath());
		if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty()) { chosen = dialog.selectedFiles().first(); }
	}
	if (chosen.isEmpty()) { return; }

	// The same command an agent runs, so the window and the control surface cannot disagree.
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("plugin.open_file"),
		QJsonObject{{QStringLiteral("target"), control::trackIdOf(p->instrumentTrack())},
			{QStringLiteral("path"), QFileInfo(chosen).absoluteFilePath()}});
	if (!result.ok) { p->reportOpenFailure(result.errorMessage); }
	update();
}

void SfzPlayerView::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setPen(QColor(235, 235, 235));
	QFont title = painter.font();
	title.setBold(true);
	title.setPixelSize(22);
	painter.setFont(title);
	painter.drawText(QRect(10, 10, 230, 32), Qt::AlignLeft | Qt::AlignVCenter, tr("SFZ PLAYER"));

	painter.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	painter.setPen(QColor(150, 150, 150));
	painter.drawText(QRect(10, 42, 230, 14), Qt::AlignLeft, tr("sfizz engine - velocity layers, round robins, key switches"));

	const SfzPlayer* p = player();
	painter.setPen(QColor(255, 255, 255));
	const QString name = p->file().isEmpty() ? tr("No file loaded") : QFileInfo(p->file()).fileName();
	painter.drawText(QRect(10, 74, 230, 16), Qt::AlignLeft | Qt::AlignVCenter,
		painter.fontMetrics().elidedText(name, Qt::ElideMiddle, 230));
	painter.setPen(QColor(190, 190, 190));
	painter.drawText(QRect(10, 92, 230, 16), Qt::AlignLeft | Qt::AlignVCenter,
		painter.fontMetrics().elidedText(p->status(), Qt::ElideRight, 230));

	painter.setPen(QColor(200, 200, 200));
	painter.drawText(14, 168, tr("VOLUME"));
	painter.drawText(66, 172, tr("QUALITY"));
	painter.drawText(124, 172, tr("VOICES"));
	painter.drawText(184, 172, tr("OVERSAMPLE"));
}

} // namespace lmms::gui
