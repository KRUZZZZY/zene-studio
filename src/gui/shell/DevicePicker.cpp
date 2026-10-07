/*
 * DevicePicker.cpp - the searchable device picker: name, type, format, vendor and state per row, in words
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

#include "DevicePicker.h"

#include <algorithm>

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ControlDeviceSupport.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "Ladspa2LMMS.h"
#include "PluginOrganiser.h"
#include "PluginVendors.h"
#include "lmmsconfig.h"

#ifdef LMMS_HAVE_LV2
#include "Lv2Basics.h"
#include "Lv2Manager.h"
#endif

namespace lmms::gui
{

namespace
{

enum Column { NameColumn, KindColumn, FormatColumn, VendorColumn, StateColumn, ColumnCount };

constexpr int DeviceRole = Qt::UserRole + 1;   //!< the row's dev-<n>
constexpr int HaystackRole = Qt::UserRole + 2;  //!< what the search matches, lower case
//! Page Up / Page Down move this many rows.
constexpr int PageRows = 10;

struct Row
{
	int index;
	bool favourite;
	ControlDeviceEntry entry;
};

QString stateText(const Row& row)
{
	QStringList state;
	if (row.favourite) { state << DevicePicker::tr("Favourite"); }
	if (!row.entry.loadable) { state << DevicePicker::tr("Cannot be loaded in this build"); }
	return state.join(QStringLiteral(", "));
}

QTreeWidgetItem* itemFor(const Row& row)
{
	const ControlDeviceEntry& entry = row.entry;
	auto* item = new QTreeWidgetItem(QStringList{entry.displayName, devicelabels::kindLabel(entry.kind),
		devicelabels::formatLabel(entry.format), devicelabels::vendorOf(entry), stateText(row)});
	item->setData(NameColumn, DeviceRole, control::deviceId(row.index));
	QStringList words;
	for (int c = 0; c < ColumnCount; ++c) { words << item->text(c); }
	item->setData(NameColumn, HaystackRole, words.join(QLatin1Char(' ')).toLower());
	item->setToolTip(NameColumn, words.join(QStringLiteral(" | ")));
	// A device this build cannot load stays listed, greyed AND said so in its State column - not by colour alone.
	if (!entry.loadable) { item->setFlags(item->flags() & ~Qt::ItemIsEnabled); }
	return item;
}

bool matches(const QTreeWidgetItem* item, const QStringList& words)
{
	const QString haystack = item->data(NameColumn, HaystackRole).toString();
	return std::all_of(words.begin(), words.end(), [&haystack](const QString& word) { return haystack.contains(word); });
}

} // namespace

QString devicelabels::formatLabel(const QString& format)
{
	if (format == QLatin1String("builtin")) { return DevicePicker::tr("Built-in"); }
	if (format == QLatin1String("ladspa")) { return QStringLiteral("LADSPA"); }
	if (format == QLatin1String("lv2")) { return QStringLiteral("LV2"); }
	return format.toUpper();  // vst3, clap
}

QString devicelabels::kindLabel(const QString& kind)
{
	if (kind == QLatin1String("instrument")) { return DevicePicker::tr("Instrument"); }
	if (kind == QLatin1String("effect")) { return DevicePicker::tr("Effect"); }
	if (kind == QLatin1String("tool")) { return DevicePicker::tr("Analysis tool"); }
	return DevicePicker::tr("Other");
}

namespace
{

QString ladspaMaker(const ControlDeviceEntry& entry)
{
	return Engine::getLADSPAManager() != nullptr
		? Engine::getLADSPAManager()->getMaker(ladspa_key_t(entry.file, entry.label)) : QString();
}

QString lv2Maker(const ControlDeviceEntry& entry)
{
#ifdef LMMS_HAVE_LV2
	if (Engine::getLv2Manager() == nullptr) { return QString(); }
	if (const LilvPlugin* plugin = Engine::getLv2Manager()->getPlugin(entry.uri))
	{
		return qStringFromPluginNode(plugin, lilv_plugin_get_author_name);
	}
#else
	(void)entry;
#endif
	return QString();
}

} // namespace

QString devicelabels::vendorOf(const ControlDeviceEntry& entry)
{
	QString maker;
	if (entry.format == QLatin1String("builtin")) { maker = QStringLiteral("Zene Studio"); }
	else if (entry.format == QLatin1String("ladspa")) { maker = ladspaMaker(entry); }
	else if (entry.format == QLatin1String("lv2")) { maker = lv2Maker(entry); }
	// VST3 and CLAP: the vendor the module's own factory reported when the catalogue listed it (PluginVendors.h).
	else if (entry.format == QLatin1String("vst3")) { maker = pluginvendors::vendorOf(entry.file, entry.className); }
	else if (entry.format == QLatin1String("clap")) { maker = pluginvendors::vendorOf(entry.file, entry.name); }
	return maker.trimmed().isEmpty() ? DevicePicker::tr("Not reported") : maker.trimmed();
}

DevicePicker::DevicePicker(Kind kind, const QString& title, QWidget* parent)
	: QDialog(parent)
{
	setObjectName(QStringLiteral("devicePicker"));
	setWindowTitle(title);
	setAccessibleName(title);
	resize(3 * tokens::size::browserWidth, 2 * tokens::size::bottomHeight);
	auto* column = new QVBoxLayout(this);
	column->setSpacing(tokens::space::sm);
	m_search = new QLineEdit(this);
	m_search->setObjectName(QStringLiteral("devicePickerSearch"));
	m_search->setPlaceholderText(tr("Search by name, vendor, format or type"));
	m_search->setAccessibleName(tr("Search devices"));
	m_search->setClearButtonEnabled(true);
	m_search->installEventFilter(this);
	m_rows = new QTreeWidget(this);
	m_rows->setObjectName(QStringLiteral("devicePickerRows"));
	m_rows->setAccessibleName(tr("Devices"));
	m_rows->setRootIsDecorated(false);
	m_rows->setUniformRowHeights(true);
	m_rows->setAllColumnsShowFocus(true);
	m_rows->setHeaderLabels({tr("Name"), tr("Type"), tr("Format"), tr("Vendor"), tr("State")});
	m_rows->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_rows->header()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
	m_count = new QLabel(this);
	m_count->setObjectName(QStringLiteral("deviceCaption"));
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Ok)->setText(kind == Kind::Instrument ? tr("Use Instrument") : tr("Add Device"));
	column->addWidget(m_search);
	column->addWidget(m_rows, 1);
	column->addWidget(m_count);
	column->addWidget(buttons);
	connect(m_search, &QLineEdit::textChanged, this, &DevicePicker::applyFilter);
	connect(m_search, &QLineEdit::returnPressed, this, &DevicePicker::accept);
	connect(m_rows, &QTreeWidget::itemActivated, this, &DevicePicker::accept);
	connect(buttons, &QDialogButtonBox::accepted, this, &DevicePicker::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &DevicePicker::reject);
	fill(kind);
	applyFilter();
	m_search->setFocus();
}

void DevicePicker::fill(Kind kind)
{
	const QString wanted = kind == Kind::Instrument ? QStringLiteral("instrument") : QStringLiteral("effect");
	const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
	std::vector<Row> rows;
	for (int i = 0; i < catalogue.size(); ++i)
	{
		const ControlDeviceEntry& entry = catalogue.at(i);
		const QString key = entry.kind == wanted ? pluginorganiser::keyOf(entry) : QString();
		const pluginorganiser::Entry organised = key.isEmpty() ? pluginorganiser::Entry{} : pluginorganiser::entry(key);
		if (entry.kind == wanted && !organised.hidden) { rows.push_back(Row{i, organised.favourite, entry}); }
	}
	// Favourites first, then by name - the order a musician scans.
	std::stable_sort(rows.begin(), rows.end(), [](const Row& l, const Row& r) {
		if (l.favourite != r.favourite) { return l.favourite; }
		return QString::compare(l.entry.displayName, r.entry.displayName, Qt::CaseInsensitive) < 0;
	});
	for (const Row& row : rows) { m_rows->addTopLevelItem(itemFor(row)); }
}

void DevicePicker::applyFilter()
{
	const QStringList words = m_search->text().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
	QTreeWidgetItem* first = nullptr;
	for (int i = 0; i < m_rows->topLevelItemCount(); ++i)
	{
		QTreeWidgetItem* item = m_rows->topLevelItem(i);
		item->setHidden(!matches(item, words));
		if (first == nullptr && !item->isHidden() && (item->flags() & Qt::ItemIsEnabled)) { first = item; }
	}
	QTreeWidgetItem* current = m_rows->currentItem();
	if (current == nullptr || current->isHidden()) { m_rows->setCurrentItem(first); }
	const int shown = shownRows();
	m_count->setText(shown == 0 ? tr("No device matches \"%1\"").arg(m_search->text()) : tr("%n device(s)", "", shown));
	m_count->setAccessibleName(m_count->text());
}

int DevicePicker::shownRows() const
{
	int shown = 0;
	for (int i = 0; i < m_rows->topLevelItemCount(); ++i) { shown += m_rows->topLevelItem(i)->isHidden() ? 0 : 1; }
	return shown;
}

QString DevicePicker::shownRowText(int row) const
{
	for (int i = 0, seen = 0; i < m_rows->topLevelItemCount(); ++i)
	{
		const QTreeWidgetItem* item = m_rows->topLevelItem(i);
		if (item->isHidden() || seen++ != row) { continue; }
		return item->toolTip(NameColumn);
	}
	return QString();
}

void DevicePicker::setQuery(const QString& query) { m_search->setText(query); }

QString DevicePicker::selectedDevice() const
{
	const QTreeWidgetItem* item = m_rows->currentItem();
	if (item == nullptr || item->isHidden() || !(item->flags() & Qt::ItemIsEnabled)) { return QString(); }
	return item->data(NameColumn, DeviceRole).toString();
}

void DevicePicker::moveCurrent(int key)
{
	QList<QTreeWidgetItem*> shown;
	for (int i = 0; i < m_rows->topLevelItemCount(); ++i)
	{
		if (!m_rows->topLevelItem(i)->isHidden()) { shown << m_rows->topLevelItem(i); }
	}
	if (shown.isEmpty()) { return; }
	const int step = key == Qt::Key_Up ? -1 : key == Qt::Key_Down ? 1 : key == Qt::Key_PageUp ? -PageRows : PageRows;
	const int at = std::clamp(static_cast<int>(shown.indexOf(m_rows->currentItem())) + step, 0, static_cast<int>(shown.size()) - 1);
	m_rows->setCurrentItem(shown.at(at));
	m_rows->scrollToItem(shown.at(at));
}

bool DevicePicker::eventFilter(QObject* watched, QEvent* event)
{
	// The rows answer the arrow keys while the focus stays in the search field, so typing and choosing never swap focus.
	if (watched == m_search && event->type() == QEvent::KeyPress)
	{
		const int key = static_cast<QKeyEvent*>(event)->key();
		if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp || key == Qt::Key_PageDown)
		{
			moveCurrent(key);
			return true;
		}
	}
	return QDialog::eventFilter(watched, event);
}

void DevicePicker::accept()
{
	// Enter on a row this build cannot load (or on nothing) keeps the picker open: the State column says why.
	if (selectedDevice().isEmpty()) { return; }
	QDialog::accept();
}

QString DevicePicker::pick(QWidget* parent, Kind kind, const QString& title)
{
	DevicePicker picker(kind, title, parent);
	return picker.exec() == QDialog::Accepted ? picker.selectedDevice() : QString();
}

} // namespace lmms::gui
