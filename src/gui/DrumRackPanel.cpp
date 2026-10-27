/*
 * DrumRackPanel.cpp - a drum rack's sample pads: name, trigger key and sample (Q050-066)
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

#include "DrumRackPanel.h"

#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "FileDialog.h"

namespace lmms::gui
{

namespace
{

constexpr int RefreshMs = 500;

std::function<QString(QWidget*)>& sampleChooser()
{
	static std::function<QString(QWidget*)> chooser;
	return chooser;
}

QToolButton* makeButton(QWidget* parent, const QString& command, const QString& text)
{
	auto* button = new QToolButton(parent);
	button->setText(text);
	button->setAccessibleName(text);
	button->setProperty("controlCommand", command);
	button->setFocusPolicy(Qt::StrongFocus);
	return button;
}

} // namespace


DrumRackPanel::DrumRackPanel(const QString& folderId, QWidget* parent) :
	QWidget(parent),
	m_folderId(folderId)
{
	setObjectName(QStringLiteral("DrumRackPanel"));
	auto* outer = new QVBoxLayout(this);
	m_grid = new QGridLayout;
	outer->addLayout(m_grid);
	auto* actions = new QHBoxLayout;
	m_add = makeButton(this, QStringLiteral("track.drum_rack_pad_add"), tr("Add Pad..."));
	m_add->setToolTip(tr("A new pad playing a sample file, on the next free key"));
	connect(m_add, &QToolButton::clicked, this, [this] {
		const QString file = chooseSample();
		if (!file.isEmpty()) { run(QStringLiteral("track.drum_rack_pad_add"), {{QStringLiteral("track"), m_folderId}, {QStringLiteral("sample"), file}}); }
	});
	actions->addWidget(m_add);
	actions->addStretch(1);
	outer->addLayout(actions);
	m_status = new QLabel(this);
	m_status->setAccessibleName(tr("Drum rack status"));
	m_status->setWordWrap(true);
	outer->addWidget(m_status);
	outer->addStretch(1);
	connect(&m_timer, &QTimer::timeout, this, &DrumRackPanel::refresh);
	m_timer.start(RefreshMs);
	refresh();
}


void DrumRackPanel::setSampleChooser(std::function<QString(QWidget*)> chooser)
{
	sampleChooser() = std::move(chooser);
}


QString DrumRackPanel::chooseSample()
{
	if (sampleChooser()) { return sampleChooser()(this); }
	return FileDialog::getOpenFileName(this, tr("Sample for the pad"), QString(),
		tr("Audio files (*.wav *.ogg *.flac *.aif *.aiff *.mp3);;All files (*)"));
}


ControlResult DrumRackPanel::run(const QString& command, const QJsonObject& args)
{
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	m_status->setText(result.ok ? tr("%1: done").arg(command) : result.errorMessage);
	refresh();
	return result;
}


void DrumRackPanel::setPad(int pad, const QString& field, const QJsonValue& value)
{
	if (const Row* row = rowAt(pad))
	{
		run(QStringLiteral("track.drum_rack_pad_set"), {{QStringLiteral("track"), row->track}, {field, value}});
	}
}


void DrumRackPanel::refresh()
{
	const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("track.drum_rack_get_state"),
		{{QStringLiteral("track"), m_folderId}});
	if (!state.ok)
	{
		m_status->setText(state.errorMessage);
		return;
	}
	setWindowTitle(tr("Drum rack - %1").arg(state.result.value(QStringLiteral("name")).toString()));
	const QJsonArray pads = state.result.value(QStringLiteral("pads")).toArray();
	QStringList signature;
	for (const QJsonValue& pad : pads) { signature << pad.toObject().value(QStringLiteral("track")).toString(); }
	if (signature != m_signature) { rebuild(pads, signature); }
	showPads(pads);
}


void DrumRackPanel::rebuild(const QJsonArray& pads, const QStringList& signature)
{
	for (const Row& row : m_rows)
	{
		for (QWidget* widget : std::initializer_list<QWidget*>{row.name, row.key, row.sample, row.replace})
		{
			widget->hide();
			widget->deleteLater();
		}
	}
	m_rows.clear();
	m_signature = signature;
	for (int pad = 0; pad < pads.size(); ++pad) { addRow(pad, signature.at(pad)); }
}


void DrumRackPanel::addRow(int pad, const QString& track)
{
	Row row;
	row.track = track;
	row.name = new QLineEdit(this);
	row.name->setAccessibleName(tr("Name of pad %1").arg(pad + 1));
	row.name->setProperty("controlCommand", QStringLiteral("track.drum_rack_pad_set"));
	connect(row.name, &QLineEdit::editingFinished, this, [this, pad, edit = row.name] {
		if (edit->isModified()) { setPad(pad, QStringLiteral("name"), edit->text()); }
		edit->setModified(false);
	});
	row.key = new QSpinBox(this);
	row.key->setRange(0, 127);
	row.key->setAccessibleName(tr("Trigger key of pad %1").arg(pad + 1));
	row.key->setToolTip(tr("The note that plays the pad's sample at its own pitch"));
	row.key->setProperty("controlCommand", QStringLiteral("track.drum_rack_pad_set"));
	connect(row.key, &QSpinBox::editingFinished, this, [this, pad, box = row.key] {
		if (box->value() != box->property("shownKey").toInt()) { setPad(pad, QStringLiteral("key"), box->value()); }
	});
	row.sample = new QLabel(this);
	row.sample->setAccessibleName(tr("Sample of pad %1").arg(pad + 1));
	row.replace = makeButton(this, QStringLiteral("track.drum_rack_pad_set"), tr("Sample..."));
	row.replace->setAccessibleName(tr("Replace the sample of pad %1").arg(pad + 1));
	connect(row.replace, &QToolButton::clicked, this, [this, pad] {
		const QString file = chooseSample();
		if (!file.isEmpty()) { setPad(pad, QStringLiteral("sample"), file); }
	});
	m_grid->addWidget(row.name, pad, 0);
	m_grid->addWidget(row.key, pad, 1);
	m_grid->addWidget(row.sample, pad, 2);
	m_grid->addWidget(row.replace, pad, 3);
	m_rows.push_back(row);
}


void DrumRackPanel::showPads(const QJsonArray& pads)
{
	for (int pad = 0; pad < pads.size() && pad < rowCount(); ++pad)
	{
		const QJsonObject state = pads.at(pad).toObject();
		const Row& row = m_rows[static_cast<std::size_t>(pad)];
		const int key = state.value(QStringLiteral("key")).toInt();
		row.key->setProperty("shownKey", key);
		if (!row.name->hasFocus()) { row.name->setText(state.value(QStringLiteral("name")).toString()); }  // never under typing
		if (!row.key->hasFocus())
		{
			const QSignalBlocker blocker(row.key);
			row.key->setValue(key);
		}
		const QString sample = state.value(QStringLiteral("sample")).toString();
		row.sample->setText(sample.isEmpty() ? tr("(embedded)") : QFileInfo(sample).fileName());
		row.sample->setToolTip(sample);
	}
}


const DrumRackPanel::Row* DrumRackPanel::rowAt(int pad) const
{
	return pad >= 0 && pad < rowCount() ? &m_rows[static_cast<std::size_t>(pad)] : nullptr;
}

QLineEdit* DrumRackPanel::nameEdit(int pad) const { return rowAt(pad) != nullptr ? rowAt(pad)->name : nullptr; }
QSpinBox* DrumRackPanel::keyBox(int pad) const { return rowAt(pad) != nullptr ? rowAt(pad)->key : nullptr; }
QToolButton* DrumRackPanel::sampleButton(int pad) const { return rowAt(pad) != nullptr ? rowAt(pad)->replace : nullptr; }

QString DrumRackPanel::statusText() const
{
	return m_status->text();
}


DrumRackPanel* showDrumRackPanel(QWidget* parent, const QString& folderId)
{
	auto* panel = new DrumRackPanel(folderId, parent);
	panel->setWindowFlag(Qt::Window);
	panel->setAttribute(Qt::WA_DeleteOnClose);
	panel->show();
	return panel;
}

} // namespace lmms::gui
