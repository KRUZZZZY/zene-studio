/*
 * ChannelPickDialog.cpp - choose several mixer channels, for an edit that applies to all of them
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

#include "ChannelPickDialog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"

namespace lmms::gui
{

QStringList pickChannels(QWidget* parent, const QString& title, const QString& exclude)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(title);
	auto* layout = new QVBoxLayout(&dialog);
	auto* list = new QListWidget(&dialog);
	list->setAccessibleName(QObject::tr("Channels"));
	list->setAccessibleDescription(QObject::tr("Tick the channels to include"));
	Mixer* mixer = Engine::mixer();
	for (int i = 1; i < mixer->numChannels(); ++i)
	{
		const QString id = control::channelIdOf(mixer->mixerChannel(i));
		if (id == exclude) { continue; }
		auto* item = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(mixer->mixerChannel(i)->m_name, id), list);
		item->setData(Qt::UserRole, id);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Unchecked);
	}
	layout->addWidget(list);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	// OK needs a ticked channel: with none it would do nothing, as Cancel does.
	QPushButton* ok = buttons->button(QDialogButtonBox::Ok);
	ok->setEnabled(false);
	QObject::connect(list, &QListWidget::itemChanged, ok, [list, ok] {
		bool any = false;
		for (int i = 0; i < list->count() && !any; ++i) { any = list->item(i)->checkState() == Qt::Checked; }
		ok->setEnabled(any);
	});
	layout->addWidget(buttons);
	QStringList picked;
	if (dialog.exec() != QDialog::Accepted) { return picked; }
	for (int i = 0; i < list->count(); ++i)
	{
		if (list->item(i)->checkState() == Qt::Checked) { picked.append(list->item(i)->data(Qt::UserRole).toString()); }
	}
	return picked;
}

} // namespace lmms::gui
